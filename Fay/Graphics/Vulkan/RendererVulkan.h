#pragma once
#include "Graphics/GraphicsConfig.h"

#if FAY_HAS_VULKAN
#include "Graphics/RendererBase.h"
#include <vulkan/vulkan.hpp>
#include <nvrhi/vulkan.h>
#include <queue>
#include <unordered_set>

namespace fay
{
	class RendererVulkan final : public Renderer
	{
	public:
		RendererVulkan() {}

		bool EnumerateAdapters(std::vector<AdapterInfo>& outAdapters) override;
		inline std::wstring_view GetRendererName() const override { return m_rendererString; }
		inline nvrhi::IDevice* GetDevice() const override
		{
			if (m_validationLayer) return m_validationLayer;
			return m_nvrhiDevice;
		}
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
		bool CreateInstanceInternal();
		void InstallDebugCallback();
		bool PickPhysicalDevice();
		bool FindQueueFamilies(vk::PhysicalDevice physicalDevice);
		bool CreateDeviceInternal();
		bool CreateSwapChainInternal();
		void DestroySwapChainInternal();

	private:
		struct VulkanExtensionSet
		{
			std::unordered_set<std::string> instance;
			std::unordered_set<std::string> layers;
			std::unordered_set<std::string> device;
		};

		// Minimal required extensions
		VulkanExtensionSet m_enabledExtensions = {
			// instance
			{ VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME },
			// layers
			{ },
			// device
			{ }
		};

		// Optional extensions
		VulkanExtensionSet m_optionalExtensions = {
			// instance
			{
				VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
				VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
			},
			// layers
			{ },
			// device
			{
				VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
				VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
				VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
				VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
				VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
				VK_EXT_MESH_SHADER_EXTENSION_NAME,
				VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
			}
		};

		std::unordered_set<std::string> m_rayTracingExtensions = {
			VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
			VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
			VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
			VK_KHR_RAY_QUERY_EXTENSION_NAME,
			VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		};

		std::wstring m_rendererString;

		// Instance & debug
		vk::Instance                          m_vulkanInstance;
		vk::DebugReportCallbackEXT            m_debugReportCallback;

		// Physical device & surface
		vk::PhysicalDevice                    m_vulkanPhysicalDevice;
		vk::SurfaceKHR                        m_windowSurface;

		// Queue families
		i32                                   m_graphicsQueueFamily = -1;
		i32                                   m_computeQueueFamily = -1;
		i32                                   m_transferQueueFamily = -1;
		i32                                   m_presentQueueFamily = -1;

		// Logical device & queues
		vk::Device                            m_vulkanDevice;
		vk::Queue                             m_graphicsQueue;
		vk::Queue                             m_computeQueue;
		vk::Queue                             m_transferQueue;
		vk::Queue                             m_presentQueue;

		// Swap chain
		vk::SurfaceFormatKHR                  m_swapChainFormat;
		vk::SwapchainKHR                      m_vkSwapChain;
		bool                                  m_swapChainMutableFormatSupported = false;

		struct SwapChainImage
		{
			vk::Image image;
			nvrhi::TextureHandle rhiHandle;
		};

		std::vector<SwapChainImage>           m_swapChainImages;
		u32                                   m_swapChainIndex = u32(-1);

		// NVRHI device
		nvrhi::vulkan::DeviceHandle           m_nvrhiDevice;
		nvrhi::DeviceHandle                   m_validationLayer;

		// Synchronization (semaphore-based, matching NVRHI pattern)
		std::vector<vk::Semaphore>            m_acquireSemaphores;
		std::vector<vk::Semaphore>            m_presentSemaphores;
		u32                                   m_acquireSemaphoreIndex = 0;

		// Frame pacing via NVRHI event queries
		std::queue<nvrhi::EventQueryHandle>   m_framesInFlight;
		std::vector<nvrhi::EventQueryHandle>  m_queryPool;

		bool                                  m_bufferDeviceAddressSupported = false;
	};
}
#endif