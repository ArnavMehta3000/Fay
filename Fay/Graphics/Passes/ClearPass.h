#include "Graphics/RendererBase.h"
#include <nvrhi/utils.h>

namespace fay
{
    class ClearPass : public fay::IRenderPass
    {
    public:
        ClearPass(fay::Renderer* renderer, nvrhi::Color clearColor)
            : IRenderPass(renderer), m_clearColor(clearColor)
        {
            m_cmdList = GetDevice()->createCommandList();
        }

        void OnRender(nvrhi::IFramebuffer* framebuffer) override
        {
            m_cmdList->open();

            nvrhi::utils::ClearColorAttachment(m_cmdList, framebuffer, 0, m_clearColor);
            m_cmdList->close();

            GetDevice()->executeCommandList(m_cmdList);
        }

        std::string_view GetName() const override { return "ClearPass"; }
        bool SupportsDepthBuffer() override { return true; }
        void OnBackBufferResizeBegin() override {}
        void OnBackBufferResizeEnd(
            u32 /*width*/,
            u32 /*height*/,
            u32 /*sampleCount*/) override { }

    private:
        nvrhi::Color m_clearColor;
        nvrhi::CommandListHandle m_cmdList;
    };
}