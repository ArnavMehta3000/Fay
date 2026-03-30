#pragma once
#include "Common/Macros.h"
#include "Common/Types.h"
#include <d3dcommon.h>
#include <dxgi1_6.h>
#include <functional>
#include <list>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <variant>

namespace fay
{
    class Window;
    class Renderer;

	struct RendererInitInfo
	{
        bool EnableDebugRuntime         : 1 = false;
        bool EnableGPUValidation        : 1 = false;  // Only affects DX12
        bool LogBufferLifetime          : 1 = true;
        bool EnableHeapDirectlyIndexed  : 1 = false;  // Only affects DX12
        bool EnableWarningsAsErrors     : 1 = false;  // Will assert/crash when reporting live objects (DX12)
        bool EnableNVRHIValidationLayer : 1 = false;
        bool EnableComputeQueue         : 1 = false;
        bool EnableCopyQueue            : 1 = false;
        bool EnableVSync                : 1 = false;

        nvrhi::Format SwapChainFormat = nvrhi::Format::SRGBA8_UNORM;
        nvrhi::Format DepthBufferFormat = nvrhi::Format::UNKNOWN;  // If unkown, then depth buffer is not created

        std::pair<u32, u32> BackBufferSize = { 0, 0 };
        u32 MaxFramesInFlight              = 2;
        u32 RefreshRate                    = 0;
        u32 SwapChainBufferCount           = 3;
        u32 SwapChainSampleCount           = 1;
        u32 SwapChainSampleQuality         = 0;

        DXGI_USAGE SwapChainUsage      = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT;
        D3D_FEATURE_LEVEL FeatureLevel = D3D_FEATURE_LEVEL_11_1;
	};

    struct AdapterInfo
    {
        using AdapterUUID = std::array<u8, 16>;
        using AdapterLUID = std::array<u8, 8>;

        // Annoying variant trick cause Windows is quirly like that
        std::variant<std::wstring, std::string> Name;
        u32 VendorID = 0;
        u32 DeviceID = 0;
        u64 DedicatedVideoMemory = 0;

        std::optional<AdapterUUID> UUID;
        std::optional<AdapterLUID> LUID;

        nvrhi::RefCountPtr<IDXGIAdapter> DXGIAdapter;
    };

    class IRenderer
    {
    public:
        virtual bool Init(const RendererInitInfo& info, Window& targetWindow) = 0;
        virtual void Shutdown() = 0;
        virtual bool EnumerateAdapters(std::vector<AdapterInfo>& outAdapters) = 0;
        virtual std::wstring_view GetRendererName() const = 0;
        virtual nvrhi::IDevice* GetDevice() const = 0;
        virtual nvrhi::ITexture* GetCurrentBackBuffer() const = 0;
        virtual nvrhi::ITexture* GetBackBuffer(u32 index) const = 0;
        virtual u32 GetCurrentBackBufferIndex() const = 0;
        virtual u32 GetBackBufferCount() const = 0;
        virtual void ReportLiveObjects() = 0;

    protected:
        virtual bool CreateDeviceIndependentResources() = 0;
        virtual bool CreateDevice() = 0;
        virtual bool CreateSwapChain() = 0;
        virtual void DestroyDeviceAndSwapChain() = 0;
        virtual void ResizeSwapChain() = 0;
        virtual bool BeginFrame() = 0;
        virtual bool Present() = 0;
    };

    class IRenderPass
    {
    public:
        explicit IRenderPass(Renderer* renderer) : m_renderer(renderer) {}
        virtual ~IRenderPass() = default;

        [[nodiscard]] inline Renderer* GetRenderer() const { return m_renderer; }
        [[nodiscard]] inline nvrhi::IDevice* GetDevice() const;

        virtual std::string_view GetName() const = 0;
        virtual void OnRender(nvrhi::IFramebuffer* framebuffer) = 0;

        // If true, will call render pass with a frame buffer that supports depth
        virtual bool SupportsDepthBuffer() = 0;

        // Release all size dependant resource in prep for resize
        virtual void OnBackBufferResizeBegin() = 0;

        // Recreate all size dependant resources after resize
        virtual void OnBackBufferResizeEnd(u32 width, u32 height, u32 sampleCount) = 0;

    private:
        Renderer* m_renderer;
    };

    class Renderer
        : public IRenderer
        , public nvrhi::IMessageCallback
    {
    public:
        static Renderer* Create();
        virtual ~Renderer() = default;

        bool Init(const RendererInitInfo& info, Window& targetWindow) override final;
        virtual void Shutdown() override;

        void Resize();

        [[nodiscard]] inline const RendererInitInfo& GetInitInfo() const { return m_initInfo; }
        [[nodiscard]] inline u32 GetFrameIndex() const { return m_frameIndex; }
        [[nodiscard]] inline nvrhi::ITexture* GetDepthBuffer() const { return m_depthBuffer; }

        [[nodiscard]] nvrhi::IFramebuffer* GetCurrentFrameBuffer(bool withDepth = true);
        [[nodiscard]] nvrhi::IFramebuffer* GetFrameBuffer(u32 index, bool withDepth = true);

        bool PreRender();
        void DoRenderPasses();
        bool PostRender();

        void AddRenderPassToFront(IRenderPass* renderPass);
        void AddRenderPassToBack(IRenderPass* renderPass);
        void RemoveRenderPass(IRenderPass* renderPass);

    protected:
        Renderer() {}

        bool CreateInstance();
        void BackBufferResizeBegin();
        void BackBufferResizeEnd();
        void CreateDepthBuffer();

        // IMessageCallback
        void message(nvrhi::MessageSeverity severity, const char* messageText) override;

    protected:
        RendererInitInfo                      m_initInfo;
        u32                                   m_frameIndex = 0;
        Window*                               m_window = nullptr;
        bool                                  m_instanceCreated = false;
        std::vector<nvrhi::FramebufferHandle> m_swapChainFrameBuffers;
        std::vector<nvrhi::FramebufferHandle> m_swapChainWithDepthFrameBuffers;
        nvrhi::TextureHandle                  m_depthBuffer;
        std::list<IRenderPass*>               m_renderPasses;  // non-owning
    };

    inline nvrhi::IDevice* fay::IRenderPass::GetDevice() const
    {
        return m_renderer->GetDevice();
    }
}