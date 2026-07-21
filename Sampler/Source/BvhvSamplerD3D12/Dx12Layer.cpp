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

#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include "Windows.h"
#include <Psapi.h>
#include <map>
#include <atomic>
#include <string>
#include <nvapi.h>
#include <nvapi_interface.h>
#include <vector>
#include "Version.h"
#include "Dx12Layer.h"
#include "Dx12Hooks.h"
#include "Dx12PipelineComputeStateTracker.h"
#include "Connection.h"
#include "LogMessage.h"

extern SamplerConnection g_Connection;
extern PipelineComputeStateTracker g_StateTracker;
Dx12Layer g_Layer = {};

inline uintptr_t* GetVtableStart(void* ptr)
{
    return (uintptr_t*)(*((uintptr_t*)ptr));
}

uintptr_t GetNativeCreateSwapChainForHwndPointer()
{
    HMODULE dxgiModule = GetModuleHandle(L"dxgi.dll");
    if (!dxgiModule)
        return 0;

    IDXGIFactory2* dxgiFactory = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr) || !dxgiFactory)
        return 0;

    uintptr_t* pVTable = (uintptr_t*)(*((uintptr_t*)dxgiFactory));
    dxgiFactory->Release();

    const uintptr_t createSwapChainForHwndPtr = pVTable[15];

    MODULEINFO dxgiInfo = {};
    GetModuleInformation(GetCurrentProcess(), dxgiModule, &dxgiInfo, sizeof(dxgiInfo));

    const bool isVtablePatched = createSwapChainForHwndPtr < uintptr_t(dxgiInfo.lpBaseOfDll) ||
        createSwapChainForHwndPtr >= uintptr_t(dxgiInfo.lpBaseOfDll) + uintptr_t(dxgiInfo.SizeOfImage);

    if (!isVtablePatched)
        return createSwapChainForHwndPtr;

    const uintptr_t offset = g_Connection.GetDxgiDesc().CreateSwapChainForHwnd;
    if (isVtablePatched && offset == 0)
        return 0;

    return uintptr_t(dxgiModule) + offset;
}

inline uint32_t GetNvApiFunctionId(const char* name)
{
    constexpr uint32_t count = uint32_t(std::size(nvapi_interface_table));
    for (uint32_t i = 0; i < count; ++i)
    {
        if (strcmp(name, nvapi_interface_table[i].func) == 0)
            return nvapi_interface_table[i].id;
    }
    return uint32_t(~0);
}

Dx12Layer::~Dx12Layer()
{
    if(m_Terminating)
        return;

    m_BvhViewer.Shutdown();
    m_BvhViewer.Deinitialize();

    m_hDxgi = NULL;
    m_hD3d12 = NULL;
    CloseHandle(m_Self);
    m_Self = NULL;
    m_Device = nullptr;
    m_ReflexMarkerState.clear();
    m_UserClosedTheViewer = false;

    ReleaseAllHooks();

    g_Connection.Close();
}

void BvhViewerWindowCloseCallback(void* userData)
{
    (void)(userData);
    g_Layer.m_UserClosedTheViewer = true;
}

void Dx12Layer::OnNewFrame(IDXGISwapChain* pSwapChain)
{
    ID3D12Device* d3d12Device = nullptr;
    if (pSwapChain->GetDevice(IID_PPV_ARGS(&d3d12Device)) != S_OK)
        return;

    constexpr uint32_t noReflexPresentCounterCutOff = 32;
    static uint32_t noReflexPresentCounter = noReflexPresentCounterCutOff;
    bool isReflex = m_ReflexMarkerState.test(std::memory_order_relaxed);
    bool handleNewFrameHere = isReflex ? false : noReflexPresentCounter >= noReflexPresentCounterCutOff;
    if(handleNewFrameHere)
    {
        m_BvhViewer.OnNewFrame();
        m_ReflexMarkerState.clear(std::memory_order_release);
    }
    noReflexPresentCounter = isReflex ? 0 : ++noReflexPresentCounter;

    InitializeDx12Device(d3d12Device); // init here if device creation was missed
    d3d12Device->Release();
}

void Dx12Layer::InitializeDx12Device(ID3D12Device* device)
{
    if(device == nullptr)
        return;

    if(m_Device == device)
        return;

    D3D12DispatchTable dispatchTable = {};
    if(dispatchTable.Init(device) != true)
        return; // initialize bvh viewers dispatch table from device

    {
        uintptr_t* vtable = GetVtableStart(device);
        CREATE_HOOK(ID3D12Device, CreateCommandList, vtable[12]);
    }

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0x1};
    ID3D12CommandQueue* commandQueue = nullptr;
    HRESULT hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
    if (hr != S_OK)
    {
        LogMessage("[Dx12Layer]", "InitializeDx12Device: ID3D12Device::CreateCommandQueue failed");
        return;
    }

    { // commandQueue
        uintptr_t* vtable = GetVtableStart(commandQueue);
        CREATE_HOOK(ID3D12CommandQueue, ExecuteCommandLists, vtable[10]);
        commandQueue->Release();
    }

    { // ID3D12GraphicsCommandList: need to go through both compute and graphics cmd list types since they have partially different vtables
        auto HandleCommandListFunctions = [&](D3D12_COMMAND_LIST_TYPE type)
            {
                ID3D12CommandAllocator* commandAllocator = nullptr;
                ID3D12GraphicsCommandList* graphicsCommandList = nullptr;
                ID3D12GraphicsCommandList4* graphicsCommandList4 = nullptr;
                if (device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)) != S_OK)
                {
                    LogMessage("[Dx12Layer]", "InitializeDx12Device: ID3D12Device::CreateCommandAllocator failed");
                    return;
                }

                if (device->CreateCommandList(0x1, type, commandAllocator, nullptr, IID_PPV_ARGS(&graphicsCommandList)) != S_OK)
                {
                    commandAllocator->Release();
                    LogMessage("[Dx12Layer]", "InitializeDx12Device: ID3D12Device::CreateCommandList failed");
                    return;
                }

                if (graphicsCommandList->QueryInterface(&graphicsCommandList4) != S_OK)
                { //  not capable of RT
                    graphicsCommandList->Release();
                    commandAllocator->Release();
                    LogMessage("[Dx12Layer]", "InitializeDx12Device: ID3D12GraphicsCommandList::QueryInterface failed");
                    return;
                }

                const uint32_t id = uint32_t(type);
                uintptr_t* vtable = nullptr;
                for (uint32_t i = 0; i < 2; ++i)
                {
                    const bool promoteToBypass = i == 1;
                    if (promoteToBypass)
                    {
                        graphicsCommandList->Close();
                        commandAllocator->Reset();
                        graphicsCommandList->Reset(commandAllocator, nullptr);
                    }

                    vtable = GetVtableStart(graphicsCommandList);
                    if(g_Connection.GetViewerDesc().inlineRendering)
                    {
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, Close, vtable[9]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, Reset, vtable[10]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetPipelineState, vtable[25]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetDescriptorHeaps, vtable[28]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRootSignature, vtable[29]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRootDescriptorTable, vtable[31]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRoot32BitConstant, vtable[33]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRoot32BitConstants, vtable[35]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRootConstantBufferView, vtable[37]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRootUnorderedAccessView, vtable[41]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList, SetComputeRootShaderResourceView, vtable[39]);
                        ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList4, SetPipelineState1, vtable[75]);
                    }

                    ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList4, BuildRaytracingAccelerationStructure, vtable[72]);
                    ADD_TYPED_FUNCTION_DETOUR(id, ID3D12GraphicsCommandList4, CopyRaytracingAccelerationStructure, vtable[74]);
                }

                uintptr_t cmdListDT[uint32_t(ID3D12GraphicsCommandList_DT::Layout::Count)] = {};
                for (uint32_t dtId = 0; dtId < uint32_t(ID3D12GraphicsCommandList_DT::Layout::Count); ++dtId)
                {
                    const auto it = FunctionHook::g_Hooks.find(vtable[ID3D12GraphicsCommandList_DT::VtableOffsets[dtId]]);
                    if (it != FunctionHook::g_Hooks.end())
                        cmdListDT[dtId] = it->second;
                }
                dispatchTable.GetCmdList(type).SetDispatchTable(cmdListDT);

                graphicsCommandList4->Release();
                graphicsCommandList->Release();
                commandAllocator->Release();
            };

        HandleCommandListFunctions(D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT);
        HandleCommandListFunctions(D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_COMPUTE);
    }

    { // nvapi
        HMODULE nvapiDll = LoadLibraryA("nvapi64.dll");
        if (nvapiDll)
        {
            typedef FARPROC(_cdecl* nvapi_QueryInterface_FP)(uint32_t id);
            nvapi_QueryInterface_FP pNvApiQueryInterface = (nvapi_QueryInterface_FP)GetProcAddress(nvapiDll, "nvapi_QueryInterface");
            if (pNvApiQueryInterface)
            {
                CREATE_NVAPI_HOOK(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx, GetNvApiFunctionId, pNvApiQueryInterface);
                CREATE_NVAPI_HOOK(NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect, GetNvApiFunctionId, pNvApiQueryInterface);
                CREATE_NVAPI_HOOK(NvAPI_D3D12_SetAsyncFrameMarker, GetNvApiFunctionId, pNvApiQueryInterface);
                CREATE_NVAPI_HOOK(NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation, GetNvApiFunctionId, pNvApiQueryInterface);
            }
        }
    }

    { // Kick off the viewer
        // since dxgi and d3d12 hooks are set, we need to feed native function pointers to the viewer, so we don't intercept ourselves
        uintptr_t dxgiDT[uint32_t(DXGI_DT::Layout::Count)] = {};
        dxgiDT[uint32_t(DXGI_DT::Layout::CreateSwapChainForHwnd)] = GetNativeCreateSwapChainForHwndPointer();
        dxgiDT[uint32_t(DXGI_DT::Layout::Present)] = (uintptr_t)DXGI_Present_S;
        dispatchTable.DXGI.SetDispatchTable(dxgiDT);

        uintptr_t cqDT[uint32_t(ID3D12CommandQueue_DT::Layout::Count)] = {};
        cqDT[uint32_t(ID3D12CommandQueue_DT::Layout::ExecuteCommandLists)] = (uintptr_t)ID3D12CommandQueue_ExecuteCommandLists_S;
        dispatchTable.CmdQueue.SetDispatchTable(cqDT);

        g_StateTracker.Initialize(dispatchTable);

        NvBvhViewerD3D12::InitDesc desc = {};
        desc.device = device;
        desc.dispatchTable = dispatchTable;
        desc.numBufferedFrames = 3;
        desc.outputWidth = g_Connection.GetViewerDesc().width;
        desc.outputHeight = g_Connection.GetViewerDesc().height;
        desc.windowCloseCallback = BvhViewerWindowCloseCallback;
        desc.windowResizeCallback = nullptr;
        desc.callbackUserData = nullptr;
        desc.enableClusterGeometryTracking = true;
        desc.enableInlineRendering = g_Connection.GetViewerDesc().inlineRendering;
        desc.logMessageCallback = LogViewerMessage;
        m_BvhViewer.Initialize(desc);

        if(m_BvhViewer.IsInitialized())
        {
            m_BvhViewer.Launch();
            if(m_Self == nullptr)
            {// increment module ref count to avoid dll detach when host closes windows hook
                std::wstring moduleName = std::wstring(TEXT(STR(BVHVIEWER_D3D12_MODULE_NAME))) + std::wstring(L".dll");
                GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, moduleName.c_str(), &m_Self);
            }
        }
    }

    m_Device = device;
    return;
}

void Dx12Layer::LoadLibraryCommon(const char* nameA, const wchar_t* nameW, HMODULE hModule)
{
    bool isDxgi = nameA ? _stricmp(nameA, "dxgi.dll") == 0 : false;
    isDxgi = nameW ? _wcsicmp(nameW, L"dxgi.dll") == 0 : isDxgi;
    if (isDxgi && hModule)
    {
        CreateDxgiHooks(hModule, nullptr);
        m_hDxgi = hModule;
    }

    bool isDx12 = nameA ? _stricmp(nameA, "d3d12.dll") == 0 : false;
    isDx12 = nameW ? _wcsicmp(nameW, L"d3d12.dll") == 0 : isDx12;
    if (isDx12 && hModule)
    {
        CREATE_DLL_HOOK_EX(Dx12, D3D12CreateDevice, d3d12.dll);
        m_hD3d12 = hModule;
    }

    if (m_hD3d12 && m_hDxgi)
    {
        LOAD_LIBRARY_MAP(DISABLE_HOOK);
    }
}

bool Dx12Layer::CreateDxgiHooks(HMODULE dxgi, HMODULE d3d12)
{
    if (dxgi == NULL)
        return false;

    if(d3d12)
    {
        CREATE_DLL_HOOK_EX(Dx12, D3D12CreateDevice, d3d12.dll);
    }

    // these are the real pointers to functions inside dxgi.dll
    uintptr_t present = uintptr_t(dxgi) + g_Connection.GetDxgiDesc().Present;
    uintptr_t present1 = uintptr_t(dxgi) + g_Connection.GetDxgiDesc().Present1;

    CREATE_HOOK(DXGI, Present, present);
    CREATE_HOOK(DXGI, Present1, present1);

    bool verify = true;
    verify = verify && DXGI_Present_S;
    verify = verify && DXGI_Present1_S;

    return verify;
}

bool Dx12Layer::CreateLoadLibraryHooks(HMODULE kernel32)
{
    if (!kernel32)
        return false;

    LOAD_LIBRARY_MAP(CREATE_DLL_HOOK);

    bool verify = true;
#define VERIFY_HOOK(interfaceName, functionName)\
        verify = verify && interfaceName##_##functionName##_S;
    LOAD_LIBRARY_MAP(VERIFY_HOOK);

    return verify;
}