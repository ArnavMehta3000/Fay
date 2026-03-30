#include "Graphics/ImGuiRenderer.h"
#include "Platform/FileReader.h"
#include "Common/Log.h"
#include "Common/Profiling.h"

import std;

namespace fay
{
	void ImGuiContext::Init(nvrhi::IDevice* device)
	{
		ZoneScoped;

		Device = device;
		CmdList = Device->createCommandList();

		LoadShaders();
		CreateInputLayout();
		CreatePSO();
	}

	void ImGuiContext::OnRender(nvrhi::IFramebuffer* framebuffer)
	{
		ImDrawData* drawData = ImGui::GetDrawData();
		const auto& io = ImGui::GetIO();

		CmdList->open();
		CmdList->beginMarker("ImGui");

		if (!UpdateGeometry(CmdList))
		{
			CmdList->close();
			return;
		}

		// handle DPI scaling
		drawData->ScaleClipRects(io.DisplayFramebufferScale);

		float invDisplaySize[2] = { 1.f / io.DisplaySize.x, 1.f / io.DisplaySize.y };

		// set up graphics state
		auto drawState = nvrhi::GraphicsState()
			.setFramebuffer(framebuffer)
			.setPipeline(GetPSO(framebuffer->getFramebufferInfo()))
			.setViewport(nvrhi::ViewportState()
				.addViewport(nvrhi::Viewport(
					io.DisplaySize.x * io.DisplayFramebufferScale.x,
					io.DisplaySize.y * io.DisplayFramebufferScale.y)))
			.addVertexBuffer(nvrhi::VertexBufferBinding()
				.setBuffer(VertexBuffer)
				.setSlot(0)
				.setOffset(0))
			.setIndexBuffer(nvrhi::IndexBufferBinding()
				.setBuffer(IndexBuffer)
				.setFormat((sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT))
				.setOffset(0));

		i32 vtxOffset = 0, idxOffset = 0;

		for (i32 i = 0; i < drawData->CmdListsCount; i++)
		{
			const ImDrawList* cmdList = drawData->CmdLists[i];

			for (i32 j = 0; j < cmdList->CmdBuffer.Size; j++)
			{
				const ImDrawCmd* cmd = &cmdList->CmdBuffer[j];

				if (cmd->UserCallback)
				{
					cmd->UserCallback(cmdList, cmd);
				}
				else
				{
					drawState.bindings = { GetBindingSet((nvrhi::ITexture*)cmd->TexRef.GetTexID()) };

					drawState.viewport.scissorRects[0] = nvrhi::Rect(
						i32(cmd->ClipRect.x),
						i32(cmd->ClipRect.z),
						i32(cmd->ClipRect.y),
						i32(cmd->ClipRect.w));

					auto drawArgs  = nvrhi::DrawArguments()
						.setVertexCount(cmd->ElemCount)
						.setStartIndexLocation(idxOffset)
						.setStartVertexLocation(vtxOffset);

					CmdList->setGraphicsState(drawState);
					CmdList->setPushConstants(&invDisplaySize, sizeof(invDisplaySize));
					CmdList->drawIndexed(drawArgs);
				}
				
				idxOffset += cmd->ElemCount;
			}

			vtxOffset += cmdList->VtxBuffer.Size;
		}

		CmdList->endMarker();
		CmdList->close();
		Device->executeCommandList(CmdList);
	}

	void ImGuiContext::OnResizeBegin()
	{
		PSO = nullptr;
	}

	void ImGuiContext::LoadShaders()
	{
		FileReader::ReadResult result = FileReader::Read("Shaders/ImGuiVS.cso");
		if (!result)
		{
			Log::Error("Failed to read ImGuiVS shader: {}", result.error());
			return;
		}
		auto& data = result.value();

		nvrhi::ShaderDesc desc;
		desc.setDebugName("ImGuiVS").setEntryName("VSMain").setShaderType(nvrhi::ShaderType::Vertex);

		VertexShader = Device->createShader(desc, data.data(), data.size());

		result = FileReader::Read("Shaders/ImGuiPS.cso");
		if (!result)
		{
			Log::Error("Failed to read ImGuiPS shader: {}", result.error());
			return;
		}
		data = result.value();
		desc.setDebugName("ImGuiPS").setEntryName("PSMain").setShaderType(nvrhi::ShaderType::Pixel);

		PixelShader = Device->createShader(desc, data.data(), data.size());
	}

	void ImGuiContext::CreateInputLayout()
	{
		nvrhi::VertexAttributeDesc vertexAttribLayout[] =
		{
			{ "POSITION", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert, pos), sizeof(ImDrawVert), false },
			{ "TEXCOORD", nvrhi::Format::RG32_FLOAT,  1, 0, offsetof(ImDrawVert, uv),  sizeof(ImDrawVert), false },
			{ "COLOR",    nvrhi::Format::RGBA8_UNORM, 1, 0, offsetof(ImDrawVert, col), sizeof(ImDrawVert), false }
		};

		ShaderAttribLayout = Device->createInputLayout(vertexAttribLayout, std::size(vertexAttribLayout) / sizeof(vertexAttribLayout[0]), nullptr);
	}

	void ImGuiContext::CreatePSO()
	{
		auto renderState = nvrhi::RenderState()
			.setBlendState(nvrhi::BlendState()
				.setRenderTarget(0, nvrhi::BlendState::RenderTarget()
					.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
					.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
					.setSrcBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha)
					.setDestBlendAlpha(nvrhi::BlendFactor::Zero)))
			.setDepthStencilState(nvrhi::DepthStencilState()
				.disableDepthTest()
				.enableDepthWrite()
				.disableStencil()
				.setDepthFunc(nvrhi::ComparisonFunc::Always))
			.setRasterState(nvrhi::RasterState()
				.setFillSolid()
				.setCullNone()
				.setScissorEnable(true)
				.setDepthClipEnable(true));

		auto layoutDesc = nvrhi::BindingLayoutDesc().setVisibility(nvrhi::ShaderType::All);
		layoutDesc.bindings =
		{
			nvrhi::BindingLayoutItem::PushConstants(0, sizeof(f32) * 2),
			nvrhi::BindingLayoutItem::Texture_SRV(0),
			nvrhi::BindingLayoutItem::Sampler(0)
		};

		BindingLayout = Device->createBindingLayout(layoutDesc);

		BasePSODesc
			.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setInputLayout(ShaderAttribLayout)
			.setVertexShader(VertexShader)
			.setPixelShader(PixelShader)
			.setRenderState(renderState)
			.addBindingLayout(BindingLayout);

		auto samplerDesc = nvrhi::SamplerDesc()
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
			.setAllFilters(true);

		FontSampler = Device->createSampler(samplerDesc);
	}

	bool ImGuiContext::UpdateFontTexture()
	{
		ImGuiIO& io = ImGui::GetIO();

		if (FontTexture && io.Fonts->TexRef.GetTexID())
		{
			return true;
		}

		byte* pixels;
		i32 width, height;

		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		if (!pixels)
		{
			return false;
		}

		auto texDesc = nvrhi::TextureDesc()
			.setWidth(width)
			.setHeight(height)
			.setFormat(nvrhi::Format::RGBA8_UNORM)
			.setDebugName("ImGuiFont");

		FontTexture = Device->createTexture(texDesc);
		if (!FontTexture)
		{
			return false;
		}

		CmdList->open();
		CmdList->beginTrackingTextureState(FontTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
		CmdList->writeTexture(FontTexture, 0, 0, pixels, width * 4);
		CmdList->setPermanentTextureState(FontTexture, nvrhi::ResourceStates::ShaderResource);
		CmdList->commitBarriers();
		CmdList->close();

		Device->executeCommandList(CmdList);

		io.Fonts->TexRef = ImTextureRef(FontTexture.Get());

		return true;
	}

	bool ImGuiContext::UpdateGeometry(nvrhi::ICommandList* cmdList)
	{
		if (ImDrawData* drawData = ImGui::GetDrawData())
		{
			// Create/resize vertex and index buffers if needed
			if (!ReallocateBuffer(VertexBuffer,
				drawData->TotalVtxCount * sizeof(ImDrawVert),
				(drawData->TotalVtxCount + 5000) * sizeof(ImDrawVert),
				false))
			{
				return false;
			}

			if (!ReallocateBuffer(IndexBuffer,
				drawData->TotalIdxCount * sizeof(ImDrawIdx),
				(drawData->TotalIdxCount + 5000) * sizeof(ImDrawIdx),
				true))
			{
				return false;
			}

			ImGuiVertexBuffer.resize(VertexBuffer->getDesc().byteSize / sizeof(ImDrawVert));
			ImGuiIndexBuffer.resize(IndexBuffer->getDesc().byteSize / sizeof(ImDrawIdx));

			// copy and convert all vertices into a single contiguous buffer
			ImDrawVert* vtxDst = ImGuiVertexBuffer.data();
			ImDrawIdx* idxDst = ImGuiIndexBuffer.data();

			for (i32 n = 0; n < drawData->CmdListsCount; n++)
			{
				const ImDrawList* cmdList = drawData->CmdLists[n];

				std::memcpy(vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
				std::memcpy(idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

				vtxDst += cmdList->VtxBuffer.Size;
				idxDst += cmdList->IdxBuffer.Size;
			}

			cmdList->writeBuffer(VertexBuffer, ImGuiVertexBuffer.data(), VertexBuffer->getDesc().byteSize);
			cmdList->writeBuffer(IndexBuffer, ImGuiIndexBuffer.data(), IndexBuffer->getDesc().byteSize);

			return true;
		}
	}

	bool ImGuiContext::ReallocateBuffer(nvrhi::BufferHandle& buffer, u64 requiredSize, u64 reallocateSize, bool isIndexBuffer)
	{
		if (buffer == nullptr || buffer->getDesc().byteSize < requiredSize)
		{
			auto bufferDesc = nvrhi::BufferDesc()
				.setByteSize(reallocateSize)
				.setStructStride(0)
				.setDebugName(IndexBuffer ? "ImGuiIndexBuffer" : "ImGuiVertexBuffer")
				.setCanHaveUAVs(false)
				.setIsVertexBuffer(!isIndexBuffer)
				.setIsVertexBuffer(isIndexBuffer)
				.setIsDrawIndirectArgs(false)
				.setIsVolatile(false)
				.setInitialState(IndexBuffer ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer)
				.setKeepInitialState(true);

			buffer = Device->createBuffer(bufferDesc);

			if (!buffer)
			{
				return false;
			}
		}

		return true;
	}
	
	nvrhi::IGraphicsPipeline* ImGuiContext::GetPSO(const nvrhi::FramebufferInfo& info)
	{
		if (PSO)
		{
			return PSO;
		}
		
		PSO = Device->createGraphicsPipeline(BasePSODesc, info);
		return PSO;
	}

	nvrhi::IBindingSet* ImGuiContext::GetBindingSet(nvrhi::ITexture* texture)
	{
		auto iter = BindingsCache.find(texture);
		if (iter != BindingsCache.end())
		{
			return iter->second;
		}

		nvrhi::BindingSetDesc desc;

		desc.bindings = {
			nvrhi::BindingSetItem::PushConstants(0, sizeof(f32) * 2),
			nvrhi::BindingSetItem::Texture_SRV(0, texture),
			nvrhi::BindingSetItem::Sampler(0, FontSampler)
		};

		nvrhi::BindingSetHandle binding;
		binding = Device->createBindingSet(desc, BindingLayout);

		BindingsCache[texture] = binding;
		return binding;
	}
}