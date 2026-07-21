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
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#if defined(_WIN32)
#include <vulkan/vulkan_win32.h>
#else
#include <vulkan/vulkan_wayland.h>
#endif

#define VK_INSTANCE_FUNC_MAP(VK_FUNC_MACRO)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceQueueFamilyProperties)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceMemoryProperties)\
    VK_FUNC_MACRO(vkDestroySurfaceKHR)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceSurfaceSupportKHR)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceSurfaceFormatsKHR)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceSurfacePresentModesKHR)\
    VK_FUNC_MACRO(vkEnumerateDeviceExtensionProperties)\
    VK_FUNC_MACRO(vkGetPhysicalDeviceProperties2)

#define VK_DEVICE_FUNC_MAP(VK_FUNC_MACRO)\
    VK_FUNC_MACRO(vkDestroyFramebuffer)\
    VK_FUNC_MACRO(vkGetImageMemoryRequirements)\
    VK_FUNC_MACRO(vkCreateShaderModule)\
    VK_FUNC_MACRO(vkCmdCopyBufferToImage)\
    VK_FUNC_MACRO(vkDestroyCommandPool)\
    VK_FUNC_MACRO(vkCreateImageView)\
    VK_FUNC_MACRO(vkCmdBindIndexBuffer)\
    VK_FUNC_MACRO(vkFreeCommandBuffers)\
    VK_FUNC_MACRO(vkBindImageMemory)\
    VK_FUNC_MACRO(vkDestroySemaphore)\
    VK_FUNC_MACRO(vkFreeMemory)\
    VK_FUNC_MACRO(vkCmdSetViewport)\
    VK_FUNC_MACRO(vkCmdBindDescriptorSets)\
    VK_FUNC_MACRO(vkDestroyShaderModule)\
    VK_FUNC_MACRO(vkFreeDescriptorSets)\
    VK_FUNC_MACRO(vkCmdPushConstants)\
    VK_FUNC_MACRO(vkUnmapMemory)\
    VK_FUNC_MACRO(vkCreateBuffer)\
    VK_FUNC_MACRO(vkDestroySwapchainKHR)\
    VK_FUNC_MACRO(vkFlushMappedMemoryRanges)\
    VK_FUNC_MACRO(vkDestroyPipelineLayout)\
    VK_FUNC_MACRO(vkDestroyBuffer)\
    VK_FUNC_MACRO(vkUpdateDescriptorSets)\
    VK_FUNC_MACRO(vkCreateRenderPass)\
    VK_FUNC_MACRO(vkCreateCommandPool)\
    VK_FUNC_MACRO(vkDestroyFence)\
    VK_FUNC_MACRO(vkDestroyDescriptorSetLayout)\
    VK_FUNC_MACRO(vkCmdBindVertexBuffers)\
    VK_FUNC_MACRO(vkAllocateMemory)\
    VK_FUNC_MACRO(vkBindBufferMemory)\
    VK_FUNC_MACRO(vkDestroyRenderPass)\
    VK_FUNC_MACRO(vkAllocateCommandBuffers)\
    VK_FUNC_MACRO(vkDestroyImageView)\
    VK_FUNC_MACRO(vkCreatePipelineLayout)\
    VK_FUNC_MACRO(vkCmdSetScissor)\
    VK_FUNC_MACRO(vkCmdBindPipeline)\
    VK_FUNC_MACRO(vkCreateGraphicsPipelines)\
    VK_FUNC_MACRO(vkCreateDescriptorSetLayout)\
    VK_FUNC_MACRO(vkGetSwapchainImagesKHR)\
    VK_FUNC_MACRO(vkCmdDrawIndexed)\
    VK_FUNC_MACRO(vkDeviceWaitIdle)\
    VK_FUNC_MACRO(vkCmdPipelineBarrier)\
    VK_FUNC_MACRO(vkDestroySampler)\
    VK_FUNC_MACRO(vkDestroyImage)\
    VK_FUNC_MACRO(vkCreateImage)\
    VK_FUNC_MACRO(vkCreateSwapchainKHR)\
    VK_FUNC_MACRO(vkCreateFramebuffer)\
    VK_FUNC_MACRO(vkMapMemory)\
    VK_FUNC_MACRO(vkAllocateDescriptorSets)\
    VK_FUNC_MACRO(vkCreateSampler)\
    VK_FUNC_MACRO(vkCreateSemaphore)\
    VK_FUNC_MACRO(vkGetBufferMemoryRequirements)\
    VK_FUNC_MACRO(vkDestroyPipeline)\
    VK_FUNC_MACRO(vkCreateFence)\
    VK_FUNC_MACRO(vkResetCommandBuffer)\
    VK_FUNC_MACRO(vkCreateComputePipelines)\
    VK_FUNC_MACRO(vkCreateDescriptorPool)\
    VK_FUNC_MACRO(vkCreateBufferView)\
    VK_FUNC_MACRO(vkEndCommandBuffer)\
    VK_FUNC_MACRO(vkCmdDispatch)\
    VK_FUNC_MACRO(vkGetFenceStatus)\
    VK_FUNC_MACRO(vkGetDeviceQueue)\
    VK_FUNC_MACRO(vkBeginCommandBuffer)\
    VK_FUNC_MACRO(vkCmdBeginRenderPass)\
    VK_FUNC_MACRO(vkCmdEndRenderPass)\
    VK_FUNC_MACRO(vkQueueSubmit)\
    VK_FUNC_MACRO(vkQueuePresentKHR)\
    VK_FUNC_MACRO(vkResetCommandPool)\
    VK_FUNC_MACRO(vkAcquireNextImageKHR)\
    VK_FUNC_MACRO(vkWaitForFences)\
    VK_FUNC_MACRO(vkResetFences)\
    VK_FUNC_MACRO(vkCmdCopyImage)\
    VK_FUNC_MACRO(vkDestroyDescriptorPool)\
    VK_FUNC_MACRO(vkDestroyBufferView)\
    VK_FUNC_MACRO(vkQueueWaitIdle)\
    VK_FUNC_MACRO(vkCmdCopyBuffer)\
    VK_FUNC_MACRO(vkGetAccelerationStructureDeviceAddressKHR)

#define VK_DEVICE_DESCRIPTOR_BUFFER_FUNC_MAP(VK_FUNC_MACRO)\
    VK_FUNC_MACRO(vkGetDescriptorSetLayoutSizeEXT)\
    VK_FUNC_MACRO(vkGetDescriptorSetLayoutBindingOffsetEXT)\
    VK_FUNC_MACRO(vkGetDescriptorEXT)\
    VK_FUNC_MACRO(vkGetBufferDeviceAddress)\
    VK_FUNC_MACRO(vkCmdBindDescriptorBuffersEXT)\
    VK_FUNC_MACRO(vkCmdSetDescriptorBufferOffsetsEXT)

#define DECLARE_ENTRY(name) static PFN_##name name;

DECLARE_ENTRY(vkGetInstanceProcAddr);
DECLARE_ENTRY(vkGetDeviceProcAddr);
DECLARE_ENTRY(vkSetDeviceLoaderData);

#if defined(_WIN32)
DECLARE_ENTRY(vkCreateWin32SurfaceKHR)
#else
DECLARE_ENTRY(vkCreateWaylandSurfaceKHR)
#endif

VK_INSTANCE_FUNC_MAP(DECLARE_ENTRY)
VK_DEVICE_FUNC_MAP(DECLARE_ENTRY)
VK_DEVICE_DESCRIPTOR_BUFFER_FUNC_MAP(DECLARE_ENTRY)

struct VkDispatchTableInitDesc
{
    VkDevice device;
    VkInstance instance;
    void* getDeviceProcAddrFuncPtr;
    void* getInstanceProcAddrFuncPtr;
    void* setDeviceLoaderData;
    bool enableDescriptorBufferExt;
};

static void FillVkDispatchTable(const VkDispatchTableInitDesc& desc)
{
    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)desc.getDeviceProcAddrFuncPtr;
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)desc.getInstanceProcAddrFuncPtr;
    vkSetDeviceLoaderData = (PFN_vkSetDeviceLoaderData)desc.setDeviceLoaderData;

    #define GET_INSTANCE_VK_FUNC(name) \
    { \
        if(desc.instance && vkGetInstanceProcAddr)\
            name = (PFN_##name)vkGetInstanceProcAddr(desc.instance, #name);\
    }

    VK_INSTANCE_FUNC_MAP(GET_INSTANCE_VK_FUNC);

    #define GET_DEVICE_VK_FUNC(name) \
    { \
        if(desc.device && vkGetDeviceProcAddr)\
            name = (PFN_##name)vkGetDeviceProcAddr(desc.device, #name);\
    }

    VK_DEVICE_FUNC_MAP(GET_DEVICE_VK_FUNC);

    if(desc.enableDescriptorBufferExt)
    {
        VK_DEVICE_DESCRIPTOR_BUFFER_FUNC_MAP(GET_DEVICE_VK_FUNC);
    }

    #if defined(_WIN32)
        GET_INSTANCE_VK_FUNC(vkCreateWin32SurfaceKHR);
    #else
        GET_INSTANCE_VK_FUNC(vkCreateWaylandSurfaceKHR);
    #endif
}
