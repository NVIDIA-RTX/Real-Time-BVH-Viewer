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

enum class ResourceFormats
{
    Unknown,
    R32_Uint,
    Rgb32_Uint,
    R8_Uint,
    Rgb10A2_Unorm,
    Count
};

enum class BufferGroup
{
    PerTlas,
    PerTlasPerFrame,
    PerFrame,
    Unknown,
};

enum class BvhViewerResources
{
    //images
    OutputTexture,

    // internal dynamic buffers
    InstanceDescInternal,
    BlasHashes,
    BlasDescs,
    GeometryDescs,
    ReadbackBuffer,
    UploadBuffer,
    IndirectOpsArgs,

    // internal static buffers
    FeedbackBuffer,
    ConstantBuffer,
    IndirectOpsCountInternal,

    //push constants
    RenderPushConstants,
    PtlasIndirectPushConstants,

#if defined( VIEWER_API_D3D12 )
    // multi indirect. all root
    MiPushConstants,
    MiBlasBuildPushConstants,
    MiSmallArgBuffer,
    MiArgBuffer,
    MiCountBufferInternal,

    MiOriginalCountBuffer,       // buffer as received from the host
    MiOriginalInputBuffer,       //
    MiOriginalDestinationBuffer, //

    MiResolvedCountBuffer,       // buffer resolved from indirected original in case of indirection
    MiResolvedInputBuffer,       //
    MiResolvedDestinationBuffer, //

    MiTemplateKeysBuffer,
    MiTemplateDataBuffer,

    MiClasKeysBuffer,
    MiClasDataBuffer,

    MiBlasKeysBuffer,
    MiBlasDataBuffer,

    MiStart = MiPushConstants,
    MiEnd = MiBlasDataBuffer,
#endif // VIEWER_API_D3D12

    //external
    Tlas,
    InstanceDescExternal,

    IndirectOpsBufferExternal,
    IndirectOpsCountExternal,
    IndirectOpsData,

    GpuVendorReserved,
    Count,

    // constants
    FirstBuffer = InstanceDescInternal,
    DynamicBuffersBegin = InstanceDescInternal,
    StaticBuffersBegin = FeedbackBuffer,

#if defined( VIEWER_API_D3D12 )
    DynamicBuffersEnd = IndirectOpsArgs,
#else
    DynamicBuffersEnd = UploadBuffer,
#endif

#if defined( VIEWER_API_D3D12 )
    StaticBuffersEnd = IndirectOpsCountInternal,
#else
    StaticBuffersEnd = ConstantBuffer,
#endif

    NumDynamicBuffers = DynamicBuffersEnd - DynamicBuffersBegin + 1,
    NumStaticBuffers = StaticBuffersEnd - StaticBuffersBegin + 1,
    NumBuffers = NumDynamicBuffers + NumStaticBuffers,
};

inline BvhViewerResources GetBufferResourceFromLinearId(uint32_t linearId)
{
    const uint32_t dynamicBuffersBegin = uint32_t(BvhViewerResources::DynamicBuffersBegin);
    const uint32_t numDynamicBuffers = uint32_t(BvhViewerResources::NumDynamicBuffers);
    const uint32_t staticBuffersBegin = uint32_t(BvhViewerResources::StaticBuffersBegin);
    return BvhViewerResources(linearId >= numDynamicBuffers ? staticBuffersBegin + linearId - numDynamicBuffers : dynamicBuffersBegin + linearId);
}

inline uint32_t GetBufferResourceLinearId(BvhViewerResources resource)
{
    const uint32_t numDynamicBuffers = uint32_t(BvhViewerResources::NumDynamicBuffers);
    const uint32_t dynamicBuffersBegin = uint32_t(BvhViewerResources::DynamicBuffersBegin);
    const uint32_t gap = uint32_t(BvhViewerResources::StaticBuffersBegin) - uint32_t(BvhViewerResources::DynamicBuffersEnd) - 1;
    uint32_t id = uint32_t(resource) - dynamicBuffersBegin;
    return id > numDynamicBuffers ? id - gap : id;
}

inline BufferGroup GetBufferGroup(BvhViewerResources resource)
{
    BufferGroup bufferGroup = BufferGroup::Unknown;
    {
        switch (resource)
        {
        case BvhViewerResources::InstanceDescInternal:
        case BvhViewerResources::UploadBuffer:
            bufferGroup = BufferGroup::PerTlas;
            break;
        case BvhViewerResources::FeedbackBuffer:
        case BvhViewerResources::ReadbackBuffer:
        case BvhViewerResources::BlasDescs:
        case BvhViewerResources::GeometryDescs:
        case BvhViewerResources::BlasHashes:
        case BvhViewerResources::IndirectOpsArgs:
        case BvhViewerResources::IndirectOpsCountInternal:
        case BvhViewerResources::ConstantBuffer:
            bufferGroup = BufferGroup::PerTlasPerFrame;
            break;
        default: break;
        }
    }
    return bufferGroup;
}

inline uint32_t GetBufferResourceCount(BvhViewerResources bufferResource, const uint32_t numBufferedFrames)
{
    const BufferGroup group = GetBufferGroup(bufferResource);
    switch (group)
    {
    case BufferGroup::PerTlas:          return c_MaxTlasPerFrame;
    case BufferGroup::PerTlasPerFrame:  return numBufferedFrames * c_MaxTlasPerFrame;
    case BufferGroup::PerFrame:         return numBufferedFrames;
    case BufferGroup::Unknown:          return 0;
    }
    return 0;
}