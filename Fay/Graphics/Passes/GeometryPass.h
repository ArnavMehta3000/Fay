#pragma once
#include "Graphics/RendererBase.h"
#include <SimpleMath.h>

namespace fay
{
	namespace SM = DirectX::SimpleMath;
	class Scene;

	class GeometryPass : IRenderPass
	{
	public:
		using IRenderPass::IRenderPass;
		virtual ~GeometryPass() = default;

		void SetFrameData(const Scene* scene, const SM::Matrix& viewProj);

		virtual bool SupportsDepthBuffer() override { return true; }
		virtual void OnBackBufferResizeBegin() {}
		virtual void OnBackBufferResizeEnd(
			u32 /*width*/,
			u32 /*height*/,
			u32 /*sampleCount*/) {}

	private:
		const Scene* m_scene = nullptr;
		SM::Matrix m_viewProj;
	};
}