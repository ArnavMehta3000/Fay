#pragma once
#include <string>
#include <vector>
#include "Common/Types.h"
#include "Common/Macros.h"
#include "Graphics/GraphicsConfig.h"

struct SDL_Window;
union SDL_Event;

namespace fay
{
    enum class API : u8
    {
        Auto,
        D3D12,
		Vulkan
    };

    static constexpr inline API GetPlatformAPI(API api = API::Auto)
    {
        if (api == API::Auto)
        {
#if FAY_HAS_D3D
            return API::D3D12;
#else
            return API::VULKAN;
#endif
        }
        return api;
    }

    struct IWindowEventHook
    {
        virtual void OnWindowEvent(const SDL_Event&) = 0;
    };

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

        inline void AddEventHook(IWindowEventHook* hook) { m_eventHooks.push_back(hook); }
		[[nodiscard]] inline SDL_Window* GetSDL() const { return m_window; }
        [[nodiscard]] inline std::pair<u32, u32> GetSize() const { return m_desc.Size; }
	
	#if FAY_OS_WINDOWS
		void* GetHWND() const;
	#endif

	bool CreateVulkanSurface(void* instance, void* outSurface) const;

    private:
        SDL_Window* m_window;
        Desc        m_desc;
		bool        m_isRunning;
        std::vector<IWindowEventHook*> m_eventHooks;
    };
}