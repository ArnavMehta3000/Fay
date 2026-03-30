#include "Platform/Window.h"
#include "Common/Log.h"
#include "Common/Assert.h"
#include "Common/Profiling.h"
#include <SDL3/SDL.h>

namespace fay
{
    Window::Window(const Window::Desc& desc)
        : m_window(nullptr)
		, m_desc(desc)
		, m_isRunning(true)
    {
		ZoneScoped;
		if (SDL_Init(SDL_INIT_VIDEO) == false)
		{
			Log::Error("Failed to initialize SDL! SDL Error: {}", SDL_GetError());
			Assert(m_window);
		}
		
		u32 flags = 0;
		if (desc.IsResizeable)
		{
			flags |= SDL_WINDOW_RESIZABLE;
		}

		m_window = SDL_CreateWindow(
			m_desc.Name.c_str(), 
			m_desc.Size.first,
			m_desc.Size.second,
			flags);
		
		Assert(m_window);
		Log::Info("Window created successfully!");
    }

	Window::~Window()
	{
		if (m_window)
		{
			SDL_DestroyWindow(m_window);
			m_window = nullptr;
		}

		SDL_Quit();
	}

	bool Window::PumpEvents()
	{
		ZoneScoped;
		SDL_Event e;
		SDL_zero(e);

		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_EVENT_QUIT:
				Log::Info("Window received quit event");
				m_isRunning = false;
				break;

			case SDL_EVENT_WINDOW_RESIZED:
				m_desc.Size = { static_cast<u32>(e.window.data1), static_cast<u32>(e.window.data2) };
				Log::Info("Window received resize ({}x{}) event", m_desc.Size.first, m_desc.Size.second);
				break;
			}

			// Fire off hooks
			for (IWindowEventHook* hook : m_eventHooks)
			{
				if (hook) { hook->OnWindowEvent(e); }
			}
		}

		return m_isRunning;
	}

#if FAY_OS_WINDOWS
	void* Window::GetHWND() const
	{
		SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
		return SDL_GetPointerProperty(
			props,
			SDL_PROP_WINDOW_WIN32_HWND_POINTER,
			nullptr);
	}
#endif
}
