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

#include "BlasTracker.h"

#include "CPU_Bridge.h"
#include "ScopedLock.h"

void BlasTracker::Insert(ViewerBlasDesc& blasDesc)
{
    BvhViewerScopedLock scopedLock(m_Lock);
    bool isUpdate = bool(blasDesc.buildFlags & AS_BUILD_FLAG_PERFORM_UPDATE);
    if (isUpdate)
    {
        auto it = m_HashToBlasDesc.find(blasDesc.hash);
        if (it != m_HashToBlasDesc.end())
            blasDesc.buildFlags |= AS_BUILD_FLAG_COMPACTED & it->second.buildFlags;
    }
    m_HashToBlasDesc.insert_or_assign(blasDesc.hash, blasDesc);
    CleanUp();
}

uint64_t BlasTracker::PrepareData()
{
    BvhViewerScopedLock scopedLock(m_Lock);

    if (m_HashToBlasDesc.empty())
        return 0;

    m_WorkerVector.clear();
    uint64_t numGeometries = 0;
    for (auto& iterator : m_HashToBlasDesc)
    {
        numGeometries += uint32_t(iterator.second.geometries.size());
        m_WorkerVector.push_back(iterator.second);
    }
    std::sort(m_WorkerVector.begin(), m_WorkerVector.end(), [](ViewerBlasDesc& a, ViewerBlasDesc& b) {return (a.hash) < (b.hash); });

    uint64_t hashBufferSize = uint64_t(m_WorkerVector.size()) * uint64_t(sizeof(uint32_t));
    uint64_t blasBufferSize = uint64_t(m_WorkerVector.size()) * uint64_t(sizeof(ViewerBlasDescGPU));
    uint64_t geometryBufferSize = numGeometries * uint64_t(sizeof(ViewerRtGeometryDescGPU));

    return hashBufferSize + blasBufferSize + geometryBufferSize;
}

void BlasTracker::UploadData(BlasTracker::UploadDesc& desc)
{
    BvhViewerScopedLock scopedLock(m_Lock);

    desc.outBlasDescRange.offset = 0;
    desc.outBlasDescRange.size = m_WorkerVector.size() * uint64_t(sizeof(ViewerBlasDescGPU));

    desc.outHashesRange.offset = desc.outBlasDescRange.offset + desc.outBlasDescRange.size;
    desc.outHashesRange.size = m_WorkerVector.size() * uint64_t(sizeof(uint32_t));

    desc.outGeometryDescRange.offset = desc.outHashesRange.offset + desc.outHashesRange.size;
    if (desc.mappedUploadBuffer == nullptr)
        return;

    ViewerBlasDescGPU* mappedBlases = (ViewerBlasDescGPU*)((uint8_t*)(desc.mappedUploadBuffer) + desc.outBlasDescRange.offset);
    uint32_t* mappedHashes = (uint32_t*)((uint8_t*)(desc.mappedUploadBuffer) + desc.outHashesRange.offset);
    ViewerRtGeometryDescGPU* mappedGeometry = (ViewerRtGeometryDescGPU*)((uint8_t*)(desc.mappedUploadBuffer) + desc.outGeometryDescRange.offset);

    uint32_t geometryCounter = 0;
    for (size_t i = 0; i < m_WorkerVector.size(); ++i)
    {
        ViewerBlasDesc& blasDesc = m_WorkerVector[i];

        mappedHashes[i] = blasDesc.hash;

        ViewerBlasDescGPU& gpuBlasDesc = mappedBlases[i];
        gpuBlasDesc.flags = blasDesc.buildFlags;
        gpuBlasDesc.geometryOffset = geometryCounter;
        gpuBlasDesc.updateFrameId = blasDesc.updateFrameId;

        for (size_t geometryId = 0; geometryId < blasDesc.geometries.size(); ++geometryId)
        {
            uint8_t type = uint8_t(blasDesc.geometries[geometryId].type);
            uint8_t flags = uint8_t(blasDesc.geometries[geometryId].flags);
            mappedGeometry[geometryCounter++] = ViewerRtGeometryDescGPU(uint8_t((type << 2) | flags));
        }
    }
    desc.outGeometryDescRange.size = geometryCounter * uint64_t(sizeof(ViewerRtGeometryDescGPU));
}

ViewerBlasDesc* BlasTracker::GetBlasDesc(uint32_t blasHash)
{
    BvhViewerScopedLock scopedLock(m_Lock);

    if (m_HashToBlasDesc.empty())
        return nullptr;

    auto it = m_HashToBlasDesc.find(blasHash);

    if (it == m_HashToBlasDesc.end())
        return nullptr;

    return &it->second;
}

void BlasTracker::ProcessBlasCopy(uint64_t dst, uint64_t src, bool isCompaction)
{
    BvhViewerScopedLock scopedLock(m_Lock);

    uint32_t srcHash = HashUint64(src);
    auto iterator = m_HashToBlasDesc.find(srcHash);
    if (iterator == m_HashToBlasDesc.end())
        return;

    uint32_t dstHash = HashUint64(dst);
    ViewerBlasDesc descCopy = iterator->second;
    descCopy.hash = dstHash;
    descCopy.buildFlags |= isCompaction ? AS_BUILD_FLAG_COMPACTED : 0;
    m_HashToBlasDesc.insert_or_assign(dstHash, descCopy);

    // let's assume that application is not going to use both compacted and non-compacted blas for rendering
    if (isCompaction)
        m_HashToBlasDesc.erase(srcHash);

    CleanUp();
}

void BlasTracker::ParseInstanceDescs(void* ptr, uint32_t numDescs, uint32_t currentMaxInstanceNum, double3* cameraPos)
{
    if (numDescs == 0 || ptr == nullptr)
        return;

    BvhViewerScopedLock scopedLock(m_Lock);

    m_MaxInstanceCount = std::max(currentMaxInstanceNum, m_MaxInstanceCount);

    TlasInstanceDesc* data = (TlasInstanceDesc*)ptr;

    double3 posWeight = double3(1.0 / double(numDescs));
    m_WorldCenter = {};
    m_ClosestInstance = {};
    double closest = 50.0 * 1000.0;

    uint32_t noDataCounter = 0;
    const uint32_t noDataLimit = 10000;
    for (uint32_t i = 0; i < numDescs; ++i)
    {
        if (noDataCounter >= noDataLimit)
            break; // fuse

        TlasInstanceDesc& desc = data[i];
        if (desc.virtualGpuAddress == 0)
        {
            ++noDataCounter;
            continue;
        }

        uint32_t hash = HashUint64(desc.virtualGpuAddress);
        double3 instancePos = double3(desc.Transform[0][3], desc.Transform[1][3], desc.Transform[2][3]);
        m_WorldCenter += instancePos * posWeight;

        if (cameraPos)
        {
            double distance = length(*cameraPos - instancePos);
            if (closest > distance)
            {
                closest = distance;
                m_ClosestInstance = instancePos;
            }
        }

        auto iterator = m_HashToBlasDesc.find(hash);
        if (iterator == m_HashToBlasDesc.end())
            continue; // ignore unknown

        ViewerBlasDesc& blas = iterator->second;
        blas.lastReferenceFrameId = m_UpdateCounter;
    }
    ++m_UpdateCounter;
}

void BlasTracker::CleanUp()
{
    constexpr uint32_t okayNumberOfTrackedBlases = 10 * 1000;
    const uint32_t conservativeNumberOfInstances = m_MaxInstanceCount + (m_MaxInstanceCount >> 3);
    if (GetBlasNumber() < okayNumberOfTrackedBlases || (conservativeNumberOfInstances > GetBlasNumber()))
        return;

    m_WorkerVector.clear();
    for (auto& iterator : m_HashToBlasDesc)
        m_WorkerVector.push_back(iterator.second);
    std::sort(m_WorkerVector.begin(), m_WorkerVector.end(), [](ViewerBlasDesc& a, ViewerBlasDesc& b) {return (a.lastReferenceFrameId) < (b.lastReferenceFrameId); });

    for (uint32_t i = 0; i < uint32_t(m_WorkerVector.size()) && m_MaxInstanceCount < GetBlasNumber(); ++i)
    {
        if ((m_WorkerVector[i].buildFlags & AS_BUILD_FLAG_ALLOW_UPDATE) == 0)
            continue; // get rid of stale dynamic geometry first
        m_HashToBlasDesc.erase(m_WorkerVector[i].hash);
    }

    for (uint32_t i = 0; i < uint32_t(m_WorkerVector.size()) && m_MaxInstanceCount < GetBlasNumber(); ++i)
    {
        if ((m_WorkerVector[i].buildFlags & AS_BUILD_FLAG_ALLOW_UPDATE) != 0)
            continue; // go through stale static geometry if above was not enough
        m_HashToBlasDesc.erase(m_WorkerVector[i].hash);
    }
}