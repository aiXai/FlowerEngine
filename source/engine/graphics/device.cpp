#include "context.h"
#include "log.h"

#include "../engine.h"

namespace engine
{
    static AutoCVarBool cVarRHIHDRFeatureEnable(
        "r.RHI.HDREnable",
        "Enable hdr feature or not.",
        "RHI",
        true,
        CVarFlags::ReadOnly
    );

    static AutoCVarBool cVarRHIRayTraceFeatureEnable(
        "r.RHI.RayTraceEnable",
        "Enable ray trace feature or not.",
        "RHI",
        true,
        CVarFlags::ReadOnly
    );

    static AutoCVarBool cVarRHIDLSSFeatureEnable(
        "r.RHI.DLSS",
        "Enable dlss feature or not.",
        "RHI",
        true,
        CVarFlags::ReadOnly
    );

    void VulkanContext::initDeviceAndQueue()
    {
        ASSERT(m_gpu != VK_NULL_HANDLE, "You must select one gpu before init device.");

        // Query all useful device extensions.
        uint32_t deviceExtensionCount;
        vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &deviceExtensionCount, nullptr);
        std::vector<VkExtensionProperties> availableDeviceExtensions(deviceExtensionCount);
        vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &deviceExtensionCount, availableDeviceExtensions.data());

        // Helper function for find extension exist or not.
        auto existDeviceExtension = [&](const char* name)
        {
            for (auto& availableExtension : availableDeviceExtensions)
            {
                if (strcmp(availableExtension.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        };

        // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
        // Vulkan extension open.

        std::vector<const char*> deviceExtensionNames{ };

        // Some basic device extension always require support.
        deviceExtensionNames.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
        deviceExtensionNames.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);

        // Add swapchain extension for windows application.
        if (m_engine->isWindowApplication())
        {
            deviceExtensionNames.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        auto tryInsertIfExistExtension = [&](const char* name)
        {
            if (existDeviceExtension(name))
            {
                deviceExtensionNames.push_back(name);
                return true;
            }
            return false;
        };

        // HDR extension.
        m_graphicsSupportStates.bSupportHDR = m_engine->isWindowApplication() && cVarRHIHDRFeatureEnable.get();
        if (m_graphicsSupportStates.bSupportHDR)
        {
            m_graphicsSupportStates.bSupportHDR &= tryInsertIfExistExtension(VK_EXT_HDR_METADATA_EXTENSION_NAME);
        }

        // Raytracing extension.
        m_graphicsSupportStates.bSupportRaytrace = cVarRHIRayTraceFeatureEnable.get();
        if (m_graphicsSupportStates.bSupportRaytrace)
        {
            m_graphicsSupportStates.bSupportRaytrace &= tryInsertIfExistExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            m_graphicsSupportStates.bSupportRaytrace &= tryInsertIfExistExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            m_graphicsSupportStates.bSupportRaytrace &= tryInsertIfExistExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            m_graphicsSupportStates.bSupportRaytrace &= tryInsertIfExistExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        }

        // 
        // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


        // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
        // Vulkan feature enabled.

        // Standard core features.
        VkPhysicalDeviceFeatures         enable10GpuFeatures = {}; // vulkan 1.0
        VkPhysicalDeviceVulkan11Features enable11GpuFeatures = {}; // vulkan 1.1
        VkPhysicalDeviceVulkan12Features enable12GpuFeatures = {}; // vulkan 1.2
        VkPhysicalDeviceVulkan13Features enable13GpuFeatures = {}; // vulkan 1.3

        // Raytracing features.
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };

        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicStateFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };

        // Enable gpu features 1.0 here.
        enable10GpuFeatures.samplerAnisotropy = true;
        enable10GpuFeatures.depthClamp = true;
        enable10GpuFeatures.shaderSampledImageArrayDynamicIndexing = true;
        enable10GpuFeatures.multiDrawIndirect = VK_TRUE;
        enable10GpuFeatures.drawIndirectFirstInstance = VK_TRUE;
        enable10GpuFeatures.independentBlend = VK_TRUE;
        enable10GpuFeatures.multiViewport = VK_TRUE;
        enable10GpuFeatures.fragmentStoresAndAtomics = VK_TRUE;
        enable10GpuFeatures.shaderInt16 = VK_TRUE;
        enable10GpuFeatures.fillModeNonSolid = VK_TRUE;
        enable10GpuFeatures.depthBiasClamp = VK_TRUE;
        enable10GpuFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;

        // Enable gpu features 1.1 here.
        enable11GpuFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        enable11GpuFeatures.pNext = &enable12GpuFeatures;
        enable11GpuFeatures.shaderDrawParameters = VK_TRUE;

        // Enable gpu features 1.2 here.
        enable12GpuFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enable12GpuFeatures.pNext = &enable13GpuFeatures;
        enable12GpuFeatures.drawIndirectCount = VK_TRUE;
        enable12GpuFeatures.drawIndirectCount = VK_TRUE;
        enable12GpuFeatures.imagelessFramebuffer = VK_TRUE;
        enable12GpuFeatures.separateDepthStencilLayouts = VK_TRUE;
        enable12GpuFeatures.descriptorIndexing = VK_TRUE;
        enable12GpuFeatures.runtimeDescriptorArray = VK_TRUE;
        enable12GpuFeatures.descriptorBindingPartiallyBound = VK_TRUE;
        enable12GpuFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
        enable12GpuFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        enable12GpuFeatures.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        enable12GpuFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        enable12GpuFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        enable12GpuFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        enable12GpuFeatures.timelineSemaphore = VK_TRUE;
        enable12GpuFeatures.bufferDeviceAddress = VK_TRUE;
        enable12GpuFeatures.shaderFloat16 = VK_TRUE;
        enable12GpuFeatures.hostQueryReset = VK_TRUE;
        
        // Enable gpu features 1.3 here.
        enable13GpuFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enable13GpuFeatures.dynamicRendering = VK_TRUE;
        enable13GpuFeatures.synchronization2 = VK_TRUE;
        enable13GpuFeatures.maintenance4 = VK_TRUE;
        enable13GpuFeatures.pNext = &accelFeature;

        // Raytrace.
        accelFeature.pNext = &rtPipelineFeature;
        rtPipelineFeature.pNext = &rayQueryFeatures;
        if (m_graphicsSupportStates.bSupportRaytrace)
        {
            accelFeature.accelerationStructure   = VK_TRUE;
            rtPipelineFeature.rayTracingPipeline = VK_TRUE;
            rayQueryFeatures.rayQuery            = VK_TRUE;
        }

        // Dynamic state.
        rayQueryFeatures.pNext = &dynamicStateFeatures;
        dynamicStateFeatures.extendedDynamicState3PolygonMode = VK_TRUE;


        // Other features in the future.
        dynamicStateFeatures.pNext = nullptr;

        // 
        // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

        // Prepare graphics queue.
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamilyCount, queueFamilies.data());

        uint32_t graphicsQueueCounts = 0;
        uint32_t computeQueueCounts = 0;
        uint32_t copyQueueCounts = 0;

        bool bGraphicsQueueSet = false;
        bool bCopyQueueSet = false;
        bool bComputeQueueSet = false;

        // NOTE: queueFamilies sort by VkQueueFlagBits in my nVidia graphics card, no ensure the order is same with AMD graphics card.
        uint32_t queueIndex = 0;
        for (const auto& queueFamily : queueFamilies)
        {
            const bool bSupportGraphics = queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT;
            const bool bSupportCompute = (!bSupportGraphics) && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT); // Only support compute queue.
            const bool bSupportCopy = (!bSupportGraphics) && (!bSupportCompute) && (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT); // Only support copy queue.

            // Sparse binding.
            const bool bSupportSparseBinding = queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT;

            if (bSupportGraphics && (!bGraphicsQueueSet))
            {
                m_queues.graphicsFamily = queueIndex;
                graphicsQueueCounts = queueFamily.queueCount;

                bGraphicsQueueSet = true;
            }
            else if (bSupportCompute && (!bComputeQueueSet))
            {
                m_queues.computeFamily = queueIndex;
                computeQueueCounts = queueFamily.queueCount;

                bComputeQueueSet = true;
            }
            else if (bSupportCopy && (!bCopyQueueSet))
            {
                m_queues.copyFamily = queueIndex;
                copyQueueCounts = queueFamily.queueCount;

                bCopyQueueSet = true;
            }

            queueIndex++;
        }

        ASSERT(graphicsQueueCounts > 2, "We need more than two graphics queues to do some async dispatch.");  // NOTE: Most graphics card exist 16+ graphics queues.
        ASSERT(computeQueueCounts  > 1, "We need more than one compute queues to do some async dispatch.");   // NOTE: Most graphics card exist 8+ compute queues.
        ASSERT(copyQueueCounts     > 0, "We need at least one copy queues to do some async dispatch.");      // NOTE: Most graphics card exist 2+ copy queue.

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

        // Prepare queue priority. all 0.5f.
        std::vector<float> graphicsQueuePriority(graphicsQueueCounts, 0.5f);
        std::vector<float>   computeQueuePriority(computeQueueCounts, 0.5f);
        std::vector<float>         copyQueuePriority(copyQueueCounts, 0.5f);

        // Major queue use for present and render UI.
        graphicsQueuePriority[0] = 1.0f;

        // Major compute queue and second major graphics queue. 
        computeQueuePriority[0]  = 0.8f;
        graphicsQueuePriority[1] = 0.8f;

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_queues.graphicsFamily;
        queueCreateInfo.queueCount = graphicsQueueCounts;
        queueCreateInfo.pQueuePriorities = graphicsQueuePriority.data();
        queueCreateInfos.push_back(queueCreateInfo);

        if (computeQueueCounts > 0)
        {
            queueCreateInfo.queueFamilyIndex = m_queues.computeFamily;
            queueCreateInfo.queueCount = computeQueueCounts;
            queueCreateInfo.pQueuePriorities = computeQueuePriority.data();
            queueCreateInfos.push_back(queueCreateInfo);
        }

        if (copyQueueCounts > 0)
        {
            queueCreateInfo.queueFamilyIndex = m_queues.copyFamily;
            queueCreateInfo.queueCount = copyQueueCounts;
            queueCreateInfo.pQueuePriorities = copyQueuePriority.data();
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkDeviceCreateInfo createInfo{};
        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};

        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());

        // We use physical device features2.
        createInfo.pEnabledFeatures = nullptr;
        createInfo.pNext = &physicalDeviceFeatures2;
        {
            physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physicalDeviceFeatures2.features = enable10GpuFeatures;
            physicalDeviceFeatures2.pNext    = &enable11GpuFeatures;
        }

        // device extension.
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
        createInfo.ppEnabledExtensionNames = deviceExtensionNames.data();

        // No special device layer, all control by instance layer.
        createInfo.ppEnabledLayerNames = NULL;
        createInfo.enabledLayerCount = 0;

        RHICheck(vkCreateDevice(m_gpu, &createInfo, nullptr, &m_device));

        // get major queues.
        m_queues.graphcisQueues.resize(graphicsQueueCounts);
        for (uint32_t id = 0; id < graphicsQueueCounts; id++)
        {
            vkGetDeviceQueue(m_device, m_queues.graphicsFamily, id, &m_queues.graphcisQueues[id]);
        }

        m_queues.computeQueues.resize(computeQueueCounts);
        for (uint32_t id = 0; id < computeQueueCounts; id++)
        {
            vkGetDeviceQueue(m_device, m_queues.computeFamily, id, &m_queues.computeQueues[id]);
        }

        m_queues.copyQueues.resize(copyQueueCounts);
        for (uint32_t id = 0; id < copyQueueCounts; id++)
        {
            vkGetDeviceQueue(m_device, m_queues.copyFamily, id, &m_queues.copyQueues[id]);
        }

        // After create, check feature state to ensure the device is actually support or not.
        if (m_graphicsSupportStates.bSupportRaytrace)
        {
            // Get the acceleration structure features, which we'll need later.
            VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
            asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

            VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
            rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            asFeatures.pNext = &rtPipelineFeatures;

            VkPhysicalDeviceFeatures2 deviceFeatures{};
            deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            deviceFeatures.pNext = &asFeatures;

            vkGetPhysicalDeviceFeatures2(m_gpu, &deviceFeatures);

            m_graphicsSupportStates.bSupportRaytrace = (asFeatures.accelerationStructure && rtPipelineFeatures.rayTracingPipeline);
            if (!m_graphicsSupportStates.bSupportRaytrace)
            {
                LOG_WARN("Try enable hardware ray tracing, but no support in your machine, close here.");
            }
            else
            {
                LOG_TRACE("Raytrace extension and feature enable and opening...");
            }
        }
    }

    void VulkanContext::destroyDevice()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_device, nullptr);
        }
    }
}