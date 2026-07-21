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
#include <map>
#include <vector>
#include <atomic>
#include <vulkan/vulkan.h>
#include "Lock.h"
#include "VkDispatchTable.h"

class VkPipelineComputeStateTracker
{
    static const uint32_t c_MaxDescriptorSets = 32;
    static const uint32_t c_MaxPushConstantsSizeInBytes = 256;

    struct DescriptorSetBindRecord
    {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        uint32_t firstSet = 0;
        std::vector<VkDescriptorSet> sets;
        std::vector<uint32_t> dynamicOffsets;
    };

    struct State
    {
        State()
        {
            m_Pipeline = VK_NULL_HANDLE;
            m_PipelineLayout = VK_NULL_HANDLE;
            m_DescriptorSetBindRecords.clear();
            memset(m_PushConstantsData, 0, sizeof(m_PushConstantsData));
            m_PushConstantsDirtyMin = c_MaxPushConstantsSizeInBytes;
            m_PushConstantsDirtyMax = 0;
            m_PushConstantsStageFlags = 0;
        }

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

        // Keyed by firstSet to deduplicate overlapping bind calls.
        // Each record captures one vkCmdBindDescriptorSets call.
        std::map<uint32_t, DescriptorSetBindRecord> m_DescriptorSetBindRecords;

        uint8_t m_PushConstantsData[c_MaxPushConstantsSizeInBytes] = {};
        uint32_t m_PushConstantsDirtyMin = c_MaxPushConstantsSizeInBytes;
        uint32_t m_PushConstantsDirtyMax = 0;
        VkShaderStageFlags m_PushConstantsStageFlags = 0;
    };

    std::atomic_flag m_WriteLock = ATOMIC_FLAG_INIT;
    std::atomic_uint m_ReadLock = 0;
    std::map<VkCommandBuffer, State> m_States = {};

public:
    bool CanRestore(VkCommandBuffer commandBuffer)
    {
        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
        return m_States.find(commandBuffer) != m_States.end();
    }

    void Open(VkCommandBuffer commandBuffer)
    {
        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Writer);
        m_States[commandBuffer] = State();
    }

    void Close(VkCommandBuffer commandBuffer)
    {
        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Writer);
        auto it = m_States.find(commandBuffer);
        if (it != m_States.end())
            m_States.erase(it);
    }

    void UpdatePipeline(VkCommandBuffer commandBuffer, VkPipeline pipeline)
    {
        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
        auto it = m_States.find(commandBuffer);
        if (it == m_States.end())
            return;
        it->second.m_Pipeline = pipeline;
    }

    void UpdateDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
    {
        if (firstSet >= c_MaxDescriptorSets)
            return;

        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
        auto it = m_States.find(commandBuffer);
        if (it == m_States.end())
            return;

        State& state = it->second;
        state.m_PipelineLayout = layout;

        const uint32_t clampedCount = (firstSet + descriptorSetCount > c_MaxDescriptorSets)
            ? (c_MaxDescriptorSets - firstSet)
            : descriptorSetCount;

        DescriptorSetBindRecord record;
        record.layout = layout;
        record.firstSet = firstSet;
        record.sets.assign(pDescriptorSets, pDescriptorSets + clampedCount);
        if (dynamicOffsetCount > 0 && pDynamicOffsets)
            record.dynamicOffsets.assign(pDynamicOffsets, pDynamicOffsets + dynamicOffsetCount);

        state.m_DescriptorSetBindRecords[firstSet] = std::move(record);
    }

    void UpdatePushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
    {
        if (offset + size > c_MaxPushConstantsSizeInBytes)
            return;

        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
        auto it = m_States.find(commandBuffer);
        if (it == m_States.end())
            return;

        State& state = it->second;
        state.m_PipelineLayout = layout;
        state.m_PushConstantsStageFlags |= stageFlags;
        memcpy(state.m_PushConstantsData + offset, pValues, size);

        if (offset < state.m_PushConstantsDirtyMin)
            state.m_PushConstantsDirtyMin = offset;

        const uint32_t end = offset + size;
        if (end > state.m_PushConstantsDirtyMax)
            state.m_PushConstantsDirtyMax = end;
    }

    void Restore(VkCommandBuffer commandBuffer, const VulkanDispatchTable& dt);
};

__forceinline void VkPipelineComputeStateTracker::Restore(VkCommandBuffer commandBuffer, const VulkanDispatchTable& dt)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);

    auto it = m_States.find(commandBuffer);
    if (it == m_States.end())
        return;

    State& state = it->second;

    if (state.m_Pipeline)
        dt.vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, state.m_Pipeline);

    for (const auto& [firstSet, record] : state.m_DescriptorSetBindRecords)
    {
        if (record.sets.empty())
            continue;

        dt.vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            record.layout,
            record.firstSet,
            uint32_t(record.sets.size()),
            record.sets.data(),
            uint32_t(record.dynamicOffsets.size()),
            record.dynamicOffsets.empty() ? nullptr : record.dynamicOffsets.data()
        );
    }

    if (state.m_PushConstantsStageFlags != 0 &&
        state.m_PipelineLayout != VK_NULL_HANDLE &&
        state.m_PushConstantsDirtyMin < state.m_PushConstantsDirtyMax)
    {
        const uint32_t offset = state.m_PushConstantsDirtyMin;
        const uint32_t size = state.m_PushConstantsDirtyMax - state.m_PushConstantsDirtyMin;
        dt.vkCmdPushConstants(
            commandBuffer,
            state.m_PipelineLayout,
            state.m_PushConstantsStageFlags,
            offset,
            size,
            state.m_PushConstantsData + offset
        );
    }
}
