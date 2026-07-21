/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <unordered_map>
#include <atomic>
#include <string>
#include "VkDispatchTable.h"
#include "Connection.h"
#include "NvBvhViewerVK.h"
#include "Lock.h"
#include "VkPipelineComputeStateTracker.h"
#include "LogMessage.h"

std::atomic_flag g_GlobalLock = ATOMIC_FLAG_INIT;
SamplerConnection g_Connection = {};

///////////////////////////////////////////////
#define EXPORT extern "C" __declspec(dllexport)
#define RETURN_FUNC( func ) \
if( strcmp( pName, #func ) == 0 ) \
{ \
    /*LOG_DEBUG( "Function address returned: '%s'", pName );*/ \
    return (PFN_vkVoidFunction)& BvhvSampler_##func; \
}
///////////////////////////////////////////////

struct InstanceData
{
    VkInstance instance = VK_NULL_HANDLE;
    VulkanDispatchTable dispatchTable = {};
};

struct DeviceData
{
    VkDevice device = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    InstanceData* instanceData = nullptr;
    NvBvhViewerVK* bvhViewer = nullptr;
    VulkanDispatchTable dispatchTable = {};
    VkPipelineComputeStateTracker stateTracker;
    std::unordered_map<VkQueue, ViewerQueueType> queueToTypeMap;
};

typedef void*  Key;

template<typename T>
Key GetKey(T handle)
{
    return *(void**)handle;
}

template<typename T>
T FindInMap(std::unordered_map<Key, T>& map, Key key)
{
    auto it = map.find(key);
    if(it != map.end())
        return it->second;
    return T(0);
}

std::unordered_map<Key, InstanceData*> g_InstanceDataMap;
std::unordered_map<Key, DeviceData*> g_DeviceDataMap;

///////////////////////////////////////////////
inline VkLayerInstanceCreateInfo* GetLayerLinkInfo(const VkInstanceCreateInfo* instanceCreateInfo)
{
    VkLayerInstanceCreateInfo* info = (VkLayerInstanceCreateInfo*)instanceCreateInfo->pNext;

    while (info != nullptr)
    {
        if (info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && info->function == VK_LAYER_LINK_INFO)
            return info;

        info = (VkLayerInstanceCreateInfo*)info->pNext;
    }

    return nullptr;
}

VkLayerDeviceCreateInfo* GetLayerLinkInfo(const VkDeviceCreateInfo* deviceCreateInfo)
{
    VkLayerDeviceCreateInfo* info = (VkLayerDeviceCreateInfo*)deviceCreateInfo->pNext;

    while (info != nullptr)
    {
        if (info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && info->function == VK_LAYER_LINK_INFO)
            return info;

        info = (VkLayerDeviceCreateInfo*)info->pNext;
    }

    return nullptr;
}

PFN_vkSetDeviceLoaderData GetLoaderCallback(const VkDeviceCreateInfo* deviceCreateInfo)
{
    VkLayerDeviceCreateInfo* info = (VkLayerDeviceCreateInfo*)deviceCreateInfo->pNext;

    while (info != nullptr)
    {
        if (info->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && info->function == VK_LOADER_DATA_CALLBACK)
            return info->u.pfnSetDeviceLoaderData;

        info = (VkLayerDeviceCreateInfo*)info->pNext;
    }

    return nullptr;
}

void VKAPI_CALL BvhvSampler_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(device));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkDestroyDevice: Failed to retrieve divice data from VkDevice");
        return;
    }

    if(deviceData->bvhViewer)
    {
        deviceData->bvhViewer->Shutdown();
        deviceData->bvhViewer->Deinitialize();
        delete(deviceData->bvhViewer);
        deviceData->bvhViewer = nullptr;
    }

    deviceData->dispatchTable.vkDestroyDevice(device, pAllocator);
    ScopedLock scopedLock = ScopedLock(g_GlobalLock);
    g_DeviceDataMap.erase(GetKey(device));
    delete deviceData;
}

VkResult VKAPI_CALL BvhvSampler_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    InstanceData* instanceData = FindInMap(g_InstanceDataMap, GetKey(physicalDevice));
    if(instanceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCreateDevice: Failed to retrieve divice data from VkPhysicalDevice");
        return VK_ERROR_UNKNOWN;
    }

    VkLayerDeviceCreateInfo* layerCreateInfo = GetLayerLinkInfo(pCreateInfo);
    if (layerCreateInfo == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCreateDevice: GetLayerLinkInfo failed");
        return VK_ERROR_UNKNOWN;
    }

    VkDeviceCreateInfo info = *pCreateInfo;
    std::vector<const char*> extNameArray(info.ppEnabledExtensionNames, info.ppEnabledExtensionNames + info.enabledExtensionCount);

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeature = {};
    descriptorBufferFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    descriptorBufferFeature.descriptorBuffer = true;

    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR positionFetchFeature = {};
    positionFetchFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR;
    positionFetchFeature.rayTracingPositionFetch = true;

    bool enableRaytracing = false;
    bool enableLss = false;
    bool enablePTLAS = false;
    bool enablePositionFetch = false;
    bool enableDescriptorBuffers = false;

    { // detect which features are requested by the caller
        bool appRequestedSpirv_1_4 = false;
        bool appRequestedMaintenance5 = false;
        bool appRequestedLss = false;
        for (uint32_t i = 0; i < info.enabledExtensionCount; ++i)
        {
            if (strcmp(extNameArray[i], VK_KHR_SPIRV_1_4_EXTENSION_NAME) == 0)
                appRequestedSpirv_1_4 = true;

            const bool ray_tracing = std::string(extNameArray[i]).find("ray_tracing") != std::string::npos;
            const bool ray_query = std::string(extNameArray[i]).find("ray_query") != std::string::npos;
            if (ray_tracing || ray_query)
                enableRaytracing = true;

            if (strcmp(extNameArray[i], VK_KHR_MAINTENANCE_5_EXTENSION_NAME) == 0)
                appRequestedMaintenance5 = true;

            if (strcmp(extNameArray[i], VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME) == 0)
                appRequestedLss = true;

            if(strcmp(extNameArray[i], VK_NV_PARTITIONED_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
                enablePTLAS = true;

            if(strcmp(extNameArray[i], VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME) == 0)
                enablePositionFetch = true;

            if(strcmp(extNameArray[i], VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0)
                enableDescriptorBuffers = true;
        }
        enableLss = appRequestedSpirv_1_4 && appRequestedMaintenance5 && appRequestedLss;
    }

    if(enableRaytracing)
    {
        uint32_t deviceExtCount = 0;
        instanceData->dispatchTable.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtCount, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(deviceExtCount);
        instanceData->dispatchTable.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtCount, deviceExtensions.data());

        if (enablePTLAS && !enableDescriptorBuffers)
        { // PTLAS can only be bound via device address. VK_EXT_descriptor_buffer is required for it. Force target device to enable it if supports.
          // TODO: consider switching to descriptor_heap in the future
            for (uint32_t decideExtId = 0; decideExtId < deviceExtCount; ++decideExtId)
            {
                if (strcmp(deviceExtensions[decideExtId].extensionName, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0)
                {
                    enableDescriptorBuffers = true;
                    extNameArray.push_back(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
                    break;
                }
            }

            if (enableDescriptorBuffers)
            {
                descriptorBufferFeature.pNext = (void*)info.pNext;
                info.pNext = &descriptorBufferFeature;
            }
        }

        const bool forcePositionFetch = g_Connection.IsOpened() ? g_Connection.GetViewerDesc().forcePositionFetchExt : false;
        if (enableRaytracing && !enablePositionFetch && forcePositionFetch)
        {
            for (uint32_t decideExtId = 0; decideExtId < deviceExtCount; ++decideExtId)
            {
                if (strcmp(deviceExtensions[decideExtId].extensionName, VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME) == 0)
                {
                    enablePositionFetch = true;
                    extNameArray.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
                    break;
                }
            }

            if(enablePositionFetch)
            {
                positionFetchFeature.pNext = (void*)info.pNext;
                info.pNext = &positionFetchFeature;
            }
        }
    }

    info.ppEnabledExtensionNames = extNameArray.data();
    info.enabledExtensionCount = uint32_t(extNameArray.size());

    std::vector<VkDeviceQueueCreateInfo> newQueueCreateInfo(info.queueCreateInfoCount);
    std::vector<float> graphicsQueuePriorities;
    uint32_t graphicsQueueFamilyId = uint32_t(~0);
    uint32_t computeQueueFamilyId = uint32_t(~0);
    uint32_t customGraphicsQueueId = uint32_t(~0);
    if (enableRaytracing)
    { // request an additional graphics queue to be created
        uint32_t numFamilyProperties = 0;
        instanceData->dispatchTable.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numFamilyProperties, nullptr);
        std::vector<VkQueueFamilyProperties> familyProperties(numFamilyProperties);
        instanceData->dispatchTable.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &numFamilyProperties, familyProperties.data());

        for (uint32_t i = 0; i < numFamilyProperties; ++i)
        {
            if ((familyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
                graphicsQueueFamilyId = i;
            else if ((familyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
                computeQueueFamilyId = i;

            if (graphicsQueueFamilyId != uint32_t(~0) && computeQueueFamilyId != uint32_t(~0))
                break;
        }

        uint32_t graphicsQueueCreateInfoId = 0;
        for (; graphicsQueueCreateInfoId < info.queueCreateInfoCount; ++graphicsQueueCreateInfoId)
        {
            if (info.pQueueCreateInfos[graphicsQueueCreateInfoId].queueFamilyIndex == graphicsQueueFamilyId)
                break;
        }

        const uint32_t maxGraphicsQueueCount = familyProperties[graphicsQueueFamilyId].queueCount;
        const uint32_t totalRequestedGraphicsQueueCount = info.pQueueCreateInfos[graphicsQueueCreateInfoId].queueCount + 1;
        if (graphicsQueueFamilyId != uint32_t(~0) && maxGraphicsQueueCount >= totalRequestedGraphicsQueueCount)
        {
            memcpy(newQueueCreateInfo.data(), info.pQueueCreateInfos, info.queueCreateInfoCount * sizeof(VkDeviceQueueCreateInfo));
            customGraphicsQueueId = newQueueCreateInfo[graphicsQueueCreateInfoId].queueCount;

            graphicsQueuePriorities.resize(customGraphicsQueueId + 1, 0.5f);
            memcpy(graphicsQueuePriorities.data(), newQueueCreateInfo[graphicsQueueFamilyId].pQueuePriorities, customGraphicsQueueId * sizeof(float));

            newQueueCreateInfo[graphicsQueueCreateInfoId].queueCount += 1;
            newQueueCreateInfo[graphicsQueueCreateInfoId].pQueuePriorities = graphicsQueuePriorities.data();

            info.pQueueCreateInfos = newQueueCreateInfo.data();
        }
    }

    VkLayerDeviceLink*& layerDeviceLink = layerCreateInfo->u.pLayerInfo;
    const PFN_vkGetInstanceProcAddr getInstanceProcAddr = layerDeviceLink->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr getDeviceProcAddr = layerDeviceLink->pfnNextGetDeviceProcAddr;
    layerDeviceLink = layerDeviceLink->pNext;

    const PFN_vkCreateDevice createDevice = (PFN_vkCreateDevice)getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateDevice");
    const VkResult result = createDevice(physicalDevice, &info, pAllocator, pDevice);

    if(result != VK_SUCCESS)
        return result;

    ScopedLock scopedLock = ScopedLock(g_GlobalLock);

    if (g_DeviceDataMap.empty())
        g_DeviceDataMap.reserve(256);

    DeviceData* deviceData = new DeviceData();
    deviceData->instanceData = instanceData;
    deviceData->device = *pDevice;
    deviceData->instance = instanceData->instance;
    deviceData->physicalDevice = physicalDevice;
    deviceData->queueToTypeMap.reserve(256);
    FillVkDispatchTable(&deviceData->dispatchTable, deviceData->device, deviceData->instance, getDeviceProcAddr, getInstanceProcAddr, GetLoaderCallback(pCreateInfo));
    g_DeviceDataMap.insert(std::pair<Key, DeviceData*>(GetKey(*pDevice), deviceData));

    if(g_Connection.IsOpened() == false)
        return result;

    if (!enableRaytracing)
        return result;

    bool hasComputeQueue = false;
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
    {
        const VkDeviceQueueCreateInfo& queueInfo = pCreateInfo->pQueueCreateInfos[i];
        for (uint32_t j = 0; j < queueInfo.queueCount; j++)
        {
            bool isGraphics = queueInfo.queueFamilyIndex == graphicsQueueFamilyId;
            bool isCompute = queueInfo.queueFamilyIndex == computeQueueFamilyId;
            hasComputeQueue = hasComputeQueue || isCompute;

            VkQueue queue = VK_NULL_HANDLE;
            deviceData->dispatchTable.vkGetDeviceQueue(*pDevice, queueInfo.queueFamilyIndex, j, &queue);
            if (queue)
            {
                deviceData->dispatchTable.vkSetDeviceLoaderData(*pDevice, queue);
                ViewerQueueType queueType = isGraphics ? ViewerQueueType::Graphics : ViewerQueueType::Count;
                queueType = isCompute ? ViewerQueueType::Compute : queueType;
                deviceData->queueToTypeMap.insert({queue, queueType});
            }
        }
    }

    VkQueue internalGraphicsQueue = VK_NULL_HANDLE;
    if (customGraphicsQueueId != uint32_t(~0) && graphicsQueueFamilyId != uint32_t(~0))
    {
        VkQueue queue = VK_NULL_HANDLE;
        deviceData->dispatchTable.vkGetDeviceQueue(*pDevice, graphicsQueueFamilyId, customGraphicsQueueId, &queue);
        deviceData->dispatchTable.vkSetDeviceLoaderData(*pDevice, queue); 
        internalGraphicsQueue = queue;
    }

    if (!internalGraphicsQueue)
        return result;

    deviceData->bvhViewer = new NvBvhViewerVK();
    if (deviceData->bvhViewer)
    {
        NvBvhViewerVK::InitDesc initDesc = {};
        initDesc.vkPhysicalDevice = deviceData->physicalDevice;
        initDesc.vkDevice = deviceData->device;
        initDesc.vkInstance = deviceData->instance;
        initDesc.vkViewerGraphicsQueue = internalGraphicsQueue;
        initDesc.vkGetDeviceProcAddrFuncPtr = deviceData->dispatchTable.vkGetDeviceProcAddr;
        initDesc.vkGetInstanceProcAddrFuncPtr = deviceData->dispatchTable.vkGetInstanceProcAddr;
        initDesc.vkSetDeviceLoaderDataPtr = deviceData->dispatchTable.vkSetDeviceLoaderData;
        initDesc.outputWidth = g_Connection.GetViewerDesc().width;
        initDesc.outputHeight = g_Connection.GetViewerDesc().height;
        initDesc.numBufferedFrames = 3;
        initDesc.enableInlineRendering = g_Connection.GetViewerDesc().inlineRendering;
        initDesc.hasComputeQueue = hasComputeQueue;
        initDesc.enableLss = enableLss;
        initDesc.enablePositionFetch = enablePositionFetch;
        initDesc.enableDescriptorBuffers = enableDescriptorBuffers;
        initDesc.logMessageCallback = LogViewerMessage;
        deviceData->bvhViewer->Initialize(initDesc);
        deviceData->bvhViewer->Launch();
    }

    return result;
}

void VKAPI_CALL BvhvSampler_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    InstanceData* instanceData = FindInMap(g_InstanceDataMap, GetKey(instance));
    if(instanceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkDestroyInstance: Failed to retrieve instance data from VkInstance");
        return;
    }

    instanceData->dispatchTable.vkDestroyInstance(instance, pAllocator);
    ScopedLock scopedLock = ScopedLock(g_GlobalLock);
    if(g_Connection.IsOpened())
        g_Connection.Close();
    g_InstanceDataMap.erase(GetKey(instance));
    _aligned_free(instanceData);
}

VkResult VKAPI_CALL BvhvSampler_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)
{
    VkLayerInstanceCreateInfo* layerCreateInfo = GetLayerLinkInfo(pCreateInfo);
    if (layerCreateInfo == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCreateInstance: GetLayerLinkInfo failed");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkLayerInstanceLink*& layerLink = layerCreateInfo->u.pLayerInfo;
    const PFN_vkGetInstanceProcAddr getInstanceProcAddr = layerLink->pfnNextGetInstanceProcAddr;
    layerLink = layerLink->pNext;

    const PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    const VkResult result = vkCreateInstance(pCreateInfo, pAllocator, pInstance);

    if(result == VK_SUCCESS)
    {
        ScopedLock scopedLock = ScopedLock(g_GlobalLock);

        if(g_InstanceDataMap.empty())
            g_InstanceDataMap.reserve(256);

        InstanceData* instanceData = (InstanceData*)_aligned_malloc(sizeof(InstanceData), alignof(InstanceData));
        if(instanceData)
        {
            *instanceData = InstanceData();
            instanceData->instance = *pInstance;
            g_InstanceDataMap.insert(std::pair<Key, InstanceData*>(GetKey(*pInstance), instanceData));
            if(g_Connection.IsOpened() == false)
                g_Connection.Open(nullptr);
            FillVkDispatchTable(&instanceData->dispatchTable, 0, instanceData->instance, 0, getInstanceProcAddr, 0);
        }
    }

    return result;
}

VkResult VKAPI_CALL BvhvSampler_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    VkResult result = VK_ERROR_UNKNOWN;
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(queue));
    if(deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkQueueSubmit: Failed to retrieve device data from VkQueue");
        return VK_ERROR_UNKNOWN; 
    }

    ViewerQueueType queueType = ViewerQueueType::Count;
    {
        auto it = deviceData->queueToTypeMap.find(queue);
        if (it != deviceData->queueToTypeMap.end())
            queueType = it->second;
    }

    if(deviceData->bvhViewer == nullptr || queueType == ViewerQueueType::Count)
        return deviceData->dispatchTable.vkQueueSubmit(queue, submitCount, pSubmits, fence);

    static thread_local std::vector<VkSubmitInfo> submits;
    submits.clear();
    submits.resize(submitCount);
    memcpy(submits.data(), pSubmits, submitCount * sizeof(VkSubmitInfo));

    static thread_local std::vector<std::vector<VkCommandBuffer>> commandBuffersPerSubmit;
    commandBuffersPerSubmit.clear();
    commandBuffersPerSubmit.resize(submitCount);

    VkFence appFence = fence;
    if (appFence != VK_NULL_HANDLE)
    { // encountered a condition, when using application's fence for sync is not safe.
      // application may destroy its fence, thus waiting on it will result in a crash.
      // to WAR it, ugly empty submitQueue trick is used to signal viewer's fence
        fence = VK_NULL_HANDLE; // null this fence, so viewer returns an internal one
    }

    for (uint32_t i = 0; i < submitCount; ++i)
    { // TODO: unify code in BvhvSampler_vkQueueSubmit with BvhvSampler_vkQueueSubmit2
        VkSubmitInfo& submit = submits[i];
        NvBvhViewerVK::QueueSubmitDesc desc = {};
        desc.inQueue = queue;
        desc.inCommandBuffers = submit.pCommandBuffers;
        desc.inCommandBufferNum = submit.commandBufferCount;
        desc.inQueueType = queueType;
        NvBvhViewerVK::QueueSubmitResult submitResult = deviceData->bvhViewer->OnQueueSubmit(desc, nullptr, fence);

        fence = submitResult.outQueueSubmitSignalFence ? submitResult.outQueueSubmitSignalFence : fence;

        if (!submitResult.outAppendCommandBuffer)
            continue;

        std::vector<VkCommandBuffer>& commandBufferArray = commandBuffersPerSubmit[i];
        commandBufferArray.resize(submit.commandBufferCount + 1);
        memcpy(commandBufferArray.data(), submit.pCommandBuffers, submit.commandBufferCount * sizeof(VkCommandBuffer));
        commandBufferArray.back() = submitResult.outAppendCommandBuffer;

        submit.commandBufferCount += 1;
        submit.pCommandBuffers = commandBuffersPerSubmit[i].data();
    }

    VkFence viewerFence = fence;
    if (appFence != VK_NULL_HANDLE)
        fence = appFence;

    result = deviceData->dispatchTable.vkQueueSubmit(queue, submitCount, submits.data(), fence);

    if (appFence != VK_NULL_HANDLE && viewerFence != VK_NULL_HANDLE)
        deviceData->dispatchTable.vkQueueSubmit(queue, 0, nullptr, viewerFence);

    return result;
}

VkResult VKAPI_CALL BvhvSampler_vkQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence)
{
    VkResult result = VK_ERROR_UNKNOWN;
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(queue));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkQueueSubmit2: Failed to retrieve device data from VkQueue");
        return VK_ERROR_UNKNOWN;
    }

    ViewerQueueType queueType = ViewerQueueType::Count;
    {
        auto it = deviceData->queueToTypeMap.find(queue);
        if (it != deviceData->queueToTypeMap.end())
            queueType = it->second;
    }

    if (deviceData->bvhViewer == nullptr || queueType == ViewerQueueType::Count)
        return deviceData->dispatchTable.vkQueueSubmit2(queue, submitCount, pSubmits, fence);

    static thread_local std::vector<VkSubmitInfo2> submits;
    submits.clear();
    submits.resize(submitCount);
    memcpy(submits.data(), pSubmits, submitCount * sizeof(VkSubmitInfo2));

    static thread_local std::vector<std::vector<VkCommandBufferSubmitInfo>> commandBufferInfoPerSubmit;
    commandBufferInfoPerSubmit.clear();
    commandBufferInfoPerSubmit.resize(submitCount);

    static thread_local std::vector<std::vector<VkCommandBuffer>> commandBuffersPerSubmit;
    commandBuffersPerSubmit.clear();
    commandBuffersPerSubmit.resize(submitCount);

    VkFence appFence = fence;
    if (appFence != VK_NULL_HANDLE)
    { // encountered a condition, when using application's fence for sync is not safe.
      // application may destroy its fence, thus waiting on it will result in an error
      // to WAR it, ugly empty submitQueue trick is used
        fence = VK_NULL_HANDLE; // null this fence, so viewer returns an internal one
    }

    for(uint32_t i = 0; i < submitCount; ++i)
    { // TODO: unify code in BvhvSampler_vkQueueSubmit with BvhvSampler_vkQueueSubmit2
        VkSubmitInfo2& submit = submits[i];

        std::vector<VkCommandBuffer>& commandBufferArray = commandBuffersPerSubmit[i];
        commandBufferArray.resize(submit.commandBufferInfoCount);

        for(uint32_t j = 0; j < submit.commandBufferInfoCount; ++j)
            commandBufferArray[j] = submit.pCommandBufferInfos[j].commandBuffer;

        NvBvhViewerVK::QueueSubmitDesc desc = {};
        desc.inQueue = queue;
        desc.inCommandBuffers = commandBufferArray.data();
        desc.inCommandBufferNum = submit.commandBufferInfoCount;
        desc.inQueueType = queueType;
        NvBvhViewerVK::QueueSubmitResult submitResult = deviceData->bvhViewer->OnQueueSubmit(desc, nullptr, fence);

        fence = submitResult.outQueueSubmitSignalFence ? submitResult.outQueueSubmitSignalFence : fence;

        if (!submitResult.outAppendCommandBuffer)
            continue;

        std::vector<VkCommandBufferSubmitInfo>& commandBufferSubmitInfos = commandBufferInfoPerSubmit[i];
        commandBufferSubmitInfos.clear();
        commandBufferSubmitInfos.resize(submit.commandBufferInfoCount + 1);
        memcpy(commandBufferSubmitInfos.data(), submit.pCommandBufferInfos, submit.commandBufferInfoCount * sizeof(VkCommandBufferSubmitInfo));

        commandBufferSubmitInfos.back().commandBuffer = submitResult.outAppendCommandBuffer;
        commandBufferSubmitInfos.back().sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandBufferSubmitInfos.back().pNext = nullptr;
        commandBufferSubmitInfos.back().deviceMask = 0x1;

        submit.commandBufferInfoCount += 1;
        submit.pCommandBufferInfos = commandBufferSubmitInfos.data();

        fence = submitResult.outQueueSubmitSignalFence ? submitResult.outQueueSubmitSignalFence : fence;
    }

    VkFence viewerFence = fence;
    if(appFence != VK_NULL_HANDLE)
        fence = appFence;

    result = deviceData->dispatchTable.vkQueueSubmit2(queue, submitCount, submits.data(), fence);

    if(appFence != VK_NULL_HANDLE && viewerFence != VK_NULL_HANDLE)
        deviceData->dispatchTable.vkQueueSubmit2(queue, 0, nullptr, viewerFence);

    return result;
}

VkResult VKAPI_CALL BvhvSampler_vkQueueSubmit2KHR(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2KHR* pSubmits, VkFence fence)
{
    return BvhvSampler_vkQueueSubmit2(queue, submitCount, pSubmits, fence);
}

VkResult VKAPI_CALL BvhvSampler_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(queue));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkQueuePresentKHR: Failed to retrieve device data from VkQueue");
        return VK_ERROR_UNKNOWN;
    }

    if (deviceData->bvhViewer)
        deviceData->bvhViewer->OnNewFrame();

    return deviceData->dispatchTable.vkQueuePresentKHR(queue, pPresentInfo);
}

void VKAPI_CALL BvhvSampler_vkCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdCopyAccelerationStructureKHR: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    deviceData->dispatchTable.vkCmdCopyAccelerationStructureKHR(commandBuffer, pInfo);

    if(deviceData->bvhViewer == nullptr)
        return;

    deviceData->bvhViewer->OnCopyAccelerationStructure(commandBuffer, pInfo);
}

void VKAPI_CALL BvhvSampler_vkCmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdBuildAccelerationStructuresKHR: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    deviceData->dispatchTable.vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);

    if (deviceData->bvhViewer == nullptr)
        return;

    static bool inlineRendering = g_Connection.IsOpened() ? g_Connection.GetViewerDesc().inlineRendering : false;
    if(inlineRendering)
    {
        if(!deviceData->stateTracker.CanRestore(commandBuffer))
            return;
        if(deviceData->bvhViewer->OnBuildAccelerationStructures(commandBuffer, infoCount, pInfos, ppBuildRangeInfos))
                deviceData->stateTracker.Restore(commandBuffer, deviceData->dispatchTable);
    }
    else
        deviceData->bvhViewer->OnBuildAccelerationStructures(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
}

void VKAPI_CALL BvhvSampler_vkCmdBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdBuildPartitionedAccelerationStructuresNV: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    deviceData->dispatchTable.vkCmdBuildPartitionedAccelerationStructuresNV(commandBuffer, pBuildInfo);

    if (deviceData->bvhViewer == nullptr)
        return;

    static bool inlineRendering = g_Connection.IsOpened() ? g_Connection.GetViewerDesc().inlineRendering : false;
    if (inlineRendering)
    {
        if (!deviceData->stateTracker.CanRestore(commandBuffer))
            return;
        if (deviceData->bvhViewer->OnBuildPartitionedAccelerationStructuresNV(commandBuffer, pBuildInfo))
            deviceData->stateTracker.Restore(commandBuffer, deviceData->dispatchTable);
    }
    else
        deviceData->bvhViewer->OnBuildPartitionedAccelerationStructuresNV(commandBuffer, pBuildInfo);
}

VkResult VKAPI_CALL BvhvSampler_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkBeginCommandBuffer: Failed to retrieve device data from VkCommandBuffer");
        return VK_ERROR_UNKNOWN; //TODO LOG:
    }

    deviceData->stateTracker.Open(commandBuffer);

    return deviceData->dispatchTable.vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

VkResult VKAPI_CALL BvhvSampler_vkEndCommandBuffer(VkCommandBuffer commandBuffer)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkEndCommandBuffer: Failed to retrieve device data from VkCommandBuffer");
        return VK_ERROR_UNKNOWN;
    }

    deviceData->stateTracker.Close(commandBuffer);

    return deviceData->dispatchTable.vkEndCommandBuffer(commandBuffer);
}

void VKAPI_CALL BvhvSampler_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdBindPipeline: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    if(pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
        deviceData->stateTracker.UpdatePipeline(commandBuffer, pipeline);

    return deviceData->dispatchTable.vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

void VKAPI_CALL BvhvSampler_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet,
    uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdBindDescriptorSets: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
        deviceData->stateTracker.UpdateDescriptorSets(commandBuffer, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

    return deviceData->dispatchTable.vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

void VKAPI_CALL BvhvSampler_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{
    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(commandBuffer));
    if (deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkCmdPushConstants: Failed to retrieve device data from VkCommandBuffer");
        return;
    }

    deviceData->stateTracker.UpdatePushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);

    return deviceData->dispatchTable.vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

EXPORT PFN_vkVoidFunction VKAPI_CALL BvhvSampler_vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    RETURN_FUNC(vkGetDeviceProcAddr);
    RETURN_FUNC(vkCreateDevice);
    RETURN_FUNC(vkDestroyDevice);

    DeviceData* deviceData = FindInMap(g_DeviceDataMap, GetKey(device));
    if(deviceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkGetDeviceProcAddr: Failed to retrieve device data from VkDevice");
        return nullptr;
    }

    if(g_Connection.IsOpened())
    {
        RETURN_FUNC(vkQueueSubmit);
        RETURN_FUNC(vkQueueSubmit2);
        RETURN_FUNC(vkQueueSubmit2KHR);
        RETURN_FUNC(vkQueuePresentKHR);
        RETURN_FUNC(vkCmdCopyAccelerationStructureKHR);
        RETURN_FUNC(vkCmdBuildAccelerationStructuresKHR);
        RETURN_FUNC(vkCmdBuildPartitionedAccelerationStructuresNV);

        if(g_Connection.GetViewerDesc().inlineRendering)
        {
            RETURN_FUNC(vkBeginCommandBuffer);
            RETURN_FUNC(vkEndCommandBuffer);
            RETURN_FUNC(vkCmdBindPipeline);
            RETURN_FUNC(vkCmdBindDescriptorSets);
            RETURN_FUNC(vkCmdPushConstants);
        }
    }

    return deviceData->dispatchTable.vkGetDeviceProcAddr(device, pName);
}

EXPORT PFN_vkVoidFunction VKAPI_CALL BvhvSampler_vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    RETURN_FUNC(vkCreateInstance);
    RETURN_FUNC(vkDestroyInstance);
    RETURN_FUNC(vkGetInstanceProcAddr);
    RETURN_FUNC(vkCreateDevice);
    RETURN_FUNC(vkDestroyDevice);
    RETURN_FUNC(vkGetDeviceProcAddr);

    if (instance == VK_NULL_HANDLE)
        return nullptr;

    InstanceData* instanceData = FindInMap(g_InstanceDataMap, GetKey(instance));
    if (instanceData == nullptr)
    {
        LogMessage("[VkLayer]", "BvhvSampler_vkGetInstanceProcAddr: Failed to retrieve instance data from VkInstance");
        return nullptr;
    }

    return instanceData->dispatchTable.vkGetInstanceProcAddr(instance, pName);
}

EXPORT VkResult VKAPI_CALL BvhvSampler_vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct)
{
    if (pVersionStruct == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;

    if (pVersionStruct->loaderLayerInterfaceVersion >= 2)
        pVersionStruct->loaderLayerInterfaceVersion = 2;

    pVersionStruct->pfnGetInstanceProcAddr = BvhvSampler_vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = BvhvSampler_vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;

    return VK_SUCCESS;
}