#include <SDL3/SDL.h>
#include "Platform/Window.h"
#include "Common/Profiling.h"
#include "Graphics/RendererBase.h"
#include <nvrhi/utils.h>
#include <memory>

static constexpr inline nvrhi::GraphicsAPI GetPlatformAPI()
{
#if FAY_HAS_D3D
	return nvrhi::GraphicsAPI::D3D12;
#else
	return nvrhi::GraphicsAPI::VULKAN;
#endif
}


class ClearPass : public fay::IRenderPass
{
public:
    ClearPass(fay::Renderer* renderer, nvrhi::Color clearColor)
        : IRenderPass(renderer), m_clearColor(clearColor)
    {
        m_cmdList = GetRenderer()->GetDevice()->createCommandList();
    }

    void OnRender(nvrhi::IFramebuffer* framebuffer) override
    {
        m_cmdList->open();
        nvrhi::utils::ClearColorAttachment(m_cmdList, framebuffer, 0, m_clearColor);
        m_cmdList->close();

        GetRenderer()->GetDevice()->executeCommandList(m_cmdList);
    }

    std::string_view GetName() const override { return "ClearPass"; }
    bool SupportsDepthBuffer() override { return true; }
    void OnBackBufferResizeBegin() override {}
    void OnBackBufferResizeEnd(
        [[maybe_unused]] const fay::u32 width, 
        [[maybe_unused]] const fay::u32 height, 
        [[maybe_unused]] const fay::u32 sampleCount) override {}
    
private:
    nvrhi::Color m_clearColor;
    nvrhi::CommandListHandle m_cmdList;
};

static void Run()
{
    fay::Window window(fay::Window::Desc::Default());

    std::unique_ptr<fay::Renderer> renderer(fay::Renderer::Create(GetPlatformAPI()));

    fay::RendererInitInfo info{};
    info.DepthBufferFormat = nvrhi::Format::D32;
    info.EnableWarningsAsErrors = false;
    info.EnableNVRHIValidationLayer
        = info.EnableDebugRuntime
        = info.EnableGPUValidation = FAY_DEBUG;
    info.LogBufferLifetime = true;

    if (renderer->Init(info, window))
    {
        ClearPass pass(renderer.get(), nvrhi::Color(0.015f));
        renderer->AddRenderPassToBack(&pass);

        while (window.PumpEvents())
        {
            if (window.HasSizeChanged())
            {
                renderer->Resize();
            }

            if (renderer->PreRender())
            {
                renderer->DoRenderPasses();
                renderer->PostRender();
            }

            SDL_UpdateWindowSurface(window.GetSDL());

            FrameMark;
        }
    }

    renderer->Shutdown();
}

int main()
{
    Run();

    return 0;
}
