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
#include <atomic>
#include <stdint.h>
#include <algorithm>
#include <unordered_map>
#include <ml.h>
#include "Descs.h"

static inline uint32_t HashUint64(uint64_t a)
{
    uint32_t low = uint32_t(uint64_t(a) & 0xFFFFFFFF);
    uint32_t high = uint32_t((uint64_t(a) >> 32) & 0xFFFFFFFF);
    uint32_t hash = low ^ high;
    return hash;
}

struct BlasTracker
{
public:
    struct BufferRange { uint64_t offset; uint64_t size; };

    struct UploadDesc
    {
        void* mappedUploadBuffer;
        BufferRange outHashesRange;
        BufferRange outBlasDescRange;
        BufferRange outGeometryDescRange;
    };

    uint64_t PrepareData();
    void UploadData(UploadDesc& desc);
    void Insert(ViewerBlasDesc& blasDesc);
    void ProcessBlasCopy(uint64_t dst, uint64_t src, bool isCompaction);
    ViewerBlasDesc* GetBlasDesc(uint32_t blasHash);
    void ParseInstanceDescs(void* ptr, uint32_t numDescs, uint32_t currentMaxInstanceNum, double3* cameraPos);
    const double3* GetWorldCenter() const { return &m_WorldCenter; };
    const double3* GetWorldClosest() const { return &m_ClosestInstance; };

    inline uint32_t GetBlasNumber() const
    {
        return uint32_t(m_HashToBlasDesc.size());
    }
private:
    void CleanUp();

private:
    std::unordered_map<uint32_t, ViewerBlasDesc> m_HashToBlasDesc;
    std::vector<ViewerBlasDesc> m_WorkerVector;

    double3 m_WorldCenter = {};
    double3 m_ClosestInstance = {};
    uint32_t m_UpdateCounter = 0;
    uint32_t m_MaxInstanceCount = 50 * 1000;
    std::atomic_flag m_Lock = ATOMIC_FLAG_INIT;
};