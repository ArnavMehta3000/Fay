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

	GeometryPass::GeometryPass(Renderer* renderer)
		: IRenderPass(renderer)
	{
		auto device = GetRenderer()->GetDevice();
		
		LoadShaders();
		CreateInputLayout();
		CreateConstantBuffers();
		CreateCBBindings();

		m_cmdList = device->createCommandList();
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
			.addBindingLayout(m_bindingLayout)
			.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setRenderState(nvrhi::RenderState()
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

		// Object constants (b1) - updated per draw call
		m_objectCB = device->createBuffer(
			nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(ObjectConstants), "ObjectConstants", 128));
	}

	void GeometryPass::CreateCBBindings()
	{
		nvrhi::BindingSetDesc bsDesc;
		bsDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_frameCB));
		bsDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(1, m_objectCB));

		if (!nvrhi::utils::CreateBindingSetAndLayout(
			GetDevice(), nvrhi::ShaderType::Vertex, 0, bsDesc,
			m_bindingLayout, m_bindingSet))
		{
			Log::Error("Failed to create binding set/layout!");
		}
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

		// Update frame constants (b0)
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
			// Write per-object constants (b1)
			ObjectConstants oc
			{
				.WorldMatrix = worldMatrix.Transpose(),

				// NormalMatrix = transpose(inverse(world))
				// Since we transpose everything for HLSL row-major consumption,
				// we send: transpose(transpose(inverse(world))) = inverse(world)
				.NormalMatrix = worldMatrix.Invert()
			};

			m_cmdList->writeBuffer(m_objectCB, &oc, sizeof(oc));

			// Build graphics state
			nvrhi::GraphicsState state;
			state.setPipeline(m_pipeline)
				.setFramebuffer(framebuffer)
				.setViewport(nvrhi::ViewportState()
					.addViewportAndScissorRect(viewport))
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
			for (const SubMesh& sub : mesh.GetSubMeshes())
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