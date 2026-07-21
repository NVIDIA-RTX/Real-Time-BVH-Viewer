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

#include "Windows.h"
#include <filesystem>
#include <string>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <atlbase.h>
#include "Connection.h"
#include "Dx12Layer.h"
#include "LogMessage.h"

SamplerConnection g_Connection = {};
extern Dx12Layer g_Layer;

#define EXPORT extern "C" __declspec(dllexport)

EXPORT LRESULT CALLBACK CBTProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam)
{ return CallNextHookEx(0, nCode, wParam, lParam); }

EXPORT uint32_t SamplerD3D12_InitializeDXGI(SamplerConnection* connection)
{
    if(connection == nullptr || connection->IsOpened() == false)
        return 0;

    CComPtr<ID3D12Device> device = nullptr;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    if (hr != S_OK)
    {
        LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: D3D12CreateDevice failed");
        return 0;
    }

    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (dxgi == nullptr)
    {
        LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: failed to get dxgi.dll handle");
        return 0;
    }

    typedef HRESULT(WINAPI* FP_CreateDXGIFactory2)(UINT Flags, REFIID riid, _COM_Outptr_ void** ppFactory);
    FP_CreateDXGIFactory2 _CreateDXGIFactory2 = (FP_CreateDXGIFactory2)GetProcAddress(dxgi, "CreateDXGIFactory2");
    if(_CreateDXGIFactory2 == nullptr)
    {
        LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: failed to get CreateDXGIFactory2 ProcAddress");
        return 0;
    }

    CComPtr<IDXGIFactory2> dxgiFactory2 = nullptr;
    hr = _CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory2));
    if (hr != S_OK)
    {
        LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: _CreateDXGIFactory2 failed");
        return 0;
    }

    HWND hwnd = NULL;
    const LPWSTR windowClass = (wchar_t*)L"BVHV_SAMPLER_DXGI_INIT";
    { // Create an imposter window to obtain window hwnd for swap chain creation
        WNDCLASSEX wndclass = {};
        ZeroMemory(&wndclass, sizeof(wndclass));
        wndclass.cbSize = sizeof(WNDCLASSEX);
        wndclass.style = CS_DBLCLKS;
        wndclass.lpfnWndProc = DefWindowProc;
        wndclass.cbClsExtra = 0;
        wndclass.cbWndExtra = 0;
        wndclass.hInstance = NULL;
        wndclass.hIcon = NULL;
        wndclass.hCursor = NULL;
        wndclass.hbrBackground = HBRUSH(0);
        wndclass.lpszMenuName = 0;
        wndclass.lpszClassName = windowClass;
        wndclass.hIconSm = NULL;
        ATOM result = RegisterClassEx(&wndclass);

        if (result != 0)
        {
            DWORD windowStyle{WS_EX_NOREDIRECTIONBITMAP};
            hwnd = CreateWindowEx(0, windowClass, (LPCWSTR)L"Imposter", windowStyle, 0, 0, 2, 2, NULL, NULL, NULL, NULL);
        }
        if (hwnd == NULL)
        {
            UnregisterClassW(windowClass, NULL);
            LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: CreateWindowEx failed");
            return 0;
        }
    }

    CComPtr<ID3D12CommandQueue>commandQueue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.NodeMask = 0x1;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (hr != S_OK)
    {
        LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: ID3D12Device::CreateCommandQueue failed");
        return 0;
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    CComPtr<IDXGISwapChain1> swapChain1 = nullptr;
    uint32_t result = 0;
    hr = dxgiFactory2->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1.p);
    if(hr == S_OK)
    {
        SamplerConnection::DxgiDesc dxgiDesc {};
        uintptr_t* swapChainVtable = (uintptr_t*)(*((uintptr_t*)swapChain1.p));
        uintptr_t* factoryVtable = (uintptr_t*)(*((uintptr_t*)dxgiFactory2.p));
        dxgiDesc.Present = swapChainVtable[8] - (uintptr_t)dxgi;
        dxgiDesc.Present1 = swapChainVtable[22] - (uintptr_t)dxgi;
        dxgiDesc.CreateSwapChainForHwnd = factoryVtable[15] - (uintptr_t)dxgi;
        connection->SetDxgiDesc(dxgiDesc);
        result = 1;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(windowClass, NULL);
    return result;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpvReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        if(lpvReserved)
        {
            break;
        }

        DisableThreadLibraryCalls(hModule);

        if (g_Connection.Open(nullptr) == false)
            break;

        {
            const size_t wBufferSize = 2048;
            wchar_t wBuffer[wBufferSize] = {};
            GetModuleFileName(NULL, wBuffer, wBufferSize);
            std::filesystem::path executablePath = std::filesystem::path(wBuffer);
            const wchar_t* hostPath = g_Connection.GetHostExecutablePath();
            if (wcscmp(hostPath, executablePath.c_str()) == 0)
                break;
        }

        HMODULE kernel32 = GetModuleHandle(L"kernel32.dll");
        HMODULE d3d12 = GetModuleHandle(L"d3d12.dll");
        HMODULE dxgi = GetModuleHandle(L"dxgi.dll");

        if(dxgi)
        {
            if (g_Layer.CreateDxgiHooks(dxgi, d3d12) == false)
            {
                LogMessage("[Dx12Layer]", "SamplerD3D12_InitializeDXGI: CreateDxgiHooks failed");
                break;
            }
        }

        if(d3d12 || dxgi)
            break;

        if (g_Layer.CreateLoadLibraryHooks(kernel32) == false)
            LogMessage("[Dx12Layer]", "CreateLoadLibraryHooks failed");

        break;
    }
    case DLL_PROCESS_DETACH:
    {
        if (lpvReserved)
            g_Layer.m_Terminating = true;

        break;
    }
    default: break;
    }
    return TRUE;
}