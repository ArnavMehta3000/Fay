#pragma once
#include <string>
#include "Common/Types.h"

struct SDL_Window;

namespace fay
{
    class Window
    {
    public:
        struct Desc
        {
            std::string Name         = "Fay Renderer";
            std::pair<u32, u32> Size = { 1280, 720 };
            bool  IsResizeable       = true;

			static Desc Default() { return Desc{}; }
        };

    public:
        Window(const Window::Desc& desc);
        ~Window();

		bool PumpEvents();
        bool HasSizeChanged();

		[[nodiscard]] inline SDL_Window* GetSDL() const { return m_window; }
        [[nodiscard]] inline std::pair<u32, u32> GetSize() const { return m_desc.Size; }
	
	#if FAY_OS_WINDOWS
		void* GetHWND() const;
	#endif

	bool CreateVulkanSurface(void* instance, void* outSurface) const;

    private:
        SDL_Window* m_window;
        Desc m_desc;
		bool m_isRunning;
        bool m_hasSizeChanged;
    };
}