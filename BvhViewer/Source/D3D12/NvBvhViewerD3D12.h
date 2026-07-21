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
#include "NvBvhViewerDispatchTableD3D12.h"

class NvBvhViewerD3D12 : public NvBvhViewerCommon
{
public:
    struct InitDesc
    {
        ID3D12Device* device;
        D3D12DispatchTable dispatchTable;

        NvBvhViewerCommon::PFN_LogMessageCallback logMessageCallback;
        NvBvhViewerCommon::PFN_WindowCloseCallback windowCloseCallback;
        NvBvhViewerCommon::PFN_WindowResizeCallback windowResizeCallback;
        void* callbackUserData;

        uint32_t outputWidth;
        uint32_t outputHeight;
        uint32_t numBufferedFrames;

        // Multiple visualization modes rely on accessing instance description buffer.
        // It can't be used in the default deferred mode if caller application modifies the buffer after TLAS is built.
        // Inline rendering appends viewer's render commands to the caller's command list on TLAS build, making compute state of it dirty.
        // Host has to restore the pipeline state if build RTAS functions return true.
        bool enableInlineRendering;
        bool enableClusterGeometryTracking;
    };

    struct OnEclResult
    {
        ID3D12CommandList* outCommandList; // in case of deferred rendering append this list to the host's ECL, or call it in a separate ECL right after it.
        ID3D12Fence* outFence;             // if not null, signal this fence after ECL
        uint64_t outSignalValue;           // value for the fence
    };

public:
    // main
    bool Initialize(const InitDesc& initDesc); // init viewer settings, enable cpu-side tracking of RTAS calls
    bool Launch();                             // create resources and window, launch visualization
    void Shutdown();                           // destroy window and resources
    bool Deinitialize();                       // erase viewer settings

    // functions to call right After corresponding D3D12 API calls. Return value is explained in InitDesc::enableInlineRendering
    bool OnBuildRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc);
    bool OnBuildRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const void* pParams); // nvapi
    bool OnBuildRaytracingPartitionedTlasIndirect(ID3D12GraphicsCommandList4* commandList, const void* pParams); // nvapi
    bool OnExecuteMultiIndirectClusterOperation(ID3D12GraphicsCommandList4* commandList, const void* pParams); // nvapi
    void OnCopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS dst, D3D12_GPU_VIRTUAL_ADDRESS src, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode);

    // call reght Before ECL, process OnEclResult, refer to the comments for OnEclResult fields
    OnEclResult OnExecuteCommandLists(ID3D12CommandQueue* inCommandQueue, ID3D12CommandList* const* inCommandLists, uint32_t inNumCommandLists);

    //  call this function to mark a logical frame. E.g. inside present of a real non-generated frame.
    void OnNewFrame();

    // can be used to resize window programmatically, but mainly here to support window messages
    void ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight) final;

    // state
    bool IsInitialized() const { return m_Initialized; };
    bool IsActive() const { return m_BackendInitialized; };

    // helpers for black-box integration
    bool IsInternalQueue(const ID3D12CommandQueue* commandQueue) const;

private:
    static uint32_t PresentThread(void* parameter);

private:
    struct InternalContext* m_Context = nullptr;
    bool m_BackendInitialized = false;
    bool m_Initialized = false;
    bool m_Exit = false;
};