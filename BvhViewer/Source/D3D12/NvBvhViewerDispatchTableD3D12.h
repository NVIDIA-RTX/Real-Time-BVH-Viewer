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
#include <stdint.h>
#include <dxgi1_6.h>
#include <d3d12.h>

// Why dispatch table?
// In case of blackbox integration it is now possible to explicitly specify the functions called by the viewer
// and bypass wrapped interfaces, patched vtables and hooks, so viewer calls are not intercepted by the host,
// which can get confused by another rendering pipeline running alongside the main application

#define BVHV_DECLARE_FUNCTION_TYPE(vtableOffset, returnType, interfaceType, function, ...) typedef returnType (__stdcall * function##_FP)(interfaceType* This __VA_OPT__(,) __VA_ARGS__);
#define BVHV_DECLARE_FUNCTION_VAR(vtableOffset, returnType, interfaceType, function, ...) function##_FP function;
#define BVHV_DECLARE_ENUM(vtableOffset, returnType, interfaceType, function, ...) function,
#define BVHV_DECLARE_OFFSETS(vtableOffset, returnType, interfaceType, function, ...) vtableOffset,

#define BVHV_CMD_LIST_FUNCTIONS(MACRO)\
MACRO(9 , HRESULT, ID3D12GraphicsCommandList, Close)\
MACRO(10, HRESULT, ID3D12GraphicsCommandList, Reset, _In_  ID3D12CommandAllocator* pAllocator, _In_opt_  ID3D12PipelineState* pInitialState)\
MACRO(25, void, ID3D12GraphicsCommandList, SetPipelineState, _In_  ID3D12PipelineState* pPipelineState)\
MACRO(28, void, ID3D12GraphicsCommandList, SetDescriptorHeaps, UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps)\
MACRO(29, void, ID3D12GraphicsCommandList, SetComputeRootSignature, _In_opt_  ID3D12RootSignature* pRootSignature)\
MACRO(31, void, ID3D12GraphicsCommandList, SetComputeRootDescriptorTable, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)\
MACRO(35, void, ID3D12GraphicsCommandList, SetComputeRoot32BitConstants, _In_  UINT RootParameterIndex, _In_  UINT Num32BitValuesToSet, _In_  const void* pSrcData, _In_  UINT DestOffsetIn32BitValues)\
MACRO(37, void, ID3D12GraphicsCommandList, SetComputeRootConstantBufferView, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)\
MACRO(39, void, ID3D12GraphicsCommandList, SetComputeRootShaderResourceView, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)\
MACRO(41, void, ID3D12GraphicsCommandList, SetComputeRootUnorderedAccessView, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)\
MACRO(75, void, ID3D12GraphicsCommandList4, SetPipelineState1, _In_  ID3D12StateObject* pStateObject)\

#define BVHV_CMD_QUEUE_FUNCTIONS(MACRO)\
MACRO(10, void, ID3D12CommandQueue, ExecuteCommandLists, _In_  UINT NumCommandLists, _In_reads_(NumCommandLists)  ID3D12CommandList* const* ppCommandLists)\
MACRO(14, HRESULT, ID3D12CommandQueue, Signal, ID3D12Fence* pFence, UINT64 Value)\
MACRO(15, HRESULT, ID3D12CommandQueue, Wait, ID3D12Fence* pFence, UINT64 Value)

#define BVHV_DXGI_FUNCTIONS(MACRO)\
MACRO(15, HRESULT, IDXGIFactory2, CreateSwapChainForHwnd, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)\
MACRO(8 , HRESULT, IDXGISwapChain, Present, UINT SyncInterval, UINT Flags)

struct ID3D12GraphicsCommandList_DT
{
public:
    enum class Layout : uint32_t
    {
        BVHV_CMD_LIST_FUNCTIONS(BVHV_DECLARE_ENUM)
        Count
    };

    static constexpr uint32_t VtableOffsets[uint32_t(Layout::Count)] =
    {
        BVHV_CMD_LIST_FUNCTIONS(BVHV_DECLARE_OFFSETS)
    };

public:
    BVHV_CMD_LIST_FUNCTIONS(BVHV_DECLARE_FUNCTION_TYPE);
    BVHV_CMD_LIST_FUNCTIONS(BVHV_DECLARE_FUNCTION_VAR);

public:
    bool Init(ID3D12GraphicsCommandList4* list);
    bool SetDispatchTable(uintptr_t* layout);
};

struct ID3D12CommandQueue_DT
{
public:
    enum class Layout : uint32_t
    {
        BVHV_CMD_QUEUE_FUNCTIONS(BVHV_DECLARE_ENUM)
        Count
    };
public:
    BVHV_CMD_QUEUE_FUNCTIONS(BVHV_DECLARE_FUNCTION_TYPE);
    BVHV_CMD_QUEUE_FUNCTIONS(BVHV_DECLARE_FUNCTION_VAR);

public:
    bool Init(ID3D12CommandQueue* queue);
    bool SetDispatchTable(uintptr_t* layout);
};

struct DXGI_DT
{
public:
    enum class Layout : uint32_t
    {
        BVHV_DXGI_FUNCTIONS(BVHV_DECLARE_ENUM)
        Count
    };
public:

    BVHV_DXGI_FUNCTIONS(BVHV_DECLARE_FUNCTION_TYPE);
    BVHV_DXGI_FUNCTIONS(BVHV_DECLARE_FUNCTION_VAR);

public:
    bool Init(ID3D12CommandQueue* graphicsQueue);
    bool SetDispatchTable(uintptr_t* layout);
};

struct D3D12DispatchTable
{
private:

    static constexpr uint32_t c_NumCommandListTypes = uint32_t(D3D12_COMMAND_LIST_TYPE_COMPUTE) + 1;

private:
    ID3D12GraphicsCommandList_DT CmdList[c_NumCommandListTypes];

public:
    inline ID3D12GraphicsCommandList_DT& GetCmdList(D3D12_COMMAND_LIST_TYPE type)
    { return CmdList[uint32_t(type)]; }

    bool Init(ID3D12Device* device);
    bool IsInitialized() const;

public:
    ID3D12CommandQueue_DT CmdQueue;
    DXGI_DT DXGI;
};