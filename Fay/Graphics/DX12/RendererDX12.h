#pragma once

#include "Graphics/RendererBase.h"
#include <nvrhi/d3d12.h>

namespace fay
{
	class RendererDX12 final : public Renderer
	{
	public:
		RendererDX12() {}

		bool EnumerateAdapters(std::vector<AdapterInfo>& outAdapters) override;
		inline std::wstring_view GetRendererName() const override { return m_rendererString; }
		inline nvrhi::IDevice* GetDevice() const override { return m_nvrhiDevice; }
		nvrhi::ITexture* GetCurrentBackBuffer() const override;
		nvrhi::ITexture* GetBackBuffer(u32 index) const override;
		u32 GetCurrentBackBufferIndex() const override;
		u32 GetBackBufferCount() const override;
		void ReportLiveObjects() override;
		void Shutdown() override;

	protected:
		bool CreateDeviceIndependentResources() override;
		bool CreateDevice() override;
		bool CreateSwapChain() override;
		void DestroyDeviceAndSwapChain() override;
		void ResizeSwapChain() override;
		bool BeginFrame() override;
		bool Present() override;

	private:
		bool CreateRenderTargets();
		void ReleaseRenderTargets();

	private:
		nvrhi::RefCountPtr<IDXGIFactory2>               m_dxgiFactory;
		nvrhi::RefCountPtr<ID3D12Device>                m_device;
		nvrhi::RefCountPtr<ID3D12CommandQueue>          m_graphicsQueue;
		nvrhi::RefCountPtr<ID3D12CommandQueue>          m_computeQueue;
		nvrhi::RefCountPtr<ID3D12CommandQueue>          m_copyQueue;
		nvrhi::RefCountPtr<IDXGISwapChain3>             m_swapChain;
		nvrhi::RefCountPtr<IDXGIAdapter>                m_dxgiAdapter;
		DXGI_SWAP_CHAIN_DESC1                           m_swapChainDesc{};
		DXGI_SWAP_CHAIN_FULLSCREEN_DESC                 m_fullScreenDesc{};
		std::vector<nvrhi::RefCountPtr<ID3D12Resource>> m_swapChainBuffers;
		std::vector<nvrhi::TextureHandle>               m_rhiSwapChainBuffers;
		nvrhi::RefCountPtr<ID3D12Fence>                 m_frameFence;
		std::vector<HANDLE>                             m_frameFenceEvents;
		u64                                             m_frameCount = 1;
		nvrhi::DeviceHandle                             m_nvrhiDevice;
		std::wstring                                    m_rendererString;
		bool                                            m_isTearingSupported = false;
	};
}
