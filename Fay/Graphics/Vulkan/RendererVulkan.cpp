#include "Graphics/Vulkan/RendererVulkan.h"

#if FAY_HAS_VULKAN
#include <unordered_set>
#include <sstream>
#include <numeric>
#include "Common/Assert.h"
#include "Common/Profiling.h"
#include "Common/Log.h"
#include "Platform/Window.h"
#include <nvrhi/validation.h>
#include <SDL3/SDL_vulkan.h>
#include "RendererVulkan.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define CHECK(x) if (!(x)) { return false; }

namespace fay
{
	static constexpr u32 s_computeQueueIndex  = 0;
	static constexpr u32 s_graphicsQueueIndex = 0;
	static constexpr u32 s_presentQueueIndex  = 0;
	static constexpr u32 s_transferQueueIndex = 0;

	struct VulkanExtensionSet
	{
		std::unordered_set<std::string> Instance;
		std::unordered_set<std::string> Layers;
		std::unordered_set<std::string> Device;
	};

#pragma region Vulkan Extensions
	// minimal set of required extensions
	VulkanExtensionSet g_enabledExtensions =
	{
		// instance
		{
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
		},
		// layers
		{ },
		// device
		{
			VK_KHR_MAINTENANCE1_EXTENSION_NAME
		},
	};

	// optional extensions
	VulkanExtensionSet g_optionalExtensions =
	{
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
			VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
			VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
			VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
			VK_EXT_MESH_SHADER_EXTENSION_NAME,
			VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
		},
	};

	std::unordered_set<std::string> g_rayTracingExtensions =
	{
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
		VK_KHR_RAY_QUERY_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME
	};
#pragma endregion


	static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
		[[maybe_unused]] vk::DebugReportFlagsEXT flags,
		[[maybe_unused]] vk::DebugReportObjectTypeEXT objType,
		[[maybe_unused]] u64 obj,
		size_t location,
		i32 code,
		const char* layerPrefix,
		const char* msg,
		void* userData)
	{
		if (const RendererVulkan* renderer = (const RendererVulkan*)userData)
		{
			const std::vector<size_t>& ignored = renderer->GetInitInfo().IgnoredVulkanValidationMessageLocations;

			const auto found = std::find(ignored.begin(), ignored.end(), location);
			if (found != ignored.end())
			{
				return VK_FALSE;
			}
		}

		Log::Warn("[Vulkan: location={:x} code={}, layerPrefix='{}'] %s", location, code, layerPrefix, msg);

		return VK_FALSE;
	}

	std::vector<const char*> StringSetToVector(const std::unordered_set<std::string>& set)
	{
		std::vector<const char*> vec;
		vec.reserve(set.size());
		for (const auto& str : set)
		{
			vec.push_back(str.c_str());
		}
		return vec;
	};

	template <typename T>
	static std::vector<T> SetToVector(const std::unordered_set<T>& set)
	{
		std::vector<T> ret;
		for(const auto& s : set)
		{
			ret.push_back(s);
		}

		return ret;
	}

	bool RendererVulkan::EnumerateAdapters(std::vector<AdapterInfo>& outAdapters)
	{
		ZoneScoped;
		if (!m_vkInstance)
		{
			return false;
		}

		std::vector<vk::PhysicalDevice> devices = m_vkInstance.enumeratePhysicalDevices();
		outAdapters.clear();

		for (vk::PhysicalDevice physicalDevice : devices)
		{
			vk::PhysicalDeviceProperties2 properties2;
			vk::PhysicalDeviceIDProperties idProperties;

			properties2.pNext = &idProperties;
			physicalDevice.getProperties2(&properties2);

			const auto& properties = properties2.properties;

			AdapterInfo adapterInfo{};
			adapterInfo.Name                 = properties.deviceName.data();
			adapterInfo.VendorID             = properties.vendorID;
			adapterInfo.DeviceID             = properties.deviceID;
			adapterInfo.DedicatedVideoMemory = 0;

			AdapterInfo::AdapterUUID uuid;
			static_assert(uuid.size() == idProperties.deviceUUID.size());

			std::memcpy(uuid.data(), idProperties.deviceUUID.data(), uuid.size());
			adapterInfo.UUID = uuid;

			if (idProperties.deviceLUIDValid)
			{
				AdapterInfo::AdapterLUID luid;
				static_assert(luid.size() == idProperties.deviceLUID.size());

				std::memcpy(luid.data(), idProperties.deviceLUID.data(), luid.size());
				adapterInfo.LUID = luid;
			}

			const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
			for (u32 heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
			{
				vk::MemoryHeap const& heap = memoryProperties.memoryHeaps[heapIndex];
				if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal)
				{
					adapterInfo.DedicatedVideoMemory += heap.size;
				}
			}

			outAdapters.push_back(std::move(adapterInfo));
		}

		return true;
	}

    nvrhi::ITexture *RendererVulkan::GetCurrentBackBuffer() const
    {
        return m_swapChainImages[m_swapChainIndex].NvrhiHandle;
    }

    nvrhi::ITexture *RendererVulkan::GetBackBuffer(u32 index) const
    {
		if (index < (u32)m_swapChainImages.size())
		{
			return m_swapChainImages[index].NvrhiHandle;
		}

        return nullptr;
    }

    u32 RendererVulkan::GetCurrentBackBufferIndex() const
    {
        return m_swapChainIndex;
    }

    u32 RendererVulkan::GetBackBufferCount() const
    {
        return u32(m_swapChainImages.size());
    }

    void RendererVulkan::InstallDebugCallback()
	{
		using enum vk::DebugReportFlagBitsEXT;

		auto info = vk::DebugReportCallbackCreateInfoEXT()
			.setFlags(eError | eWarning | /*eInformation |*/ ePerformanceWarning)
			.setPfnCallback(VulkanDebugCallback)
			.setPUserData(this);

		Assert(m_vkInstance.createDebugReportCallbackEXT(&info, nullptr, &m_debugReportCallback) == vk::Result::eSuccess);
	}

	bool RendererVulkan::PickPhysicalDevice()
	{
		VkFormat requestedFormat = nvrhi::vulkan::convertFormat(m_initInfo.SwapChainFormat);
		vk::Extent2D requestedExtent(m_initInfo.BackBufferSize.first, m_initInfo.BackBufferSize.second);

		const std::vector<vk::PhysicalDevice> devices = m_vkInstance.enumeratePhysicalDevices();

		i32 firstDevice = 0;
		i32 lastDevice = i32(devices.size()) - 1;

		// Error string in case no device is found
		std::stringstream errorStream;
		errorStream << "Cannot find a Vulkan device that supports all the required extensions and properties.";

		std::vector<vk::PhysicalDevice> discreteGPUs;
		std::vector<vk::PhysicalDevice> otherGPUs;

		for (i32 deviceIndex = firstDevice; deviceIndex <= lastDevice; ++deviceIndex)
		{
			vk::PhysicalDevice const& dev = devices[deviceIndex];
			vk::PhysicalDeviceProperties prop = dev.getProperties();

			errorStream << std::endl << prop.deviceName.data() << ":";

			std::unordered_set<std::string> requiredExtensions = g_enabledExtensions.Device;
			auto deviceExtensions = dev.enumerateDeviceExtensionProperties();

			for (const vk::ExtensionProperties& ext : deviceExtensions)
			{
				requiredExtensions.erase(std::string(ext.extensionName.data()));
			}

			bool deviceIsGood = true;

			if (!requiredExtensions.empty())
			{
				for (const std::string& ext : requiredExtensions)  // Device is missing one or more required extensions
				{
					errorStream << std::endl << "  - missing " << ext;
				}
				deviceIsGood = false;
			}

			vk::PhysicalDeviceFeatures2 deviceFeatures2{};
			vk::PhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
			deviceFeatures2.pNext = &dynamicRenderingFeatures;

			dev.getFeatures2(&deviceFeatures2);

			if (!deviceFeatures2.features.samplerAnisotropy)
			{
				// device is an old boi
				errorStream << std::endl << "  - does not support samplerAnisotropy";
				deviceIsGood = false;
			}
			if (!deviceFeatures2.features.textureCompressionBC)
			{
				errorStream << std::endl << "  - does not support textureCompressionBC";
				deviceIsGood = false;
			}
			if (!dynamicRenderingFeatures.dynamicRendering)
			{
				errorStream << std::endl << "  - does not support dynamicRendering";
				deviceIsGood = false;
			}

			if (!FindQueueFamilies(dev))
			{
				// device doesn't have all the queue families we need
				errorStream << std::endl << "  - does not support the necessary queue types";
				deviceIsGood = false;
			}

			if (deviceIsGood && m_windowSurface)
			{
				if (!dev.getSurfaceSupportKHR(m_presentQueueFamily, m_windowSurface))
				{
					errorStream << std::endl << "  - does not support the window surface";
					deviceIsGood = false;
				}
				else
				{
					// check that this device supports our i32ended swap chain creation parameters
					auto surfaceCaps = dev.getSurfaceCapabilitiesKHR(m_windowSurface);
					auto surfaceFmts = dev.getSurfaceFormatsKHR(m_windowSurface);

					if (surfaceCaps.minImageCount > m_initInfo.SwapChainBufferCount
						|| (surfaceCaps.maxImageCount < m_initInfo.SwapChainBufferCount
							&& surfaceCaps.maxImageCount > 0))
					{
						errorStream << std::endl << "  - cannot support the requested swap chain image count:";
						errorStream << " requested " << m_initInfo.SwapChainBufferCount << ", available " << surfaceCaps.minImageCount << " - " << surfaceCaps.maxImageCount;
						deviceIsGood = false;
					}

					if (surfaceCaps.minImageExtent.width     > requestedExtent.width
						|| surfaceCaps.minImageExtent.height > requestedExtent.height
						|| surfaceCaps.maxImageExtent.width  < requestedExtent.width
						|| surfaceCaps.maxImageExtent.height < requestedExtent.height)
					{
						errorStream << std::endl << "  - cannot support the requested swap chain size:";
						errorStream << " requested " << requestedExtent.width << "x" << requestedExtent.height << ", ";
						errorStream << " available " << surfaceCaps.minImageExtent.width << "x" << surfaceCaps.minImageExtent.height;
						errorStream << " - " << surfaceCaps.maxImageExtent.width << "x" << surfaceCaps.maxImageExtent.height;
						deviceIsGood = false;
					}

					bool surfaceFormatPresent = false;
					for (const vk::SurfaceFormatKHR& surfaceFmt : surfaceFmts)
					{
						if (surfaceFmt.format == vk::Format(requestedFormat))
						{
							surfaceFormatPresent = true;
							break;
						}
					}

					if (!surfaceFormatPresent)
					{
						// Can't create a swap chain using the format requested
						errorStream << std::endl << "  - does not support the requested swap chain format";
						deviceIsGood = false;
					}

					// Check that we can present from the graphics queue
					u32 canPresent = dev.getSurfaceSupportKHR(m_graphicsQueueFamily, m_windowSurface);
					if (!canPresent)
					{
						errorStream << std::endl << "  - cannot present";
						deviceIsGood = false;
					}
				}
			}

			if (!deviceIsGood)
			{
				continue;
			}

			if (prop.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
			{
				discreteGPUs.push_back(dev);
			}
			else
			{
				otherGPUs.push_back(dev);
			}
		}

		// Pick the first discrete GPU if it exists, otherwise the first i32egrated GPU
		if (!discreteGPUs.empty())
		{
			u32 selectedIndex = 0;
			m_vkPhysicalDevice = discreteGPUs[selectedIndex];
			return true;
		}

		if (!otherGPUs.empty())
		{
			u32 selectedIndex = 0;
			m_vkPhysicalDevice = otherGPUs[selectedIndex];
			return true;
		}

		Log::Error("{}", errorStream.str());
		return false;
	}

	bool RendererVulkan::FindQueueFamilies(vk::PhysicalDevice physicalDevice)
	{
		auto props = physicalDevice.getQueueFamilyProperties();

		for (i32 i = 0; i < i32(props.size()); i++)
		{
			const vk::QueueFamilyProperties& queueFamily = props[i];

			if (m_graphicsQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0
					&& (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_graphicsQueueFamily = i;
				}
			}

			if (m_computeQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0
					&& (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)
					&& !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_computeQueueFamily = i;
				}
			}

			if (m_transferQueueFamily == -1)
			{
				if (queueFamily.queueCount > 0
					&& (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer)
					&& !(queueFamily.queueFlags & vk::QueueFlagBits::eCompute)
					&& !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
				{
					m_transferQueueFamily = i;
				}
			}

			if (m_presentQueueFamily == -1)
			{
				VkSurfaceKHR surface{};
				if (m_window->CreateVulkanSurface(m_vkInstance, &surface))
				{
					[[maybe_unused]] u32 presentQueueFamily = std::numeric_limits<u32>::max();
					for (u32 idx = 0; idx < queueFamily.queueCount; ++idx)
					{
						VkBool32 supported = VK_FALSE;

						vkGetPhysicalDeviceSurfaceSupportKHR(
							physicalDevice,
							idx,
							surface,
							&supported);

						if (supported)
						{
							presentQueueFamily = idx;
							break;
						}
					}
				}
			}
		}

		if (m_graphicsQueueFamily == -1
			|| (m_computeQueueFamily == -1 && m_initInfo.EnableComputeQueue)
			|| (m_transferQueueFamily == -1 && m_initInfo.EnableCopyQueue))
		{
			return false;
		}

		return true;
	}

	bool RendererVulkan::CreateDevice()
	{
		for (const std::string& name : m_initInfo.RequiredVulkanDeviceExtensions)
		{
			g_enabledExtensions.Device.insert(name);
		}

		for (const std::string& name : m_initInfo.OptionalVulkanDeviceExtensions)
		{
			g_optionalExtensions.Device.insert(name);
		}

		if (m_initInfo.SwapChainFormat == nvrhi::Format::SRGBA8_UNORM)
		{
			m_initInfo.SwapChainFormat = nvrhi::Format::SBGRA8_UNORM;
		}
        else if (m_initInfo.SwapChainFormat == nvrhi::Format::RGBA8_UNORM)
        {
			m_initInfo.SwapChainFormat = nvrhi::Format::BGRA8_UNORM;
		}

        CHECK(CreateWindowSurface())
		CHECK(PickPhysicalDevice())
		CHECK(FindQueueFamilies(m_vkPhysicalDevice))
		CHECK(CreateDeviceInteral())

		auto vecInstanceExt = StringSetToVector(g_enabledExtensions.Instance);
		auto vecLayers      = StringSetToVector(g_enabledExtensions.Layers);
		auto vecDeviceExt   = StringSetToVector(g_enabledExtensions.Device);

		nvrhi::vulkan::DeviceDesc desc{};
		desc.errorCB            = this;
		desc.instance           = m_vkInstance;
		desc.physicalDevice     = m_vkPhysicalDevice;
		desc.device             = m_vkDevice;
		desc.graphicsQueue      = m_graphicsQueue;
		desc.graphicsQueueIndex = m_graphicsQueueFamily;

		if (m_initInfo.EnableComputeQueue)
		{
			desc.computeQueue      = m_computeQueue;
			desc.computeQueueIndex = m_computeQueueFamily;
		}

		if (m_initInfo.EnableCopyQueue)
		{
			desc.transferQueue = m_transferQueue;
			desc.transferQueueIndex = m_transferQueueFamily;
		}

		desc.instanceExtensions           = vecInstanceExt.data();
		desc.numInstanceExtensions        = vecInstanceExt.size();
		desc.deviceExtensions             = vecDeviceExt.data();
		desc.numDeviceExtensions          = vecDeviceExt.size();
		desc.bufferDeviceAddressSupported = m_bufferDeviceAddressSupported;
		desc.logBufferLifetime            = m_initInfo.LogBufferLifetime;

		return true;
	}

    bool RendererVulkan::CreateDeviceInteral()
    {
		// figure out which optional extensions are supported
		auto deviceExtensions = m_vkPhysicalDevice.enumerateDeviceExtensionProperties();
		for (const auto& ext : deviceExtensions)
		{
			const std::string name = ext.extensionName;
			if (g_optionalExtensions.Device.find(name) != g_optionalExtensions.Device.end())
			{
				if (name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME)
				{
					continue;
				}

				g_enabledExtensions.Device.insert(name);
			}

			if (m_initInfo.EnableRayTracingExtensions && g_rayTracingExtensions.find(name) != g_rayTracingExtensions.end())
			{
				g_enabledExtensions.Device.insert(name);
			}
		}

		g_enabledExtensions.Device.insert(VK_KHR_SWAPCHAIN_EXTENSION_NAME);


		const vk::PhysicalDeviceProperties physicalDeviceProperties = m_vkPhysicalDevice.getProperties();
		m_rendererString = std::wstring(physicalDeviceProperties.deviceName.begin(), physicalDeviceProperties.deviceName.end());  // HACK: Cursed narrow to wide string conversion

		bool accelStructSupported                  = false;
		bool rayPipelineSupported                  = false;
		bool rayQuerySupported                     = false;
		bool vrsSupported                          = false;
		bool interlockSupported                    = false;
		bool barycentricSupported                  = false;
		bool synchronization2Supported             = false;
		bool maintenance4Supported                 = false;
		bool aftermathSupported                    = false;
		bool clusterAccelerationStructureSupported = false;
		bool mutableDescriptorTypeSupported        = false;
		bool linearSweptSpheresSupported           = false;
		bool meshShaderSupported                   = false;

		Log::Info("Enabled Vulkan device extensions:");

		for (const auto& ext : g_enabledExtensions.Device)
		{
			Log::Info("    {}", ext.c_str());

			if (ext == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)                accelStructSupported                  = true;
			else if (ext == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)             rayPipelineSupported                  = true;
			else if (ext == VK_KHR_RAY_QUERY_EXTENSION_NAME)                        rayQuerySupported                     = true;
			else if (ext == VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)            vrsSupported                          = true;
			else if (ext == VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME)        interlockSupported                    = true;
			else if (ext == VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME)      barycentricSupported                  = true;
			else if (ext == VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)                synchronization2Supported             = true;
			else if (ext == VK_KHR_MAINTENANCE_4_EXTENSION_NAME)                    maintenance4Supported                 = true;
			else if (ext == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME)         m_swapChainMutableFormatSupported     = true;
			else if (ext == VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME)         aftermathSupported                    = true;
			else if (ext == VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME)    clusterAccelerationStructureSupported = true;
			else if (ext == VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME)          mutableDescriptorTypeSupported        = true;
			else if (ext == VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)  linearSweptSpheresSupported           = true;
			else if (ext == VK_EXT_MESH_SHADER_EXTENSION_NAME)                      meshShaderSupported                   = true;
		}
#define APPEND_EXTENSION(condition, desc) if (condition) { (desc).pNext = nextPtr; nextPtr = &(desc); }
		void* nextPtr = nullptr;

		vk::PhysicalDeviceFeatures2 physicalDeviceFeatures2;
		auto bufferDeviceAddressFeatures = vk::PhysicalDeviceBufferDeviceAddressFeatures();  // Determine support for Buffer Device Address, the Vulkan 1.2 way
		auto maintenance4Features        = vk::PhysicalDeviceMaintenance4Features();         // Determine support for maintenance4
		auto aftermathPhysicalFeatures   = vk::PhysicalDeviceDiagnosticsConfigFeaturesNV();  // Determine support for aftermath
		auto meshShaderFeatures          = vk::PhysicalDeviceMeshShaderFeaturesEXT();        // Determine support for mesh and task shaders

		// Put the user-provided extension structure at the end of the chain
		APPEND_EXTENSION(true, bufferDeviceAddressFeatures);
		APPEND_EXTENSION(maintenance4Supported, maintenance4Features);
		APPEND_EXTENSION(aftermathSupported, aftermathPhysicalFeatures);
		APPEND_EXTENSION(meshShaderSupported, meshShaderFeatures);

		physicalDeviceFeatures2.pNext = nextPtr;
		m_vkPhysicalDevice.getFeatures2(&physicalDeviceFeatures2);

		std::unordered_set<i32> uniqueQueueFamilies = { m_graphicsQueueFamily, m_presentQueueFamily };

		if (m_initInfo.EnableComputeQueue) uniqueQueueFamilies.insert(m_computeQueueFamily);
		if (m_initInfo.EnableCopyQueue) uniqueQueueFamilies.insert(m_transferQueueFamily);

		f32 priority = 1.0f;
		std::vector<vk::DeviceQueueCreateInfo> queueDesc;
		queueDesc.reserve(uniqueQueueFamilies.size());
		for (i32 queueFamily : uniqueQueueFamilies)
		{
			queueDesc.push_back(vk::DeviceQueueCreateInfo()
				.setQueueFamilyIndex(queueFamily)
				.setQueueCount(1)
				.setPQueuePriorities(&priority));
		}

		auto accelStructFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
			.setAccelerationStructure(true);
		auto rayPipelineFeatures = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
			.setRayTracingPipeline(true)
			.setRayTraversalPrimitiveCulling(true);
		auto rayQueryFeatures = vk::PhysicalDeviceRayQueryFeaturesKHR()
			.setRayQuery(true);
		auto interlockFeatures = vk::PhysicalDeviceFragmentShaderInterlockFeaturesEXT()
			.setFragmentShaderPixelInterlock(true);
		auto barycentricFeatures = vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR()
			.setFragmentShaderBarycentric(true);
		auto vrsFeatures = vk::PhysicalDeviceFragmentShadingRateFeaturesKHR()
			.setPipelineFragmentShadingRate(true)
			.setPrimitiveFragmentShadingRate(true)
			.setAttachmentFragmentShadingRate(true);
		auto vulkan13features = vk::PhysicalDeviceVulkan13Features()
			.setDynamicRendering(true)
			.setSynchronization2(synchronization2Supported)
			.setMaintenance4(maintenance4Features.maintenance4);
		auto clusterAccelerationStructureFeatures = vk::PhysicalDeviceClusterAccelerationStructureFeaturesNV()
			.setClusterAccelerationStructure(true);
		auto mutableDescriptorTypeFeatures = vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT()
			.setMutableDescriptorType(true);
		auto dynamicRenderingFeatures = vk::PhysicalDeviceDynamicRenderingFeatures()
			.setDynamicRendering(true);
		auto linearSweptSpheresFeatures = vk::PhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV()
			.setSpheres(true)
			.setLinearSweptSpheres(true);

		nextPtr = nullptr;
		APPEND_EXTENSION(accelStructSupported, accelStructFeatures)
		APPEND_EXTENSION(rayPipelineSupported, rayPipelineFeatures)
		APPEND_EXTENSION(rayQuerySupported, rayQueryFeatures)
		APPEND_EXTENSION(vrsSupported, vrsFeatures)
		APPEND_EXTENSION(interlockSupported, interlockFeatures)
		APPEND_EXTENSION(barycentricSupported, barycentricFeatures)
		APPEND_EXTENSION(clusterAccelerationStructureSupported, clusterAccelerationStructureFeatures)
		APPEND_EXTENSION(mutableDescriptorTypeSupported, mutableDescriptorTypeFeatures)
		APPEND_EXTENSION(physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3, vulkan13features)
		APPEND_EXTENSION(physicalDeviceProperties.apiVersion < VK_API_VERSION_1_3 && maintenance4Supported, maintenance4Features)
		APPEND_EXTENSION(physicalDeviceProperties.apiVersion < VK_API_VERSION_1_3, dynamicRenderingFeatures)
		APPEND_EXTENSION(linearSweptSpheresSupported, linearSweptSpheresFeatures)
		APPEND_EXTENSION(meshShaderSupported, meshShaderFeatures)

		// These mesh shader features require other device features to be enabled:
		// - VkPhysicalDeviceMultiviewFeaturesKHR::multiview
		// - VkPhysicalDeviceFragmentShadingRateFeaturesKHR::primitiveFragmentShadingRate
		// Disable the mesh shader features by default
		meshShaderFeatures.multiviewMeshShader = false;
		meshShaderFeatures.primitiveFragmentShadingRateMeshShader = false;
#undef APPEND_EXTENSION

		auto deviceFeatures = vk::PhysicalDeviceFeatures()
			.setShaderImageGatherExtended(true)
			.setSamplerAnisotropy(true)
			.setTessellationShader(true)
			.setTextureCompressionBC(true)
			.setGeometryShader(true)
			.setImageCubeArray(true)
			.setShaderInt16(true)
			.setFillModeNonSolid(true)
			.setFragmentStoresAndAtomics(true)
			.setDualSrcBlend(true)
			.setVertexPipelineStoresAndAtomics(true)
			.setShaderInt64(true)
			.setShaderStorageImageWriteWithoutFormat(true)
			.setShaderStorageImageReadWithoutFormat(true);

		// Add a Vulkan 1.1 structure with default settings to make it easier for apps to modify them
		auto vulkan11features = vk::PhysicalDeviceVulkan11Features()
			.setStorageBuffer16BitAccess(true)
			.setPNext(nextPtr);

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

		auto extVec = StringSetToVector(g_enabledExtensions.Device);

		auto deviceDesc = vk::DeviceCreateInfo()
			.setPQueueCreateInfos(queueDesc.data())
			.setQueueCreateInfoCount(uint32_t(queueDesc.size()))
			.setPEnabledFeatures(&deviceFeatures)
			.setEnabledExtensionCount(uint32_t(extVec.size()))
			.setPpEnabledExtensionNames(extVec.data())
			.setPNext(&vulkan12features);

		if (m_initInfo.VkDeviceCreateInfoCallback)
		{
			m_initInfo.VkDeviceCreateInfoCallback(deviceDesc);
		}

		if (auto res = m_vkPhysicalDevice.createDevice(&deviceDesc, nullptr, &m_vkDevice); res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create a Vulkan physical device, error code = {}", nvrhi::vulkan::resultToString(VkResult(res)));
			return false;
		}

		m_vkDevice.getQueue(m_graphicsQueueFamily, s_graphicsQueueIndex, &m_graphicsQueue);
		if (m_initInfo.EnableComputeQueue)
		{
			m_vkDevice.getQueue(m_computeQueueFamily, s_computeQueueIndex, &m_computeQueue);
		}

		if (m_initInfo.EnableCopyQueue)
		{
			m_vkDevice.getQueue(m_transferQueueFamily, s_transferQueueIndex, &m_transferQueue);
		}

		m_vkDevice.getQueue(m_presentQueueFamily, s_presentQueueIndex, &m_presentQueue);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkDevice);

		m_bufferDeviceAddressSupported = vulkan12features.bufferDeviceAddress;
		Log::Info("Created vulkan device!");

		return true;
	}

	bool RendererVulkan::CreateWindowSurface()
    {
		if (!m_window->CreateVulkanSurface(m_vkInstance, &m_windowSurface))
		{
			Log::Error("Failed to create a valid window surface!");
			return false;
		}
		return true;
    }

	void RendererVulkan::DestroySwapChain()
	{
		if (m_vkDevice)
		{
			m_vkDevice.waitIdle();
		}

		if (m_swapChain)
		{
			m_vkDevice.destroySwapchainKHR(m_swapChain);
			m_swapChain = nullptr;
		}

		m_swapChainImages.clear();
	}

	bool RendererVulkan::CreateSwapChainInternal()
	{
		DestroySwapChain();

		m_swapChainFormat.format     = vk::Format(nvrhi::vulkan::convertFormat(m_initInfo.SwapChainFormat));
		m_swapChainFormat.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;

		vk::Extent2D extent(m_initInfo.BackBufferSize.first, m_initInfo.BackBufferSize.second);

		std::unordered_set<u32> uniqueQueues =
		{
			static_cast<u32>(m_graphicsQueueFamily),
			static_cast<u32>(m_presentQueueFamily)
		};

		std::vector<u32> queues = SetToVector(uniqueQueues);

		const bool enableSwapChainSharing = queues.size() > 1;

		using enum vk::ImageUsageFlagBits;
		using enum vk::SharingMode;
		using enum vk::SwapchainCreateFlagBitsKHR;
		using enum vk::PresentModeKHR;

		auto desc = vk::SwapchainCreateInfoKHR()
			.setSurface(m_windowSurface)
			.setMinImageCount(m_initInfo.SwapChainBufferCount)
			.setImageFormat(m_swapChainFormat.format)
			.setImageColorSpace(m_swapChainFormat.colorSpace)
			.setImageExtent(extent)
			.setImageArrayLayers(1)
			.setImageUsage(eColorAttachment | eTransferDst | eSampled)
			.setImageSharingMode(enableSwapChainSharing ? eConcurrent : eExclusive)
			.setFlags(m_swapChainMutableFormatSupported ? eMutableFormat : vk::SwapchainCreateFlagBitsKHR(0))
			.setQueueFamilyIndexCount(enableSwapChainSharing ? uint32_t(queues.size()) : 0)
			.setPQueueFamilyIndices(enableSwapChainSharing ? queues.data() : nullptr)
			.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
			.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
			.setPresentMode(m_initInfo.EnableVSync ? eFifo : eImmediate)
			.setClipped(true)
			.setOldSwapchain(nullptr);

		std::vector<vk::Format> imageFormats = { m_swapChainFormat.format };
		switch(m_swapChainFormat.format)
		{
			case vk::Format::eR8G8B8A8Unorm: imageFormats.push_back(vk::Format::eR8G8B8A8Srgb);  break;
			case vk::Format::eR8G8B8A8Srgb:  imageFormats.push_back(vk::Format::eR8G8B8A8Unorm); break;
			case vk::Format::eB8G8R8A8Unorm: imageFormats.push_back(vk::Format::eB8G8R8A8Srgb);  break;
			case vk::Format::eB8G8R8A8Srgb:  imageFormats.push_back(vk::Format::eB8G8R8A8Unorm); break;
			default: break;
		}

		auto imageFormatListCreatInfo = vk::ImageFormatListCreateInfo().setViewFormats(imageFormats);

		if (m_swapChainMutableFormatSupported)
		{
			desc.pNext = &imageFormatListCreatInfo;
		}

		const vk::Result res = m_vkDevice.createSwapchainKHR(&desc, nullptr, &m_swapChain);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create a Vulkan swap chain, error code: {}", nvrhi::vulkan::resultToString(VkResult(res)));
			return false;
		}

		 // retrieve swap chain images
		auto images = m_vkDevice.getSwapchainImagesKHR(m_swapChain);
		for(auto image : images)
		{
			SwapChainImage sci;
			sci.Image = image;

			nvrhi::TextureDesc textureDesc;
			textureDesc.width            = m_initInfo.BackBufferSize.first;
			textureDesc.height           = m_initInfo.BackBufferSize.second;
			textureDesc.format           = m_initInfo.SwapChainFormat;
			textureDesc.debugName        = "Swap chain image";
			textureDesc.initialState     = nvrhi::ResourceStates::Present;
			textureDesc.keepInitialState = true;
			textureDesc.isRenderTarget   = true;

			sci.NvrhiHandle = m_nvrhiDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(sci.Image), textureDesc);
			m_swapChainImages.push_back(sci);
		}

    	m_swapChainIndex = 0;
		return true;
	}

    bool RendererVulkan::CreateDeviceIndependentResources()
	{
		if (m_initInfo.EnableDebugRuntime)
		{
			g_enabledExtensions.Instance.insert("VK_EXT_debug_report");
        	g_enabledExtensions.Layers.insert("VK_LAYER_KHRONOS_validation");
		}

		m_dynamicLoader = std::make_unique<VulkanDynamicLoader>("");

		PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
			m_dynamicLoader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

		return CreateInstance();
	}

	bool RendererVulkan::CreateInstance()
	{
		for (const std::string& name : m_initInfo.RequiredVulkanDeviceExtensions)
		{
			g_enabledExtensions.Instance.insert(name);
		}

		for (const std::string& name : m_initInfo.OptionalVulkanInstanceExtensions)
		{
			g_optionalExtensions.Instance.insert(name);
		}

		for (const std::string& name : m_initInfo.RequiredVulkanLayers)
		{
			g_enabledExtensions.Layers.insert(name);
		}
		
		for (const std::string& name : m_initInfo.OptionalVulkanLayers)
		{
			g_optionalExtensions.Layers.insert(name);
		}

		std::unordered_set<std::string> requiredExtensions = g_enabledExtensions.Instance;

		for(const vk::ExtensionProperties& instanceExt : vk::enumerateInstanceExtensionProperties())
		{
			const std::string& name = instanceExt.extensionName;
			if (g_optionalExtensions.Instance.find(name) != g_optionalExtensions.Instance.end())
			{
				g_enabledExtensions.Instance.insert(name);
			}

			requiredExtensions.erase(name);
		}

		if (!requiredExtensions.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
			for (const auto& ext : requiredExtensions)
			{
				ss << std::endl << "  - " << ext;
			}

			Log::Error("{}", ss.str().c_str());
			return false;
		}

		Log::Info("Enabled Vulkan instance extensions:");

		for (const auto& ext : g_enabledExtensions.Instance)
		{
			Log::Info("    {}", ext.c_str());
		}

		std::unordered_set<std::string> requiredLayers = g_enabledExtensions.Layers;

		for(const auto& layer : vk::enumerateInstanceLayerProperties())
		{
			const std::string& name = layer.layerName;
			if (g_optionalExtensions.Layers.find(name) != g_optionalExtensions.Layers.end())
			{
				g_enabledExtensions.Layers.insert(name);
			}

			requiredLayers.erase(name);
		}

		if (!requiredLayers.empty())
		{
			std::stringstream ss;
			ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
			for (const auto& ext : requiredLayers)
				ss << std::endl << "  - " << ext;

			Log::Error("{}", ss.str().c_str());
			return false;
		}
		
		Log::Info("Enabled Vulkan layers:");
		for (const auto& layer : g_enabledExtensions.Layers)
		{
			Log::Info("    {}", layer.c_str());
		}

		auto instanceExtVec = StringSetToVector(g_enabledExtensions.Instance);
		auto layerVec = StringSetToVector(g_enabledExtensions.Layers);
		
		auto applicationInfo = vk::ApplicationInfo();

		// Query the Vulkan API version supported on the system to make sure we use at least 1.3 when that's present.
		vk::Result res = vk::enumerateInstanceVersion(&applicationInfo.apiVersion);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Call to vkEnumerateInstanceVersion failed, error code: {}", nvrhi::vulkan::resultToString(VkResult(res)));
			return false;
		}

		const uint32_t minimumVulkanVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);

		// Check if the Vulkan API version is sufficient.
		if (applicationInfo.apiVersion < minimumVulkanVersion)
		{
			Log::Error("The Vulkan API version supported on the system ({}.{}.{}) is too low, at least {}.{}.{} is required.",
				VK_API_VERSION_MAJOR(applicationInfo.apiVersion), VK_API_VERSION_MINOR(applicationInfo.apiVersion), VK_API_VERSION_PATCH(applicationInfo.apiVersion),
				VK_API_VERSION_MAJOR(minimumVulkanVersion), VK_API_VERSION_MINOR(minimumVulkanVersion), VK_API_VERSION_PATCH(minimumVulkanVersion));
			return false;
		}

		// Spec says: A non-zero variant indicates the API is a variant of the Vulkan API and applications will typically need to be modified to run against it.
		if (VK_API_VERSION_VARIANT(applicationInfo.apiVersion) != 0)
		{
			Log::Error("The Vulkan API supported on the system uses an unexpected variant: {}.", VK_API_VERSION_VARIANT(applicationInfo.apiVersion));
			return false;
		}

		// Create the vulkan instance
		vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
			.setEnabledLayerCount(uint32_t(layerVec.size()))
			.setPpEnabledLayerNames(layerVec.data())
			.setEnabledExtensionCount(uint32_t(instanceExtVec.size()))
			.setPpEnabledExtensionNames(instanceExtVec.data())
			.setPApplicationInfo(&applicationInfo);

		res = vk::createInstance(&info, nullptr, &m_vkInstance);
		if (res != vk::Result::eSuccess)
		{
			Log::Error("Failed to create a Vulkan instance, error code: {}", nvrhi::vulkan::resultToString(VkResult(res)));
			return false;
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkInstance);

		return true;
	}

	bool RendererVulkan::CreateSwapChain()
	{
		CHECK(CreateSwapChainInternal())

		const size_t numPresentSemaphores = m_swapChainImages.size();
		m_presentSemaphores.reserve(numPresentSemaphores);

		for (u32 i = 0; i < numPresentSemaphores; ++i)
		{
			m_presentSemaphores.push_back(m_vkDevice.createSemaphore(vk::SemaphoreCreateInfo()));
		}

		const size_t numAcquireSemaphores = std::max(size_t(m_initInfo.MaxFramesInFlight), m_swapChainImages.size());
		m_acquireSemaphores.reserve(numAcquireSemaphores);
		for (u32 i = 0; i < numAcquireSemaphores; ++i)
		{
			m_acquireSemaphores.push_back(m_vkDevice.createSemaphore(vk::SemaphoreCreateInfo()));
		}

		return true;
	}

	void RendererVulkan::DestroyDeviceAndSwapChain()
	{
		DestroySwapChain();

		for (auto& semaphore : m_presentSemaphores)
		{
			if (semaphore)
			{
				m_vkDevice.destroySemaphore(semaphore);
				semaphore = vk::Semaphore();
			}
		}
		
		for (auto& semaphore : m_acquireSemaphores)
		{
			if (semaphore)
			{
				m_vkDevice.destroySemaphore(semaphore);
				semaphore = vk::Semaphore();
			}
		}

		m_nvrhiDevice  = nullptr;
		m_validationLayer = nullptr;
		m_rendererString.clear();
		
		if (m_vkDevice)
		{
			m_vkDevice.destroy();
			m_vkDevice = nullptr;
		}

		if (m_windowSurface)
		{
			Assert(m_vkInstance);
			m_vkInstance.destroySurfaceKHR(m_windowSurface);
			m_windowSurface = nullptr;
		}

		if (m_debugReportCallback)
		{
			m_vkInstance.destroyDebugReportCallbackEXT(m_debugReportCallback);
		}
		
		if (m_vkInstance)
		{
			m_vkInstance.destroy();
			m_vkInstance = nullptr;
		}
	}

    void RendererVulkan::ResizeSwapChain()
    {
		if (m_vkDevice)
		{
			DestroySwapChain();
			CreateSwapChainInternal();
		}
    }

    bool RendererVulkan::BeginFrame()
    {
		 const auto& semaphore = m_acquireSemaphores[m_acquireSemaphoreIndex];

		vk::Result res;

		int const maxAttempts = 3;
		for (int attempt = 0; attempt < maxAttempts; ++attempt)
		{
			res = m_vkDevice.acquireNextImageKHR(
				m_swapChain,
				std::numeric_limits<u64>::max(), // timeout
				semaphore,
				vk::Fence(),
				&m_swapChainIndex);

			if ((res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) && attempt < maxAttempts)
			{
				BackBufferResizeBegin();
				
				auto surfaceCaps = m_vkPhysicalDevice.getSurfaceCapabilitiesKHR(m_windowSurface);
				m_initInfo.BackBufferSize = { surfaceCaps.currentExtent.width, surfaceCaps.currentExtent.height };

				ResizeSwapChain();
				BackBufferResizeEnd();
			}
			else
				break;
		}

		m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % m_acquireSemaphores.size();

		if (res == vk::Result::eSuccess || res == vk::Result::eSuboptimalKHR) // Suboptimal is considered a success
		{
			// Schedule the wait. The actual wait operation will be submitted when the app executes any command list.
			m_nvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
			return true;
		}

		return false;
	}

	bool RendererVulkan::Present()
	{
		const auto& semaphore = m_presentSemaphores[m_swapChainIndex];

		m_nvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);

		// NVRHI buffers the semaphores and signals them when something is submitted to a queue.
		// Call 'executeCommandLists' with no command lists to actually signal the semaphore.
		m_nvrhiDevice->executeCommandLists(nullptr, 0);

		vk::PresentInfoKHR info = vk::PresentInfoKHR()
									.setWaitSemaphoreCount(1)
									.setPWaitSemaphores(&semaphore)
									.setSwapchainCount(1)
									.setPSwapchains(&m_swapChain)
									.setPImageIndices(&m_swapChainIndex);

		const vk::Result res = m_presentQueue.presentKHR(&info);
		if (!(res == vk::Result::eSuccess || res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR))
		{
			return false;
		}

	#if FAY_OS_WINDOWS
		if (m_initInfo.EnableVSync || m_initInfo.EnableDebugRuntime)
		{
			// according to vulkan-tutorial.com, "the validation layer implementation expects
			// the application to explicitly synchronize with the GPU"
			m_PresentQueue.waitIdle();
		}
	#endif

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
#undef CHECK
#endif
