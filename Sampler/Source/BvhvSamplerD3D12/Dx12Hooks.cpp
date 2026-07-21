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

#include <atomic>
#include "Dx12Hooks.h"
#include "Dx12PipelineComputeStateTracker.h"
#include "Connection.h"
#include "LogMessage.h"

extern Dx12Layer g_Layer;
extern SamplerConnection g_Connection;

PipelineComputeStateTracker g_StateTracker = {};

inline bool AllowTrackingCall()
{
    return g_Layer.m_BvhViewer.IsActive() && g_Layer.m_BvhViewer.IsPresentThread() == false;
}

BYTE* FunctionHook::CreateHookApi(const wchar_t* moduleName, const char* functionName, BYTE* dst)
{
    if (m_Jmp != nullptr)
        return m_Jmp;

    if (g_MinHookState.load(std::memory_order_relaxed) == false)
    {
        MH_Initialize();
        g_MinHookState.store(true, std::memory_order_relaxed);
    }
    MH_CreateHookApiEx(moduleName, functionName, dst, (void**)&m_Jmp, (void**)&m_Src);
    return m_Jmp;
}

BYTE* FunctionHook::CreateHook(BYTE* src, BYTE* dst)
{
    if (m_Jmp != nullptr)
        return m_Jmp;

    if(g_MinHookState.load(std::memory_order_relaxed) == false)
    {
        MH_Initialize();
        g_MinHookState.store(true, std::memory_order_relaxed);
    }
    m_Src = src;
    
    MH_STATUS status = MH_CreateHook(src, dst, (void**)&m_Jmp);
    if(status != MH_OK)
    {
        auto it = FunctionHook::g_Hooks.find(uintptr_t(src));
        if(it != FunctionHook::g_Hooks.end())
            m_Jmp = (BYTE*)it->second;
    }
    else
    {
        FunctionHook::g_Hooks.insert(std::make_pair(uintptr_t(src), uintptr_t(m_Jmp)));
    }

    return m_Jmp;
}

void FunctionHook::EnableHook()
{
    if (m_Jmp == nullptr || m_IsEnabled)
        return;
    MH_EnableHook(m_Src);
    m_IsEnabled = true;
}

void FunctionHook::DisableHook()
{
    if (m_Jmp == nullptr || !m_IsEnabled)
        return;
    MH_DisableHook(m_Src);
    m_IsEnabled = false;
}

void FunctionHook::ReleaseHook()
{
    if (m_Jmp == nullptr)
        return;
    MH_RemoveHook(m_Src);
    m_Src = m_Jmp = nullptr;
    m_IsEnabled = false;
}

void ReleaseAllHooks()
{
    if(FunctionHook::g_MinHookState.load(std::memory_order_relaxed) == false)
        return;
    MH_Uninitialize();
    FunctionHook::g_Hooks.clear();
    FunctionHook::g_MinHookState.store(false);
}

#define HOOK_BODY(returnType, interfaceName, functionName, ...) \
    interfaceName##_##functionName##_FP interfaceName##_##functionName##_S = nullptr;\
    returnType STDMETHODCALLTYPE interfaceName##_##functionName##_H(__VA_ARGS__)

#define NVAPI_HOOK_BODY(functionName, ...) \
    functionName##_FP functionName##_S = nullptr;\
    NvAPI_Status __cdecl functionName##_H(__VA_ARGS__)

inline bool IsNestedCall(int& counter) 
{
    return counter > 1;
}

struct ScopedCounter
{
    ScopedCounter(int& inCounter) : counter(inCounter)
    {
        ++counter;
    };

    ~ScopedCounter()
    {
        --counter;
    }
    int& counter;
};

#define _D3D12_HOOK_BEGIN(returnType, interfaceName, functionName, ...)\
    returnType STDMETHODCALLTYPE interfaceName##_##functionName##_H(__VA_ARGS__)\
{\
    thread_local static int nestedCounter = 0; /* working around d3d12 debug layer duplicating command list calls*/\
    ScopedCounter scopedCounter(nestedCounter);

#define D3D12_HOOK_BEGIN(returnType, interfaceName, functionName, ...) \
    interfaceName##_##functionName##_FP interfaceName##_##functionName##_S = nullptr;\
    _D3D12_HOOK_BEGIN(returnType, interfaceName, functionName, __VA_ARGS__)

#define D3D12_HOOK_END }

#define D3D12_CMDLIST_HOOK_BEGIN(returnType, interfaceName, functionName, ...) \
    interfaceName##_##functionName##_FP interfaceName##_##functionName##_S[c_NumCmdListTypes] = {};\
    FunctionHook interfaceName##_##functionName##_FH[c_MaxHookPermutations] = {};\
    HookPermutation<interfaceName##_##functionName##_FP> interfaceName##_##functionName##_Ctx[c_MaxHookPermutations] = {};\
    std::atomic_uint32_t interfaceName##_##functionName##_CtxCount = 0;\
    template<uint32_t... ids>\
    static constexpr std::array<interfaceName##_##functionName##_FP, sizeof...(ids)> Make##interfaceName##_##functionName##Hooks(std::integer_sequence<uint32_t, ids...>)\
    {\
        return { &interfaceName##_##functionName##_H<ids>... };\
    }\
    static constexpr auto interfaceName##_##functionName##_Hooks = Make##interfaceName##_##functionName##Hooks(std::make_integer_sequence<uint32_t, c_MaxHookPermutations>{});\
    uintptr_t Get##interfaceName##_##functionName##Hook(uint32_t id)\
    {\
        return id < c_MaxHookPermutations ? (uintptr_t)interfaceName##_##functionName##_Hooks[id] : 0;\
    }\
    static thread_local int interfaceName##_##functionName##_NestedCounter = 0; /* working around d3d12 debug layer duplicating command list calls, shared by all permutations*/\
    template<uint32_t id>\
    returnType STDMETHODCALLTYPE interfaceName##_##functionName##_H(__VA_ARGS__)\
{\
    int& nestedCounter = interfaceName##_##functionName##_NestedCounter;\
    ScopedCounter scopedCounter(nestedCounter);\
    [[maybe_unused]] const HookPermutation<interfaceName##_##functionName##_FP>& hookPermutation = interfaceName##_##functionName##_Ctx[id];

#pragma region [LoadLibraryHooks]
HOOK_BODY(HMODULE, kernel32, LoadLibraryA, _In_ LPCSTR lpLibFileName)
{
    HMODULE hModule = kernel32_LoadLibraryA_S(lpLibFileName);
    g_Layer.LoadLibraryCommon(lpLibFileName, 0, hModule);
    return hModule;
}

HOOK_BODY(HMODULE, kernel32, LoadLibraryW, _In_ LPCWSTR lpLibFileName)
{
    HMODULE hModule = kernel32_LoadLibraryW_S(lpLibFileName);
    g_Layer.LoadLibraryCommon(0, lpLibFileName, hModule);
    return hModule;
}

HOOK_BODY(HMODULE, kernel32, LoadLibraryExA, _In_ LPCSTR lpLibFileName, _Reserved_ HANDLE hFile, _In_ DWORD dwFlags)
{
    HMODULE hModule = kernel32_LoadLibraryExA_S(lpLibFileName, hFile, dwFlags);
    g_Layer.LoadLibraryCommon(lpLibFileName, 0, hModule);
    return hModule;
}

HOOK_BODY(HMODULE, kernel32, LoadLibraryExW, _In_ LPCWSTR lpLibFileName, _Reserved_ HANDLE hFile, _In_ DWORD dwFlags)
{
    HMODULE hModule = kernel32_LoadLibraryExW_S(lpLibFileName, hFile, dwFlags);
    g_Layer.LoadLibraryCommon(0, lpLibFileName, hModule);
    return hModule;
}
#pragma endregion

#pragma region [DxgiHooks]
HOOK_BODY(HRESULT, DXGI, Present, IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
{
    g_Layer.OnNewFrame(This);
    return DXGI_Present_S(This, SyncInterval, Flags);
}

HOOK_BODY(HRESULT, DXGI, Present1, IDXGISwapChain1* This, UINT SyncInterval, UINT Flags, _In_  const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    g_Layer.OnNewFrame(This);
    return DXGI_Present1_S(This, SyncInterval, Flags, pPresentParameters);
}
#pragma endregion

#pragma region[Dx12Hooks]
HOOK_BODY(HRESULT, Dx12, D3D12CreateDevice, _In_opt_ IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, _In_ REFIID riid, __allowed(on_parameter) void** ppDevice)
{
    HRESULT result = Dx12_D3D12CreateDevice_S(pAdapter, MinimumFeatureLevel, riid, ppDevice);
    if (result != S_OK)
        return result;

    if(!ppDevice)
        return result;

    ID3D12Device* device = (ID3D12Device*)*ppDevice;
    IDXGIFactory4* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    DXGI_ADAPTER_DESC1 adapterDesc1 = {};

    bool check = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)) == S_OK;
    check = check ? factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)) == S_OK : check;
    check = check ? adapter->GetDesc1(&adapterDesc1) == S_OK : check;
    check = check ? (adapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 : check;

    if(check)
        g_Layer.InitializeDx12Device(device);
    if(adapter)
        adapter->Release();
    if(factory)
        factory->Release();
    return result;
}

//Compute State Tracking
HOOK_BODY(HRESULT, ID3D12Device, CreateCommandList, ID3D12Device* This, _In_  UINT nodeMask, _In_  D3D12_COMMAND_LIST_TYPE type, _In_  ID3D12CommandAllocator* pCommandAllocator, _In_opt_  ID3D12PipelineState* pInitialState, REFIID riid, _COM_Outptr_  void** ppCommandList)
{
    HRESULT result = ID3D12Device_CreateCommandList_S(This, nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList);
    if(type != D3D12_COMMAND_LIST_TYPE_COMPUTE && type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return result;

    if (AllowTrackingCall())
    {
        ID3D12GraphicsCommandList* graphicsCommandList = (ID3D12GraphicsCommandList*)(*ppCommandList);
        g_StateTracker.Open(graphicsCommandList, pInitialState);
    }
    return result;
}

D3D12_HOOK_BEGIN(void, ID3D12CommandQueue, ExecuteCommandLists, ID3D12CommandQueue* This,
    _In_  UINT NumCommandLists, _In_reads_(NumCommandLists)  ID3D12CommandList* const* ppCommandLists)
{
    if (g_Layer.m_BvhViewer.IsInternalQueue(This) || IsNestedCall(nestedCounter))
        return ID3D12CommandQueue_ExecuteCommandLists_S(This, NumCommandLists, ppCommandLists);

    NvBvhViewerD3D12::OnEclResult eclResult = g_Layer.m_BvhViewer.OnExecuteCommandLists(This, ppCommandLists, NumCommandLists);
    ID3D12CommandQueue_ExecuteCommandLists_S(This, NumCommandLists, ppCommandLists);

    // ipovarov: initial idea was to append viewer's cmdlist to ECL above, but split barriers tracking scope is ECL wide,
    // thus when used may result in d3d debug layer errors
    // having a dedicated ECL allows to have clean validation layer, without itroducing explicit resource state tracking
    if(eclResult.outCommandList)
        ID3D12CommandQueue_ExecuteCommandLists_S(This, 1, &eclResult.outCommandList);

    if (eclResult.outFence)
        This->Signal(eclResult.outFence, eclResult.outSignalValue);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(HRESULT, ID3D12GraphicsCommandList, Close, ID3D12GraphicsCommandList* This)
{
    HRESULT result = hookPermutation.native(This);
    if (IsNestedCall(nestedCounter)) return result;
    g_StateTracker.Close(This);
    return result;
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(HRESULT, ID3D12GraphicsCommandList, Reset, ID3D12GraphicsCommandList* This, _In_  ID3D12CommandAllocator* pAllocator, _In_opt_  ID3D12PipelineState* pInitialState)
{
    HRESULT result = hookPermutation.native(This, pAllocator, pInitialState);
    if (IsNestedCall(nestedCounter)) return result;
    if (AllowTrackingCall() && SUCCEEDED(result))
        g_StateTracker.Open(This, pInitialState);
    return result;
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRootSignature, ID3D12GraphicsCommandList* This,
    _In_opt_  ID3D12RootSignature* pRootSignature)
{
    hookPermutation.native(This, pRootSignature);
    if (IsNestedCall(nestedCounter)) return;
    if (AllowTrackingCall())
        g_StateTracker.SetComputeRootSignature(This, pRootSignature);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRootDescriptorTable, ID3D12GraphicsCommandList* This,
    _In_  UINT RootParameterIndex, _In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    hookPermutation.native(This, RootParameterIndex, BaseDescriptor);
    if (IsNestedCall(nestedCounter)) 
        return;
    if (AllowTrackingCall())
        g_StateTracker.UpdateDescriptorTable(This, RootParameterIndex, BaseDescriptor);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetDescriptorHeaps, ID3D12GraphicsCommandList* This,
    UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    hookPermutation.native(This, NumDescriptorHeaps, ppDescriptorHeaps);
    
    if (IsNestedCall(nestedCounter))
        return;
    
    if (AllowTrackingCall())
        g_StateTracker.UpdateDescriptorHeaps(This, NumDescriptorHeaps, ppDescriptorHeaps);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetPipelineState, ID3D12GraphicsCommandList* This, _In_  ID3D12PipelineState* pPipelineState)
{
    hookPermutation.native(This, pPipelineState);

    if (IsNestedCall(nestedCounter))
        return;

    if (AllowTrackingCall())
        g_StateTracker.UpdatePipelineState(This, pPipelineState);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList4, SetPipelineState1, ID3D12GraphicsCommandList* This, _In_  ID3D12StateObject* pStateObject)
{
    hookPermutation.native(This, pStateObject);

    if (IsNestedCall(nestedCounter))
        return;

    if (AllowTrackingCall())
        g_StateTracker.UpdatePipelineState(This, pStateObject);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRoot32BitConstant, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  UINT SrcData, _In_  UINT DestOffsetIn32BitValues)
{
    hookPermutation.native(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
    if (IsNestedCall(nestedCounter)) return;
    if (AllowTrackingCall())
        g_StateTracker.UpdateConstant(This, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRoot32BitConstants, ID3D12GraphicsCommandList* This,
    _In_  UINT RootParameterIndex, _In_  UINT Num32BitValuesToSet, _In_  const void* pSrcData, _In_  UINT DestOffsetIn32BitValues)
{
    hookPermutation.native(This, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
    if (IsNestedCall(nestedCounter)) return;
    for(uint32_t i = 0; i < Num32BitValuesToSet && AllowTrackingCall(); ++i)
        g_StateTracker.UpdateConstant(This, RootParameterIndex, ((const uint32_t*)(pSrcData))[i], DestOffsetIn32BitValues + i);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRootConstantBufferView, ID3D12GraphicsCommandList* This,
    _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    hookPermutation.native(This, RootParameterIndex, BufferLocation);
    if (IsNestedCall(nestedCounter)) return;
    if (AllowTrackingCall())
        g_StateTracker.UpdateConstantBufferView(This, RootParameterIndex, BufferLocation);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRootShaderResourceView, ID3D12GraphicsCommandList* This,
    _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    hookPermutation.native(This, RootParameterIndex, BufferLocation);
    if (IsNestedCall(nestedCounter)) return;
    if (AllowTrackingCall())
        g_StateTracker.UpdateShaderResourceView(This, RootParameterIndex, BufferLocation);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList, SetComputeRootUnorderedAccessView, ID3D12GraphicsCommandList* This,
    _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    hookPermutation.native(This, RootParameterIndex, BufferLocation);

    if (IsNestedCall(nestedCounter)) return;

    if (AllowTrackingCall())
        g_StateTracker.UpdateUnorderedAccessView(This, RootParameterIndex, BufferLocation);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList4, CopyRaytracingAccelerationStructure, ID3D12GraphicsCommandList4* This,
    _In_  D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData, _In_  D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData, _In_  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode)
{
    hookPermutation.native(This, DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
    if (IsNestedCall(nestedCounter)) return;
    g_Layer.m_BvhViewer.OnCopyRaytracingAccelerationStructure(DestAccelerationStructureData, SourceAccelerationStructureData, Mode);
}
D3D12_HOOK_END

D3D12_CMDLIST_HOOK_BEGIN(void, ID3D12GraphicsCommandList4, BuildRaytracingAccelerationStructure, ID3D12GraphicsCommandList4* This,
    _In_  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc, _In_  UINT NumPostbuildInfoDescs,
    _In_reads_opt_(NumPostbuildInfoDescs)  const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pPostbuildInfoDescs)
{
    hookPermutation.native(This, pDesc, NumPostbuildInfoDescs, pPostbuildInfoDescs);

    if (IsNestedCall(nestedCounter))
        return;

    if(g_Layer.m_BvhViewer.IsInitialized() == false)
        return;

    static bool isInlineRendering = g_Connection.GetViewerDesc().inlineRendering;
    const bool isTlas = pDesc->Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    if (isInlineRendering && isTlas ? g_StateTracker.CanRestore(This) : true)
    {
        const bool hasAppendedCommands = g_Layer.m_BvhViewer.OnBuildRaytracingAccelerationStructure(This, pDesc);
        if(hasAppendedCommands)
            g_StateTracker.Restore(This);
    }
}
D3D12_HOOK_END
#pragma endregion

#pragma region[NvApiHooks]
NVAPI_HOOK_BODY(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS* pParams)
{
    NvAPI_Status result = NvAPI_D3D12_BuildRaytracingAccelerationStructureEx_S(pCommandList, pParams);
    if (result != NVAPI_OK)
        return result;

    static bool isInlineRendering = g_Connection.GetViewerDesc().inlineRendering;
    const bool isTlas = pParams->pDesc->inputs.type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    if (isInlineRendering && isTlas ? g_StateTracker.CanRestore(pCommandList) : true)
    {
        const bool hasAppendedCommands = g_Layer.m_BvhViewer.OnBuildRaytracingAccelerationStructure(pCommandList, pParams);
        if (hasAppendedCommands)
            g_StateTracker.Restore(pCommandList);
    }
    return result;
}

NVAPI_HOOK_BODY(NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS* pParams)
{
    NvAPI_Status result = NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect_S(pCommandList, pParams);
    if (result != NVAPI_OK)
        return result;

    static bool isInlineRendering = g_Connection.GetViewerDesc().inlineRendering;
    if ((g_StateTracker.CanRestore(pCommandList) && g_Connection.GetViewerDesc().inlineRendering) || !isInlineRendering)
    {
        const bool hasAppendedCommands = g_Layer.m_BvhViewer.OnBuildRaytracingPartitionedTlasIndirect(pCommandList, pParams);
        if (hasAppendedCommands)
            g_StateTracker.Restore(pCommandList);
    }

    return result;
}

NVAPI_HOOK_BODY(NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS* pParams)
{
    NvAPI_Status result = NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation_S(pCommandList, pParams);
    if (result != NVAPI_OK)
        return result;

    if(g_StateTracker.CanRestore(pCommandList) == false)
        return result;

    bool hasAppendedCommands = g_Layer.m_BvhViewer.OnExecuteMultiIndirectClusterOperation(pCommandList, pParams);
    if(hasAppendedCommands)
        g_StateTracker.Restore(pCommandList);

    return result;
}

NVAPI_HOOK_BODY(NvAPI_D3D12_SetAsyncFrameMarker, __in ID3D12CommandQueue* pCommandQueue, __in NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
    if(pSetAsyncFrameMarkerParams->markerType == NV_LATENCY_MARKER_TYPE::PRESENT_START)
    { // this marker has to be set before application present
        g_Layer.m_ReflexMarkerState.test_and_set(std::memory_order_acquire);
        g_Layer.m_BvhViewer.OnNewFrame();
    }
    return NvAPI_D3D12_SetAsyncFrameMarker_S(pCommandQueue, pSetAsyncFrameMarkerParams);
}
#pragma endregion