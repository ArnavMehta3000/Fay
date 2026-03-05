#include "App.h"
#include "Common/Profiling.h"
#include "Common/Assert.h"
#include "Common/Log.h"
#include <SDL3/SDL.h>

namespace fay
{
	namespace
	{
		static constexpr inline nvrhi::GraphicsAPI GetPlatformAPI()
		{
#if FAY_HAS_D3D
			return nvrhi::GraphicsAPI::D3D12;
#else
			return nvrhi::GraphicsAPI::VULKAN;
#endif
		}
	}

	App::App()
		: m_initialized(false)
		, m_window(Window::Desc::Default())
		, m_renderer(fay::Renderer::Create(GetPlatformAPI()))
	{
		ZoneScoped;

		Log::Info("Created application");

		fay::RendererInitInfo info{};
		info.DepthBufferFormat = nvrhi::Format::D32;
		info.EnableWarningsAsErrors = false;
		info.EnableNVRHIValidationLayer
			= info.EnableDebugRuntime
			= info.EnableGPUValidation = FAY_DEBUG;
		info.LogBufferLifetime = true;

		if (!m_renderer->Init(info, m_window))
		{
			Log::Error("Failed to initialize renderer!");
		}

		m_initialized = true;
	}
	
	App::~App()
	{
		ZoneScoped;

		Log::Info("Destroying application");
		m_renderer->Shutdown();
	}
	
	void App::Run()
	{
		if (!m_initialized)
		{
			Log::Error("App not initialized! Quitting...");
			return;
		}

		Log::Info("Running application");

		while (m_window.PumpEvents())
		{
			if (m_window.HasSizeChanged())
			{
				m_renderer->Resize();
			}

			if (m_renderer->PreRender())
			{
				m_renderer->DoRenderPasses();  // Maybe rename this to RenderFrame?
				Assert(m_renderer->PostRender());
			}

			::SDL_UpdateWindowSurface(m_window.GetSDL());

			FrameMark;
		}
	}
}