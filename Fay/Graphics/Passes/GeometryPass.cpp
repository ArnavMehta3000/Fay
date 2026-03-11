#include "Graphics/Passes/GeometryPass.h"

namespace fay
{
	void GeometryPass::SetFrameData(const Scene* scene, const SM::Matrix& viewProj)
	{
		m_scene = scene;
		m_viewProj = viewProj;
	}
}