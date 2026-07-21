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

#include "NvBvhViewerDispatchTableD3D12.h"
#include <algorithm>

#define INIT_FUNC(vtableOffset, returnType, interfaceType, function, ...) function = (function##_FP)vtable[vtableOffset];
#define ASSIGN_FUNC(vtableOffset, returnType, interfaceType, function, ...) if(layout[uint32_t(Layout:: function)]) function = (function##_FP)layout[uint32_t(Layout:: function)];
#define VERIFY_FUNC(vtableOffset, returnType, interfaceType, function, ...) verification = verification && dispatchTable. function;

bool ID3D12CommandQueue_DT::Init(ID3D12CommandQueue* queue)
{
    if(queue == nullptr)
        return false;

    const uintptr_t* vtable = (uintptr_t*)(*((uintptr_t*)queue));

    BVHV_CMD_QUEUE_FUNCTIONS(INIT_FUNC);

    return true;
}

bool ID3D12CommandQueue_DT::SetDispatchTable(uintptr_t* layout)
{
    if (layout == nullptr)
        return false;

    BVHV_CMD_QUEUE_FUNCTIONS(ASSIGN_FUNC);

    return true;
}

bool ID3D12GraphicsCommandList_DT::Init(ID3D12GraphicsCommandList4* list)
{
    if (list == nullptr)
        return false;

    const uintptr_t* vtable = (uintptr_t*)(*((uintptr_t*)list));
    BVHV_CMD_LIST_FUNCTIONS(INIT_FUNC);

    return true;
}

bool ID3D12GraphicsCommandList_DT::SetDispatchTable(uintptr_t* layout)
{
    if(layout == nullptr)
        return false;

    BVHV_CMD_LIST_FUNCTIONS(ASSIGN_FUNC);

    return true;
}

bool DXGI_DT::Init(ID3D12CommandQueue* graphicsQueue)
{
    if(graphicsQueue == nullptr)
        return false;

    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain1* swapChain1 = nullptr;

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

    if(hr != S_OK)
        return false;

    bool result = false;
    HWND hwnd = NULL;
    const LPWSTR windowClass = (wchar_t*)L"_dummywindow_";
    {
        WNDCLASSEX wndclass = {};
        ZeroMemory(&wndclass, sizeof(wndclass));
        wndclass.cbSize = sizeof(WNDCLASSEX);
        wndclass.style = CS_DBLCLKS;
        wndclass.lpfnWndProc = DefWindowProc;
        wndclass.lpszClassName = windowClass;
        ATOM classRegistrationResult = RegisterClassEx(&wndclass);

        if (classRegistrationResult != 0)
        {
            DWORD windowStyle{WS_EX_NOREDIRECTIONBITMAP};
            hwnd = CreateWindowEx(0, windowClass, (LPCWSTR)windowClass, windowStyle, 0, 0, 2, 2, NULL, NULL, NULL, NULL);
        }
        else
            UnregisterClassW(windowClass, NULL);

    }

    if(hwnd)
    {
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = 2;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        swapChainDesc.Width = swapChainDesc.Height = 1;

        if (factory->CreateSwapChainForHwnd(graphicsQueue, hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1) == S_OK)
        {
            const uintptr_t* IDXGIFactory2_vtable = (uintptr_t*)(*((uintptr_t*)factory));
            const uintptr_t* IDXGISwapChain_vtable = (uintptr_t*)(*((uintptr_t*)swapChain1));

            CreateSwapChainForHwnd = (CreateSwapChainForHwnd_FP)IDXGIFactory2_vtable[15];
            Present = (Present_FP)IDXGISwapChain_vtable[8];

            result = true;
        }
    }

    if(swapChain1)
        swapChain1->Release();

    if (hwnd)
    {
        DestroyWindow(hwnd);
        UnregisterClassW(windowClass, NULL);
    }

    if(factory)
        factory->Release();

    return result;
}

bool DXGI_DT::SetDispatchTable(uintptr_t* layout)
{
    if (layout == nullptr)
        return false;

    BVHV_DXGI_FUNCTIONS(ASSIGN_FUNC);

    return true;
}

bool D3D12DispatchTable::Init(ID3D12Device* device)
{
    if(device == nullptr)
        return false;

    bool result = true;
    {
        D3D12_COMMAND_QUEUE_DESC cqDesc = {};
        cqDesc.NodeMask = 0x1;
        cqDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        ID3D12CommandQueue* queue = nullptr;
        HRESULT hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&queue));
        if (hr != S_OK)
            return false;

        result = result && CmdQueue.Init(queue);
        result = result && DXGI.Init(queue);

        queue->Release();
    }

    ID3D12CommandAllocator* allocators[c_NumCommandListTypes] = {};
    ID3D12GraphicsCommandList* lists[c_NumCommandListTypes] = {};
    for(uint32_t i = 0; i < c_NumCommandListTypes; ++i)
    {
        D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE(i);

        if(type == D3D12_COMMAND_LIST_TYPE_BUNDLE)
            continue;

        {
            ID3D12CommandAllocator*& allocator = allocators[i];
            HRESULT hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator));
            if(hr != S_OK)
                continue;

            ID3D12GraphicsCommandList*& list = lists[i];
            hr = device->CreateCommandList(0x1, type, allocator, nullptr, IID_PPV_ARGS(&list));
            if(hr != S_OK)
                continue;

            ID3D12GraphicsCommandList4* list4 = nullptr;
            hr = list->QueryInterface(IID_PPV_ARGS(&list4));
            if (hr != S_OK)
                continue;

            ID3D12GraphicsCommandList_DT& cmdListDt = GetCmdList(type);
            result = result && cmdListDt.Init(list4);
            list4->Release();
        }
    }

    for (uint32_t i = 0; i < c_NumCommandListTypes; ++i)
    {
        if(lists[i]) lists[i]->Release();
        if(allocators[i]) allocators[i]->Release();
    }

    return result;
}

bool D3D12DispatchTable::IsInitialized() const
{
    bool verification = true;

    const D3D12_COMMAND_LIST_TYPE supportedTypes[] = { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE };
    for(size_t i = 0; i < std::size(supportedTypes); ++i)
    {
        const ID3D12GraphicsCommandList_DT& dispatchTable = CmdList[uint32_t(supportedTypes[i])];
        BVHV_CMD_LIST_FUNCTIONS(VERIFY_FUNC);
    }

    {
        const ID3D12CommandQueue_DT& dispatchTable = CmdQueue;
        BVHV_CMD_QUEUE_FUNCTIONS(VERIFY_FUNC);
    }

    {
        const DXGI_DT& dispatchTable = DXGI;
        BVHV_DXGI_FUNCTIONS(VERIFY_FUNC);
    }

    return verification;
}
