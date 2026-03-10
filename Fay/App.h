#pragma once
#include <memory>
#include "Platform/Window.h"
#include "Graphics/RendererBase.h"
#include "Graphics/Camera.h"
#include "Scene/GLTFImporter.h"
#include "Scene/SceneGraph.h"

namespace fay
{
	class App : IWindowEventHook
	{
	public:
		App();
		~App();

		void Run();
		void OnWindowEvent(const SDL_Event&) override;

	private:
		void LoadScene();

	private:
		Window                        m_window;
		std::unique_ptr<Renderer>     m_renderer;
		std::unique_ptr<Scene>        m_scene;
		std::unique_ptr<GLTFImporter> m_gltfImporter;

		Camera m_camera;
	};
}