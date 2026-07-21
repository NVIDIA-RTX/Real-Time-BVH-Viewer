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

#pragma once
#include "../Common/NvBvhViewerCommon.h"

#define BVHV_VK_HANDLE_FORWARD_DECLARATION( name ) typedef struct name##_T* name;
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkQueue)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkCommandBuffer)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkFence)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkPhysicalDevice)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkDevice)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkInstance)
BVHV_VK_HANDLE_FORWARD_DECLARATION(VkSurfaceKHR)

#define BVHV_VK_STRUCT_FORWARD_DECLARATION(name) struct name;
BVHV_VK_STRUCT_FORWARD_DECLARATION(VkAccelerationStructureBuildGeometryInfoKHR)
BVHV_VK_STRUCT_FORWARD_DECLARATION(VkAccelerationStructureBuildRangeInfoKHR)
BVHV_VK_STRUCT_FORWARD_DECLARATION(VkCopyAccelerationStructureInfoKHR)
BVHV_VK_STRUCT_FORWARD_DECLARATION(VkBuildPartitionedAccelerationStructureInfoNV)

class NvBvhViewerVK : private NvBvhViewerCommon
{
public:
    struct InitDesc
    {
        VkPhysicalDevice vkPhysicalDevice;
        VkDevice vkDevice;
        VkInstance vkInstance;

        // dedicated graphics queue. Host is required to request an additional graphics queue ond device creation and provide it to the viewer.
        VkQueue vkViewerGraphicsQueue;

        void* vkGetDeviceProcAddrFuncPtr;
        void* vkGetInstanceProcAddrFuncPtr;
        void* vkSetDeviceLoaderDataPtr;

        NvBvhViewerCommon::PFN_LogMessageCallback logMessageCallback;
        NvBvhViewerCommon::PFN_WindowCloseCallback windowCloseCallback;
        NvBvhViewerCommon::PFN_WindowResizeCallback windowResizeCallback;
        void* callbackUserData;

        uint32_t outputWidth;
        uint32_t outputHeight;
        uint32_t numBufferedFrames;

        bool hasComputeQueue;         // caller device was created with compute queue
        bool enableDescriptorBuffers; // mark if device has VK_EXT_descriptor_buffer enabled. REQUIRED for PTLAS visualization.
        bool enableLss;               // mark if device supports and requests Line Swept Spheres in RT shaders. VK_KHR_spirv_1_4 && VK_NV_ray_tracing_linear_swept_spheres.
        bool enablePositionFetch;     // mark if device has VK_KHR_ray_tracing_position_fetch enabled.

        // Viewer copies instance description buffers for several visualization modes. If this buffer gets modified after TLAS build, those modes won't work correctly.
        // In case of inline rendering render commands on TLAS builds will be appended to the host's commandBuffers. Pipeline state will become dirty.
        // Thus if RTAS build functions return true, host has to restore the pipeline state itslef.
        bool enableInlineRendering;
    };

    struct QueueSubmitResult
    {
        // if after QueueSubmitCallback this buffer is not nullptr, this buffer should be appended to QueueSubmit call or submited separately right after
        VkCommandBuffer outAppendCommandBuffer;
        // signal this fence on Queue Submit if not nullptr
        VkFence outQueueSubmitSignalFence;
    };

    struct QueueSubmitDesc
    {
        VkQueue inQueue;
        const VkCommandBuffer* inCommandBuffers;
        uint32_t inCommandBufferNum;
        ViewerQueueType inQueueType;
    };

public:
    // main
    bool Initialize(const InitDesc& initDesc);  // set viewer initial state, enable cpu-side tracking of RTAS build calls.
    bool Launch();                              // creates internal resoruces and window, launches visualization
    void Shutdown();                            // stops the viewer and frees internal resources
    bool Deinitialize();                        // resets the initial state

    // functions to call right After the corresponding VK API functions. Return value is explained in InitDesc::enableInlineRendering
    bool OnBuildAccelerationStructures(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
    bool OnBuildPartitionedAccelerationStructuresNV(VkCommandBuffer commandBuffer, const VkBuildPartitionedAccelerationStructureInfoNV* pBuildInfo);
    void OnCopyAccelerationStructure(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo);

    // call OnQueueSubmit right Before QueueSubmit on the host. Process QueueSubmitResult. See comments for QueueSubmitResult fields for more details.
    // If optInWriteToCommandBuffer is not null the viewer will append its rendering to this buffer in deferred rendeing mode. It has to be opened and closed by the host.
    // If optInQueueSubmitSignalFence is not null the viewer will use it for synchronization.
    QueueSubmitResult OnQueueSubmit(QueueSubmitDesc& desc, VkCommandBuffer optInWriteToCommandBuffer, VkFence optInQueueSubmitSignalFence);

    // call OnNewFrame before the logical frame. E.g. inside present for a real non-generated frame.
    void OnNewFrame();

    // state
    bool IsInitialized() const { return m_Initialized; };
    bool IsActive() const { return m_BackendInitialized; };

    // can be used to resize window programmatically, but mainly here to support window messages
    void ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight) override final;

private:
    static uint32_t PresentThread(void* parameter);

private:
    struct InternalContext* m_Context = nullptr;
    bool m_BackendInitialized = false;
    bool m_Initialized = false;
    bool m_Exit = false;
};