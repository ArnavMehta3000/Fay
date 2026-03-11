#pragma once
#include "Graphics/RendererBase.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Camera.h"
#include <SimpleMath.h>

namespace fay
{
	class Scene;

	class GeometryPass : public IRenderPass
	{
	public:
		explicit GeometryPass(Renderer* renderer);
		~GeometryPass() override = default;

		void SetFrameData(const Scene* scene, const Camera& camera);

		std::string_view GetName() const override { return "GeometryPass"; }
		void OnRender(nvrhi::IFramebuffer* framebuffer) override;
		bool SupportsDepthBuffer() override { return true; }
		void OnBackBufferResizeBegin() override;
		void OnBackBufferResizeEnd(u32 width, u32 height, u32 sampleCount) override;

	private:
		void CreatePipeline(nvrhi::IFramebuffer* framebuffer);

	private:
		// CB structs matching Common.hlsli
		struct FrameConstants
		{
			SM::Matrix ViewMatrix;
			SM::Matrix ProjMatrix;
			SM::Matrix ViewProjMatrix;
			SM::Vector3 CameraPosition;
			f32 Time;
		};

		struct ObjectConstants
		{
			SM::Matrix WorldMatrix;
			SM::Matrix NormalMatrix;
		};

		static_assert(sizeof(FrameConstants) == 208);
		static_assert(sizeof(ObjectConstants) == 128);

	private:
		const Scene* m_scene = nullptr;
		Camera m_camera;
		f32 m_time = 0.0f;

		nvrhi::CommandListHandle      m_cmdList;
		nvrhi::GraphicsPipelineHandle m_pipeline;
		nvrhi::InputLayoutHandle      m_inputLayout;
		nvrhi::ShaderHandle           m_vs;
		nvrhi::ShaderHandle           m_ps;

		// Both CBs in a single binding layout (space 0)
		nvrhi::BufferHandle        m_frameCB;
		nvrhi::BufferHandle        m_objectCB;
		nvrhi::BindingLayoutHandle m_bindingLayout;
		nvrhi::BindingSetHandle    m_bindingSet;
	};
}