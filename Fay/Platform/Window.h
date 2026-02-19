#pragma once

struct SDL_Window;

namespace fay
{
    class Window
    {
    public:
        struct Desc
        {
            std::string Name         = "Fay Renderer";
            std::pair<i32, i32> Size = { 1280, 720 };
            bool  IsResizeable       = true;

			static Desc Default() { return Desc{}; }
        };

    public:
        Window(const Window::Desc& desc);
        ~Window();

		bool PumpEvents();

		[[nodiscard]] inline SDL_Window* GetSDL() const { return m_window; }
	
	#if FAY_OS_WINDOWS
		void* GetHWND() const;
	#endif

	bool CreateVulkanSurface(void* instance, void* outSurface) const;

    private:
        SDL_Window* m_window;
        Desc m_desc;
		bool m_isRunning;
    };
}