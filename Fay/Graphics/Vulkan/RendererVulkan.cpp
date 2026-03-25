#include "Graphics/Vulkan/RendererVulkan.h"

#if FAY_HAS_VULKAN
#include "Platform/Window.h"
#include "Common/Assert.h"
#include "Common/Profiling.h"
#include "Common/Log.h"
#include <nvrhi/validation.h>
#include <SDL3/SDL_vulkan.h>

import std;

#undef min
#undef max

// Define the Vulkan dynamic dispatcher storage.
// This must exist in exactly one translation unit in the program.
// If NVRHI is built as a shared library (NVRHI_SHARED_LIBRARY_BUILD), it defines its own.
// If NVRHI is static, this is required. If you get duplicate symbol errors, remove this line.
#if !defined(NVRHI_SHARED_LIBRARY_BUILD)
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

namespace fay
{
	static constexpr u32 kMinimumVulkanVersion = VK_API_VERSION_1_3;

	// --- Utility ---

	static std::vector<const char*> StringSetToVector(const std::unordered_set<std::string>& set)
	{
		std::vector<const char*> ret;
		for (const auto& s : set)
		{
			ret.push_back(s.c_str());
		}
		return ret;
	}

	template <typename T>
	static std::vector<T> SetToVector(const std::unordered_set<T>& set)
	{
		std::vector<T> ret;
		for (const auto& s : set)
		{
			ret.push_back(s);
		}
		return ret;
	}

	// --- Debug callback ---

	static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT /*objType*/,
		uint64_t /*obj*/,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* /*userData*/)
	{
		if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
		{
			Log::Error("[Vulkan: location=0x{:x} code={}, layer='{}'] {}", location, code, layerPrefix, msg);
		}
		else if (flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT))
		{
			Log::Warn("[Vulkan: location=0x{:x} code={}, layer='{}'] {}", location, code, layerPrefix, msg);
		}
		else
		{
			Log::Info("[Vulkan: location=0x{:x} code={}, layer='{}'] {}", location, code, layerPrefix, msg);
		}

		return VK_FALSE;
	}

	// --- EnumerateAdapters ---

	bool RendererVulkan::EnumerateAdapters(std::vector<AdapterInfo>& outAdapters)
	{
		ZoneScoped;
		if (!m_vulkanInstance)
		{
			return false;
		}

		auto devices = m_vulkanInstance.enumeratePhysicalDevices();
		outAdapters.clear();

		for (auto physicalDevice : devices)
		{
			vk::PhysicalDeviceProperties2 properties2;
			vk::PhysicalDeviceIDProperties idProperties;
			properties2.pNext = &idProperties;
			physicalDevice.getProperties2(&properties2);

			auto const& properties = properties2.properties;

			AdapterInfo info
			{
				.Name = std::string(properties.deviceName.data()),
				.VendorID = properties.vendorID,
				.DeviceID = properties.deviceID,
				.DedicatedVideoMemory = 0,
				.vkPhysicalDevice = physicalDevice
			};

			// UUID
			AdapterInfo::AdapterUUID uuid;
			static_assert(uuid.size() == idProperties.deviceUUID.size());
			std::memcpy(uuid.data(), idProperties.deviceUUID.data(), uuid.size());
			info.UUID = uuid;

			// LUID
			if (idProperties.deviceLUIDValid)
			{
				AdapterInfo::AdapterLUID luid;
				static_assert(luid.size() == idProperties.deviceLUID.size());
				std::memcpy(luid.data(), idProperties.deviceLUID.data(), luid.size());
				info.LUID = luid;
			}

			// Sum device-local memory heaps
			auto memProps = physicalDevice.getMemoryProperties();
			for (u32 i = 0; i < memProps.memoryHeapCount; ++i)
			{
				if (memProps.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal)
				{
					info.DedicatedVideoMemory += memProps.memoryHeaps[i].size;
				}
			}

			outAdapters.push_back(std::move(info));
		}

		return true;
	}

	nvrhi::ITexture* RendererVulkan::GetCurrentBackBuffer() const
	{
		return m_swapChainImages[m_swapChainIndex].rhiHandle;
	}

	nvrhi::ITexture* RendererVulkan::GetBackBuffer(u32 index) const
	{
		return (index < static_cast<u32>(m_swapChainImages.size()))
			? m_swapChainImages[index].rhiHandle : nullptr;
	}

	u32 RendererVulkan::GetCurrentBackBufferIndex() const
	{
		return m_swapChainIndex;
	}

	u32 RendererVulkan::GetBackBufferCount() const
	{
		return static_cast<u32>(m_swapChainImages.size());
	}

	void RendererVulkan::ReportLiveObjects()
	{
		ZoneScoped;
		// Vulkan doesn't have a direct equivalent of DX12's ReportLiveObjects.
		// The validation layers will report leaks on destroy if enabled.
		Log::Info("Vulkan live object reporting relies on validation layers");
	}

	void RendererVulkan::Shutdown()
	{
		ZoneScoped;
		Renderer::Shutdown();

		if (m_initInfo.EnableDebugRuntime)
		{
			ReportLiveObjects();
		}
	}

	// --- CreateDeviceIndependentResources (Instance) ---

	bool RendererVulkan::CreateDeviceIndependentResources()
	{
		ZoneScoped;

		if (m_initInfo.EnableDebugRuntime)
		{
			m_enabledExtensions.instance.insert("VK_EXT_debug_report");
			m_enabledExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
		}

		// Initialize dynamic dispatcher via SDL
		auto vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
		if (!vkGetInstanceProcAddr)
		{
			Log::Error("Failed to get vkGetInstanceProcAddr from SDL!");
			return false;
		}
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

		return CreateInstanceInternal();
	}

	bool RendererVulkan::CreateInstanceInternal()
	{
		ZoneScoped;

		// Add SDL-required instance extensions
		u32 sdlExtCount = 0;
		const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
		for (u32 i = 0; i < sdlExtCount; i++)
		{
			m_enabledExtensions.instance.insert(std::string(sdlExts[i]));
		}

		// Add user-requested instance extensions and layers
		for (const auto& name : m_initInfo.RequiredVulkanInstanceExtensions)
			m_enabledExtensions.instance.insert(name);
		for (const auto& name : m_initInfo.OptionalVulkanInstanceExtensions)
			m_optionalExtensions.instance.insert(name);
		for (const auto& name : m_initInfo.RequiredVulkanLayers)
			m_enabledExtensions.layers.insert(name);
		for (const auto& name : m_initInfo.OptionalVulkanLayers)
			m_optionalExtensions.layers.insert(name);

		std::unordered_set<std::string> requiredExtensions = m_enabledExtensions.instance;

		// Resolve optional instance extensions
		for (const auto& instanceExt : vk::enumerateInstanceExtensionProperties())
		{
			const std::string name = instanceExt.extensionName;
			if (m_optionalExtensions.instance.find(name) != m_optionalExtensions.instance.end())
			{
				m_enabledExtensions.instance.insert(name);
			}
			requiredExtensions.erase(name);
		}

		if (!requiredExtensions.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
			for (const auto& ext : requiredExtensions)
				ss << "\n  - " << ext;
			Log::Error("{}", ss.str());
			return false;
		}

		// Resolve optional layers
		std::unordered_set<std::string> requiredLayers = m_enabledExtensions.layers;
		for (const auto& layer : vk::enumerateInstanceLayerProperties())
		{
			const std::string name = layer.layerName;
			if (m_optionalExtensions.layers.find(name) != m_optionalExtensions.layers.end())
			{
				m_enabledExtensions.layers.insert(name);
			}
			requiredLayers.erase(name);
		}

		if (!requiredLayers.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
			for (const auto& layer : requiredLayers)
				ss << "\n  - " << layer;
			Log::Error("{}", ss.str());
			return false;
		}

		Log::Info("Enabled Vulkan instance extensions:");
		for (const auto& ext : m_enabledExtensions.instance)
			Log::Info("    {}", ext);

		Log::Info("Enabled Vulkan layers:");
		for (const auto& layer : m_enabledExtensions.layers)
			Log::Info("    {}", layer);

		auto instanceExtVec = StringSetToVector(m_enabledExtensions.instance);
		auto layerVec = StringSetToVector(m_enabledExtensions.layers);

		// Query and validate API version
		auto applicationInfo = vk::ApplicationInfo();
		vk::Result res = vk::enumerateInstanceVersion(&applicationInfo.apiVersion);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to enumerate Vulkan instance version!");
			return false;
		}

		if (applicationInfo.apiVersion < kMinimumVulkanVersion)
		{
			Log::Error("Vulkan API version {}.{}.{} is too low, at least {}.{}.{} required",
				VK_API_VERSION_MAJOR(applicationInfo.apiVersion),
				VK_API_VERSION_MINOR(applicationInfo.apiVersion),
				VK_API_VERSION_PATCH(applicationInfo.apiVersion),
				VK_API_VERSION_MAJOR(kMinimumVulkanVersion),
				VK_API_VERSION_MINOR(kMinimumVulkanVersion),
				VK_API_VERSION_PATCH(kMinimumVulkanVersion));
			return false;
		}

		if (VK_API_VERSION_VARIANT(applicationInfo.apiVersion) != 0)
		{
			Log::Error("Vulkan API uses an unexpected variant: {}", VK_API_VERSION_VARIANT(applicationInfo.apiVersion));
			return false;
		}

		auto instanceInfo = vk::InstanceCreateInfo()
			.setEnabledLayerCount(static_cast<u32>(layerVec.size()))
			.setPpEnabledLayerNames(layerVec.data())
			.setEnabledExtensionCount(static_cast<u32>(instanceExtVec.size()))
			.setPpEnabledExtensionNames(instanceExtVec.data())
			.setPApplicationInfo(&applicationInfo);

		res = vk::createInstance(&instanceInfo, nullptr, &m_vulkanInstance);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create Vulkan instance! VkResult: {}", static_cast<i32>(res));
			return false;
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vulkanInstance);
		return true;
	}

	// --- Debug Callback ---

	void RendererVulkan::InstallDebugCallback()
	{
		VkDebugReportCallbackCreateInfoEXT debugInfo
		{
			.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
			.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT
						 | VK_DEBUG_REPORT_WARNING_BIT_EXT
						 | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
			.pfnCallback = VulkanDebugCallback,
			.pUserData = this
		};

		vk::Result res = m_vulkanInstance.createDebugReportCallbackEXT(
			reinterpret_cast<const vk::DebugReportCallbackCreateInfoEXT*>(&debugInfo),
			nullptr, &m_debugReportCallback);
		Assert(res == vk::Result::eSuccess);
	}

	// --- PickPhysicalDevice ---

	bool RendererVulkan::PickPhysicalDevice()
	{
		ZoneScoped;

		VkFormat requestedFormat = nvrhi::vulkan::convertFormat(m_initInfo.SwapChainFormat);
		auto devices = m_vulkanInstance.enumeratePhysicalDevices();

		std::stringstream errorStream;
		errorStream << "Cannot find a Vulkan device that supports all required extensions and properties.";

		std::vector<vk::PhysicalDevice> discreteGPUs;
		std::vector<vk::PhysicalDevice> otherGPUs;

		for (const auto& dev : devices)
		{
			vk::PhysicalDeviceProperties prop = dev.getProperties();
			errorStream << "\n" << prop.deviceName.data() << ":";

			bool deviceIsGood = true;

			// Check required device extensions
			std::unordered_set<std::string> requiredDeviceExts = m_enabledExtensions.device;
			auto deviceExtensions = dev.enumerateDeviceExtensionProperties();
			for (const auto& ext : deviceExtensions)
			{
				requiredDeviceExts.erase(std::string(ext.extensionName.data()));
			}

			if (!requiredDeviceExts.empty())
			{
				for (const auto& ext : requiredDeviceExts)
					errorStream << "\n  - missing " << ext;
				deviceIsGood = false;
			}

			// Check minimum Vulkan version
			if (prop.apiVersion < kMinimumVulkanVersion)
			{
				errorStream << "\n  - does not support Vulkan "
					<< VK_API_VERSION_MAJOR(kMinimumVulkanVersion) << "."
					<< VK_API_VERSION_MINOR(kMinimumVulkanVersion);
				deviceIsGood = false;
			}

			// Check required features
			vk::PhysicalDeviceFeatures2 deviceFeatures2{};
			vk::PhysicalDeviceVulkan13Features vulkan13Features{};
			deviceFeatures2.pNext = &vulkan13Features;
			dev.getFeatures2(&deviceFeatures2);

			if (!deviceFeatures2.features.samplerAnisotropy)
			{
				errorStream << "\n  - does not support samplerAnisotropy";
				deviceIsGood = false;
			}

			if (!vulkan13Features.dynamicRendering)
			{
				errorStream << "\n  - does not support dynamicRendering";
				deviceIsGood = false;
			}

			if (!vulkan13Features.synchronization2)
			{
				errorStream << "\n  - does not support synchronization2";
				deviceIsGood = false;
			}

			// Check queue families
			if (!FindQueueFamilies(dev))
			{
				errorStream << "\n  - does not support the necessary queue types";
				deviceIsGood = false;
			}

			// Check surface support
			if (m_windowSurface)
			{
				bool surfaceSupported = dev.getSurfaceSupportKHR(m_presentQueueFamily, m_windowSurface);
				if (!surfaceSupported)
				{
					errorStream << "\n  - does not support the window surface";
					deviceIsGood = false;
				}
				else
				{
					auto surfaceCaps = dev.getSurfaceCapabilitiesKHR(m_windowSurface);
					auto surfaceFmts = dev.getSurfaceFormatsKHR(m_windowSurface);

					if (surfaceCaps.minImageCount > m_initInfo.SwapChainBufferCount ||
						(surfaceCaps.maxImageCount < m_initInfo.SwapChainBufferCount && surfaceCaps.maxImageCount > 0))
					{
						errorStream << "\n  - cannot support the requested swap chain image count";
						deviceIsGood = false;
					}

					bool surfaceFormatPresent = false;
					for (const auto& surfaceFmt : surfaceFmts)
					{
						if (surfaceFmt.format == vk::Format(requestedFormat))
						{
							surfaceFormatPresent = true;
							break;
						}
					}

					if (!surfaceFormatPresent)
					{
						errorStream << "\n  - does not support the requested swap chain format";
						deviceIsGood = false;
					}
				}
			}

			if (!deviceIsGood)
				continue;

			if (prop.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
				discreteGPUs.push_back(dev);
			else
				otherGPUs.push_back(dev);
		}

		if (!discreteGPUs.empty())
		{
			m_vulkanPhysicalDevice = discreteGPUs[0];
			return true;
		}

		if (!otherGPUs.empty())
		{
			m_vulkanPhysicalDevice = otherGPUs[0];
			return true;
		}

		Log::Error("{}", errorStream.str());
		return false;
	}

	// --- FindQueueFamilies ---

	bool RendererVulkan::FindQueueFamilies(vk::PhysicalDevice physicalDevice)
	{
		auto props = physicalDevice.getQueueFamilyProperties();

		m_graphicsQueueFamily = -1;
		m_computeQueueFamily = -1;
		m_transferQueueFamily = -1;
		m_presentQueueFamily = -1;

		for (i32 i = 0; i < static_cast<i32>(props.size()); i++)
		{
			const auto& queueFamily = props[i];

			if (m_graphicsQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_graphicsQueueFamily = i;
				}
			}

			// Prefer dedicated compute queue (no graphics)
			if (m_computeQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_computeQueueFamily = i;
				}
			}

			// Prefer dedicated transfer queue (no compute, no graphics)
			if (m_transferQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0 &&
					(queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
					!(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_transferQueueFamily = i;
				}
			}

			// Present queue - check surface support via SDL
			if (m_presentQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0)
				{
					// On Linux without a surface yet, the graphics queue is a safe default.
					// With a surface, we verify support in PickPhysicalDevice.
					if (m_windowSurface)
					{
						VkBool32 presentSupport = VK_FALSE;
						vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_windowSurface, &presentSupport);
						if (presentSupport)
						{
							m_presentQueueFamily = i;
						}
					}
					else if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
					{
						// Assume the graphics queue can present when we have no surface yet
						m_presentQueueFamily = i;
					}
				}
			}
		}

		if (m_graphicsQueueFamily == -1 ||
			m_presentQueueFamily == -1 ||
			(m_computeQueueFamily == -1 && m_initInfo.EnableComputeQueue) ||
			(m_transferQueueFamily == -1 && m_initInfo.EnableCopyQueue))
		{
			return false;
		}

		return true;
	}

	// --- CreateDevice ---

	bool RendererVulkan::CreateDevice()
	{
		ZoneScoped;

		if (m_initInfo.EnableDebugRuntime)
		{
			InstallDebugCallback();
		}

		// Add user-requested device extensions
		for (const auto& name : m_initInfo.RequiredVulkanDeviceExtensions)
			m_enabledExtensions.device.insert(name);
		for (const auto& name : m_initInfo.OptionalVulkanDeviceExtensions)
			m_optionalExtensions.device.insert(name);

		// Prefer BGRA over RGBA on Vulkan (as Donut does)
		if (m_initInfo.SwapChainFormat == nvrhi::Format::SRGBA8_UNORM)
			m_initInfo.SwapChainFormat = nvrhi::Format::SBGRA8_UNORM;
		else if (m_initInfo.SwapChainFormat == nvrhi::Format::RGBA8_UNORM)
			m_initInfo.SwapChainFormat = nvrhi::Format::BGRA8_UNORM;

		// Create window surface
		Assert(m_window);
		{
			VkInstance instance = m_vulkanInstance;
			VkSurfaceKHR surface = VK_NULL_HANDLE;
			if (!m_window->CreateVulkanSurface(&instance, &surface))
			{
				Log::Error("Failed to create Vulkan surface!");
				return false;
			}
			m_windowSurface = vk::SurfaceKHR(surface);
		}

		if (!PickPhysicalDevice())
		{
			Log::Error("Failed to pick a suitable Vulkan physical device!");
			return false;
		}

		if (!FindQueueFamilies(m_vulkanPhysicalDevice))
		{
			Log::Error("Failed to find required queue families on selected device!");
			return false;
		}

		if (!CreateDeviceInternal())
		{
			Log::Error("Failed to create Vulkan logical device!");
			return false;
		}

		// Create NVRHI device
		auto vecInstanceExt = StringSetToVector(m_enabledExtensions.instance);
		auto vecDeviceExt = StringSetToVector(m_enabledExtensions.device);

		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.errorCB = this;
		deviceDesc.instance = m_vulkanInstance;
		deviceDesc.physicalDevice = m_vulkanPhysicalDevice;
		deviceDesc.device = m_vulkanDevice;
		deviceDesc.graphicsQueue = m_graphicsQueue;
		deviceDesc.graphicsQueueIndex = m_graphicsQueueFamily;

		if (m_initInfo.EnableComputeQueue)
		{
			deviceDesc.computeQueue = m_computeQueue;
			deviceDesc.computeQueueIndex = m_computeQueueFamily;
		}

		if (m_initInfo.EnableCopyQueue)
		{
			deviceDesc.transferQueue = m_transferQueue;
			deviceDesc.transferQueueIndex = m_transferQueueFamily;
		}

		deviceDesc.instanceExtensions           = vecInstanceExt.data();
		deviceDesc.numInstanceExtensions        = vecInstanceExt.size();
		deviceDesc.deviceExtensions             = vecDeviceExt.data();
		deviceDesc.numDeviceExtensions          = vecDeviceExt.size();
		deviceDesc.bufferDeviceAddressSupported = m_bufferDeviceAddressSupported;
		deviceDesc.logBufferLifetime            = false;//m_initInfo.LogBufferLifetime;

		m_nvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);

		if (m_initInfo.EnableNVRHIValidationLayer)
		{
			m_validationLayer = nvrhi::validation::createValidationLayer(m_nvrhiDevice);
		}

		return true;
	}

	bool RendererVulkan::CreateDeviceInternal()
	{
		ZoneScoped;

		// Resolve optional device extensions
		auto deviceExtensions = m_vulkanPhysicalDevice.enumerateDeviceExtensionProperties();
		for (const auto& ext : deviceExtensions)
		{
			const std::string name = ext.extensionName;
			if (m_optionalExtensions.device.find(name) != m_optionalExtensions.device.end())
			{
				if (name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME)
					m_swapChainMutableFormatSupported = true;
				m_enabledExtensions.device.insert(name);
			}

			if (m_initInfo.EnableRayTracingExtensions &&
				m_rayTracingExtensions.find(name) != m_rayTracingExtensions.end())
			{
				m_enabledExtensions.device.insert(name);
			}
		}

		// Always need swapchain
		m_enabledExtensions.device.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		// Cache renderer string
		{
			auto physProps = m_vulkanPhysicalDevice.getProperties();
			std::string name(physProps.deviceName.data());
			m_rendererString = std::wstring(name.begin(), name.end());
			Log::Info("Creating Vulkan device: {}", name);
		}

		// Detect supported features
		bool accelStructSupported = false;
		bool rayPipelineSupported = false;
		bool rayQuerySupported = false;
		bool meshShaderSupported = false;
		bool mutableDescriptorTypeSupported = false;

		Log::Info("Enabled Vulkan device extensions:");
		for (const auto& ext : m_enabledExtensions.device)
		{
			Log::Info("    {}", ext);

			if (ext == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
				accelStructSupported = true;
			else if (ext == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
				rayPipelineSupported = true;
			else if (ext == VK_KHR_RAY_QUERY_EXTENSION_NAME)
				rayQuerySupported = true;
			else if (ext == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME)
				m_swapChainMutableFormatSupported = true;
			else if (ext == VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME)
				mutableDescriptorTypeSupported = true;
			else if (ext == VK_EXT_MESH_SHADER_EXTENSION_NAME)
				meshShaderSupported = true;
		}

		// Build pNext chain for feature structures
#define APPEND_EXTENSION(condition, desc) if (condition) { (desc).pNext = pNext; pNext = &(desc); }

		void* pNext = nullptr;

		// Query buffer device address support
		auto bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures();
		auto meshShaderFeatures = vk::PhysicalDeviceMeshShaderFeaturesEXT();
		vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2;

		APPEND_EXTENSION(true, bufferDeviceAddressFeatures);
		APPEND_EXTENSION(meshShaderSupported, meshShaderFeatures);
		physicalDeviceFeatures2.pNext = pNext;
		m_vulkanPhysicalDevice.getFeatures2(&physicalDeviceFeatures2);

		// Queue creation
		std::unordered_set<i32> uniqueQueueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily };
		if (m_initInfo.EnableComputeQueue)
			uniqueQueueFamilies.insert(m_computeQueueFamily);
		if (m_initInfo.EnableCopyQueue)
			uniqueQueueFamilies.insert(m_transferQueueFamily);

		float priority = 1.0f;
		std::vector<vk::DeviceQueueCreateInfo> queueDesc;
		queueDesc.reserve(uniqueQueueFamilies.size());
		for (i32 queueFamily : uniqueQueueFamilies)
		{
			queueDesc.push_back(vk::DeviceQueueCreateInfo()
				.setQueueFamilyIndex(queueFamily)
				.setQueueCount(1)
				.setPQueuePriorities(&priority));
		}

		// Feature chains for device creation
		auto accelStructFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
			.setAccelerationStructure(true);
		auto rayPipelineFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
			.setRayTracingPipeline(true)
			.setRayTraversalPrimitiveCulling(true);
		auto rayQueryFeatures = vk::PhysicalDeviceRayQueryFeaturesKHR()
			.setRayQuery(true);
		auto vulkan13features = vk::PhysicalDeviceVulkan13Features()
			.setDynamicRendering(true)
			.setSynchronization2(true)
			.setMaintenance4(true);
		auto mutableDescriptorTypeFeatures = vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT()
			.setMutableDescriptorType(true);

		pNext = nullptr;
		APPEND_EXTENSION(true, vulkan13features)
			APPEND_EXTENSION(accelStructSupported, accelStructFeatures)
			APPEND_EXTENSION(rayPipelineSupported, rayPipelineFeatures)
			APPEND_EXTENSION(rayQuerySupported, rayQueryFeatures)
			APPEND_EXTENSION(mutableDescriptorTypeSupported, mutableDescriptorTypeFeatures)
			APPEND_EXTENSION(meshShaderSupported, meshShaderFeatures)

			// Disable mesh shader features that require other features
			meshShaderFeatures.multiviewMeshShader = false;
		meshShaderFeatures.primitiveFragmentShadingRateMeshShader = false;

#undef APPEND_EXTENSION

		auto deviceFeatures = vk::PhysicalDeviceFeatures()
			.setShaderImageGatherExtended(true)
			.setSamplerAnisotropy(true)
			.setTessellationShader(true)
			.setTextureCompressionBC(physicalDeviceFeatures2.features.textureCompressionBC)
			.setGeometryShader(true)
			.setImageCubeArray(true)
			.setShaderInt16(true)
			.setFillModeNonSolid(true)
			.setFragmentStoresAndAtomics(true)
			.setDualSrcBlend(true)
			.setVertexPipelineStoresAndAtomics(true)
			.setShaderInt64(physicalDeviceFeatures2.features.shaderInt64)
			.setShaderStorageImageWriteWithoutFormat(true)
			.setShaderStorageImageReadWithoutFormat(true);

		auto vulkan11features = vk::PhysicalDeviceVulkan11Features()
			.setStorageBuffer16BitAccess(true)
			.setPNext(pNext);

		auto vulkan12features = vk::PhysicalDeviceVulkan12Features()
			.setDescriptorIndexing(true)
			.setRuntimeDescriptorArray(true)
			.setDescriptorBindingPartiallyBound(true)
			.setDescriptorBindingVariableDescriptorCount(true)
			.setTimelineSemaphore(true)
			.setShaderSampledImageArrayNonUniformIndexing(true)
			.setBufferDeviceAddress(bufferDeviceAddressFeatures.bufferDeviceAddress)
			.setShaderSubgroupExtendedTypes(true)
			.setScalarBlockLayout(true)
			.setPNext(&vulkan11features);

		m_bufferDeviceAddressSupported = bufferDeviceAddressFeatures.bufferDeviceAddress;

		auto extVec = StringSetToVector(m_enabledExtensions.device);

		auto deviceDesc = vk::DeviceCreateInfo()
			.setPQueueCreateInfos(queueDesc.data())
			.setQueueCreateInfoCount(static_cast<u32>(queueDesc.size()))
			.setPEnabledFeatures(&deviceFeatures)
			.setEnabledExtensionCount(static_cast<u32>(extVec.size()))
			.setPpEnabledExtensionNames(extVec.data())
			.setPNext(&vulkan12features);

		// Allow user modification of device create info
		if (m_initInfo.VkDeviceCreateInfoCallback)
			m_initInfo.VkDeviceCreateInfoCallback(deviceDesc);

		const vk::Result res = m_vulkanPhysicalDevice.createDevice(&deviceDesc, nullptr, &m_vulkanDevice);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create Vulkan logical device! VkResult: {}", static_cast<i32>(res));
			return false;
		}

		// Retrieve queues
		m_vulkanDevice.getQueue(m_graphicsQueueFamily, 0, &m_graphicsQueue);
		m_vulkanDevice.getQueue(m_presentQueueFamily, 0, &m_presentQueue);

		if (m_initInfo.EnableComputeQueue)
			m_vulkanDevice.getQueue(m_computeQueueFamily, 0, &m_computeQueue);
		if (m_initInfo.EnableCopyQueue)
			m_vulkanDevice.getQueue(m_transferQueueFamily, 0, &m_transferQueue);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vulkanDevice);

		return true;
	}

	// --- CreateSwapChain ---

	bool RendererVulkan::CreateSwapChain()
	{
		ZoneScoped;

		if (!CreateSwapChainInternal())
		{
			Log::Error("Failed to create Vulkan swap chain!");
			return false;
		}

		// Create present semaphores (one per swap chain image)
		const size_t numPresentSemaphores = m_swapChainImages.size();
		m_presentSemaphores.reserve(numPresentSemaphores);
		for (u32 i = 0; i < numPresentSemaphores; ++i)
		{
			m_presentSemaphores.push_back(m_vulkanDevice.createSemaphore(vk::SemaphoreCreateInfo()));
		}

		// Create acquire semaphores (at least maxFramesInFlight, at least swap chain image count)
		const size_t numAcquireSemaphores = std::max(
			static_cast<size_t>(m_initInfo.MaxFramesInFlight),
			m_swapChainImages.size());
		m_acquireSemaphores.reserve(numAcquireSemaphores);
		for (u32 i = 0; i < numAcquireSemaphores; ++i)
		{
			m_acquireSemaphores.push_back(m_vulkanDevice.createSemaphore(vk::SemaphoreCreateInfo()));
		}

		return true;
	}

	bool RendererVulkan::CreateSwapChainInternal()
	{
		ZoneScoped;
		DestroySwapChainInternal();

		m_swapChainFormat = {
			vk::Format(nvrhi::vulkan::convertFormat(m_initInfo.SwapChainFormat)),
			vk::ColorSpaceKHR::eSrgbNonlinear
		};

		auto [width, height] = m_initInfo.BackBufferSize;
		vk::Extent2D extent(width, height);

		std::unordered_set<u32> uniqueQueues = {
			static_cast<u32>(m_graphicsQueueFamily),
			static_cast<u32>(m_presentQueueFamily)
		};
		std::vector<u32> queues = SetToVector(uniqueQueues);
		const bool enableSharing = queues.size() > 1;

		auto desc = vk::SwapchainCreateInfoKHR()
			.setSurface(m_windowSurface)
			.setMinImageCount(m_initInfo.SwapChainBufferCount)
			.setImageFormat(m_swapChainFormat.format)
			.setImageColorSpace(m_swapChainFormat.colorSpace)
			.setImageExtent(extent)
			.setImageArrayLayers(1)
			.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment
				| vk::ImageUsageFlagBits::eTransferDst
				| vk::ImageUsageFlagBits::eSampled)
			.setImageSharingMode(enableSharing ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive)
			.setFlags(m_swapChainMutableFormatSupported ? vk::SwapchainCreateFlagBitsKHR::eMutableFormat : vk::SwapchainCreateFlagBitsKHR(0))
			.setQueueFamilyIndexCount(enableSharing ? static_cast<u32>(queues.size()) : 0)
			.setPQueueFamilyIndices(enableSharing ? queues.data() : nullptr)
			.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
			.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
			.setPresentMode(m_initInfo.EnableVSync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate)
			.setClipped(true)
			.setOldSwapchain(nullptr);

		// Mutable format support: add both SRGB and UNORM views
		std::vector<vk::Format> imageFormats = { m_swapChainFormat.format };
		switch (m_swapChainFormat.format)
		{
		case vk::Format::eR8G8B8A8Unorm:  imageFormats.push_back(vk::Format::eR8G8B8A8Srgb);  break;
		case vk::Format::eR8G8B8A8Srgb:   imageFormats.push_back(vk::Format::eR8G8B8A8Unorm);  break;
		case vk::Format::eB8G8R8A8Unorm:  imageFormats.push_back(vk::Format::eB8G8R8A8Srgb);  break;
		case vk::Format::eB8G8R8A8Srgb:   imageFormats.push_back(vk::Format::eB8G8R8A8Unorm);  break;
		default: break;
		}

		auto imageFormatListCreateInfo = vk::ImageFormatListCreateInfo()
			.setViewFormats(imageFormats);

		if (m_swapChainMutableFormatSupported)
			desc.pNext = &imageFormatListCreateInfo;

		const vk::Result res = m_vulkanDevice.createSwapchainKHR(&desc, nullptr, &m_vkSwapChain);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create Vulkan swap chain! VkResult: {}", static_cast<i32>(res));
			return false;
		}

		// Retrieve swap chain images and create NVRHI handles
		auto images = m_vulkanDevice.getSwapchainImagesKHR(m_vkSwapChain);
		for (auto image : images)
		{
			SwapChainImage sci;
			sci.image = image;

			nvrhi::TextureDesc textureDesc;
			textureDesc.width = width;
			textureDesc.height = height;
			textureDesc.format = m_initInfo.SwapChainFormat;
			textureDesc.debugName = "Swap chain image";
			textureDesc.initialState = nvrhi::ResourceStates::Present;
			textureDesc.keepInitialState = true;
			textureDesc.isRenderTarget = true;

			sci.rhiHandle = m_nvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, nvrhi::Object(sci.image), textureDesc);

			m_swapChainImages.push_back(sci);
		}

		m_swapChainIndex = 0;
		return true;
	}

	void RendererVulkan::DestroySwapChainInternal()
	{
		if (m_vulkanDevice)
		{
			m_vulkanDevice.waitIdle();
		}

		if (m_vkSwapChain)
		{
			m_vulkanDevice.destroySwapchainKHR(m_vkSwapChain);
			m_vkSwapChain = nullptr;
		}

		m_swapChainImages.clear();
	}

	// --- ResizeSwapChain ---

	void RendererVulkan::ResizeSwapChain()
	{
		ZoneScoped;
		if (m_vulkanDevice)
		{
			DestroySwapChainInternal();
			CreateSwapChainInternal();
		}
	}

	// --- DestroyDeviceAndSwapChain ---

	void RendererVulkan::DestroyDeviceAndSwapChain()
	{
		ZoneScoped;

		DestroySwapChainInternal();

		// Destroy present semaphores
		for (auto& semaphore : m_presentSemaphores)
		{
			if (semaphore)
			{
				m_vulkanDevice.destroySemaphore(semaphore);
				semaphore = vk::Semaphore();
			}
		}
		m_presentSemaphores.clear();

		// Destroy acquire semaphores
		for (auto& semaphore : m_acquireSemaphores)
		{
			if (semaphore)
			{
				m_vulkanDevice.destroySemaphore(semaphore);
				semaphore = vk::Semaphore();
			}
		}
		m_acquireSemaphores.clear();

		// Clear NVRHI frame queries
		while (!m_framesInFlight.empty())
			m_framesInFlight.pop();
		m_queryPool.clear();

		m_nvrhiDevice = nullptr;
		m_validationLayer = nullptr;
		m_rendererString.clear();

		if (m_vulkanDevice)
		{
			m_vulkanDevice.destroy();
			m_vulkanDevice = nullptr;
		}

		if (m_windowSurface)
		{
			Assert(m_vulkanInstance);
			m_vulkanInstance.destroySurfaceKHR(m_windowSurface);
			m_windowSurface = nullptr;
		}

		if (m_debugReportCallback)
		{
			m_vulkanInstance.destroyDebugReportCallbackEXT(m_debugReportCallback);
			m_debugReportCallback = nullptr;
		}

		if (m_vulkanInstance)
		{
			m_vulkanInstance.destroy();
			m_vulkanInstance = nullptr;
		}
	}

	// --- BeginFrame ---

	bool RendererVulkan::BeginFrame()
	{
		ZoneScoped;

		const auto& semaphore = m_acquireSemaphores[m_acquireSemaphoreIndex];

		vk::Result res;
		constexpr i32 maxAttempts = 3;

		for (i32 attempt = 0; attempt < maxAttempts; ++attempt)
		{
			res = m_vulkanDevice.acquireNextImageKHR(
				m_vkSwapChain,
				std::numeric_limits<u64>::max(),
				semaphore,
				vk::Fence(),
				&m_swapChainIndex);

			if ((res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) && attempt < maxAttempts - 1)
			{
				auto surfaceCaps = m_vulkanPhysicalDevice.getSurfaceCapabilitiesKHR(m_windowSurface);
				m_initInfo.BackBufferSize = {
					surfaceCaps.currentExtent.width,
					surfaceCaps.currentExtent.height
				};

				BackBufferResizeBegin();
				ResizeSwapChain();
				BackBufferResizeEnd();
			}
			else
			{
				break;
			}
		}

		m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % static_cast<u32>(m_acquireSemaphores.size());

		if (res == vk::Result::eSuccess || res == vk::Result::eSuboptimalKHR)
		{
			// Schedule the wait. The actual wait will be submitted when the app executes any command list.
			m_nvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
			return true;
		}

		return false;
	}

	// --- Present ---

	bool RendererVulkan::Present()
	{
		ZoneScoped;

		const auto& semaphore = m_presentSemaphores[m_swapChainIndex];

		m_nvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);

		// NVRHI buffers semaphores and signals them when something is submitted to a queue.
		// Call executeCommandLists with no command lists to actually signal the semaphore.
		m_nvrhiDevice->executeCommandLists(nullptr, 0);

		auto presentInfo = vk::PresentInfoKHR()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&semaphore)
			.setSwapchainCount(1)
			.setPSwapchains(&m_vkSwapChain)
			.setPImageIndices(&m_swapChainIndex);

		const vk::Result res = m_presentQueue.presentKHR(&presentInfo);
		if (!(res == vk::Result::eSuccess ||
			res == vk::Result::eErrorOutOfDateKHR ||
			res == vk::Result::eSuboptimalKHR))
		{
			return false;
		}

#ifndef _WIN32
		if (m_initInfo.EnableVSync || m_initInfo.EnableDebugRuntime)
		{
			// On Linux, validation layers expect explicit GPU sync
			m_presentQueue.waitIdle();
		}
#endif

		// Frame pacing via NVRHI event queries
		while (m_framesInFlight.size() >= m_initInfo.MaxFramesInFlight)
		{
			auto query = m_framesInFlight.front();
			m_framesInFlight.pop();

			m_nvrhiDevice->waitEventQuery(query);
			m_queryPool.push_back(query);
		}

		nvrhi::EventQueryHandle query;
		if (!m_queryPool.empty())
		{
			query = m_queryPool.back();
			m_queryPool.pop_back();
		}
		else
		{
			query = m_nvrhiDevice->createEventQuery();
		}

		m_nvrhiDevice->resetEventQuery(query);
		m_nvrhiDevice->setEventQuery(query, nvrhi::CommandQueue::Graphics);
		m_framesInFlight.push(query);

		return true;
	}
}

#endif