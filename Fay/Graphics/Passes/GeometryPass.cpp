#include "Graphics/Passes/GeometryPass.h"
#include "Scene/SceneGraph.h"
#include "Graphics/Mesh.h"
#include "Platform/FileReader.h"
#include "Platform/Timer.h"
#include "Common/Log.h"
#include "Common/Profiling.h"
#include <nvrhi/utils.h>

namespace fay
{
	extern Timer g_timer;

	namespace
	{
		static nvrhi::TextureHandle CreateSolidTexture(
			nvrhi::IDevice* device,
			nvrhi::ICommandList* cmdList, 
			const char* name,
			u8 r, u8 g, u8 b, u8 a = 255)
		{
			auto desc = nvrhi::TextureDesc()
				.setFormat(nvrhi::Format::RGBA8_UNORM)
				.setDebugName(name)
				.setInitialState(nvrhi::ResourceStates::CopyDest)
				.setKeepInitialState(false);

			nvrhi::TextureHandle tex = device->createTexture(desc);

			u8 pixels[4] = { r, g, b, a };

			cmdList->open();
			cmdList->beginTrackingTextureState(tex, nvrhi::AllSubresources, nvrhi::ResourceStates::CopyDest);
			cmdList->writeTexture(tex, 0, 0, pixels, 4);
			cmdList->setPermanentTextureState(tex, nvrhi::ResourceStates::ShaderResource);
			cmdList->close();

			device->executeCommandList(cmdList);

			return tex;
		}
	}

	GeometryPass::GeometryPass(Renderer* renderer)
		: IRenderPass(renderer)
	{
		auto device = GetRenderer()->GetDevice();
		
		m_cmdList = device->createCommandList();

		LoadShaders();
		CreateInputLayout();
		CreateConstantBuffers();
		CreateMaterialBindingLayout();
		CreateCBBindings();
		CreateFallbackTextures();
	}

	void GeometryPass::SetFrameData(const Scene* scene, const Camera& camera)
	{
		m_scene = scene;
		m_camera = camera;
	}

	void GeometryPass::SetFillMode(nvrhi::RasterFillMode fillMode)
	{
		m_fillMode = fillMode;
		m_pipeline = nullptr;  // need to recreate the pipeline with the new fill mode
	}

	void GeometryPass::CreatePipeline(nvrhi::IFramebuffer* framebuffer)
	{
		auto device = GetRenderer()->GetDevice();

		auto psoDesc = nvrhi::GraphicsPipelineDesc()
			.setVertexShader(m_vs)
			.setPixelShader(m_ps)
			.setInputLayout(m_inputLayout)
			.addBindingLayout(m_bindingLayout)           // space 0
			.addBindingLayout(m_materialBindingLayout)   // space 1
			.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setRenderState(nvrhi::RenderState()
				.setDepthStencilState(nvrhi::DepthStencilState()
					.setDepthTestEnable(true)
					.setDepthWriteEnable(true)
					.setDepthFunc(nvrhi::ComparisonFunc::Less))
				.setRasterState(nvrhi::RasterState()
					.setFillMode(m_fillMode)
					.setFrontCounterClockwise(true)));

		m_pipeline = device->createGraphicsPipeline(psoDesc, framebuffer->getFramebufferInfo());
	}

	void GeometryPass::LoadShaders()
	{
		auto device = GetDevice();

		FileReader::ReadResult result = FileReader::Read("Shaders/MeshVS.cso");
		if (!result)
		{
			Log::Error("Failed to read MeshVS shader: {}", result.error());
			return;
		}
		auto& data = result.value();

		nvrhi::ShaderDesc desc;
		desc.setDebugName("MeshVS").setEntryName("VSMain").setShaderType(nvrhi::ShaderType::Vertex);
		m_vs = device->createShader(desc, data.data(), data.size());

		result = FileReader::Read("Shaders/MeshPS.cso");
		if (!result)
		{
			Log::Error("Failed to read MeshPS shader: {}", result.error());
			return;
		}
		data = result.value();

		desc.setDebugName("MeshPS").setEntryName("PSMain").setShaderType(nvrhi::ShaderType::Pixel);
		m_ps = device->createShader(desc, data.data(), data.size());
	}

	void GeometryPass::CreateInputLayout()
	{
		auto layout = GetStandardVertexLayout();
		m_inputLayout = GetDevice()->createInputLayout(layout.data(), static_cast<u32>(layout.size()), m_vs);
	}

	void GeometryPass::CreateConstantBuffers()
	{
		auto device = GetDevice();

		m_frameCB = device->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FrameConstants), "FrameConstants", 16));

		m_objectCB = device->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ObjectConstants), "ObjectConstants", 128));
	}

	void GeometryPass::CreateCBBindings()
	{
		nvrhi::BindingSetDesc bsDesc;
		bsDesc.bindings =
		{
			nvrhi::BindingSetItem::ConstantBuffer(0, m_frameCB),
			nvrhi::BindingSetItem::ConstantBuffer(1, m_objectCB)			
		};

		if (!nvrhi::utils::CreateBindingSetAndLayout(
			GetDevice(), nvrhi::ShaderType::All, 0, bsDesc,
			m_bindingLayout, m_bindingSet))
		{
			Log::Error("Failed to create binding set/layout!");
		}
	}

	void GeometryPass::CreateMaterialBindingLayout()
	{
		//  Layout:
		//    b0          = MaterialConstantsCB
		//    t0..t4      = base color, metallic-roughness, normal, occlusion, emissive
		//    s0..s4      = corresponding samplers

		auto layoutDesc = nvrhi::BindingLayoutDesc()
			.setVisibility(nvrhi::ShaderType::All)
			.setRegisterSpace(1)
			.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));

		for (u32 i = 0; i < 5; i++)
		{
			layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(i));
		}

		for (u32 i = 0; i < 5; i++)
		{
			layoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(i));
		}

		m_materialBindingLayout = GetDevice()->createBindingLayout(layoutDesc);
	}

	void GeometryPass::CreateFallbackTextures()
	{
		auto device = GetDevice();

		m_fallbackWhite  = CreateSolidTexture(device, m_cmdList, "Fallback_White" , 255, 255, 255, 255);
		m_fallbackNormal = CreateSolidTexture(device, m_cmdList, "Fallback_Normal", 128, 128, 255, 255);
		m_fallbackBlack  = CreateSolidTexture(device, m_cmdList, "Fallback_Black" , 0,     0,   0, 255);
		m_fallbackMR     = CreateSolidTexture(device, m_cmdList, "Fallback_MR"    , 0,    255,  0, 255); // R=0(ao), G=1(roughness), B=0(metallic)

		m_fallbackSampler = device->createSampler(nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Repeat));
	}

	nvrhi::BindingSetHandle GeometryPass::GetOrCreateMaterialBindingSet(const Material* material)
	{
		// This function assumes that m_cmdList has already been opened
		// Since this function is only called from within the render loop

		auto it = m_materialBindingSets.find(material);
		if (it != m_materialBindingSets.end())
		{
			m_cmdList->writeBuffer(it->second.first, &material->GetConstants(), sizeof(MaterialConstants));
			return it->second.second;
		}

		auto device = GetDevice();
		nvrhi::BufferHandle matCB = device->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(MaterialConstants), "MaterialCB", 16));

		m_cmdList->writeBuffer(matCB, &material->GetConstants(), sizeof(MaterialConstants));


		auto tex = [&](const TextureResource& res, nvrhi::ITexture* fallback) -> nvrhi::ITexture*
		{
			return res.IsValid() ? res.Texture.Get() : fallback;
		};
		
		auto samp = [&](const TextureResource& res) -> nvrhi::ISampler*
		{
			return (res.IsValid() && res.Sampler) ? res.Sampler.Get() : m_fallbackSampler.Get();
		};

		nvrhi::BindingSetDesc bsDesc;
		bsDesc.bindings =
		{
			nvrhi::BindingSetItem::ConstantBuffer(0, matCB),
			nvrhi::BindingSetItem::Texture_SRV(0, tex(material->GetBaseColorTexture(), m_fallbackWhite)),
			nvrhi::BindingSetItem::Texture_SRV(1, tex(material->GetMetallicRoughnessTexture(), m_fallbackMR)),
			nvrhi::BindingSetItem::Texture_SRV(2, tex(material->GetNormalTexture(), m_fallbackNormal)),
			nvrhi::BindingSetItem::Texture_SRV(3, tex(material->GetOcclusionTexture(), m_fallbackWhite)),
			nvrhi::BindingSetItem::Texture_SRV(4, tex(material->GetEmissiveTexture(), m_fallbackBlack)),
			nvrhi::BindingSetItem::Sampler(0, samp(material->GetBaseColorTexture())),
			nvrhi::BindingSetItem::Sampler(1, samp(material->GetMetallicRoughnessTexture())),
			nvrhi::BindingSetItem::Sampler(2, samp(material->GetNormalTexture())),
			nvrhi::BindingSetItem::Sampler(3, samp(material->GetOcclusionTexture())),
			nvrhi::BindingSetItem::Sampler(4, samp(material->GetEmissiveTexture())),
		};

		nvrhi::BindingSetHandle bindingSet = device->createBindingSet(bsDesc, m_materialBindingLayout);
		m_materialBindingSets[material] = { matCB, bindingSet };

		return bindingSet;
	}

	void GeometryPass::OnRender(nvrhi::IFramebuffer* framebuffer)
	{
		ZoneScoped;

		if (!m_scene) return;

		if (!m_pipeline)
		{
			CreatePipeline(framebuffer);
		}
		m_cmdList->open();

		// Update frame constants (b0, space0)
		{
			FrameConstants fc
			{
				.ViewMatrix     = m_camera.GetViewMatrix().Transpose(),
				.ProjMatrix     = m_camera.GetProjectionMatrix().Transpose(),
				.ViewProjMatrix = m_camera.GetViewProjectionMatrix().Transpose(),
				.CameraPosition = m_camera.Transform.GetPosition(),
				.Time           = g_timer.ElapsedTime(),
			};
			m_cmdList->writeBuffer(m_frameCB, &fc, sizeof(fc));
		}

		// Set up viewport from framebuffer
		const auto& fbInfo = framebuffer->getFramebufferInfo();
		nvrhi::Viewport viewport(
			static_cast<f32>(fbInfo.width),
			static_cast<f32>(fbInfo.height));

		// Draw each mesh node
		m_scene->ForEachMeshNode([&]([[maybe_unused]] const SceneNode& node, const Mesh& mesh, const SM::Matrix& worldMatrix)
		{
			// Write per-object constants (b1, space0)
			ObjectConstants oc
			{
				.WorldMatrix = worldMatrix.Transpose(),
				.NormalMatrix = worldMatrix.Invert()
			};

			m_cmdList->writeBuffer(m_objectCB, &oc, sizeof(oc));

			for (const SubMesh& sub : mesh.GetSubMeshes())
			{
				auto* materials = m_scene->ResolveMaterials(*node.GetComponent<SceneMeshComponent>());

				// ── Resolve material binding set ──
				nvrhi::BindingSetHandle materialBS;
				if (materials
					&& sub.MaterialIndex >= 0
					&& sub.MaterialIndex < static_cast<i32>(materials->size()))
				{
					materialBS = GetOrCreateMaterialBindingSet((*materials)[sub.MaterialIndex].get());
				}
				else
				{
					// No material assigned — use a default
					static Material defaultMat;
					materialBS = GetOrCreateMaterialBindingSet(&defaultMat);
				}

				// ── Graphics state ──
				auto state = nvrhi::GraphicsState()
					.setPipeline(m_pipeline)
					.setFramebuffer(framebuffer)
					.setViewport(nvrhi::ViewportState()
						.addViewportAndScissorRect(viewport))
					.addBindingSet(m_bindingSet)       // space 0
					.addBindingSet(materialBS)         // space 1
					.setIndexBuffer(nvrhi::IndexBufferBinding()
						.setBuffer(mesh.GetIndexBuffer())
						.setFormat(nvrhi::Format::R32_UINT)
						.setOffset(0))
					.addVertexBuffer(nvrhi::VertexBufferBinding()
						.setBuffer(mesh.GetVertexBuffer())
						.setSlot(0)
						.setOffset(0));

				m_cmdList->setGraphicsState(state);

				nvrhi::DrawArguments args;
				args.setVertexCount(sub.IndexCount)
					.setStartVertexLocation(sub.VertexOffset)
					.setStartIndexLocation(sub.IndexOffset);

				m_cmdList->drawIndexed(args);
			}
			});

		m_cmdList->close();
		GetRenderer()->GetDevice()->executeCommandList(m_cmdList);
	}

	void GeometryPass::OnBackBufferResizeBegin()
	{
		m_pipeline = nullptr;
	}

	void GeometryPass::OnBackBufferResizeEnd(
		[[maybe_unused]] u32 width,
		[[maybe_unused]] u32 height,
		[[maybe_unused]] u32 sampleCount)
	{
	}
}