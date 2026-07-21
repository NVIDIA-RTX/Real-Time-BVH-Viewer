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

enum class DescriptorTypes
{
    Tlas,
    TypedBuffer,
    RawBuffer,
    RawBufferStorage,
    StructuredBuffer,
    StorageTexture,
    StorageTypedBuffer,
    StorageStructuredBuffer,
    ConstantBuffer,
    PushConstants,
    Count
};

struct DescriptorBindingDesc
{
    BvhViewerResources resource;
    DescriptorTypes type;
    ResourceFormats format;
    uint64_t stride;
};

struct ShaderDesc
{
    void* code = nullptr;
    uint64_t codeSize = 0;
};

struct CommonPipelineDesc
{
    std::vector< DescriptorBindingDesc > bindings;
    std::vector< ShaderDesc > shaderPermutations;
    const wchar_t* debugName;
};

enum class Pipelines
{
    InstanceCopy,
    BvhVisualization,
#if defined( VIEWER_API_D3D12 )
    MiResolveIndirection,
    MiParseMove,
    MiParseClasBuild,
    MiParseInstantiation,
    MiBlasBuildDispatcher,
    MiBlasBuildParser,
    IndirectOpsDispatcher,
#endif // VIEWER_API_D3D12 
    IndirectOpsParser,
    Count
};

const CommonPipelineDesc g_ViewerPipelineDescs[uint32_t(Pipelines::Count)] =
{
    { // InstanceCopy
        {
            #if defined( VIEWER_API_D3D12 ) // in vk buffer_reference is used to read this buffer. binding is not required
            { BvhViewerResources::InstanceDescExternal, DescriptorTypes::StructuredBuffer, ResourceFormats::Unknown, sizeof(TlasInstanceDesc) },
            #endif
            { BvhViewerResources::InstanceDescInternal, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(TlasInstanceDesc) },
            { BvhViewerResources::RenderPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(PushConstantsLayout) },
        },
        #if defined( VIEWER_API_VK )
        {
            { (void*)g_InstanceDescCopy_spirv, sizeof(g_InstanceDescCopy_spirv) },
        },
        #else // D3D12
        {
            { (void*)g_InstanceDescCopy_dxil, sizeof(g_InstanceDescCopy_dxil) },
        },
        #endif // VIEWER_API_VK
        L"InstanceCopy_Compute",
    },
    { // BvhVisualization
        {
            { BvhViewerResources::Tlas, DescriptorTypes::Tlas, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::BlasHashes, DescriptorTypes::TypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::BlasDescs, DescriptorTypes::TypedBuffer, ResourceFormats::Rgb32_Uint, 0 },
            { BvhViewerResources::GeometryDescs, DescriptorTypes::TypedBuffer, ResourceFormats::R8_Uint, 0 },
            { BvhViewerResources::InstanceDescInternal, DescriptorTypes::StructuredBuffer, ResourceFormats::Unknown, sizeof(TlasInstanceDesc) },

            { BvhViewerResources::OutputTexture, DescriptorTypes::StorageTexture, ResourceFormats::Rgb10A2_Unorm, 0 },
            { BvhViewerResources::FeedbackBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(FeedbackBufferLayout) },

            #if defined( VIEWER_API_D3D12 )
            { BvhViewerResources::MiBlasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiBlasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClusteredBlasDescGPU) },
            #endif

            #if defined( VIEWER_API_D3D12 )
            { BvhViewerResources::GpuVendorReserved, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, 0 },
            #endif

            { BvhViewerResources::ConstantBuffer, DescriptorTypes::ConstantBuffer, ResourceFormats::Unknown, sizeof(ConstantBufferLayout) },
        },
        #if defined( VIEWER_API_VK )
        {
            { (void*)g_BvhVisualization_spirv_Base, sizeof(g_BvhVisualization_spirv_Base) },                    // no LSS, no Position fetch
            { (void*)g_BvhVisualization_spirv_LSS, sizeof(g_BvhVisualization_spirv_LSS) },                      // with LSS, requires spirv1.4 + LSS ext on device creation
            { (void*)g_BvhVisualization_spirv_PositionFetch, sizeof(g_BvhVisualization_spirv_PositionFetch) },  // with Position Fetch, requires VK_KHR_ray_tracing_position_fetch ext on device creation
            { (void*)g_BvhVisualization_spirv_Full, sizeof(g_BvhVisualization_spirv_Full) },                    // LSS + Position Fetch
        },
        #else // D3D12
        {
            { (void*)g_BvhVisualization_dxil_Base, sizeof(g_BvhVisualization_dxil_Base) },

            #if defined(BVHVIEWER_ENABLE_NVAPI)
                { (void*)g_BvhVisualization_dxil_NV, sizeof(g_BvhVisualization_dxil_NV) },
            #else
                { (void*)g_BvhVisualization_dxil_Base, sizeof(g_BvhVisualization_dxil_Base) },
            #endif // defined(BVHVIEWER_ENABLE_NVAPI)

            #if defined(BVHVIEWER_ENABLE_AMD_AGS)
                { (void*)g_BvhVisualization_dxil_AMD, sizeof(g_BvhVisualization_dxil_AMD) },
            #else
                { (void*)g_BvhVisualization_dxil_Base, sizeof(g_BvhVisualization_dxil_Base) },
            #endif // defined(BVHVIEWER_ENABLE_AMD_AGS)
        },
        #endif // VIEWER_API_VK
        L"BvhVisualization_Compute",
    },
    //////////////////////////////////////////
    //              MultiIndirect           //
    //////////////////////////////////////////
    #if defined( VIEWER_API_D3D12 )
    {  // MultiIndirect: MultiIndirectResolveIndirection
        {
            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiOriginalCountBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiOriginalInputBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiOriginalDestinationBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiCountBufferInternal, DescriptorTypes::RawBufferStorage, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiSmallArgBuffer, DescriptorTypes::RawBufferStorage, ResourceFormats::Unknown, 0 },
        },
        {
            { (void*)g_MultiIndirectResolveIndirection_dxil, sizeof(g_MultiIndirectResolveIndirection_dxil) },
        },
        L"MultiIndirectResolveIndirection_Compute"
    },
    { // MultiIndirect: Custer object Move Parser
        {
            { BvhViewerResources::MiResolvedCountBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedInputBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedDestinationBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiTemplateKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiTemplateDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },

            { BvhViewerResources::MiClasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiClasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },

            { BvhViewerResources::MiBlasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiBlasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClusteredBlasDescGPU) },
        },
        {
            { (void*)g_MultiIndirectParseMove_dxil, sizeof(g_MultiIndirectParseMove_dxil) },
        },
        L"MultiIndirectParseMove_Compute"
    },
    { // MultiIndirect: CLAS and Template Builds Parser
        {
            { BvhViewerResources::MiResolvedCountBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedInputBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedDestinationBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiTemplateKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiTemplateDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },

            { BvhViewerResources::MiClasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiClasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },
        },
        {
            { (void*)g_MultiIndirectParseClasBuild_dxil, sizeof(g_MultiIndirectParseClasBuild_dxil) },
        },
        L"MultiIndirectClusterBuildParser_Compute"
    },
    { // MultiIndirect: Template instantiation Parser
        {
            { BvhViewerResources::MiResolvedCountBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedInputBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedDestinationBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiTemplateKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiTemplateDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },

            { BvhViewerResources::MiClasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiClasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },
        },
        {
            { (void*)g_MultiIndirectParseInstantiation_dxil, sizeof(g_MultiIndirectParseInstantiation_dxil) },
        },
        L"MultiIndirectInstantiationParser_Compute"
    },

    { // MultiIndirect: Blas from Clas build parser dispatcher. Prepare indirect dispatches
        {
            { BvhViewerResources::MiResolvedCountBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedInputBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiResolvedDestinationBuffer, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiArgBuffer, DescriptorTypes::RawBufferStorage, ResourceFormats::Unknown, 0 },

            { BvhViewerResources::MiBlasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiBlasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClusteredBlasDescGPU) },
        },
        {
            { (void*)g_MultiIndirectBlasBuildDispatcher_dxil, sizeof(g_MultiIndirectBlasBuildDispatcher_dxil) },
        },
        L"MultiIndirectBlasBuildDispatcher_Compute"
    },
    { // MultiIndirect: indirect Blas from Clas build parser
        {
            { BvhViewerResources::IndirectOpsData, DescriptorTypes::RawBuffer, ResourceFormats::Unknown, 0 },
            { BvhViewerResources::MiBlasBuildPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectBlasBuildPushConstantsLayout) },

            { BvhViewerResources::MiPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(MultiIndirectPushConstantsLayout) },

            { BvhViewerResources::MiClasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiClasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClasBuildDescGPU) },

            { BvhViewerResources::MiBlasKeysBuffer, DescriptorTypes::StorageTypedBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::MiBlasDataBuffer, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(ClusteredBlasDescGPU) },
        },
        {
            { (void*)g_MultiIndirectBlasBuildParser_dxil, sizeof(g_MultiIndirectBlasBuildParser_dxil) },
        },
        L"MultiIndirectBlasBuildParser_Compute"
    },
    //////////////////////////////////////////
    //                 PTLAS                //
    //////////////////////////////////////////
    { // PTLAS Indirect Ops Dispatcher
        {
            { BvhViewerResources::RenderPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(PushConstantsLayout) },

            { BvhViewerResources::IndirectOpsCountExternal, DescriptorTypes::RawBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::IndirectOpsBufferExternal, DescriptorTypes::StructuredBuffer, ResourceFormats::Unknown, sizeof(IndirectOpDesc) },

            { BvhViewerResources::IndirectOpsArgs, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(IndirectOpsParserArgs) },
            { BvhViewerResources::IndirectOpsCountInternal, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(uint32_t) },
        },
        {
            { (void*)g_IndirectOpsDispatcher_dxil, sizeof(g_IndirectOpsDispatcher_dxil) },
        },
        L"IndirectDispatcher_Compute",
    },
    { // PTLAS Indirect Ops Parser
        {
            { BvhViewerResources::PtlasIndirectPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(PtlasIndirectPushConstantsLayout) },
            { BvhViewerResources::IndirectOpsData, DescriptorTypes::RawBuffer, ResourceFormats::R32_Uint, 0 },
            { BvhViewerResources::InstanceDescInternal, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(TlasInstanceDesc) },
        },
        {
            { (void*)g_IndirectOpsParser_dxil, sizeof(g_IndirectOpsParser_dxil) },
        },
        L"IndirectParser_Compute",
    },
    #else // VK:
    { // PTLAS Indirect Ops parser for Vulkan
        {
            { BvhViewerResources::RenderPushConstants, DescriptorTypes::PushConstants, ResourceFormats::Unknown, sizeof(PushConstantsLayout) },
            { BvhViewerResources::InstanceDescInternal, DescriptorTypes::StorageStructuredBuffer, ResourceFormats::Unknown, sizeof(TlasInstanceDesc) },
        },
        {
            { (void*)g_IndirectOpsParserVK_spirv, sizeof(g_IndirectOpsParserVK_spirv) },
        },
        L"IndirectParserVK_Compute"
    }
    #endif
};

#if defined( VIEWER_API_D3D12 ) 
inline uint32_t GetPermutationId(Pipelines pipeline, Vendor vendor)
{
    if (pipeline != Pipelines::BvhVisualization)
        return 0;
    if (vendor == Vendor::AMD)
        return 2;
    if (vendor == Vendor::NV)
        return 1;
    return 0;
}
#else
inline uint32_t GetPermutationId(Pipelines pipeline, bool enableLss, bool enablePositionFetch)
{
    if (pipeline != Pipelines::BvhVisualization)
        return 0;
    if (enableLss && enablePositionFetch)
        return 3;
    if (!enableLss && enablePositionFetch)
        return 2;
    if (enableLss && !enablePositionFetch)
        return 1;
    return 0;
}
#endif