#include "Graphics/Passes/GeometryPass.h"
#include "Scene/SceneGraph.h"
#include "Graphics/Mesh.h"
#include "Platform/FileReader.h"
#include "Common/Log.h"
#include "Common/Profiling.h"
#include <nvrhi/utils.h>

namespace fay
{
	GeometryPass::GeometryPass(Renderer* renderer)
		: IRenderPass(renderer)
	{
		auto device = GetRenderer()->GetDevice();

		// ── Load shaders ──
		{
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

		// ── Input layout from standard vertex format ──
		{
			auto layout = GetStandardVertexLayout();
			m_inputLayout = device->createInputLayout(layout.data(), static_cast<u32>(layout.size()), m_vs);
		}

		// ── Constant buffers ──
		{
			// Frame constants (b0) - updated once per frame
			m_frameCB = device->createBuffer(
				nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FrameConstants), "FrameConstants", 16));

			// Object constants (b1) - updated per draw call
			m_objectCB = device->createBuffer(
				nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ObjectConstants), "ObjectConstants", 128));
		}

		// ── Binding layout & set (both CBs in space 0) ──
		{
			nvrhi::BindingSetDesc bsDesc;
			bsDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_frameCB));
			bsDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_objectCB));

			if (!nvrhi::utils::CreateBindingSetAndLayout(
				device, nvrhi::ShaderType::All, 0, bsDesc,
				m_bindingLayout, m_bindingSet))
			{
				Log::Error("Failed to create binding set/layout!");
			}
		}

		m_cmdList = device->createCommandList();
	}

	void GeometryPass::SetFrameData(const Scene* scene, const Camera& camera)
	{
		m_scene = scene;
		m_camera = camera;
	}

	void GeometryPass::CreatePipeline(nvrhi::IFramebuffer* framebuffer)
	{
		auto device = GetRenderer()->GetDevice();

		auto rasterState = nvrhi::RasterState()
			.setFillSolid()
			.setCullBack();

		auto depthState = nvrhi::DepthStencilState()
			.enableDepthTest()
			.enableDepthWrite()
			.setDepthFunc(nvrhi::ComparisonFunc::Less);

		auto psoDesc = nvrhi::GraphicsPipelineDesc()
			.setVertexShader(m_vs)
			.setPixelShader(m_ps)
			.setInputLayout(m_inputLayout)
			.addBindingLayout(m_bindingLayout)
			.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setRenderState(nvrhi::RenderState()
				.setRasterState(rasterState)
				.setDepthStencilState(depthState));

		m_pipeline = device->createGraphicsPipeline(psoDesc, framebuffer->getFramebufferInfo());
	}

	void GeometryPass::OnRender(nvrhi::IFramebuffer* framebuffer)
	{
		ZoneScoped;

		if (!m_scene) return;

		if (!m_pipeline)
		{
			CreatePipeline(framebuffer);
		}

		m_time += 1.0f / 60.0f; // TODO: pass actual delta time

		m_cmdList->open();

		// ── Write frame constants (b0) ──
		{
			FrameConstants fc{};
			fc.ViewMatrix     = m_camera.GetViewMatrix().Transpose();
			fc.ProjMatrix     = m_camera.GetProjectionMatrix().Transpose();
			fc.ViewProjMatrix = m_camera.GetViewProjectionMatrix().Transpose();
			fc.CameraPosition = m_camera.Transform.GetPosition();
			fc.Time           = m_time;

			m_cmdList->writeBuffer(m_frameCB, &fc, sizeof(fc));
		}

		// ── Set up viewport from framebuffer ──
		const auto& fbInfo = framebuffer->getFramebufferInfo();
		nvrhi::Viewport viewport(
			static_cast<f32>(fbInfo.width),
			static_cast<f32>(fbInfo.height));

		// ── Draw each mesh node ──
		m_scene->ForEachMeshNode([&](const SceneNode& node, const Mesh& mesh, const SM::Matrix& worldMatrix)
		{
			// Write per-object constants (b1)
			ObjectConstants oc{};
			oc.WorldMatrix = worldMatrix.Transpose();

			// NormalMatrix = transpose(inverse(world))
			// Since we transpose everything for HLSL row-major consumption,
			// we send: transpose(transpose(inverse(world))) = inverse(world)
			oc.NormalMatrix = worldMatrix.Invert();

			m_cmdList->writeBuffer(m_objectCB, &oc, sizeof(oc));

			// Build graphics state
			nvrhi::GraphicsState state;
			state.setPipeline(m_pipeline)
				.setFramebuffer(framebuffer)
				.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(viewport))
				.addBindingSet(m_bindingSet)
				.setIndexBuffer(nvrhi::IndexBufferBinding()
					.setBuffer(mesh.GetIndexBuffer())
					.setFormat(nvrhi::Format::R32_UINT)
					.setOffset(0))
				.addVertexBuffer(nvrhi::VertexBufferBinding()
					.setBuffer(mesh.GetVertexBuffer())
					.setSlot(0)
					.setOffset(0));

			m_cmdList->setGraphicsState(state);

			// Draw each submesh
			for (const auto& sub : mesh.GetSubMeshes())
			{
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