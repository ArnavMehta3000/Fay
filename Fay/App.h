#pragma once
#include <memory>
#include "Platform/Window.h"
#include "Graphics/Camera.h"
#include "Graphics/Passes/ClearPass.h"
#include "Graphics/Passes/GeometryPass.h"
#include "Scene/GLTFImporter.h"
#include "Scene/SceneGraph.h"

namespace fay
{
	class App : IWindowEventHook
	{
	public:
		struct Desc
		{
			Window::Desc WindowDesc;
			RendererInitInfo RendererInitInfo;

			Desc() 
				: WindowDesc(Window::Desc::Default())
				, RendererInitInfo()
			{
				RendererInitInfo.DepthBufferFormat = nvrhi::Format::D32;
				RendererInitInfo.EnableWarningsAsErrors = false;
				RendererInitInfo.EnableNVRHIValidationLayer
					= RendererInitInfo.EnableDebugRuntime
					= RendererInitInfo.EnableGPUValidation = FAY_DEBUG;
				RendererInitInfo.LogBufferLifetime = true;
			}
		};
	public:
		App(const App::Desc& desc);
		~App();

		void Run();
		void OnWindowEvent(const SDL_Event&) override;

	private:
		void InitGraphics();
		void LoadScene();
		void Update();

	private:
		App::Desc                     m_desc;
		Window                        m_window;
		std::unique_ptr<Renderer>     m_renderer;
		std::unique_ptr<Scene>        m_scene;
		std::unique_ptr<GLTFImporter> m_gltfImporter;

		Camera m_camera;

		std::unique_ptr<ClearPass> m_clearPass;
		std::unique_ptr<GeometryPass> m_geometryPass;
	};
}