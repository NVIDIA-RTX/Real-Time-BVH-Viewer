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

#include "NvBvhViewerVK.h"
#include "NvBvhViewerCommonUtils.h"

#include "NvBvhViewerDispatchTableVK.h"
#include "imgui_impl_vulkan.h"

#if defined(_DEBUG)
    #define VK_RESULT_CALL(statement, failValue) assert(statement == VK_SUCCESS)
#else
    #define VK_RESULT_CALL(statement, failValue) \
    {\
        VkResult vkResult = statement;\
        if (vkResult != VK_SUCCESS) \
        { \
            if (m_LogMessageCallback)\
            {\
                const size_t messageBufferSize = 128; \
                char messageBuffer[messageBufferSize] = {}; \
                snprintf(messageBuffer, messageBufferSize, "%s failed: VkResult(%d)", #statement, int(vkResult)); \
                m_LogMessageCallback(NvBvhViewerCommon::MessageType::Error, messageBuffer); \
            }\
            return failValue;\
        };\
    }
#endif

#define VK_RESULT_BOOL(statement) VK_RESULT_CALL(statement, false)

#define SAFE_VK_RELEASE( func, owner, obj, allocator )\
{\
    if( obj != VK_NULL_HANDLE )\
    {\
        func( owner, obj, allocator ); \
        obj = VK_NULL_HANDLE;\
    }\
}

struct TlasDesc
{
    VkCommandBuffer updateCommandBuffer = nullptr;

    VkAccelerationStructureKHR accelerationStructure = nullptr;
    VkDeviceAddress instanceDescsBufferAddress = 0;

    VkDeviceAddress indirectOpsAddr = 0;
    VkDeviceAddress indirectOpsCountAddr = 0;
    VkDeviceAddress ptlasAddr = 0;

    VkBuildAccelerationStructureFlagsKHR flags = 0;
    uint32_t numInstances = 0;

    VkDescriptorSet descriptorSets[uint32_t(Pipelines::Count)] = {};
    VkBuffer descriptorBuffers[uint32_t(Pipelines::Count)] = {};
    uint64_t descriptorBufferSizes[uint32_t(Pipelines::Count)] = {};
    uint64_t descriptorBufferMemoryOffsets[uint32_t(Pipelines::Count)] = {};

    void Reset()
    {
        updateCommandBuffer = nullptr;
        accelerationStructure = nullptr;
        instanceDescsBufferAddress = 0;
        flags = 0;
        numInstances = 0;
    }
};

struct FrameDesc
{
    TlasDesc tlases[c_MaxTlasPerFrame] = {};
    uint64_t readbackOffset = 0;
    uint32_t tlasOffset = 0;
    uint32_t commandBufferOffset = 0;
    uint32_t numBlasBuilds = 0;

    VkFence renderFences[c_MaxTlasPerFrame];
    VkFence presentFence;

    void Reset()
    {
        for (uint32_t i = 0; i < c_MaxTlasPerFrame; ++i)
            tlases[i].Reset();

        tlasOffset = 0;
        commandBufferOffset = 0;
        numBlasBuilds = 0;
        readbackOffset = 0;
    }
};

struct DescriptorRangeDesc
{
    std::vector< uint8_t > data;
    std::vector< DescriptorBindingDesc > bindings;
    VkWriteDescriptorSetAccelerationStructureKHR rtasDescriptorWrites = {};
    uint32_t baseBindingId;
    VkDescriptorType type;
    uint32_t numDescriptors;
};

struct PipelineDescVk
{
    std::vector<DescriptorRangeDesc> descriptorRanges;
    VkPipeline pipeline = nullptr;
    VkShaderModule shaderModule = nullptr;
    VkPipelineLayout pipelineLayout = nullptr;
    VkDescriptorSetLayout descriptorSetLayout = nullptr;
    bool hasPushConstants = false;
};

struct ViewerBufferDesc
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkBufferView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    uint64_t totalSize = 0;
    uint64_t elementNum = 0;
    uint64_t elementStride = 0;
    uint64_t alignment = 0;
    uint64_t offsetInMemory = 0;

    uint32_t memoryTypeId = VK_MAX_MEMORY_TYPES;
    uint32_t usageFlags = 0;
    uint32_t staleFrameId = 0;
    BufferGroup group = BufferGroup::Unknown;
    VkFormat typeFormat = VK_FORMAT_UNDEFINED;
    BvhViewerResources resourceType = BvhViewerResources::Count;
    bool isDynamic = false;
};

struct ViewerImageDesc
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint64_t size = 0;
    uint64_t alignment = 0;
    uint32_t memoryTypeId = VK_MAX_MEMORY_TYPES;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t staleFrameId = 0;
};

struct CommandBuffer
{
    VkCommandPool pool;
    VkCommandBuffer buffer;
};

struct VkPresenter
{
    static constexpr uint32_t c_NumFrames = 3;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages = {};
    std::vector<VkImageView> swapChainImageViews = {};
    std::vector<VkFramebuffer> framebuffers = {};
    std::vector<VkSemaphore> renderSemaphores = {};
    std::vector<VkSemaphore> acquireSemaphores = {};
    std::vector<VkFence> fences = {};

    VkRenderPass renderPass = nullptr;
    VkPipeline pipeline = nullptr;
    VkDescriptorPool descriptorPool = nullptr;
    std::vector<CommandBuffer> commandBuffers = {};

    const VkFormat requestedFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    const VkColorSpaceKHR requestedColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    VkColorSpaceKHR colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    const VkPresentModeKHR requestedPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

    const VkClearValue clearValue = { { { 0.1f, 0.1f, 0.1f, 1.0f } } };
    uint32_t imageCount = c_NumFrames;
    uint32_t swapChainWidth = 0;
    uint32_t swapChainHeight = 0;
};

struct InternalContext
{
public:
    uint32_t GetFrameId() const { return m_FrameId.load(std::memory_order_acquire); }
    uint32_t GetBufferedFrameId() const { return GetFrameId() % m_NumBufferedFrames; }

    void RenderTlas(VkCommandBuffer commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId);
    void RenderTlasDeferred(VkFence& frameFence, VkCommandBuffer& commandBuffer, ViewerQueueType queueType, uint32_t bufferedFrameId, uint32_t tlasId);
    void RenderPresenter(void* window, std::atomic_flag& lock);

    bool CreateBackend();
    bool CreatePresenter(void* window);

    void PrepareDispatchTable();
    bool ResizeSwapChain();
    void DestroyBackend();
    void DestroySwapChain();
    void DestroyPresenter();

private:
    bool CreatePipelines();
    bool CreateBufferPool();
    bool CreateImgui(VkPresenter& p);

    bool CreateCommandBuffer(uint32_t graphicsQueueFamily, CommandBuffer& commandBuffer);
    void CreateAndBeginCommandBuffer(VkDevice vkDevice, uint32_t graphicsQueueFamily, CommandBuffer& commandBuffer);

    bool CreateBuffer(ViewerBufferDesc& buffer);
    ViewerBufferDesc& GetBuffer(BvhViewerResources resource, uint32_t frameId, uint32_t tlasId);
    bool ResizeRenderImage(ViewerImageDesc& image, VkCommandBuffer commandBuffer);
    void GrowDynamicBuffer(ViewerBufferDesc& buffer, uint64_t newSize);
    void UpdateObjectPool(bool force);
    void CopyFeedbackBuffer(VkCommandBuffer vkCommandBuffer, uint32_t bufferedFrameId, uint32_t tlasId);
    bool PreparePipeline(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId);
    bool PreparePipelineViaDescriptorPool(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId);
    bool PreparePipelineViaDescriptorBuffer(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId);

public:
    struct
    {
        std::vector<ViewerBufferDesc> buffers;
        std::vector<ViewerImageDesc> images;

        std::vector<CommandBuffer> commandBuffers[uint32_t(ViewerQueueType::Count)];
        std::vector<VkFence> fences;
        std::vector<VkDeviceMemory> staticMemories;

        uint32_t perBufferTypeOffsets[uint32_t(BvhViewerResources::NumBuffers)];

        std::vector<ViewerBufferDesc> staleBuffers;
        std::vector<ViewerImageDesc> staleImages;
    } m_ObjectPool = {};

    BlasTracker m_BlasTracker = {};
    std::vector<FrameDesc> m_Frames = {};

    VkPhysicalDevice vkPhysicalDevice = nullptr;
    VkDevice vkDevice = nullptr;
    VkInstance vkInstance = nullptr;
    VkQueue vkGraphicsPresentQueue = nullptr;
    VkAllocationCallbacks* vkAllocator = nullptr;
    void* vkGetDeviceProcAddrFuncPtr = nullptr;
    void* vkGetInstanceProcAddrFuncPtr = nullptr;
    void* vkSetDeviceLoaderDataPtr = nullptr;
    uint32_t* m_OutputWidth = nullptr;
    uint32_t* m_OutputHeight = nullptr;

    NvBvhViewerCommon::PFN_LogMessageCallback m_LogMessageCallback = nullptr;

    constexpr static uint32_t c_GpuUploadBuffering = 2;
    uint32_t m_NumBufferedFrames = 0;

    std::atomic<uint32_t> m_FrameId = 0;
    bool m_HasComputeQueue = false;
    bool m_EnableLss = false;
    bool m_DispatchTableInited = false;
    bool m_Inline = false;
    bool m_DescriptorBufferExtOn = false;
    bool m_EnablePositionFetch = false;

private:
    VkPresenter m_Presenter;
    Camera m_Camera = {};
    ConstantBufferHelper m_ConstantBufferHelper = {};
    ExposedSettings m_Settings = {};

    PipelineDescVk m_Pipelines[uint32_t(Pipelines::Count)] = {};
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDeviceMemory m_DescriptorBufferMemory = VK_NULL_HANDLE;

    uint32_t vkQueueFamilies[uint32_t(ViewerQueueType::Count)] = {};
    uint32_t m_GpuUploadBufferedId = 0;
};

inline uint32_t GetMemoryType(VkPhysicalDevice vkPhysicalDevice, uint32_t requestedMemoryType, uint32_t excludedMemoryTypes);
inline VkDescriptorType GetVkDescriptorType(const DescriptorTypes viewerType);
VkDescriptorBufferInfo GetDescriptorBufferInfo(const ViewerBufferDesc& bufferDesc);

void DestroyViewerImage(VkDevice vkDevice, ViewerImageDesc& image, VkAllocationCallbacks* vkAllocator);
void DestroyViewerBuffer(VkDevice vkDevice, ViewerBufferDesc& buffer, VkAllocationCallbacks* vkAllocator);

inline VkBufferMemoryBarrier FillBufferBarrier(const ViewerBufferDesc& buffer, VkAccessFlagBits accessBefore, VkAccessFlagBits accessAfter);
inline VkImageMemoryBarrier FillImageBarrier(VkImage image, VkAccessFlagBits accessBefore, VkImageLayout layoutBefore, VkAccessFlagBits accessAfter, VkImageLayout layoutAfter);

inline void AfterTlasCopyImageBarriers(VkCommandBuffer commandBuffer, VkImage tlasRenderImage, VkImage swapChainImage);
inline void BeforeTlasCopyImageBarriers(VkCommandBuffer commandBuffer, VkImage tlasRenderImage, VkImage swapChainImage);

#pragma region [ Impl ]
inline uint32_t GetMemoryType(VkPhysicalDevice vkPhysicalDevice, uint32_t requestedMemoryType, uint32_t excludedMemoryTypes)
{
    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(vkPhysicalDevice, &physicalDeviceMemoryProperties);

    for (uint32_t typeId = 0; typeId < physicalDeviceMemoryProperties.memoryTypeCount; ++typeId)
    {
        const VkMemoryType& memoryType = physicalDeviceMemoryProperties.memoryTypes[typeId];
        bool passInclusion = (memoryType.propertyFlags & requestedMemoryType) == requestedMemoryType;
        bool passExclusion = (memoryType.propertyFlags & excludedMemoryTypes) == 0;
        if (passExclusion && passInclusion)
            return typeId;
    }

    return VK_MAX_MEMORY_TYPES;
}

VkDescriptorType GetVkDescriptorType(const DescriptorTypes viewerType)
{
    switch (viewerType)
    {
    case DescriptorTypes::Tlas:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    case DescriptorTypes::TypedBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorTypes::StructuredBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case DescriptorTypes::RawBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    case DescriptorTypes::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case DescriptorTypes::StorageTypedBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case DescriptorTypes::StorageStructuredBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    case DescriptorTypes::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorTypes::PushConstants:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorTypes::Count:
        break;

    default: break;
    }
    return VkDescriptorType::VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

ViewerBufferDesc& InternalContext::GetBuffer(BvhViewerResources resource, uint32_t bufferedFrameId, uint32_t tlasId)
{
    const uint32_t bufferLinearId = GetBufferResourceLinearId(resource);
    ViewerBufferDesc* buffers = m_ObjectPool.buffers.data() + m_ObjectPool.perBufferTypeOffsets[bufferLinearId];

    BufferGroup bufferGroup = buffers[0].group;
    const bool perTlas = bufferGroup == BufferGroup::PerTlas;
    const bool perTlasPerFrame = bufferGroup == BufferGroup::PerTlasPerFrame;

    uint32_t bufferId = 0;
    bufferId = perTlas ? tlasId : bufferId;
    bufferId = perTlasPerFrame ? bufferedFrameId * c_MaxTlasPerFrame + tlasId : bufferId;

    return buffers[bufferId];
}

VkDescriptorBufferInfo GetDescriptorBufferInfo(const ViewerBufferDesc& bufferDesc)
{
    VkDescriptorBufferInfo info = {};
    info.buffer = bufferDesc.buffer;
    info.range = bufferDesc.totalSize;
    info.offset = 0;
    return info;
}

void DestroyViewerBuffer(VkDevice vkDevice, ViewerBufferDesc& buffer, VkAllocationCallbacks* vkAllocator)
{
    SAFE_VK_RELEASE(vkDestroyBufferView, vkDevice, buffer.view, vkAllocator);
    SAFE_VK_RELEASE(vkDestroyBuffer, vkDevice, buffer.buffer, vkAllocator);
    if (buffer.isDynamic) // dynamic buffer owns its memory
        SAFE_VK_RELEASE(vkFreeMemory, vkDevice, buffer.memory, vkAllocator);
    buffer = {};
}

void DestroyViewerImage(VkDevice vkDevice, ViewerImageDesc& image, VkAllocationCallbacks* vkAllocator)
{
    SAFE_VK_RELEASE(vkDestroyImageView, vkDevice, image.view, vkAllocator);
    SAFE_VK_RELEASE(vkDestroyImage, vkDevice, image.image, vkAllocator);
    SAFE_VK_RELEASE(vkFreeMemory, vkDevice, image.memory, vkAllocator);
}

void InternalContext::DestroySwapChain()
{
    VkPresenter& p = m_Presenter;
    for (uint32_t i = 0; i < uint32_t(p.framebuffers.size()); ++i)
        SAFE_VK_RELEASE(vkDestroyFramebuffer, vkDevice, p.framebuffers[i], vkAllocator);

    for (uint32_t i = 0; i < uint32_t(p.swapChainImageViews.size()); ++i)
        SAFE_VK_RELEASE(vkDestroyImageView, vkDevice, p.swapChainImageViews[i], vkAllocator);

    vkDestroySwapchainKHR(vkDevice, p.swapChain, vkAllocator);

    p.framebuffers.clear();
    p.swapChainImageViews.clear();
    p.swapChainImages.clear();
    p.swapChain = nullptr;
    p.swapChainWidth = p.swapChainHeight = 0;
}

void InternalContext::DestroyPresenter()
{
    VkPresenter& p = m_Presenter;
    for (size_t i = 0; i < p.renderSemaphores.size(); i++)
        SAFE_VK_RELEASE(vkDestroySemaphore, vkDevice, p.renderSemaphores[i], vkAllocator);
    for (size_t i = 0; i < p.acquireSemaphores.size(); i++)
        SAFE_VK_RELEASE(vkDestroySemaphore, vkDevice, p.acquireSemaphores[i], vkAllocator);
    for (size_t i = 0; i < p.fences.size(); i++)
        SAFE_VK_RELEASE(vkDestroyFence, vkDevice, p.fences[i], vkAllocator);

    DestroySwapChain();

    p.renderSemaphores.resize(0); p.renderSemaphores.shrink_to_fit();
    p.acquireSemaphores.resize(0); p.acquireSemaphores.shrink_to_fit();
    p.fences.resize(0); p.fences.shrink_to_fit();

    { // imgui
        ImGui_ImplVulkan_Shutdown();
        SAFE_VK_RELEASE(vkDestroyRenderPass, vkDevice, p.renderPass, vkAllocator);
        SAFE_VK_RELEASE(vkDestroyDescriptorPool, vkDevice, p.descriptorPool, vkAllocator);
    }

    { // destroy command buffers
        for (uint32_t i = 0; i < p.imageCount; ++i)
        {
            if (p.commandBuffers[i].pool)
                vkDestroyCommandPool(vkDevice, p.commandBuffers[i].pool, vkAllocator);
        }
        p.commandBuffers.resize(0); p.commandBuffers.shrink_to_fit();
    }

    SAFE_VK_RELEASE(vkDestroySurfaceKHR, vkInstance, p.surface, vkAllocator);
}

void InternalContext::DestroyBackend()
{
    { // command buffers
        for (uint32_t i = 0; i < uint32_t(ViewerQueueType::Count); ++i)
        {
            std::vector<CommandBuffer>& buffers = m_ObjectPool.commandBuffers[i];
            uint32_t count = uint32_t(buffers.size());
            for (uint32_t bufferId = 0; bufferId < count; ++bufferId)
            {
                SAFE_VK_RELEASE(vkDestroyCommandPool, vkDevice, buffers[bufferId].pool, vkAllocator);
                buffers[bufferId].buffer = VK_NULL_HANDLE;
            }
            vkQueueFamilies[i] = uint32_t(~0);
            buffers.resize(0); buffers.shrink_to_fit();
        }
    }

    { // fences
        for (uint32_t i = 0; i < uint32_t(m_ObjectPool.fences.size()); ++i)
            SAFE_VK_RELEASE(vkDestroyFence, vkDevice, m_ObjectPool.fences[i], vkAllocator);
        m_ObjectPool.fences.resize(0); m_ObjectPool.fences.shrink_to_fit();
    }

    { // buffers
        for (uint32_t i = 0; i < uint32_t(m_ObjectPool.buffers.size()); ++i)
            DestroyViewerBuffer(vkDevice, m_ObjectPool.buffers[i], vkAllocator);
        m_ObjectPool.buffers.resize(0);
        m_ObjectPool.buffers.shrink_to_fit();
        ZeroMemory(m_ObjectPool.perBufferTypeOffsets, (sizeof(uint32_t) * size_t(BvhViewerResources::NumBuffers)));
        UpdateObjectPool(true);
    }

    { // images
        for (uint32_t i = 0; i < uint32_t(m_ObjectPool.images.size()); ++i)
            DestroyViewerImage(vkDevice, m_ObjectPool.images[i], vkAllocator);
        m_ObjectPool.images.resize(0); m_ObjectPool.images.shrink_to_fit();
    }

    { // memories
        for (uint32_t i = 0; i < uint32_t(m_ObjectPool.staticMemories.size()); ++i)
            SAFE_VK_RELEASE(vkFreeMemory, vkDevice, m_ObjectPool.staticMemories[i], vkAllocator);
        m_ObjectPool.staticMemories.resize(0); m_ObjectPool.staticMemories.shrink_to_fit();
    }

    { // pipelines
        SAFE_VK_RELEASE(vkDestroyDescriptorPool, vkDevice, m_DescriptorPool, vkAllocator);

        for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
        {
            PipelineDescVk& pipeline = m_Pipelines[i];

            SAFE_VK_RELEASE(vkDestroyPipeline, vkDevice, pipeline.pipeline, vkAllocator);
            SAFE_VK_RELEASE(vkDestroyPipelineLayout, vkDevice, pipeline.pipelineLayout, vkAllocator);
            SAFE_VK_RELEASE(vkDestroyDescriptorSetLayout, vkDevice, pipeline.descriptorSetLayout, vkAllocator);

            for (size_t rangeId = 0; rangeId < pipeline.descriptorRanges.size(); ++rangeId)
            {
                DescriptorRangeDesc& range = pipeline.descriptorRanges[rangeId];
                range.data.resize(0); range.data.shrink_to_fit();
                range.bindings.resize(0); range.bindings.shrink_to_fit();
            }
            pipeline.descriptorRanges.resize(0); pipeline.descriptorRanges.shrink_to_fit();

            SAFE_VK_RELEASE(vkDestroyShaderModule, vkDevice, pipeline.shaderModule, vkAllocator);
        }

    }
    { // null descriptor sets
        for (uint32_t i = 0; i < uint32_t(m_Frames.size()); ++i)
            for (uint32_t tlasId = 0; tlasId < c_MaxTlasPerFrame; ++tlasId)
                for (uint32_t pipelineId = 0; pipelineId < uint32_t(Pipelines::Count); ++pipelineId)
                    m_Frames[i].tlases[tlasId].descriptorSets[pipelineId] = VK_NULL_HANDLE;

    }

    { // presenter
        DestroyPresenter();
    }
}

bool InternalContext::CreateCommandBuffer(uint32_t graphicsQueueFamily, CommandBuffer& commandBuffer)
{
    VkCommandPool& pool = commandBuffer.pool;
    VkCommandBuffer& buffer = commandBuffer.buffer;

    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.pNext = nullptr;
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    VK_RESULT_BOOL(vkCreateCommandPool(vkDevice, &commandPoolCreateInfo, vkAllocator, &pool));

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.pNext = nullptr;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandPool = pool;
    commandBufferAllocateInfo.commandBufferCount = 1;
    VK_RESULT_BOOL(vkAllocateCommandBuffers(vkDevice, &commandBufferAllocateInfo, &buffer));

    if (vkSetDeviceLoaderData)
        vkSetDeviceLoaderData(vkDevice, buffer);

    return true;
}

void InternalContext::CreateAndBeginCommandBuffer(VkDevice device, uint32_t graphicsQueueFamily, CommandBuffer& commandBuffer)
{
    CreateCommandBuffer(graphicsQueueFamily, commandBuffer);
    vkResetCommandPool(device, commandBuffer.pool, 0);
    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer.buffer, &commandBufferBeginInfo);
}

namespace ImGuiViaLayer
{
    VkResult VKAPI_CALL Detour_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
    {
        VkResult result = vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
        if(vkSetDeviceLoaderData && result == VK_SUCCESS)
            for(uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i)
                vkSetDeviceLoaderData(device, pCommandBuffers[i]);
        return result;
    }
}

bool InternalContext::CreateImgui(VkPresenter& p)
{
    { // load imgui vk functions
        struct UserData { VkDevice device; VkInstance instance; };
        UserData userData = {vkDevice, vkInstance};

        auto vkFuncLoader = [](const char* funcName, void* userDataPtr)
            {
                if(strcmp("vkAllocateCommandBuffers", funcName) == 0)
                    return (PFN_vkVoidFunction)ImGuiViaLayer::Detour_vkAllocateCommandBuffers;

                UserData& userData = *(UserData*)userDataPtr;
                PFN_vkVoidFunction deviceFuncAddr = vkGetDeviceProcAddr(userData.device, funcName);
                if (deviceFuncAddr)
                    return deviceFuncAddr;
                PFN_vkVoidFunction  instanceFuncAddr = vkGetInstanceProcAddr(userData.instance, funcName);
                return instanceFuncAddr;
            };
        ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, vkFuncLoader, &userData);
    }
    { // create descriptor pool
        const VkDescriptorPoolSize descriptorPoolSizes[ ] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VkPresenter::c_NumFrames },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VkPresenter::c_NumFrames },
        };
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.pNext = nullptr;
        descriptorPoolCreateInfo.flags = 0;
        descriptorPoolCreateInfo.maxSets = VkPresenter::c_NumFrames;
        descriptorPoolCreateInfo.poolSizeCount = (uint32_t)std::size(descriptorPoolSizes);
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes;
        VK_RESULT_BOOL(vkCreateDescriptorPool(vkDevice, &descriptorPoolCreateInfo, vkAllocator, &p.descriptorPool));
    }

    { // create render pass for imgui
        VkAttachmentDescription attachmentDesc = {};
        attachmentDesc.format = p.format;
        attachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDesc.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachment = {};
        colorAttachment.attachment = 0;
        colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo = {};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &attachmentDesc;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &dependency;
        VK_RESULT_BOOL(vkCreateRenderPass(vkDevice, &renderPassCreateInfo, vkAllocator, &p.renderPass));
    }
    { // init context and backend
        // Setup Platform/Renderer backends
        ImGui_ImplVulkan_InitInfo imguiInitInfo = {};
        imguiInitInfo.Instance = vkInstance;
        imguiInitInfo.PhysicalDevice = vkPhysicalDevice;
        imguiInitInfo.Device = vkDevice;
        imguiInitInfo.QueueFamily = vkQueueFamilies[uint32_t(ViewerQueueType::Graphics)];
        imguiInitInfo.Queue = vkGraphicsPresentQueue;
        imguiInitInfo.PipelineCache = nullptr;
        imguiInitInfo.DescriptorPool = p.descriptorPool;
        imguiInitInfo.Allocator = vkAllocator;
        imguiInitInfo.MinImageCount = VkPresenter::c_NumFrames;
        imguiInitInfo.ImageCount = VkPresenter::c_NumFrames;
        imguiInitInfo.CheckVkResultFn = nullptr;
        imguiInitInfo.PipelineInfoMain.RenderPass = p.renderPass;
        if (ImGui_ImplVulkan_Init(&imguiInitInfo) == false)
            return false;
    }

    return true;
}

bool InternalContext::CreatePresenter(void* window)
{
    VkPresenter& p = m_Presenter;

    #if defined(WIN32)
        { // create Win32 VK Surface
            VkWin32SurfaceCreateInfoKHR createInfo = {};
            createInfo.hinstance = GetModuleHandle(NULL);
            createInfo.hwnd = GetWindowHandle(window);
            createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            VK_RESULT_BOOL(vkCreateWin32SurfaceKHR(vkInstance, &createInfo, vkAllocator, &p.surface));
        }
    #else
        { // Create Wayland VK Surface
            VkWaylandSurfaceCreateInfoKHR waylandSurfaceInfo = {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            //waylandSurfaceInfo.display = (wl_display*);
            //waylandSurfaceInfo.surface = (wl_surface*);
            VkResult vkResult = vkCreateWaylandSurfaceKHR(vkInstance, &waylandSurfaceInfo, vkAllocator, &p.surface);
        }
    #endif

    { // check physical device support
        VkBool32 result = 0;
        VK_RESULT_BOOL(vkGetPhysicalDeviceSurfaceSupportKHR(vkPhysicalDevice, vkQueueFamilies[uint32_t(ViewerQueueType::Graphics)], p.surface, &result));
        if (result != VK_TRUE)
            return false;
    }
    { // select swap chain format
        uint32_t numFormats;
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkPhysicalDevice, p.surface, &numFormats, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(numFormats);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vkPhysicalDevice, p.surface, &numFormats, formats.data());

        bool anyOk = numFormats == 1 && formats[0].format == VK_FORMAT_UNDEFINED;
        if (anyOk == false)
        {
            bool requestedOk = false;
            for (uint32_t i = 0; i < numFormats && requestedOk == false; ++i)
            {
                requestedOk = formats[i].format == p.requestedFormat;
                requestedOk = requestedOk && (formats[i].colorSpace == p.requestedColorSpace);
            }
            if (!requestedOk)
            {
                p.format = formats[0].format;
                p.colorSpace = formats[0].colorSpace;
            }
        }
    }
    { // select present mode
        uint32_t count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vkPhysicalDevice, p.surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vkPhysicalDevice, p.surface, &count, modes.data());

        bool requestedOk = false;
        for (uint32_t i = 0; i < count && requestedOk == false; ++i)
            requestedOk = modes[i] == p.requestedPresentMode;

        const VkPresentModeKHR alwaysAvailable = VK_PRESENT_MODE_FIFO_KHR;
        p.presentMode = requestedOk ? p.presentMode : alwaysAvailable;
    }
    { // create command buffers
        p.commandBuffers.resize(p.imageCount);
        for (uint32_t i = 0; i < p.imageCount; ++i)
        {
            if (CreateCommandBuffer(vkQueueFamilies[uint32_t(ViewerQueueType::Graphics)], p.commandBuffers[i]) == false)
                return false;
        }
    }
    {
        if (CreateImgui(p) == false)
            return false;
    }
    {
        if (ResizeSwapChain() == false)
            return false;
    }
    {
        p.renderSemaphores.resize(p.imageCount);
        p.acquireSemaphores.resize(p.imageCount);
        p.fences.resize(p.imageCount);

        VkSemaphoreCreateInfo semaphoreCreateInfo = {};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreCreateInfo.pNext = nullptr;
        semaphoreCreateInfo.flags = 0;

        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.pNext = nullptr;
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < p.imageCount; i++)
        {
            VK_RESULT_BOOL(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, vkAllocator, &p.renderSemaphores[i]));
            VK_RESULT_BOOL(vkCreateSemaphore(vkDevice, &semaphoreCreateInfo, vkAllocator, &p.acquireSemaphores[i]));
            VK_RESULT_BOOL(vkCreateFence(vkDevice, &fenceCreateInfo, vkAllocator, &p.fences[i]));
        }
    }

    return true;
}

ViewerBufferDesc GetBufferResourceDesc(BvhViewerResources bufferResource, uint32_t numBufferedFrames, VkPhysicalDevice vkPhysicalDevice)
{
    (void)numBufferedFrames;
    const uint32_t c_DefaultCount = 50 * 1000;
    const BufferGroup group = GetBufferGroup(bufferResource);

    ViewerBufferDesc desc = {};
    desc.elementNum = 1;
    desc.memoryTypeId = VK_MAX_MEMORY_TYPES;
    desc.isDynamic = bufferResource >= BvhViewerResources::DynamicBuffersBegin && bufferResource <= BvhViewerResources::DynamicBuffersEnd;
    desc.group = group;
    desc.resourceType = bufferResource;

    switch (bufferResource)
    {
    case BvhViewerResources::ConstantBuffer:
    {
        desc.elementStride = sizeof(ConstantBufferLayout);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    }
    case BvhViewerResources::FeedbackBuffer:
    {
        desc.elementStride = sizeof(FeedbackBufferLayout);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    }
    case BvhViewerResources::ReadbackBuffer:
    {
        desc.elementNum = c_MaxTlasPerFrame;
        desc.elementStride = sizeof(FeedbackBufferLayout);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }
    case BvhViewerResources::InstanceDescInternal:
    {
        desc.elementNum = c_DefaultCount;
        desc.elementStride = sizeof(TlasInstanceDesc);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    }
    case BvhViewerResources::BlasHashes:
    {
        desc.elementNum = c_DefaultCount;
        desc.elementStride = sizeof(uint32_t);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }
    case BvhViewerResources::BlasDescs:
    {
        desc.elementNum = c_DefaultCount;
        desc.elementStride = sizeof(ViewerBlasDescGPU);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }
    case BvhViewerResources::GeometryDescs:
    {
        desc.elementNum = c_DefaultCount;
        desc.elementStride = sizeof(ViewerRtGeometryDescGPU);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    }
    case BvhViewerResources::UploadBuffer:
    {
        desc.elementNum = c_DefaultCount * (sizeof(uint32_t) + sizeof(ViewerBlasDescGPU) + sizeof(ViewerRtGeometryDescGPU));
        desc.elementStride = sizeof(uint8_t);
        desc.memoryTypeId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        desc.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    }
    case BvhViewerResources::OutputTexture:
    case BvhViewerResources::IndirectOpsArgs:
    case BvhViewerResources::IndirectOpsCountInternal:
    case BvhViewerResources::RenderPushConstants:
    case BvhViewerResources::PtlasIndirectPushConstants:
    case BvhViewerResources::Tlas:
    case BvhViewerResources::InstanceDescExternal:
    case BvhViewerResources::IndirectOpsBufferExternal:
    case BvhViewerResources::IndirectOpsCountExternal:
    case BvhViewerResources::IndirectOpsData:
    case BvhViewerResources::GpuVendorReserved:
    case BvhViewerResources::Count:
    default: break;
    }

    desc.totalSize = desc.elementNum * desc.elementStride;

    return desc;
}

bool InternalContext::CreateBuffer(ViewerBufferDesc& buffer)
{
    VkBufferCreateInfo bufferCreateInfo = {};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.pNext = nullptr;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkMemoryRequirements memoryRequirements = {};
    {
        VkBuffer tmpBuffer = VK_NULL_HANDLE;
        bufferCreateInfo.size = buffer.totalSize;
        bufferCreateInfo.usage = buffer.usageFlags;
        VK_RESULT_BOOL(vkCreateBuffer(vkDevice, &bufferCreateInfo, vkAllocator, &tmpBuffer));
        vkGetBufferMemoryRequirements(vkDevice, tmpBuffer, &memoryRequirements);

        buffer.alignment = memoryRequirements.alignment;
        buffer.totalSize = Align(buffer.totalSize, buffer.alignment);
        vkDestroyBuffer(vkDevice, tmpBuffer, vkAllocator);
    }

    bufferCreateInfo.size = buffer.totalSize;
    VK_RESULT_BOOL(vkCreateBuffer(vkDevice, &bufferCreateInfo, vkAllocator, &buffer.buffer));

    if (buffer.isDynamic == false)
        return true; // dynamic buffers own their memory

    { // allocate memory
        VkMemoryAllocateFlagsInfo flags = {};
        flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo memoryAllocateInfo = {};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext = &flags;
        memoryAllocateInfo.memoryTypeIndex = buffer.memoryTypeId;
        memoryAllocateInfo.allocationSize = buffer.totalSize;
        VK_RESULT_BOOL(vkAllocateMemory(vkDevice, &memoryAllocateInfo, vkAllocator, &buffer.memory));
    }

    { // bind buffer
        VK_RESULT_BOOL(vkBindBufferMemory(vkDevice, buffer.buffer, buffer.memory, 0));
    }

    if(m_DescriptorBufferExtOn)
    { // get gpuva in case of descriptor buffer usage
        VkBufferDeviceAddressInfo bufferAddressInfo = {};
        bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bufferAddressInfo.buffer = buffer.buffer;
        buffer.address = vkGetBufferDeviceAddress(vkDevice, &bufferAddressInfo);
    }

    // create view if needed
    if (buffer.typeFormat == VK_FORMAT_UNDEFINED)
        return true; // not required

    { // create view
        VkBufferViewCreateInfo bufferViewCreateInfo = {};
        bufferViewCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        bufferViewCreateInfo.pNext = nullptr;

        VkBufferView& view = buffer.view;
        bufferViewCreateInfo.format = buffer.typeFormat;
        bufferViewCreateInfo.buffer = buffer.buffer;
        bufferViewCreateInfo.offset = 0;
        bufferViewCreateInfo.range = buffer.totalSize;
        VK_RESULT_BOOL(vkCreateBufferView(vkDevice, &bufferViewCreateInfo, vkAllocator, &view));
    }
    return true;
}

bool InternalContext::CreateBufferPool()
{
    for (uint32_t i = 0; i < uint32_t(BvhViewerResources::NumBuffers); ++i)
    { // Buffer Resources
        const BvhViewerResources resource = GetBufferResourceFromLinearId(i);
        ViewerBufferDesc templateDesc = GetBufferResourceDesc(resource, m_NumBufferedFrames, vkPhysicalDevice);
        uint32_t numBuffers = GetBufferResourceCount(resource, m_NumBufferedFrames);

        if (templateDesc.memoryTypeId == VK_MAX_MEMORY_TYPES)
            return false;

        if(m_DescriptorBufferExtOn)
            templateDesc.usageFlags |= VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

        const uint32_t firstBuffer = uint32_t(m_ObjectPool.buffers.size());
        uint64_t totalSize = 0;
        for (uint32_t j = 0; j < numBuffers; ++j)
        {
            ViewerBufferDesc buffer = templateDesc;
            if (CreateBuffer(buffer) == false)
                return false;

            totalSize += buffer.totalSize;
            m_ObjectPool.buffers.push_back(buffer);
        }

        if (templateDesc.isDynamic)
            continue;

        // allocate memory for static buffers here and bind it
        VkDeviceMemory memory = VK_NULL_HANDLE;
        { // allocate memory
            VkMemoryAllocateFlagsInfo flags = {};
            flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

            VkMemoryAllocateInfo memoryAllocateInfo = {};
            memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryAllocateInfo.pNext = &flags;
            memoryAllocateInfo.memoryTypeIndex = templateDesc.memoryTypeId;
            memoryAllocateInfo.allocationSize = totalSize;
            VK_RESULT_BOOL(vkAllocateMemory(vkDevice, &memoryAllocateInfo, vkAllocator, &memory));
        }

        ViewerBufferDesc* buffers = &m_ObjectPool.buffers[firstBuffer];
        { // bind buffer object to memory
            for (uint64_t j = 0; j < numBuffers; ++j)
                VK_RESULT_BOOL(vkBindBufferMemory(vkDevice, buffers[j].buffer, memory, j * buffers[j].totalSize));
        }
        m_ObjectPool.staticMemories.push_back(memory);

        for (uint32_t j = 0; j < numBuffers; ++j)
        {
            buffers[j].memory = memory;
            buffers[j].offsetInMemory = uint64_t(j) * buffers[j].totalSize;

            if(m_DescriptorBufferExtOn)
            { // get gpuva in case of descriptor buffer usage
                VkBufferDeviceAddressInfo bufferAddressInfo = {};
                bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                bufferAddressInfo.buffer = buffers[j].buffer;
                buffers[j].address = vkGetBufferDeviceAddress(vkDevice, &bufferAddressInfo);
            }
        }
    }

    return true;
}

bool InternalContext::CreatePipelines()
{ // create pipelines
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {};
    for (uint32_t pipelineId = 0; pipelineId < uint32_t(Pipelines::Count); ++pipelineId)
    {
        uint32_t tRegShift = 0;
        uint32_t uRegShift = 10;
        uint32_t bRegShift = 20;

        const CommonPipelineDesc& pipeline = g_ViewerPipelineDescs[pipelineId];
        PipelineDescVk& viewerPipelineDesc = m_Pipelines[pipelineId];

        std::vector<VkPushConstantRange> pushConstantRanges = {};
        { // create shader module
            VkShaderModuleCreateInfo instanceCopyShaderModuleCreateInfo = {};
            instanceCopyShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            instanceCopyShaderModuleCreateInfo.pNext = nullptr;
            instanceCopyShaderModuleCreateInfo.flags = 0;

            const uint32_t permutationId = GetPermutationId(Pipelines(pipelineId), m_EnableLss, m_EnablePositionFetch);
            const ShaderDesc& shaderPermutation = pipeline.shaderPermutations[permutationId];
            instanceCopyShaderModuleCreateInfo.pCode = (uint32_t*)shaderPermutation.code;
            instanceCopyShaderModuleCreateInfo.codeSize = shaderPermutation.codeSize;
            VK_RESULT_BOOL(vkCreateShaderModule(vkDevice, &instanceCopyShaderModuleCreateInfo, vkAllocator, &viewerPipelineDesc.shaderModule));
        }
        {
            const uint32_t numBindings = (uint32_t)pipeline.bindings.size();
            std::vector<VkDescriptorSetLayoutBinding> vkBindings = {};

            for (uint32_t bindingId = 0; bindingId < numBindings; ++bindingId)
            {
                const DescriptorBindingDesc& bindingDesc = pipeline.bindings[bindingId];
                const VkDescriptorType vkDescriptorType = GetVkDescriptorType(bindingDesc.type);

                VkDescriptorSetLayoutBinding* vkBinding = nullptr;
                DescriptorRangeDesc* rangeDesc = nullptr;

                const bool isPushConstants = bindingDesc.type == DescriptorTypes::PushConstants;
                if (isPushConstants)
                {
                    VkPushConstantRange pushConstantRange = {};
                    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    pushConstantRange.offset = 0;
                    pushConstantRange.size = static_cast<uint32_t>(bindingDesc.stride);
                    pushConstantRanges.push_back(pushConstantRange);
                    viewerPipelineDesc.hasPushConstants = true;
                }
                else
                {
                    vkBinding = &vkBindings.emplace_back();
                    vkBinding->stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                    vkBinding->pImmutableSamplers = 0;
                    vkBinding->descriptorType = vkDescriptorType;
                    vkBinding->descriptorCount = 1;

                    rangeDesc = &viewerPipelineDesc.descriptorRanges.emplace_back();
                    rangeDesc->bindings.push_back(bindingDesc);
                    rangeDesc->type = vkDescriptorType;
                    rangeDesc->numDescriptors = 1;

                    switch (bindingDesc.type)
                    {
                    case DescriptorTypes::Tlas:
                    case DescriptorTypes::TypedBuffer:
                    case DescriptorTypes::StructuredBuffer:
                    case DescriptorTypes::RawBuffer:
                        vkBinding->binding = tRegShift++;
                        break;

                    case DescriptorTypes::StorageTexture:
                    case DescriptorTypes::StorageTypedBuffer:
                    case DescriptorTypes::StorageStructuredBuffer:
                        vkBinding->binding = uRegShift++;
                        break;

                    case DescriptorTypes::ConstantBuffer:
                        vkBinding->binding = bRegShift++;
                        break;
                    case DescriptorTypes::PushConstants:
                    case DescriptorTypes::Count:
                        break;
                    default:break;
                    }

                    rangeDesc->baseBindingId = vkBinding->binding;

                    bool found = false;
                    for (uint32_t poolSizeId = 0; poolSizeId < (uint32_t)descriptorPoolSizes.size(); ++poolSizeId)
                    {
                        VkDescriptorPoolSize& descriptorPoolSize = descriptorPoolSizes[poolSizeId];
                        if (descriptorPoolSize.type == vkDescriptorType)
                        {
                            ++descriptorPoolSize.descriptorCount;
                            found = true;
                            break;
                        }
                    }

                    if (found == false)
                    {
                        VkDescriptorPoolSize descriptorPoolSize = {};
                        descriptorPoolSize.descriptorCount = 1;
                        descriptorPoolSize.type = vkDescriptorType;
                        descriptorPoolSizes.push_back(descriptorPoolSize);
                    }
                }
            }

            VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
            descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorSetLayoutInfo.pNext = nullptr;
            descriptorSetLayoutInfo.flags = m_DescriptorBufferExtOn ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT : 0;
            descriptorSetLayoutInfo.bindingCount = (uint32_t)vkBindings.size();
            descriptorSetLayoutInfo.pBindings = vkBindings.data();
            VK_RESULT_BOOL(vkCreateDescriptorSetLayout(vkDevice, &descriptorSetLayoutInfo, vkAllocator, &viewerPipelineDesc.descriptorSetLayout));

            const uint32_t numDescriptorRanges = uint32_t(vkBindings.size());
            viewerPipelineDesc.descriptorRanges.resize(numDescriptorRanges);

            const size_t descriptorWriteDataStrides[ ] =
            {
                sizeof(VkDescriptorBufferInfo),
                sizeof(VkDescriptorImageInfo),
                sizeof(VkBufferView),
                sizeof(VkAccelerationStructureKHR),
            };

            size_t maxStride = 0;
            for (size_t i = 0; i < std::size(descriptorWriteDataStrides); ++i)
                maxStride = maxStride < descriptorWriteDataStrides[i] ? descriptorWriteDataStrides[i] : maxStride;

            for (uint32_t rangeId = 0; rangeId < numDescriptorRanges; ++rangeId)
            {
                DescriptorRangeDesc& descriptorRange = viewerPipelineDesc.descriptorRanges[rangeId];
                descriptorRange.data.resize(maxStride * descriptorRange.numDescriptors);
            }
        }
        { // create pipeline layout
            VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
            pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutCreateInfo.pNext = nullptr;
            pipelineLayoutCreateInfo.flags = 0;
            pipelineLayoutCreateInfo.setLayoutCount = 1;
            pipelineLayoutCreateInfo.pSetLayouts = &viewerPipelineDesc.descriptorSetLayout;
            pipelineLayoutCreateInfo.pushConstantRangeCount = uint32_t(pushConstantRanges.size());
            pipelineLayoutCreateInfo.pPushConstantRanges = pushConstantRanges.data();
            VK_RESULT_BOOL(vkCreatePipelineLayout(vkDevice, &pipelineLayoutCreateInfo, vkAllocator, &viewerPipelineDesc.pipelineLayout));
        }
        { // create the pipeline itself
            VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo = {};
            pipelineShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            pipelineShaderStageCreateInfo.pNext = nullptr;
            pipelineShaderStageCreateInfo.flags = 0;
            pipelineShaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            pipelineShaderStageCreateInfo.module = viewerPipelineDesc.shaderModule;
            pipelineShaderStageCreateInfo.pName = "main";
            pipelineShaderStageCreateInfo.pSpecializationInfo = nullptr;

            VkComputePipelineCreateInfo computePipelineCreateInfo = {};
            computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            computePipelineCreateInfo.pNext = nullptr;
            computePipelineCreateInfo.flags = m_DescriptorBufferExtOn ? VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT : 0;
            computePipelineCreateInfo.basePipelineHandle = nullptr;
            computePipelineCreateInfo.basePipelineIndex = -1;
            computePipelineCreateInfo.stage = pipelineShaderStageCreateInfo;
            computePipelineCreateInfo.layout = viewerPipelineDesc.pipelineLayout;

            VkPipelineCreateFlags2CreateInfoKHR pipelineFlagsInfo = {};
            pipelineFlagsInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR;
            pipelineFlagsInfo.pNext = nullptr;
            if (m_EnableLss)
            {
                pipelineFlagsInfo.flags = VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV | computePipelineCreateInfo.flags;
                computePipelineCreateInfo.pNext = &pipelineFlagsInfo;
            }
            VK_RESULT_BOOL(vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, vkAllocator, &viewerPipelineDesc.pipeline));
        }
    }

    if(m_DescriptorBufferExtOn == false)
    { // create descriptor pool
        for(size_t id = 0; id < descriptorPoolSizes.size(); ++id)
            descriptorPoolSizes[id].descriptorCount *= m_NumBufferedFrames * c_MaxTlasPerFrame;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.pNext = nullptr;
        descriptorPoolCreateInfo.flags = 0;
        descriptorPoolCreateInfo.maxSets = m_NumBufferedFrames * c_MaxTlasPerFrame * uint32_t(Pipelines::Count);
        descriptorPoolCreateInfo.poolSizeCount = (uint32_t)descriptorPoolSizes.size();
        descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
        VK_RESULT_BOOL(vkCreateDescriptorPool(vkDevice, &descriptorPoolCreateInfo, vkAllocator, &m_DescriptorPool));
    }
    else
    { // alocate memory for desecriptorBuffers
        VkPhysicalDeviceDescriptorBufferPropertiesEXT descBufferProps{};
        descBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = &descBufferProps;
        vkGetPhysicalDeviceProperties2(vkPhysicalDevice, &deviceProperties);

        uint64_t totalSize = 0;
        for(uint32_t pipelineId = 0; pipelineId < uint32_t(Pipelines::Count); ++pipelineId)
        {
            PipelineDescVk& pipeline = m_Pipelines[pipelineId];
            VkDeviceSize layoutSize = 0;
            vkGetDescriptorSetLayoutSizeEXT(vkDevice, pipeline.descriptorSetLayout, &layoutSize);
            layoutSize = Align(layoutSize, descBufferProps.descriptorBufferOffsetAlignment);
            totalSize += layoutSize;
        }
        totalSize *= m_NumBufferedFrames * c_MaxTlasPerFrame;

        VkMemoryAllocateFlagsInfo flags = {};
        flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo memoryAllocateInfo = {};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext = &flags;
        memoryAllocateInfo.memoryTypeIndex = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        memoryAllocateInfo.allocationSize = totalSize;
        VK_RESULT_BOOL(vkAllocateMemory(vkDevice, &memoryAllocateInfo, vkAllocator, &m_DescriptorBufferMemory));
    }

    return true;
}

bool InternalContext::CreateBackend()
{
    { // query Queue Family
        uint32_t queuePropsNum = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queuePropsNum, nullptr);
        std::vector<VkQueueFamilyProperties> queueProps(queuePropsNum);
        vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queuePropsNum, queueProps.data());

        VkQueueFlagBits queueTypes[ ] = {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT};
        uint32_t queueTypeExclude[ ] = {0, VK_QUEUE_GRAPHICS_BIT};

        for (uint32_t typeId = 0; typeId < uint32_t(ViewerQueueType::Count); ++typeId)
        {
            vkQueueFamilies[typeId] = uint32_t(~0);
            for (uint32_t i = 0; i < queuePropsNum; ++i)
            {
                uint32_t queueFlags = queueProps[i].queueFlags;
                bool accept = (queueFlags & queueTypes[typeId]) != 0;
                accept = accept && ((queueFlags & queueTypeExclude[typeId]) == 0);
                if (accept)
                {
                    vkQueueFamilies[typeId] = i;
                    break;
                }
            }
        }

        if (vkQueueFamilies[uint32_t(ViewerQueueType::Graphics)] == uint32_t(~0))
        {
            LogMessage(NvBvhViewerCommon::MessageType::Error, "NvBvhViewerVK::Impl::CreateBackend: device has no Graphics Queue (?)");
            return false;
        }
    }

    if (CreatePipelines() == false)
        return false;

    const uint32_t tlasCount = m_NumBufferedFrames * c_MaxTlasPerFrame;
    { // prepare object pool
        m_ObjectPool.commandBuffers[uint32_t(ViewerQueueType::Graphics)].reserve(tlasCount);
        if (m_HasComputeQueue)
            m_ObjectPool.commandBuffers[uint32_t(ViewerQueueType::Compute)].reserve(tlasCount);

        m_ObjectPool.images.reserve(tlasCount);
        m_ObjectPool.fences.resize(tlasCount, VK_NULL_HANDLE);

        uint32_t numBuffersTotal = 0;
        for (uint32_t i = 0; i < uint32_t(BvhViewerResources::NumBuffers); ++i)
        { // Buffer Resources
            uint32_t numBuffers = 0;
            {
                const BvhViewerResources resource = GetBufferResourceFromLinearId(i);
                numBuffers = GetBufferResourceCount(resource, m_NumBufferedFrames);
            }
            m_ObjectPool.perBufferTypeOffsets[i] = numBuffersTotal;
            numBuffersTotal += numBuffers;
        }
        m_ObjectPool.buffers.reserve(numBuffersTotal);

        m_ObjectPool.images.resize(tlasCount, ViewerImageDesc()); // image creation will be handled in-place
    }

    if (CreateBufferPool() == false)
        return false;

    for (uint32_t i = 0; i < uint32_t(std::size(m_ObjectPool.fences)); ++i)
    { // fences
        VkFenceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_RESULT_BOOL(vkCreateFence(vkDevice, &createInfo, vkAllocator, &m_ObjectPool.fences[i]));
    }

    { // command Buffers
        uint32_t queueNum = m_HasComputeQueue ? 2 : 1;
        for (uint32_t queueId = 0; queueId < queueNum; ++queueId)
        {
            for (uint32_t bufferId = 0; bufferId < m_NumBufferedFrames * c_MaxTlasPerFrame; ++bufferId)
            {
                CommandBuffer buffer = {};
                if (CreateCommandBuffer(vkQueueFamilies[queueId], buffer) == false)
                    return false;
                m_ObjectPool.commandBuffers[queueId].push_back(buffer);
            }
        }
    }

    { // init render camera
        float3 pos = float3(0.0f, 0.0f, 0.0f);
        float3 lookAt = float3(0.0f, 1.0f, 0.0f);
        m_Camera.Initialize(pos, lookAt, false);
        CameraDesc cameraDesc = {};
        cameraDesc.aspectRatio = float(*m_OutputWidth) / float(*m_OutputHeight);
        m_Camera.Update(cameraDesc);
    }

    return true;
}

inline VkImageMemoryBarrier FillImageBarrier(VkImage image, VkAccessFlagBits accessBefore, VkImageLayout layoutBefore, VkAccessFlagBits accessAfter, VkImageLayout layoutAfter)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = accessBefore;
    barrier.dstAccessMask = accessAfter;
    barrier.oldLayout = layoutBefore;
    barrier.newLayout = layoutAfter;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    return barrier;
}

inline VkBufferMemoryBarrier FillBufferBarrier(const ViewerBufferDesc& buffer, VkAccessFlagBits accessBefore, VkAccessFlagBits accessAfter)
{
    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = accessBefore;
    barrier.dstAccessMask = accessAfter;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.buffer;
    barrier.offset = 0;
    barrier.size = buffer.totalSize;
    return barrier;
}

void InternalContext::UpdateObjectPool(bool force)
{
    const uint32_t frameId = GetFrameId();
    for (size_t i = m_ObjectPool.staleBuffers.size(); i > 0;)
    {
        ViewerBufferDesc& buffer = m_ObjectPool.staleBuffers[--i];
        bool ready = (frameId - buffer.staleFrameId) > m_NumBufferedFrames * 2;
        if (ready || force)
        {
            DestroyViewerBuffer(vkDevice, buffer, vkAllocator);
            m_ObjectPool.staleBuffers.erase(m_ObjectPool.staleBuffers.begin() + i);
        }

    }
    for (size_t i = m_ObjectPool.staleImages.size(); i > 0;)
    {
        ViewerImageDesc& image = m_ObjectPool.staleImages[--i];
        bool ready = (frameId - image.staleFrameId) > m_NumBufferedFrames * 2;
        if (ready || force)
        {
            DestroyViewerImage(vkDevice, image, vkAllocator);
            m_ObjectPool.staleImages.erase(m_ObjectPool.staleImages.begin() + i);
        }
    }

}

void InternalContext::GrowDynamicBuffer(ViewerBufferDesc& buffer, uint64_t newSize)
{
    if (buffer.memoryTypeId == VK_MAX_MEMORY_TYPES || buffer.isDynamic == false)
        return;

    if (buffer.totalSize >= newSize)
        return;

    if(buffer.totalSize)
    {
        buffer.staleFrameId = GetFrameId();
        m_ObjectPool.staleBuffers.push_back(buffer);
    }

    ViewerBufferDesc settingsCopy = buffer;
    buffer = ViewerBufferDesc();
    buffer.elementStride = settingsCopy.elementStride;
    buffer.memoryTypeId = settingsCopy.memoryTypeId;
    buffer.typeFormat = settingsCopy.typeFormat;
    buffer.usageFlags = settingsCopy.usageFlags;
    buffer.group = settingsCopy.group;
    buffer.resourceType = settingsCopy.resourceType;
    buffer.isDynamic = true;
    buffer.totalSize = newSize + (newSize >> 2); // request more
    CreateBuffer(buffer);
}

bool InternalContext::ResizeSwapChain()
{
    VkPresenter& p = m_Presenter;
    VkSurfaceCapabilitiesKHR surfaceCapabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkPhysicalDevice, p.surface, &surfaceCapabilities);

    if (surfaceCapabilities.currentExtent.width == 0 || surfaceCapabilities.currentExtent.height == 0)
        return false; // can't create

    if (p.swapChain)
    {
        vkQueueWaitIdle(vkGraphicsPresentQueue);
        DestroySwapChain();
        p.framebuffers.resize(0); p.framebuffers.shrink_to_fit();
        p.swapChainImageViews.resize(0); p.swapChainImageViews.shrink_to_fit();
        p.swapChainImages.resize(0); p.swapChainImages.shrink_to_fit();
    }
    {
        VkSwapchainCreateInfoKHR swapChainCreateInfo = {};
        swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainCreateInfo.surface = p.surface;
        swapChainCreateInfo.minImageCount = VkPresenter::c_NumFrames;
        swapChainCreateInfo.imageFormat = p.format;
        swapChainCreateInfo.imageColorSpace = p.colorSpace;
        swapChainCreateInfo.imageExtent = {surfaceCapabilities.currentExtent.width, surfaceCapabilities.currentExtent.height};
        swapChainCreateInfo.imageArrayLayers = 1;
        swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainCreateInfo.presentMode = p.presentMode;
        swapChainCreateInfo.clipped = VK_TRUE;
        swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
        VkResult result = vkCreateSwapchainKHR(vkDevice, &swapChainCreateInfo, vkAllocator, &p.swapChain);
        if (result != VK_SUCCESS)
            return false;

        p.swapChainWidth = surfaceCapabilities.currentExtent.width;
        p.swapChainHeight = surfaceCapabilities.currentExtent.height;
    }
    { // get swap chain images
        vkGetSwapchainImagesKHR(vkDevice, p.swapChain, &p.imageCount, nullptr);
        p.swapChainImages.resize(p.imageCount);
        VK_RESULT_BOOL(vkGetSwapchainImagesKHR(vkDevice, p.swapChain, &p.imageCount, p.swapChainImages.data()));
    }
    { // create image views and submit barriers
        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = p.format;
        viewCreateInfo.components = {VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
        VkImageSubresourceRange imageRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        viewCreateInfo.subresourceRange = imageRange;

        CommandBuffer tmpCommandBuffer = {};
        CreateAndBeginCommandBuffer(vkDevice, vkQueueFamilies[uint32_t(ViewerQueueType::Graphics)], tmpCommandBuffer);

        p.swapChainImageViews.resize(p.imageCount);
        std::vector<VkImageMemoryBarrier> barriers(p.imageCount);
        for (uint32_t i = 0; i < p.imageCount; i++)
        {
            viewCreateInfo.image = p.swapChainImages[i];
            VK_RESULT_BOOL(vkCreateImageView(vkDevice, &viewCreateInfo, vkAllocator, &p.swapChainImageViews[i]));
            barriers[i] = FillImageBarrier(p.swapChainImages[i], VK_ACCESS_NONE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        }
        vkCmdPipelineBarrier(tmpCommandBuffer.buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, p.imageCount, barriers.data());

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &tmpCommandBuffer.buffer;
        vkEndCommandBuffer(tmpCommandBuffer.buffer);
        vkQueueSubmit(vkGraphicsPresentQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkDeviceWaitIdle(vkDevice);

        vkFreeCommandBuffers(vkDevice, tmpCommandBuffer.pool, 1, &tmpCommandBuffer.buffer);
        vkDestroyCommandPool(vkDevice, tmpCommandBuffer.pool, vkAllocator);
    }
    { // create frame buffers
        p.framebuffers.resize(p.imageCount);

        VkImageView attachment[1];
        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.renderPass = p.renderPass;
        frameBufferCreateInfo.attachmentCount = 1;
        frameBufferCreateInfo.pAttachments = attachment;
        frameBufferCreateInfo.width = p.swapChainWidth;
        frameBufferCreateInfo.height = p.swapChainHeight;
        frameBufferCreateInfo.layers = 1;
        for (uint32_t i = 0; i < p.imageCount; i++)
        {
            attachment[0] = p.swapChainImageViews[i];
            VK_RESULT_BOOL(vkCreateFramebuffer(vkDevice, &frameBufferCreateInfo, vkAllocator, &p.framebuffers[i]));
        }
    }

    return true;
}

bool InternalContext::ResizeRenderImage(ViewerImageDesc& image, VkCommandBuffer commandBuffer)
{ // lock and wait for idle before this func
    if (image.width == *m_OutputWidth && image.height == *m_OutputHeight)
        return true;

    if(image.image)
    {
        image.staleFrameId = GetFrameId();
        m_ObjectPool.staleImages.push_back(image);
    }

    uint32_t deviceMemoryId = GetMemoryType(vkPhysicalDevice, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (deviceMemoryId == VK_MAX_MEMORY_TYPES)
    {
        assert(deviceMemoryId == VK_MAX_MEMORY_TYPES);
        return false;
    }
    { // create render image
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext = nullptr;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageCreateInfo.extent = {*m_OutputWidth, *m_OutputHeight, 1};
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_RESULT_BOOL(vkCreateImage(vkDevice, &imageCreateInfo, vkAllocator, &image.image));

        VkMemoryRequirements memoryRequirements = {};
        vkGetImageMemoryRequirements(vkDevice, image.image, &memoryRequirements);

        image.alignment = memoryRequirements.alignment;
        image.size = Align(memoryRequirements.size, memoryRequirements.alignment);
        image.width = imageCreateInfo.extent.width;
        image.height = imageCreateInfo.extent.height;
    }
    {
        VkMemoryAllocateInfo memoryAllocateInfo = {};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext = nullptr;
        memoryAllocateInfo.memoryTypeIndex = deviceMemoryId;
        memoryAllocateInfo.allocationSize = image.size;
        VK_RESULT_BOOL(vkAllocateMemory(vkDevice, &memoryAllocateInfo, vkAllocator, &image.memory));
        VK_RESULT_BOOL(vkBindImageMemory(vkDevice, image.image, image.memory, 0));
    }
    {
        VkImageViewCreateInfo imageViewCreateInfo;
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.pNext = nullptr;
        imageViewCreateInfo.flags = 0;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageViewCreateInfo.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.image = image.image;
        VK_RESULT_BOOL(vkCreateImageView(vkDevice, &imageViewCreateInfo, vkAllocator, &image.view));
    }
    { // transfer images after init
        VkImageMemoryBarrier barrier = FillImageBarrier(image.image,
            VK_ACCESS_NONE, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    return true;
}

inline void BeforeTlasCopyImageBarriers(VkCommandBuffer commandBuffer, VkImage tlasRenderImage, VkImage swapChainImage)
{
    VkImageMemoryBarrier tlasImageBarrier = FillImageBarrier(tlasRenderImage,
        VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageMemoryBarrier swapChainImageBarrier = FillImageBarrier(swapChainImage,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageMemoryBarrier imageBarriers[ ] =
    {
        tlasImageBarrier,
        swapChainImageBarrier
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, nullptr, 0, nullptr, (uint32_t)std::size(imageBarriers), imageBarriers);
}

inline void AfterTlasCopyImageBarriers(VkCommandBuffer commandBuffer, VkImage tlasRenderImage, VkImage swapChainImage)
{
    VkImageMemoryBarrier tlasImageBarrier = FillImageBarrier(tlasRenderImage,
        VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

    VkImageMemoryBarrier swapChainImageBarrier = FillImageBarrier(swapChainImage,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkImageMemoryBarrier imageBarriers[ ] =
    {
        tlasImageBarrier,
        swapChainImageBarrier
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
        0, nullptr, 0, nullptr, (uint32_t)std::size(imageBarriers), imageBarriers);
}

bool InternalContext::PreparePipelineViaDescriptorPool(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId)
{
    PipelineDescVk& vkPipelineDesc = m_Pipelines[pipelineId];
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];
    VkDescriptorSet& descriptorSet = tlas.descriptorSets[pipelineId];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineDesc.pipeline);

    if (descriptorSet == nullptr)
    {
        VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
        descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAllocateInfo.pNext = nullptr;
        descriptorSetAllocateInfo.descriptorPool = m_DescriptorPool;
        descriptorSetAllocateInfo.descriptorSetCount = 1;
        descriptorSetAllocateInfo.pSetLayouts = &vkPipelineDesc.descriptorSetLayout;
        VK_RESULT_BOOL(vkAllocateDescriptorSets(vkDevice, &descriptorSetAllocateInfo, &descriptorSet));
    }

    const uint32_t numDescriptorRanges = uint32_t(vkPipelineDesc.descriptorRanges.size());
    std::vector<VkWriteDescriptorSet> descriptorSetWrites(numDescriptorRanges);

    for (uint32_t rangeId = 0; rangeId < numDescriptorRanges; ++rangeId)
    { // pre allocate data for descriptor writes
        DescriptorRangeDesc& range = vkPipelineDesc.descriptorRanges[rangeId];
        VkWriteDescriptorSet& write = descriptorSetWrites[rangeId];
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.pNext = nullptr;
        write.dstSet = descriptorSet;
        write.dstArrayElement = 0;
        write.descriptorCount = range.numDescriptors;
        write.descriptorType = range.type;
        write.dstBinding = range.baseBindingId;

        for (uint32_t bindingId = 0; bindingId < (uint32_t)range.bindings.size(); ++bindingId)
        {
            DescriptorBindingDesc& bindingDesc = range.bindings[bindingId];
            switch (bindingDesc.type)
            {
            case DescriptorTypes::Tlas:
            { // only 1 tlas is supported
                VkWriteDescriptorSetAccelerationStructureKHR& rtasWrite = range.rtasDescriptorWrites;
                rtasWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                rtasWrite.pNext = nullptr;
                rtasWrite.accelerationStructureCount = range.numDescriptors;

                VkAccelerationStructureKHR* pTlases = (VkAccelerationStructureKHR*)range.data.data();
                rtasWrite.pAccelerationStructures = pTlases;
                pTlases[bindingId] = tlas.accelerationStructure;
                write.pNext = &rtasWrite;
                break;
            }

            case DescriptorTypes::StorageTypedBuffer:
            {
                VkBufferView* ptr = (VkBufferView*)range.data.data();
                ptr[bindingId] = GetBuffer(bindingDesc.resource, bufferedFrameId, tlasId).view;
                write.pTexelBufferView = ptr;
                break;
            }
            case DescriptorTypes::TypedBuffer:
            case DescriptorTypes::RawBuffer:
            case DescriptorTypes::ConstantBuffer:
            case DescriptorTypes::StructuredBuffer:
            case DescriptorTypes::StorageStructuredBuffer:
            {
                VkDescriptorBufferInfo* ptr = (VkDescriptorBufferInfo*)range.data.data();
                write.pBufferInfo = ptr;
                ptr[bindingId] = GetDescriptorBufferInfo(GetBuffer(bindingDesc.resource, bufferedFrameId, tlasId));
                break;
            }
            case DescriptorTypes::StorageTexture:
            {
                VkDescriptorImageInfo* ptr = (VkDescriptorImageInfo*)range.data.data();
                write.pImageInfo = ptr;
                VkDescriptorImageInfo info = {};
                info.sampler = VK_NULL_HANDLE;
                uint32_t linearId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
                info.imageView = m_ObjectPool.images[linearId].view;
                info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                ptr[bindingId] = info;
                break;
            }
            case DescriptorTypes::PushConstants:
            case DescriptorTypes::Count:
                break;
            default: break;
            }
        }
    }
    vkUpdateDescriptorSets(vkDevice, uint32_t(descriptorSetWrites.size()), descriptorSetWrites.data(), 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineDesc.pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    return true;
}

bool InternalContext::PreparePipelineViaDescriptorBuffer(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId)
{
    PipelineDescVk& vkPipelineDesc = m_Pipelines[pipelineId];
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];
    VkBuffer& descriptorBuffer = tlas.descriptorBuffers[pipelineId];

    VkPhysicalDeviceDescriptorBufferPropertiesEXT descBufferProps{};
    descBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

    VkPhysicalDeviceProperties2 deviceProperties{};
    deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties.pNext = &descBufferProps;
    vkGetPhysicalDeviceProperties2(vkPhysicalDevice, &deviceProperties);

    if(descriptorBuffer == nullptr)
    { // create descriptor buffer and bind it to the memory
        VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProps{};
        descriptorBufferProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &descriptorBufferProps;
        vkGetPhysicalDeviceProperties2(vkPhysicalDevice, &props2);

        VkDeviceSize descriptorBufferSize = 0;
        vkGetDescriptorSetLayoutSizeEXT(vkDevice, vkPipelineDesc.descriptorSetLayout, &descriptorBufferSize);
        descriptorBufferSize = Align(descriptorBufferSize, descriptorBufferProps.descriptorBufferOffsetAlignment);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = descriptorBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VK_RESULT_BOOL(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &descriptorBuffer));

        uint64_t perPipelineOffsets[uint32_t(Pipelines::Count)];
        uint64_t perTlasStride = 0;
        uint64_t perFrameStride = 0;
        for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
        {
            PipelineDescVk& pipeline = m_Pipelines[i];
            perPipelineOffsets[i] = perTlasStride;
            vkGetDescriptorSetLayoutSizeEXT(vkDevice, pipeline.descriptorSetLayout, &tlas.descriptorBufferSizes[i]);
            tlas.descriptorBufferSizes[i] = Align(tlas.descriptorBufferSizes[i], descBufferProps.descriptorBufferOffsetAlignment);
            perTlasStride += tlas.descriptorBufferSizes[i];
        }
        perFrameStride = perTlasStride * c_MaxTlasPerFrame;
        tlas.descriptorBufferMemoryOffsets[pipelineId] = perFrameStride * uint64_t(bufferedFrameId) + perTlasStride * uint64_t(tlasId) + perPipelineOffsets[pipelineId];
        VK_RESULT_BOOL(vkBindBufferMemory(vkDevice, descriptorBuffer, m_DescriptorBufferMemory, tlas.descriptorBufferMemoryOffsets[pipelineId]));
    }

    void* mappedData = nullptr;
    VK_RESULT_BOOL(vkMapMemory(vkDevice, m_DescriptorBufferMemory, tlas.descriptorBufferMemoryOffsets[pipelineId], tlas.descriptorBufferSizes[pipelineId], 0, &mappedData));
    for (uint32_t rangeId = 0; rangeId < uint32_t(vkPipelineDesc.descriptorRanges.size()); ++rangeId)
    { // fill up the descriptor buffer
        DescriptorRangeDesc& range = vkPipelineDesc.descriptorRanges[rangeId];
        for (uint32_t bindingId = 0; bindingId < (uint32_t)range.bindings.size(); ++bindingId)
        {
            DescriptorBindingDesc& bindingDesc = range.bindings[bindingId];
            uint64_t descriptorSize = 0;
            VkDescriptorGetInfoEXT getInfo = {};
            getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
            getInfo.type = GetVkDescriptorType(bindingDesc.type);

            VkDeviceSize bindingOffset = 0;
            vkGetDescriptorSetLayoutBindingOffsetEXT(vkDevice, vkPipelineDesc.descriptorSetLayout, range.baseBindingId + bindingId, &bindingOffset);

            bool isBuffer = true;

            switch (bindingDesc.type)
            {
            case DescriptorTypes::Tlas:
            {
                if(tlas.accelerationStructure)
                {
                    VkAccelerationStructureDeviceAddressInfoKHR info = {};
                    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
                    info.accelerationStructure = tlas.accelerationStructure;
                    getInfo.data.accelerationStructure = vkGetAccelerationStructureDeviceAddressKHR(vkDevice, &info);

                }
                else if(tlas.ptlasAddr)
                    getInfo.data.accelerationStructure = tlas.ptlasAddr;

                vkGetDescriptorEXT(vkDevice, &getInfo, descBufferProps.accelerationStructureDescriptorSize, (uint8_t*)mappedData + bindingOffset);
                isBuffer = false;
                break;
            }
            case DescriptorTypes::StorageTexture:
            {
                uint32_t linearId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
                VkImageView view = m_ObjectPool.images[linearId].view;

                VkDescriptorImageInfo imageInfo = {};
                imageInfo.sampler = VK_NULL_HANDLE; // Can be VK_NULL_HANDLE if just a sampled/storage image
                imageInfo.imageView = view;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Must match actual 

                getInfo.data.pStorageImage = &imageInfo;
                vkGetDescriptorEXT(vkDevice, &getInfo, descBufferProps.storageImageDescriptorSize, (uint8_t*)mappedData + bindingOffset);
                isBuffer = false;
                break;
            }
            case DescriptorTypes::StorageTypedBuffer:
            {
                descriptorSize = descBufferProps.storageTexelBufferDescriptorSize;
                break;
            }
            case DescriptorTypes::ConstantBuffer:
            {
                descriptorSize = descBufferProps.uniformBufferDescriptorSize;
                break;
            }
            case DescriptorTypes::TypedBuffer:
            case DescriptorTypes::RawBuffer:
            case DescriptorTypes::StructuredBuffer:
            case DescriptorTypes::StorageStructuredBuffer:
            {
                descriptorSize = descBufferProps.storageBufferDescriptorSize;
                break;
            }
            case DescriptorTypes::PushConstants:
            case DescriptorTypes::Count:
                isBuffer = false;
                break;
            default: break;
            }

            if(isBuffer == false)
                continue;

            const ViewerBufferDesc& bufferDesc = GetBuffer(bindingDesc.resource, bufferedFrameId, tlasId);

            VkDescriptorAddressInfoEXT addressInfo = {};
            addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addressInfo.address = bufferDesc.address;
            addressInfo.range = bufferDesc.totalSize;
            addressInfo.format = bufferDesc.typeFormat;

            if(bindingDesc.type == DescriptorTypes::ConstantBuffer)
                getInfo.data.pUniformBuffer = &addressInfo;
            else
                getInfo.data.pStorageBuffer = &addressInfo;

            vkGetDescriptorEXT(vkDevice, &getInfo, descriptorSize, (uint8_t*)mappedData + bindingOffset);
        }
    }
    vkUnmapMemory(vkDevice, m_DescriptorBufferMemory);

    { // set the descriptor buffer to the command buffer
        VkBufferDeviceAddressInfo bufferAddressInfo = {};
        bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        bufferAddressInfo.buffer = descriptorBuffer;
        VkDeviceAddress descriptorBufferAddress = vkGetBufferDeviceAddress(vkDevice, &bufferAddressInfo);

        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindingInfo.address = descriptorBufferAddress;
        bindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
        vkCmdBindDescriptorBuffersEXT(commandBuffer, 1, &bindingInfo);

        uint32_t bufferIndex = 0;
        VkDeviceSize bufferOffset = 0;
        vkCmdSetDescriptorBufferOffsetsEXT(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineDesc.pipelineLayout, 0, 1, &bufferIndex, &bufferOffset);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineDesc.pipeline);
    return true;
}

bool InternalContext::PreparePipeline(VkCommandBuffer commandBuffer, uint32_t pipelineId, uint32_t bufferedFrameId, uint32_t tlasId)
{
    bool pipelineSet = false;
    if (m_DescriptorBufferExtOn)
        pipelineSet = PreparePipelineViaDescriptorBuffer(commandBuffer, pipelineId, bufferedFrameId, tlasId);
    else
        pipelineSet = PreparePipelineViaDescriptorPool(commandBuffer, pipelineId, bufferedFrameId, tlasId);

    if (!m_Pipelines[pipelineId].hasPushConstants || pipelineSet == false)
        return pipelineSet;

    PipelineDescVk& vkPipelineDesc = m_Pipelines[pipelineId];
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];
    PushConstantsLayout pushConstants = {};
    pushConstants.instanceDescBufferAddress = uint64_t(tlas.instanceDescsBufferAddress);
    pushConstants.numInstances = tlas.numInstances;
    pushConstants.indirectOpsBufferAddress = tlas.indirectOpsAddr;
    pushConstants.indirectOpsCountAddress = tlas.indirectOpsCountAddr;
    vkCmdPushConstants(commandBuffer, vkPipelineDesc.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsLayout), &pushConstants);
    return true;
}

void InternalContext::PrepareDispatchTable()
{
    if (m_DispatchTableInited == true)
        return;

    VkDispatchTableInitDesc desc = {};
    desc.device = vkDevice;
    desc.instance = vkInstance;
    desc.getDeviceProcAddrFuncPtr = vkGetDeviceProcAddrFuncPtr;
    desc.getInstanceProcAddrFuncPtr = vkGetInstanceProcAddrFuncPtr;
    desc.setDeviceLoaderData = vkSetDeviceLoaderDataPtr;
    desc.enableDescriptorBufferExt = m_DescriptorBufferExtOn;
    FillVkDispatchTable(desc);
    m_DispatchTableInited = true;
}

#pragma endregion

void InternalContext::RenderPresenter(void* window, std::atomic_flag& lock)
{
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    if (glfwGetWindowAttrib(glfwWindow, GLFW_ICONIFIED) == GLFW_TRUE)
        return;

    VkPresenter& p = m_Presenter;
    static uint32_t presentFrameId = 0;
    static uint32_t lastAppFrameId = 0;

    {
        BvhViewerScopedLock scopedLock(lock);

        const uint32_t appFrameId = m_FrameId.load() - 1; // use prev frame id. current frame may not be ready yet
        const uint32_t bufferedFrameId = appFrameId % m_NumBufferedFrames;
        const bool isNewAppFrame = lastAppFrameId != appFrameId;
        lastAppFrameId = appFrameId;

        FrameDesc& frameDesc = m_Frames[bufferedFrameId];
        uint32_t tlasId = 0;
        if (frameDesc.tlasOffset)
        {
            const uint32_t requestedTlasId = static_cast<uint32_t>(std::max(m_Settings.tlasId, 0));
            uint32_t selectedId = requestedTlasId >= frameDesc.tlasOffset ? frameDesc.tlasOffset - 1 : requestedTlasId;
            tlasId = selectedId;
            if (m_Settings.tlasSortMode == int(TlasSortModes::InstanceNum))
            {
                struct TlasShortDesc { uint32_t nativeId; TlasDesc* ptr; };
                std::vector< TlasShortDesc > sortVector; sortVector.reserve(c_MaxTlasPerFrame);
                for (uint32_t i = 0; i < frameDesc.tlasOffset && i < c_MaxTlasPerFrame; ++i)
                    sortVector.push_back({i, &frameDesc.tlases[i]});

                std::sort(sortVector.begin(), sortVector.end(), [](TlasShortDesc& a, TlasShortDesc& b) { return a.ptr->numInstances > b.ptr->numInstances; });
                tlasId = sortVector[selectedId].nativeId;
            }
        }

        TlasDesc& tlasDesc = frameDesc.tlases[tlasId];
        const ViewerBufferDesc& readbackBuffer = GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
        if (isNewAppFrame)
        { // parse instance data
            uint32_t totalInstanceNum = 0;
            for (uint32_t i = 0; i < uint32_t(std::size(frameDesc.tlases)); ++i)
                totalInstanceNum += frameDesc.tlases[i].numInstances;

            void* mappedData = nullptr;
            const uint64_t offset = readbackBuffer.offsetInMemory + sizeof(FeedbackBufferLayout) * uint64_t(c_MaxTlasPerFrame);
            const uint64_t size = uint64_t(totalInstanceNum) * sizeof(TlasInstanceDesc);

            if (offset + size <= readbackBuffer.totalSize && size)
            {
                vkMapMemory(vkDevice, readbackBuffer.memory, offset, size, 0, &mappedData);
                m_BlasTracker.ParseInstanceDescs(mappedData, totalInstanceNum, static_cast<uint32_t>(readbackBuffer.totalSize / sizeof(TlasInstanceDesc)), &m_Camera.state.globalPosition);
                vkUnmapMemory(vkDevice, readbackBuffer.memory);
            }
            UpdateObjectPool(false);
        }

        { // prepare new frame
            void* readbackPtr = nullptr;
            uint64_t offsetInMemory = readbackBuffer.offsetInMemory + uint64_t(sizeof(FeedbackBufferLayout) * tlasId);
            vkMapMemory(vkDevice, readbackBuffer.memory, offsetInMemory, sizeof(FeedbackBufferLayout), 0, &readbackPtr);

            PresenterNewFrameDesc newFrameDesc = {};
            newFrameDesc.constantBufferHelper = &m_ConstantBufferHelper;
            newFrameDesc.cbUpdateDesc.camera = &m_Camera;
            newFrameDesc.cbUpdateDesc.settings = &m_Settings;
            newFrameDesc.cbUpdateDesc.renderWidth = *m_OutputWidth;
            newFrameDesc.cbUpdateDesc.renderHeight = *m_OutputHeight;
            newFrameDesc.cbUpdateDesc.frameId = appFrameId + 1;
            newFrameDesc.cbUpdateDesc.numBufferedFrames = m_NumBufferedFrames;
            newFrameDesc.cbUpdateDesc.numBlasDescs = m_BlasTracker.GetBlasNumber();

            newFrameDesc.blasTracker = &m_BlasTracker;
            newFrameDesc.feedbackData = readbackPtr;
            newFrameDesc.window = window;
            newFrameDesc.tlasId = tlasId;
            newFrameDesc.tlasFlags = tlasDesc.flags;
            newFrameDesc.numTlasesInFrame = frameDesc.tlasOffset;
            newFrameDesc.numInstancesInTlas = tlasDesc.numInstances;
            newFrameDesc.enablePositionFetch = m_EnablePositionFetch;

            PresenterNewFrame(newFrameDesc);
            vkUnmapMemory(vkDevice, readbackBuffer.memory);
        }

        const uint32_t syncId = presentFrameId % p.imageCount;
        uint32_t backBufferId = 0;

        { // acquire back buffer image to render to
            static std::vector<VkFence> renderFences(c_MaxTlasPerFrame + 1, VK_NULL_HANDLE);
            renderFences.clear();

            renderFences.push_back(p.fences[syncId]);
            for (uint32_t i = 0; i < frameDesc.commandBufferOffset; ++i)
                renderFences.push_back(frameDesc.renderFences[i]);

            vkWaitForFences(vkDevice, uint32_t(renderFences.size()), renderFences.data(), VK_TRUE, UINT64_MAX);
            VkResult acquireResult = vkAcquireNextImageKHR(vkDevice, p.swapChain, 1000 * 1000 * 1000, p.acquireSemaphores[syncId], VK_NULL_HANDLE, &backBufferId);
            ++presentFrameId;

            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
            {
                ResizeSwapChain();
                return;
            }
            vkResetFences(vkDevice, 1, renderFences.data()); // reset only the present fence
            frameDesc.commandBufferOffset = 0; // mark as consumed
        }

        VkCommandBuffer commandBuffer = p.commandBuffers[syncId].buffer;
        VkCommandPool commandPool = p.commandBuffers[syncId].pool;
        vkResetCommandPool(vkDevice, commandPool, 0);

        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

        VkFence presentFence = p.fences[syncId];
        if (m_Settings.allowLastKnownBvhRender && !isNewAppFrame)
        {
            { // update constant buffer
                const ViewerBufferDesc& buffer = GetBuffer(BvhViewerResources::ConstantBuffer, bufferedFrameId, tlasId);
                void* mappedMemory = nullptr;
                vkMapMemory(vkDevice, buffer.memory, buffer.offsetInMemory, buffer.totalSize, 0, &mappedMemory);
                const ConstantBufferLayout& constants = m_ConstantBufferHelper.GetConstantBuffer();
                memcpy(mappedMemory, &constants, sizeof(ConstantBufferLayout));
                vkUnmapMemory(vkDevice, buffer.memory);
            }
            {
                const uint32_t linearTlasId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
                ViewerImageDesc& image = m_ObjectPool.images[linearTlasId];
                if (image.width != *m_OutputWidth || image.height != *m_OutputHeight)
                    ResizeRenderImage(image, commandBuffer);

                CopyFeedbackBuffer(commandBuffer, bufferedFrameId, tlasId);

                if(PreparePipeline(commandBuffer, uint32_t(Pipelines::BvhVisualization), bufferedFrameId, tlasId))
                { // tlas rendering
                    uint32_t x = DivideRoundUp(*m_OutputWidth, BVH_VIZ_SHDR_X);
                    uint32_t y = DivideRoundUp(*m_OutputHeight, BVH_VIZ_SHDR_Y);
                    vkCmdDispatch(commandBuffer, x, y, 1);
                }
            }
        }

        frameDesc.presentFence = presentFence;
        VkImage backBufferImage = p.swapChainImages[backBufferId];
        { // copy tlas render texture to backbuffer
            const uint32_t renderTlasLinearId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
            ViewerImageDesc tlasImage = m_ObjectPool.images[renderTlasLinearId];
            if (tlasImage.image && tlasDesc.numInstances)
            {
                BeforeTlasCopyImageBarriers(commandBuffer, tlasImage.image, backBufferImage);
                {
                    VkImageCopy copyRegion = {};
                    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copyRegion.extent = {static_cast<uint32_t>(std::min(tlasImage.width, p.swapChainWidth)), static_cast<uint32_t>(std::min(tlasImage.height, p.swapChainHeight)),  1};
                    vkCmdCopyImage(commandBuffer, tlasImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, backBufferImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
                }
                AfterTlasCopyImageBarriers(commandBuffer, tlasImage.image, backBufferImage);
            }
            else
            {
                VkImageMemoryBarrier swapChainImageBarrier = FillImageBarrier(backBufferImage,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                    0, nullptr, 0, nullptr, 1, &swapChainImageBarrier);
            }
        }
        { // render overlay
            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = p.renderPass;
            renderPassBeginInfo.framebuffer = p.framebuffers[backBufferId];
            renderPassBeginInfo.renderArea.extent.width = *m_OutputWidth;
            renderPassBeginInfo.renderArea.extent.height = *m_OutputHeight;
            renderPassBeginInfo.clearValueCount = 0;
            renderPassBeginInfo.pClearValues = &p.clearValue;

            vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer, nullptr);
            }
            vkCmdEndRenderPass(commandBuffer);
        }

        {
            vkEndCommandBuffer(commandBuffer);

            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &p.acquireSemaphores[syncId];
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &p.renderSemaphores[backBufferId];
            vkQueueSubmit(vkGraphicsPresentQueue, 1, &submitInfo, presentFence);
        }

        VkResult presentResult = {};
        {
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &p.renderSemaphores[backBufferId];
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &p.swapChain;
            presentInfo.pImageIndices = &backBufferId;

            VkResult tmpResult = {};
            presentInfo.pResults = &tmpResult;
            presentResult = vkQueuePresentKHR(vkGraphicsPresentQueue, &presentInfo);
        }

        if ((presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR))
        {
            ResizeSwapChain();
            return;
        }
    }
    BusyWait(m_Settings.fpsLimit, m_Settings.enableFpsLimit);
}

void InternalContext::CopyFeedbackBuffer(VkCommandBuffer commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId)
{
    ViewerBufferDesc& readbackBuffer = GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
    ViewerBufferDesc& feedbackBuffer = GetBuffer(BvhViewerResources::FeedbackBuffer, bufferedFrameId, tlasId);
    VkBufferMemoryBarrier beforeCopyBarrier = FillBufferBarrier(feedbackBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferMemoryBarrier afterCopyBarrier = FillBufferBarrier(feedbackBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &beforeCopyBarrier, 0, nullptr);
    VkBufferCopy copyRegion = {};
    copyRegion.size = sizeof(FeedbackBufferLayout);
    copyRegion.dstOffset = sizeof(FeedbackBufferLayout) * uint64_t(tlasId);
    copyRegion.srcOffset = 0;
    vkCmdCopyBuffer(commandBuffer, feedbackBuffer.buffer, readbackBuffer.buffer, 1, &copyRegion);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &afterCopyBarrier, 0, nullptr);
}

void InternalContext::RenderTlas(VkCommandBuffer commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId)
{
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];
    {
        { // Grow dynamic buffers
            uint64_t readbackRequiredSize = sizeof(FeedbackBufferLayout) * c_MaxTlasPerFrame;
            for (uint32_t i = 0; i < uint32_t(std::size(frame.tlases)); ++i)
                readbackRequiredSize += uint64_t(frame.tlases[i].numInstances) * sizeof(TlasInstanceDesc);
            GrowDynamicBuffer(GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId), readbackRequiredSize);
        }

        { // update constant buffer
            const ViewerBufferDesc& buffer = GetBuffer(BvhViewerResources::ConstantBuffer, bufferedFrameId, tlasId);
            void* mappedMemory = nullptr;
            vkMapMemory(vkDevice, buffer.memory, buffer.offsetInMemory, buffer.totalSize, 0, &mappedMemory);
            const ConstantBufferLayout& constants = m_ConstantBufferHelper.GetConstantBuffer();
            memcpy(mappedMemory, &constants, sizeof(ConstantBufferLayout));
            vkUnmapMemory(vkDevice, buffer.memory);
        }
        const uint64_t uploadSize = m_BlasTracker.PrepareData();
        if (uploadSize)
        { // upload tracking data
            ViewerBufferDesc& uploadBuffer = GetBuffer(BvhViewerResources::UploadBuffer, bufferedFrameId, tlasId);
            GrowDynamicBuffer(uploadBuffer, uploadSize);

            void* mappedUploadBuffer = nullptr;
            vkMapMemory(vkDevice, uploadBuffer.memory, uploadBuffer.offsetInMemory, uploadBuffer.totalSize, 0, &mappedUploadBuffer);

            BlasTracker::UploadDesc uploadDesc = {};
            uploadDesc.mappedUploadBuffer = mappedUploadBuffer;
            m_BlasTracker.UploadData(uploadDesc);
            vkUnmapMemory(vkDevice, uploadBuffer.memory);

            m_GpuUploadBufferedId = (m_GpuUploadBufferedId + 1) % c_GpuUploadBuffering;
            ViewerBufferDesc& blasDescs = GetBuffer(BvhViewerResources::BlasDescs, bufferedFrameId, tlasId);
            ViewerBufferDesc& blasHashes = GetBuffer(BvhViewerResources::BlasHashes, bufferedFrameId, tlasId);
            ViewerBufferDesc& geometryDescs = GetBuffer(BvhViewerResources::GeometryDescs, bufferedFrameId, tlasId);

            VkBufferMemoryBarrier beforeCopyBarriers[ ] =
            {
                FillBufferBarrier(blasDescs, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
                FillBufferBarrier(blasHashes, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
                FillBufferBarrier(geometryDescs, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
            };
            VkBufferMemoryBarrier afterCopyBarriers[ ] =
            {
                FillBufferBarrier(blasDescs, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                FillBufferBarrier(blasHashes, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                FillBufferBarrier(geometryDescs, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
            };

            auto bufferCopy = [&](VkBuffer src, ViewerBufferDesc& dst, BlasTracker::BufferRange range)
                {
                    GrowDynamicBuffer(dst, range.size);
                    VkBufferCopy copy = {};
                    copy.dstOffset = 0;
                    copy.srcOffset = range.offset;
                    copy.size = range.size;
                    vkCmdCopyBuffer(commandBuffer, src, dst.buffer, 1, &copy);
                };

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, std::size(beforeCopyBarriers), beforeCopyBarriers, 0, nullptr);
            bufferCopy(uploadBuffer.buffer, blasDescs, uploadDesc.outBlasDescRange);
            bufferCopy(uploadBuffer.buffer, blasHashes, uploadDesc.outHashesRange);
            bufferCopy(uploadBuffer.buffer, geometryDescs, uploadDesc.outGeometryDescRange);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, std::size(afterCopyBarriers), afterCopyBarriers, 0, nullptr);
        }
    }
    { // copy feedback buffer
        CopyFeedbackBuffer(commandBuffer, bufferedFrameId, tlasId);
        frame.readbackOffset = frame.readbackOffset == 0 ? uint64_t(sizeof(FeedbackBufferLayout) * c_MaxTlasPerFrame) : frame.readbackOffset;
    }
    { // ajust sizes
        const uint32_t linearTlasId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
        ViewerImageDesc& image = m_ObjectPool.images[linearTlasId];
        if (image.width != *m_OutputWidth || image.height != *m_OutputHeight)
            ResizeRenderImage(image, commandBuffer);
        GrowDynamicBuffer(GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), uint64_t(tlas.numInstances * sizeof(TlasInstanceDesc)));
    }
    if (tlas.instanceDescsBufferAddress)
    { // instance copy
        if (!PreparePipeline(commandBuffer, uint32_t(Pipelines::InstanceCopy), bufferedFrameId, tlasId))
            return;

        VkBufferMemoryBarrier preDispatchBarrier = FillBufferBarrier(
            GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId),
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

        VkBufferMemoryBarrier postDispatchBarrier = FillBufferBarrier(
            GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId),
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkBufferMemoryBarrier postCopyBarrier = FillBufferBarrier(
            GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId),
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &preDispatchBarrier, 0, nullptr);
        { // copy instances desc to the internal buffer via compute
            vkCmdDispatch(commandBuffer, DivideRoundUp(tlas.numInstances, INST_COPY_SHDR_X), 1, 1);
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &postDispatchBarrier, 0, nullptr);

        const uint64_t instanceDataSize = uint64_t(tlas.numInstances) * sizeof(TlasInstanceDesc);
        VkBuffer internalDescBuffer = GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId).buffer;
        ViewerBufferDesc readbackBuffer = GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
        if (tlas.numInstances > 0 && frame.readbackOffset + instanceDataSize < readbackBuffer.totalSize)
        { // readback instance descs
            VkBufferCopy copyRegion = {};
            copyRegion.size = uint64_t(tlas.numInstances) * sizeof(TlasInstanceDesc);
            copyRegion.dstOffset = frame.readbackOffset;
            copyRegion.srcOffset = 0;
            vkCmdCopyBuffer(commandBuffer, internalDescBuffer, readbackBuffer.buffer, 1, &copyRegion);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &postCopyBarrier, 0, nullptr);
            frame.readbackOffset += copyRegion.size;
        }
    }
    else if(tlas.indirectOpsAddr && tlas.indirectOpsCountAddr && m_DescriptorBufferExtOn)
    {
        if(!PreparePipeline(commandBuffer, uint32_t(Pipelines::IndirectOpsParser), bufferedFrameId, tlasId))
            return;

        VkBufferMemoryBarrier preDispatchBarrier = FillBufferBarrier(
            GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId),
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

        VkBufferMemoryBarrier postDispatchBarrier = FillBufferBarrier(
            GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId),
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &preDispatchBarrier, 0, nullptr);
        {
            const uint32_t x = DivideRoundUp(tlas.numInstances, OP_PARSE_SHDR_X);
            vkCmdDispatch(commandBuffer, x, 1, 1);
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 1, &postDispatchBarrier, 0, nullptr);
    }
    { // tlas rendering
        if(!PreparePipeline(commandBuffer, uint32_t(Pipelines::BvhVisualization), bufferedFrameId, tlasId))
            return;

        uint32_t x = DivideRoundUp(*m_OutputWidth, BVH_VIZ_SHDR_X);
        uint32_t y = DivideRoundUp(*m_OutputHeight, BVH_VIZ_SHDR_Y);
        vkCmdDispatch(commandBuffer, x, y, 1);
    }
}

void InternalContext::RenderTlasDeferred(VkFence& frameFence, VkCommandBuffer& commandBuffer, ViewerQueueType queueType, uint32_t bufferedFrameId, uint32_t tlasId)
{
    FrameDesc& frame = m_Frames[bufferedFrameId];
    const uint32_t linearId = bufferedFrameId * c_MaxTlasPerFrame + frame.commandBufferOffset;

    if (frameFence == nullptr)
    { // user doesn't provide a fence => use internal
        frameFence = m_ObjectPool.fences[linearId];
        vkResetFences(vkDevice, 1, &frameFence);
    }

    if (commandBuffer == nullptr)
    { // user doesn't provide a command buffer => use internal
        CommandBuffer* commandBuffers = m_ObjectPool.commandBuffers[uint32_t(queueType)].data();
        CommandBuffer& internalBuffer = commandBuffers[linearId];
        vkResetCommandPool(vkDevice, internalBuffer.pool, 0);
        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandBufferBeginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(internalBuffer.buffer, &commandBufferBeginInfo);
        commandBuffer = internalBuffer.buffer;
        ++frame.commandBufferOffset;
    }

    RenderTlas(commandBuffer, bufferedFrameId, tlasId);
}

uint32_t NvBvhViewerVK::PresentThread(void* parameter)
{
    NvBvhViewerVK* self = static_cast<NvBvhViewerVK*>(parameter);

    while (self->m_BackendInitialized == false)
    {
        Sleep(1);
        if (self->m_Exit)
            return 0;
    }

    InternalContext& ctx = *self->m_Context;

    { // init
        BvhViewerScopedLock scopedLock(self->m_Lock);
        if (self->CreateViewerWindow("VK") == false)
            return 0;
        if (ctx.CreatePresenter(self->m_Window) == false)
            return 0;
    }

    while (self->m_Exit == false)
    {
        if (PollWindowEvents(self->m_Window) == false)
        {
            self->NotifyWindowClose();
            return 0;
        }

        ctx.RenderPresenter(self->m_Window, self->m_Lock);
    }
    return 0;
}

NvBvhViewerVK::QueueSubmitResult NvBvhViewerVK::OnQueueSubmit(QueueSubmitDesc& desc, VkCommandBuffer optInWriteToCommandBuffer, VkFence optInQueueSubmitSignalFence)
{
    QueueSubmitResult submitResult = {};

    if (IsActive() == false)
        return submitResult;

    InternalContext& ctx = *m_Context;
    BvhViewerScopedLock scopedLock(m_Lock);

    const uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frame = ctx.m_Frames[bufferedFrameId];

    if (frame.commandBufferOffset >= c_MaxTlasPerFrame)
        return submitResult;

    VkFence& frameFence = frame.renderFences[frame.commandBufferOffset];
    frameFence = optInQueueSubmitSignalFence;
    VkCommandBuffer renderCommandBuffer = optInWriteToCommandBuffer;

    for (uint32_t inCommandBufferId = 0; inCommandBufferId < desc.inCommandBufferNum; ++inCommandBufferId)
    {
        for (uint32_t tlasId = 0; tlasId < frame.tlasOffset; ++tlasId)
        {
            TlasDesc& tlas = frame.tlases[tlasId];
            if (tlas.updateCommandBuffer != desc.inCommandBuffers[inCommandBufferId])
                continue;

            if(ctx.m_Inline == false)
            {
                ctx.RenderTlasDeferred(frameFence, renderCommandBuffer, desc.inQueueType, bufferedFrameId, tlasId);
                submitResult.outAppendCommandBuffer = optInWriteToCommandBuffer == nullptr ? renderCommandBuffer : nullptr;
            }
            else if(frameFence == nullptr)
            {
                const uint32_t linearId = bufferedFrameId * c_MaxTlasPerFrame + frame.commandBufferOffset++;
                frameFence = ctx.m_ObjectPool.fences[linearId];
                vkResetFences(ctx.vkDevice, 1, &frameFence);
            }
            submitResult.outQueueSubmitSignalFence = optInQueueSubmitSignalFence == nullptr ? frameFence : nullptr;
        }
    }

    if (submitResult.outAppendCommandBuffer != VK_NULL_HANDLE)
        vkEndCommandBuffer(submitResult.outAppendCommandBuffer);

    return submitResult;
}

void NvBvhViewerVK::OnCopyAccelerationStructure(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo)
{
    (void)commandBuffer;
    if (IsInitialized() == false)
        return;

    InternalContext& ctx = *m_Context;
    BvhViewerScopedLock scopedLock(m_Lock);

    ctx.PrepareDispatchTable();

    const bool isCompaction = pInfo->mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    const bool isClone = pInfo->mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR;

    if (isClone == false && isCompaction == false)
        return;

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = pInfo->src;
    uint64_t src = uint64_t(vkGetAccelerationStructureDeviceAddressKHR(ctx.vkDevice, &addrInfo));

    addrInfo.accelerationStructure = pInfo->dst;
    uint64_t dst = uint64_t(vkGetAccelerationStructureDeviceAddressKHR(ctx.vkDevice, &addrInfo));

    ctx.m_BlasTracker.ProcessBlasCopy(dst, src, isCompaction);
}

bool NvBvhViewerVK::OnBuildAccelerationStructures(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
    if (IsInitialized() == false)
        return false;

    InternalContext& ctx = *m_Context;
    BvhViewerScopedLock scopedLock(m_Lock);

    ctx.PrepareDispatchTable();

    const uint32_t frameId = ctx.GetFrameId();
    const uint32_t bufferedFrameId = frameId % ctx.m_NumBufferedFrames;
    FrameDesc& frame = ctx.m_Frames[frameId % ctx.m_NumBufferedFrames];

    if (pInfos[0].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) // only one type of RTAS can be submitted for build
    {
        VkMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        bool appendedRendering = false;
        for (uint32_t infoId = 0; infoId < infoCount && frame.tlasOffset < c_MaxTlasPerFrame; ++infoId)
        {
            const uint32_t tlasId = frame.tlasOffset++;
            TlasDesc& tlasDesc = frame.tlases[tlasId];
            const VkAccelerationStructureBuildGeometryInfoKHR& info = pInfos[infoId];
            const VkAccelerationStructureGeometryKHR& geometry = info.pGeometries ? *info.pGeometries : *info.ppGeometries[0];
            const VkAccelerationStructureGeometryInstancesDataKHR& instances = geometry.geometry.instances;
            tlasDesc.updateCommandBuffer = commandBuffer;
            tlasDesc.accelerationStructure = info.dstAccelerationStructure;
            tlasDesc.instanceDescsBufferAddress = instances.data.deviceAddress; // TODO: should support array of pointers here or should not?
            tlasDesc.flags = info.flags;

            const VkAccelerationStructureBuildRangeInfoKHR& range = *ppBuildRangeInfos[infoId];
            tlasDesc.numInstances = range.primitiveCount;
            
            if(tlasDesc.numInstances > 0)
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

            if (IsActive() && ctx.m_Inline && tlasDesc.numInstances > 0)
            {
                ctx.RenderTlas(commandBuffer, bufferedFrameId, tlasId);
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
                appendedRendering = true;
            }
        }
        return appendedRendering;
    }
    else
    {
        frame.numBlasBuilds += infoCount;
        for (uint32_t infoId = 0; infoId < infoCount; ++infoId)
        {
            const VkAccelerationStructureBuildGeometryInfoKHR& info = pInfos[infoId];
            const bool isUpdate = (info.mode & VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) != 0;

            ViewerBlasDesc viewerBlasDesc = {};
            viewerBlasDesc.buildFlags = isUpdate ? AS_BUILD_FLAG_PERFORM_UPDATE : 0;
            viewerBlasDesc.buildFlags |= info.flags;
            viewerBlasDesc.updateFrameId = frameId;
            viewerBlasDesc.geometries.resize(info.geometryCount);

            VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {};
            addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
            addrInfo.accelerationStructure = info.dstAccelerationStructure;
            viewerBlasDesc.address = uint64_t(vkGetAccelerationStructureDeviceAddressKHR(ctx.vkDevice, &addrInfo));
            viewerBlasDesc.hash = HashUint64(viewerBlasDesc.address);

            const VkAccelerationStructureBuildRangeInfoKHR* ranges = ppBuildRangeInfos[infoId];
            for (uint32_t geometryId = 0; geometryId < info.geometryCount; ++geometryId)
            {
                const VkAccelerationStructureGeometryKHR& vkGeometry = info.pGeometries ? info.pGeometries[geometryId] : *info.ppGeometries[geometryId];
                ViewerGeometryDesc& viewerGeometryDesc = viewerBlasDesc.geometries[geometryId];
                viewerGeometryDesc.flags = static_cast<uint16_t>(vkGeometry.flags);

                const uint32_t primitiveCount = ranges[geometryId].primitiveCount;
                uint16_t geometryType = 0;
                switch (vkGeometry.geometryType)
                {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                {
                    geometryType = uint16_t(RtGeometryTypes::Triangles);
                    viewerGeometryDesc.data[0] = static_cast<uint32_t>(vkGeometry.geometry.triangles.maxVertex);
                    viewerGeometryDesc.data[1] = primitiveCount * 3;

                    const VkAccelerationStructureGeometryTrianglesDataKHR& triangles = vkGeometry.geometry.triangles;
                    if (triangles.pNext != VK_NULL_HANDLE)
                        if (*(VkStructureType*)triangles.pNext == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_TRIANGLES_OPACITY_MICROMAP_EXT)
                            geometryType = uint16_t(RtGeometryTypes::OmmTriangles);
                    break;
                }
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                    geometryType = uint16_t(RtGeometryTypes::AABB);
                    break;
                case VK_GEOMETRY_TYPE_SPHERES_NV:
                    geometryType = uint16_t(RtGeometryTypes::Spheres);
                    break;
                case VK_GEOMETRY_TYPE_LINEAR_SWEPT_SPHERES_NV:
                    geometryType = uint16_t(RtGeometryTypes::LSS);
                    break;
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                    break;
                case VK_GEOMETRY_TYPE_MAX_ENUM_KHR:
                    break;
                default:
                    break;
                }
                viewerGeometryDesc.data[2] = primitiveCount;
                viewerGeometryDesc.type = geometryType;
            }
            ctx.m_BlasTracker.Insert(viewerBlasDesc);
        }
    }
    return false;
}

bool NvBvhViewerVK::OnBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo)
{
    if (IsInitialized() == false)
        return false;

    InternalContext& ctx = *m_Context;
    if(ctx.m_DescriptorBufferExtOn == false)
        return false;

    BvhViewerScopedLock scopedLock(m_Lock);

    ctx.PrepareDispatchTable();

    const uint32_t frameId = ctx.GetFrameId();
    const uint32_t bufferedFrameId = frameId % ctx.m_NumBufferedFrames;
    FrameDesc& frame = ctx.m_Frames[frameId % ctx.m_NumBufferedFrames];

    if (frame.tlasOffset < c_MaxTlasPerFrame)
    {
        VkMemoryBarrier memoryBarrier = {};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        const uint32_t tlasId = frame.tlasOffset++;
        TlasDesc& tlasDesc = frame.tlases[tlasId];

        const VkBuildPartitionedAccelerationStructureInfoNV& info = *pBuildInfo;
        const VkPartitionedAccelerationStructureInstancesInputNV& input = info.input;

        tlasDesc.updateCommandBuffer = commandBuffer;
        tlasDesc.flags = input.flags;
        tlasDesc.numInstances = static_cast<uint32_t>(input.instanceCount);
        tlasDesc.instanceDescsBufferAddress = 0;

        tlasDesc.indirectOpsAddr = info.srcInfos;
        tlasDesc.indirectOpsCountAddr = info.srcInfosCount;
        tlasDesc.ptlasAddr= info.dstAccelerationStructureData;

        if(tlasDesc.numInstances > 0)
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

        if (IsActive() && ctx.m_Inline && tlasDesc.numInstances > 0)
        {
            ctx.RenderTlas(commandBuffer, bufferedFrameId, tlasId);
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
            return true;
        }
    }
    return false;
}

void NvBvhViewerVK::OnNewFrame()
{
    if (IsInitialized() == false)
        return;

    InternalContext& ctx = *m_Context;
    BvhViewerScopedLock scopedLock(m_Lock);

    uint32_t currentId = ctx.GetFrameId();
    if (ctx.m_Frames[currentId % ctx.m_NumBufferedFrames].tlasOffset == 0)
        return;

    uint32_t nextFrameId = currentId + 1;
    FrameDesc& nextFrame = ctx.m_Frames[nextFrameId % ctx.m_NumBufferedFrames];
    nextFrame.Reset();

    ctx.m_FrameId.store(nextFrameId, std::memory_order_release);
}

void NvBvhViewerVK::ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight)
{
    if (m_OutputWidth == newWidth && m_OutputHeight == newHeight)
        return;
    if (!newWidth || !newHeight)
        return;

    BvhViewerScopedLock scopedLock(m_Lock);
    m_OutputWidth = newWidth;
    m_OutputHeight = newHeight;
    m_Context->ResizeSwapChain();
}

void NvBvhViewerVK::Shutdown()
{
    if (m_BackendInitialized == false)
        return;

    m_Exit = true;
    DestroyAsyncPresentThread();

    BvhViewerScopedLock scopedLock(m_Lock);
    vkDeviceWaitIdle(m_Context->vkDevice);

    m_Context->DestroyBackend();
    DestroyViewerWindow();

    m_BackendInitialized = false;
}

bool NvBvhViewerVK::Launch()
{
    if (m_BackendInitialized)
        return true;

    if (m_Initialized == false)
    {
        LogMessage(MessageType::Info, "NvBvhViewerVK::Create: viewer is not initialized");
        return false;
    }

    m_Exit = false;

    InternalContext& ctx = *m_Context;
    ctx.PrepareDispatchTable();

    m_BackendInitialized = ctx.CreateBackend();

    if (m_BackendInitialized == false)
    {
        ctx.DestroyBackend();
        return false;
    }

    if (CreatePresentThread(&NvBvhViewerVK::PresentThread) == false)
    {
        Shutdown();
        return false;
    }

    return true;
}

bool NvBvhViewerVK::Deinitialize()
{
    if (m_BackendInitialized == true)
    {
        LogMessage(MessageType::Error, "NvBvhViewerVK::Deinitialize: Viewer has to be destroyed before deinitialization");
        return false;
    }

    m_Context->m_Frames.resize(0); m_Context->m_Frames.shrink_to_fit();

    delete m_Context;
    m_Context = nullptr;

    m_OutputWidth = 0;
    m_OutputHeight = 0;
    m_WindowCloseCallback = nullptr;
    m_WindowResizeCallback = nullptr;
    m_CallbackUserData = nullptr;

    m_Initialized = false;

    return true;
}

bool NvBvhViewerVK::Initialize(const NvBvhViewerVK::InitDesc& initDesc)
{
    if (m_Initialized)
        return true;

    g_LogMessageCallback = initDesc.logMessageCallback;

    if (!initDesc.vkDevice || !initDesc.vkInstance || !initDesc.vkPhysicalDevice || !initDesc.vkViewerGraphicsQueue)
    {
        LogMessage(MessageType::Error, "NvBvhViewerVK::Initialize: Null VK objects provided by the caller");
        return false;
    }

    if (initDesc.outputWidth == 0 || initDesc.outputHeight == 0)
    {
        LogMessage(MessageType::Error, "NvBvhViewerVK::Initialize: windows dimensions can't be zero");
        return false;
    }

    if (!initDesc.vkGetDeviceProcAddrFuncPtr || !initDesc.vkGetInstanceProcAddrFuncPtr)
    {
        LogMessage(MessageType::Error, "NvBvhViewerVK::Initialize: vkGetInstanceProcAddrFuncPtr and vkGetDeviceProcAddrFuncPtr can't be null");
        return false;
    }

    m_OutputWidth = initDesc.outputWidth;
    m_OutputHeight = initDesc.outputHeight;
    m_WindowCloseCallback = initDesc.windowCloseCallback;
    m_WindowResizeCallback = initDesc.windowResizeCallback;
    m_CallbackUserData = initDesc.callbackUserData;

    m_Context = new InternalContext();
    InternalContext& ctx = *m_Context;

    ctx.vkPhysicalDevice = initDesc.vkPhysicalDevice;
    ctx.vkDevice = initDesc.vkDevice;
    ctx.vkInstance = initDesc.vkInstance;
    ctx.vkGraphicsPresentQueue = initDesc.vkViewerGraphicsQueue;
    ctx.vkGetDeviceProcAddrFuncPtr = initDesc.vkGetDeviceProcAddrFuncPtr;
    ctx.vkGetInstanceProcAddrFuncPtr = initDesc.vkGetInstanceProcAddrFuncPtr;
    ctx.vkSetDeviceLoaderDataPtr = initDesc.vkSetDeviceLoaderDataPtr;
    ctx.m_HasComputeQueue = initDesc.hasComputeQueue;
    ctx.m_NumBufferedFrames = initDesc.numBufferedFrames;
    ctx.m_OutputWidth = &m_OutputWidth;
    ctx.m_OutputHeight = &m_OutputHeight;
    ctx.m_EnableLss = initDesc.enableLss;
    ctx.m_Inline = initDesc.enableInlineRendering;
    ctx.m_DescriptorBufferExtOn = initDesc.enableDescriptorBuffers;
    ctx.m_EnablePositionFetch = initDesc.enablePositionFetch;
    ctx.m_Frames.resize(ctx.m_NumBufferedFrames, FrameDesc());

    m_Initialized = true;
    return true;
}
