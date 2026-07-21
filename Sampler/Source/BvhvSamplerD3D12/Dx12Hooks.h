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
#include <dxgi.h>
#include <d3d12.h>
#include <nvapi.h>
#include <map>
#include <array>
#include <atomic>
#include <utility>
#include "MinHook.h"
#include "Dx12Layer.h"
#include "Connection.h"

struct FunctionHook
{
public:
    BYTE* CreateHook(BYTE* src, BYTE* dst);
    BYTE* CreateHookApi(const wchar_t* moduleName, const char* functionName, BYTE* dst);
    void EnableHook();
    void DisableHook();
    void ReleaseHook();
    inline BYTE* Trampoline() const
    { return m_Jmp; }
    inline BYTE* Source() const
    { return m_Src; }
public:
    inline static std::map<uintptr_t, uintptr_t> g_Hooks;
    inline static std::atomic<bool> g_MinHookState = false;
    BYTE* m_Src = nullptr;
    BYTE* m_Jmp = nullptr;
    bool m_IsEnabled = false;
};

void ReleaseAllHooks();

#pragma region [MACRO HELL]
#define FUNCTION_TYPE(returnType, interfaceName, functionName, ...)\
    typedef returnType (STDMETHODCALLTYPE * interfaceName##_##functionName##_FP)(__VA_ARGS__);

#define DECLARE_HOOK(returnType, interfaceName, functionName, ...) \
    FUNCTION_TYPE(returnType, interfaceName, functionName, __VA_ARGS__) \
    extern interfaceName##_##functionName##_FP interfaceName##_##functionName##_S;\
    static FunctionHook interfaceName##_##functionName##_FH = {};\
    returnType STDMETHODCALLTYPE interfaceName##_##functionName##_H(__VA_ARGS__);

#define _SET_HOOK(interfaceName, functionName, addr)\
    interfaceName##_##functionName##_S = (interfaceName##_##functionName##_FP)interfaceName##_##functionName##_FH.CreateHook((BYTE*)addr, (BYTE*)interfaceName##_##functionName##_H);

#define _SET_DLL_HOOK(interfaceName, functionName, dllName)\
    if(interfaceName##_##functionName##_S == nullptr)\
    {\
        interfaceName##_##functionName##_S = (interfaceName##_##functionName##_FP)interfaceName##_##functionName##_FH.CreateHookApi(TEXT(#dllName), #functionName, (BYTE*)interfaceName##_##functionName##_H);\
    }

#define _ENABLE_HOOK_EX(finalName)\
    if (finalName##_S != nullptr)\
    { finalName##_FH.EnableHook(); }\

#define _ENABLE_HOOK(interfaceName, functionName)\
    _ENABLE_HOOK_EX(interfaceName##_##functionName)

#define CREATE_DLL_HOOK(interfaceName, functionName)\
    _SET_DLL_HOOK(interfaceName, functionName, #interfaceName ".dll")\
    _ENABLE_HOOK(interfaceName, functionName)

#define CREATE_DLL_HOOK_EX(interfaceName, functionName, dllName)\
    _SET_DLL_HOOK(interfaceName, functionName, dllName)\
    _ENABLE_HOOK(interfaceName, functionName)

#define CREATE_HOOK(interfaceName, functionName, addr)\
    _SET_HOOK(interfaceName, functionName, addr)\
    _ENABLE_HOOK(interfaceName, functionName)

#define DISABLE_HOOK(interfaceName, functionName) interfaceName##_##functionName##_FH.DisableHook();

#define DECLARE_NVAPI_HOOK(functionName, ...) \
    typedef NvAPI_Status(__cdecl* functionName##_FP)(__VA_ARGS__);\
    extern functionName##_FP functionName##_S;\
    static FunctionHook functionName##_FH = {};\
    NvAPI_Status __cdecl functionName##_H(__VA_ARGS__);

#define CREATE_NVAPI_HOOK(functionName, queryId, queryAddr) \
{ \
    uint32_t functionId = queryId(#functionName);\
    if(functionId != uint32_t(~0))\
    {\
        void* functionAddress = queryAddr(functionId);\
        functionName##_S = (functionName##_FP)functionName##_FH.CreateHook((BYTE*)functionAddress, (BYTE*)functionName##_H);\
        _ENABLE_HOOK_EX(functionName)\
    }\
}
#pragma endregion

#define ADD_TYPED_FUNCTION_DETOUR(id, interfaceName, functionName, functionSource) \
{ \
    auto hookIt = FunctionHook::g_Hooks.find(uintptr_t(functionSource));\
    if (hookIt == FunctionHook::g_Hooks.end())\
    {\
        const uint32_t permutationId = interfaceName##_##functionName##_CtxCount.fetch_add(1);\
        if (permutationId < c_MaxHookPermutations)\
        {\
            FunctionHook& functionHook = interfaceName##_##functionName##_FH[permutationId];\
            auto& permutation = interfaceName##_##functionName##_Ctx[permutationId];\
            permutation.native = (interfaceName##_##functionName##_FP)functionHook.CreateHook((BYTE*)functionSource, (BYTE*)Get##interfaceName##_##functionName##Hook(permutationId));\
            if (permutation.native)\
                functionHook.EnableHook();\
        }\
        else\
        {\
            LogMessage("[Dx12Hooks]", "Out of hook permutations for " #interfaceName "::" #functionName);\
        }\
    }\
}

// CMD LIST. Vtable differs depending on cmd list type, need to handle these permutations
constexpr uint32_t c_NumCmdListTypes = uint32_t(D3D12_COMMAND_LIST_TYPE_COPY) + 1;
constexpr uint32_t c_MaxHookPermutations = 8 * c_NumCmdListTypes;

template<typename FP>
struct HookPermutation
{
    FP native = nullptr;
};

#define DECLARE_CMDLIST_HOOK(returnType, interfaceName, functionName, ...) \
    FUNCTION_TYPE(returnType, interfaceName, functionName, __VA_ARGS__) \
    extern FunctionHook interfaceName##_##functionName##_FH[c_MaxHookPermutations];\
    extern HookPermutation<interfaceName##_##functionName##_FP> interfaceName##_##functionName##_Ctx[c_MaxHookPermutations];\
    extern std::atomic_uint32_t interfaceName##_##functionName##_CtxCount;\
    uintptr_t Get##interfaceName##_##functionName##Hook(uint32_t id);\
    template<uint32_t id>\
    returnType STDMETHODCALLTYPE interfaceName##_##functionName##_H(__VA_ARGS__);

#pragma region [LoadLibraryHooks]
#define LOAD_LIBRARY_MAP(MACRO)\
    MACRO(kernel32, LoadLibraryA)\
    MACRO(kernel32, LoadLibraryW)\
    MACRO(kernel32, LoadLibraryExA)\
    MACRO(kernel32, LoadLibraryExW)\

DECLARE_HOOK(HMODULE, kernel32, LoadLibraryA, _In_ LPCSTR lpLibFileName)
DECLARE_HOOK(HMODULE, kernel32, LoadLibraryW, _In_ LPCWSTR lpLibFileName)
DECLARE_HOOK(HMODULE, kernel32, LoadLibraryExA, _In_ LPCSTR lpLibFileName, _Reserved_ HANDLE hFile, _In_ DWORD dwFlags)
DECLARE_HOOK(HMODULE, kernel32, LoadLibraryExW, _In_ LPCWSTR lpLibFileName, _Reserved_ HANDLE hFile, _In_ DWORD dwFlags)
#pragma endregion

#pragma region [DxgiHooks]
DECLARE_HOOK(HRESULT, DXGI, Present, IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
DECLARE_HOOK(HRESULT, DXGI, Present1, IDXGISwapChain1* This, UINT SyncInterval, UINT Flags, _In_  const DXGI_PRESENT_PARAMETERS* pPresentParameters)
#pragma endregion

#pragma region[Dx12Hooks]
DECLARE_HOOK(HRESULT, Dx12, D3D12CreateDevice, _In_opt_ IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, _In_ REFIID riid, __allowed(on_parameter) void** ppDevice)
DECLARE_HOOK(void, ID3D12CommandQueue, ExecuteCommandLists, ID3D12CommandQueue* This, _In_  UINT NumCommandLists, _In_reads_(NumCommandLists)  ID3D12CommandList* const* ppCommandLists)
//Compute State Tracking
DECLARE_HOOK(HRESULT, ID3D12Device, CreateCommandList, ID3D12Device* This, _In_  UINT nodeMask, _In_  D3D12_COMMAND_LIST_TYPE type, _In_  ID3D12CommandAllocator* pCommandAllocator, _In_opt_  ID3D12PipelineState* pInitialState, REFIID riid, _COM_Outptr_  void** ppCommandList)

DECLARE_CMDLIST_HOOK(HRESULT, ID3D12GraphicsCommandList, Close, ID3D12GraphicsCommandList* This)
DECLARE_CMDLIST_HOOK(HRESULT, ID3D12GraphicsCommandList, Reset, ID3D12GraphicsCommandList* This, _In_  ID3D12CommandAllocator* pAllocator, _In_opt_  ID3D12PipelineState* pInitialState)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRootSignature, ID3D12GraphicsCommandList* This, _In_opt_  ID3D12RootSignature* pRootSignature)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRootDescriptorTable, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetDescriptorHeaps, ID3D12GraphicsCommandList* This, UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetPipelineState, ID3D12GraphicsCommandList* This, _In_  ID3D12PipelineState* pPipelineState)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRoot32BitConstant, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  UINT SrcData, _In_  UINT DestOffsetIn32BitValues)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRoot32BitConstants, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  UINT Num32BitValuesToSet, _In_  const void* pSrcData, _In_  UINT DestOffsetIn32BitValues)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRootConstantBufferView, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRootShaderResourceView, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList, SetComputeRootUnorderedAccessView, ID3D12GraphicsCommandList* This, _In_  UINT RootParameterIndex, _In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList4, SetPipelineState1, ID3D12GraphicsCommandList* This, _In_  ID3D12StateObject* pStateObject)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList4, CopyRaytracingAccelerationStructure, ID3D12GraphicsCommandList4* This, _In_  D3D12_GPU_VIRTUAL_ADDRESS DestAccelerationStructureData, _In_  D3D12_GPU_VIRTUAL_ADDRESS SourceAccelerationStructureData, _In_  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE Mode)
DECLARE_CMDLIST_HOOK(void, ID3D12GraphicsCommandList4, BuildRaytracingAccelerationStructure, ID3D12GraphicsCommandList4* This, _In_  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc, _In_  UINT NumPostbuildInfoDescs, _In_reads_opt_(NumPostbuildInfoDescs)  const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* pPostbuildInfoDescs)
#pragma endregion

#pragma region[NvapiHooks]
DECLARE_NVAPI_HOOK(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS* pParams)
DECLARE_NVAPI_HOOK(NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS* pParams)
DECLARE_NVAPI_HOOK(NvAPI_D3D12_SetAsyncFrameMarker, __in ID3D12CommandQueue* pCommandQueue, __in NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
DECLARE_NVAPI_HOOK(NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation, __in ID3D12GraphicsCommandList4* pCommandList, __in const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS* pParams);
#pragma endregion