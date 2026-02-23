#include "Graphics/Vulkan/RendererVulkan.h"

#undef min
#undef max

#if FAY_HAS_VULKAN
#include <unordered_set>
#include <sstream>
#include <numeric>
#include "Common/Assert.h"
#include "Common/Profiling.h"
#include "Platform/Window.h"
#include <nvrhi/validation.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace fay
{
	static constexpr u32 s_computeQueueIndex  = 0;
	static constexpr u32 s_graphicsQueueIndex = 0;
	static constexpr u32 s_PresentQueueIndex  = 0;
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
			VK_KHR_MAi32ENANCE1_EXTENSION_NAME
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
			VK_KHR_MAi32ENANCE_4_EXTENSION_NAME,
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

			AdapterInfo adapterInfo
			{
				.Name                 = properties.deviceName.data(),
				.VendorID             = properties.vendorID,
				.DeviceID             = properties.deviceID,
				.DedicatedVideoMemory = 0
			};

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
					u32 presentQueueFamily = std::numeric_limits<u32>::max();

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

		auto stringSetToVector = [](const std::unordered_set<std::string>& set) -> std::vector<const char*>
		{
			std::vector<const char*> vec;
			vec.reserve(set.size());
			for (const auto& str : set)
			{
				vec.push_back(str.c_str());
			}
			return vec;
		};

		auto extVec = stringSetToVector(g_enabledExtensions.Device);

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
		
		m_vkDevice.getQueue(m_PresentQueueFamily, kPresentQueueIndex, &m_PresentQueue);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_vkDevice);
	}
}
#endif