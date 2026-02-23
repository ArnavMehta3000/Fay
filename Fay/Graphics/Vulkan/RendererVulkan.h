#pragma once
#include "Graphics/GraphicsConfig.h"
#if FAY_HAS_VULKAN
#include "Graphics/RendererBase.h"
#include <vulkan/vulkan.hpp>
#include <nvrhi/vulkan.h>

namespace fay
{
	class RendererVulkan final : public Renderer
	{
	public:
		RendererVulkan() {};

		bool EnumerateAdapters(std::vector<AdapterInfo>& outAdapters) override;
		inline std::wstring_view GetRendererName() const override { return m_rendererString; }
		inline nvrhi::IDevice* GetDevice() const override { return m_validationLayer ?  m_validationLayer : m_nvrhiDevice; }
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
		void InstallDebugCallback();
		bool PickPhysicalDevice();
		bool FindQueueFamilies(vk::PhysicalDevice physicalDevice);

	private:
#if VK_HEADER_VERSION >= 301
		typedef vk::detail::DynamicLoader VulkanDynamicLoader;
#else
		typedef vk::DynamicLoader VulkanDynamicLoader;
#endif
		struct SwapChainImage
		{
			vk::Image Image;
			nvrhi::TextureHandle NvrhiHandle;
		};

		std::wstring                         m_rendererString;
		vk::Instance                         m_vkInstance;
		vk::DebugReportCallbackEXT           m_debugReportCallback;
		vk::PhysicalDevice                   m_vkPhysicalDevice;
		i32                                  m_graphicsQueueFamily = -1;
		i32                                  m_computeQueueFamily  = -1;
		i32                                  m_transferQueueFamily = -1;
		i32                                  m_presentQueueFamily  = -1;
		vk::Device                           m_vkDevice;
		vk::Queue                            m_graphicsQueue;
		vk::Queue                            m_computeQueue;
		vk::Queue                            m_transferQueue;
		vk::Queue                            m_presentQueue;
		vk::SurfaceKHR                       m_windowSurface;
		vk::SurfaceFormatKHR                 m_swapChainFormat;
		vk::SwapchainKHR                     m_swapChain;
		std::vector<SwapChainImage>          m_swapChainImages;
		nvrhi::vulkan::DeviceHandle          m_nvrhiDevice;
		nvrhi::DeviceHandle                  m_validationLayer;
		std::vector<vk::Semaphore>           m_acquireSemaphores;
		std::vector<vk::Semaphore>           m_presentSemaphores;
		u32                                  m_swapChainIndex = u32(-1);
		u32                                  m_acquireSemaphoreIndex = 0;
		std::queue<nvrhi::EventQueryHandle>  m_framesInFlight;
		std::vector<nvrhi::EventQueryHandle> m_queryPool;
		bool                                 m_swapChainMutableFormatSupported = false;
		bool                                 m_bufferDeviceAddressSupported = false;
		std::unique_ptr<VulkanDynamicLoader> m_dynamicLoader;
	};
}
#endif