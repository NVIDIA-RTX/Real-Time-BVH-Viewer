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

#include <Windows.h>
#include <string>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <chrono>

#include "Resources/Resource.h"
#include "Version.h"
#include "CmdLine.h"
#include "Connection.h"
#include "BvhvSamplerD3D12.h"
#include "BvhvSamplerVulkan.h"

namespace fs = std::filesystem;

struct SamplerContext
{
public:
    void Destroy()
    {
        if(hVkDll != NULL)
        {
            const auto DisableVulkanLayer = (PFN_SamplerVulkan_DisableLayerr)GetProcAddress(hVkDll, "SamplerVulkan_DisableLayer");
            DisableVulkanLayer(&connection);

            if (FreeLibrary(hVkDll) == FALSE)
                std::cerr << "[ERROR] failed to unload " << STR(BVHVIEWER_VULKAN_MODULE_NAME) << ".dll\n";

            hVkDll = NULL;

        }

        if(hook)
        {
            if(hDx12Dll)
            {
                FreeLibrary(hDx12Dll);
            }

            if(UnhookWindowsHookEx(hook) == 0)
                std::cerr << "[ERROR] failed to unhook the Window Hook\n";
        }
        connection.Close();
    }

    ~SamplerContext()
    { 
        Destroy();
    }

public:
    SamplerConnection connection = {};
    HMODULE hVkDll = NULL;
    HMODULE hDx12Dll = NULL;
    HHOOK hook = NULL;
public:
    SamplerConnection::ViewerDesc viewerDesc = {};
    bool disableD3D12 = false;
    bool disableVk = false;
    std::wstring targetName = L"";

} g_SamplerContext = {};

BOOL WINAPI HandlerRoutine(DWORD eventCode)
{
    switch (eventCode)
    {
    case CTRL_CLOSE_EVENT:
        g_SamplerContext.Destroy();
        return TRUE;
    }

    return FALSE;
}

int main(int argc, const char** argv)
{
    {
        SetConsoleTitleA(std::string("BvhvSampler [ver. " + std::string(VERSION_STRING) + std::string("]")).c_str());
        SetConsoleCtrlHandler(HandlerRoutine, TRUE);

        if (HWND hConsole = GetConsoleWindow())
        {
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
            SendMessage(hConsole, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage(hConsole, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }
    }

    cmdline::parser cmdLine = {};
    {
        {
            cmdLine.add("help", '?', "print this message");
            cmdLine.add("disableD3D12", 0, "disable D3D12, skip DLL attach");
            cmdLine.add("disableVK", 0, "disable VK, skip VK layer creation");
            cmdLine.add<std::string>("target", 't', "specify target process name, all other processes will be ignored. e.g. --target=MyApp.exe. Use [-t auto] for auto attach mode", false);
            cmdLine.add<uint32_t>("width", 'w', "viewer window initial width", false, 1920);
            cmdLine.add<uint32_t>("height", 'h', "viewer window initial height", false, 1080);
            cmdLine.add("inlineRendering", 0, "enable inline rendering: append commands to target's command buffers. Required for PTLAS and Clustered Geometry");
            cmdLine.add("forcePositionFetchExt", 0, "force enable position_fetch_ext in VK for wireframe visualization mode");
        }
        if (argc < 2)
        {
            printf("\n%s", cmdLine.usage().c_str());
            system("pause");
            return 1;
        }
        {
            bool parseStatus = cmdLine.parse(argc, argv);
            if (cmdLine.exist("help")) {
                printf("\n%s", cmdLine.usage().c_str());
                return 1;
            }

            if (!parseStatus) {
                printf("\n%s\n\n%s", cmdLine.error().c_str(), cmdLine.usage().c_str());
                printf("\n%s", cmdLine.usage().c_str());
                return 1;
            }
        }
        {
            g_SamplerContext.disableD3D12 = cmdLine.exist("disableD3D12");
            g_SamplerContext.disableVk = cmdLine.exist("disableVK");
            g_SamplerContext.viewerDesc.forcePositionFetchExt = cmdLine.exist("forcePositionFetchExt");
            g_SamplerContext.viewerDesc.inlineRendering = cmdLine.exist("inlineRendering");
            g_SamplerContext.viewerDesc.width = cmdLine.get<uint32_t>("width");
            g_SamplerContext.viewerDesc.height = cmdLine.get<uint32_t>("height");

            std::string targetName = cmdLine.get<std::string>("target");
            if (targetName.empty() == false)
            {
                g_SamplerContext.targetName.resize(targetName.size());
                size_t numConverted = 0;
                mbstowcs_s(&numConverted, g_SamplerContext.targetName.data(), g_SamplerContext.targetName.size() + 1, targetName.c_str(), targetName.size());
                if (numConverted > 0)
                    g_SamplerContext.targetName.resize(numConverted - 1); // numConverted includes the null terminator
                else
                    g_SamplerContext.targetName.clear();
            }
        }
    }

    {
        std::cout <<
            "//////////////////////////////////////////////\n"
            "//            NvBvhViewer Sampler           //\n"
            "//       [Black-Box Integration Sample]     //\n"
            "//                (use --help)              //\n"
            "///////////////////////////////////////////\\//\n\n";
    }

    if(g_SamplerContext.disableD3D12 && g_SamplerContext.disableVk)
    {
        std::cout << "[INFO] Both VK and D3D12 are disabled. Nothing to do here. Bye!\n";
        return 0;
    }

    fs::path executablePath = {};
    {
        wchar_t wBuffer[c_PathMax] = {};
        DWORD nameSize = DWORD(c_PathMax);
        if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, wBuffer, &nameSize) == false)
        {
            std::cerr << "[ERROR] QueryFullProcessImageNameW failed. Process can't locate itself o_O\n";
            return 1;
        }
        executablePath = fs::path(wBuffer);
    }

    if (g_SamplerContext.targetName.empty() == false)
    {
        if (wcscmp(g_SamplerContext.targetName.c_str(), L"auto") == 0)
        {
            std::wcout << L"[INFO] Auto attach is enabled\n";
            g_SamplerContext.targetName.clear();
        }
        else
            std::wcout << L"[INFO] Target process name: " << g_SamplerContext.targetName.c_str() << L"\n";
    }
    else
    {
        std::wcout << L"[ERROR] Target process name is not specified.\n";
        return 1;
    }

    { // Init Connection to communicate with DLLs inside the target process
        SamplerConnection::HostDesc desc = {};
        desc.hostPath = executablePath.c_str();
        desc.hostPID = uint32_t(GetProcessId(GetCurrentProcess()));
        desc.targetName = g_SamplerContext.targetName.empty() ? nullptr : g_SamplerContext.targetName.c_str();
        desc.viewerSettings = g_SamplerContext.viewerDesc;

        fs::path processFilterPath = executablePath.parent_path().wstring() + L"\\processFilter.txt";
        desc.processFilterPath = fs::exists(processFilterPath) ? processFilterPath.c_str() : nullptr;

        if(g_SamplerContext.connection.Open(&desc) == false)
        {
            std::cerr << "[ERROR] Failed to initialize SamplerConnection";
            return 1;
        }
    }

    const fs::path currentDirectory = executablePath.parent_path();
    if(g_SamplerContext.disableVk == false)
    { // install the VK Layer
        const std::string vkDllName = std::string(STR(BVHVIEWER_VULKAN_MODULE_NAME)) + std::string(".dll");
        const fs::path vkDllPath = currentDirectory.string() + std::string("\\") + vkDllName;
        if(fs::exists(vkDllPath) == false)
        {
            std::cerr << "[ERROR] Can't locate " << vkDllName.c_str() << "\n";
            return 1;
        }

        g_SamplerContext.hVkDll = LoadLibrary(vkDllPath.c_str());
        if(g_SamplerContext.hVkDll == NULL)
        {
            std::cerr << "[ERROR] Can't load " << vkDllName.c_str() << "\n";
            return 1;
        }

        const auto EnableVulkanLayer = (PFN_SamplerVulkan_EnableLayer)GetProcAddress(g_SamplerContext.hVkDll, "SamplerVulkan_EnableLayer");
        if(!EnableVulkanLayer || EnableVulkanLayer(&g_SamplerContext.connection) == false)
            std::cerr << "[ERROR] Failed to install Vulkan Layer " << vkDllName.c_str() << "\n";
        else
            std::cout << "[INFO] Vulkan is ready\n";
    }

    if (g_SamplerContext.disableD3D12 == false)
    { // install a WindowsHook to trigger d3d12 dll injection into active processes
        const std::string d3d12DllName = std::string(STR(BVHVIEWER_D3D12_MODULE_NAME)) + std::string(".dll");
        const fs::path d3d12DllPath = currentDirectory.string() + std::string("\\") + d3d12DllName;
        g_SamplerContext.hDx12Dll = LoadLibrary(d3d12DllPath.c_str());
        const HMODULE hDx12Dll = g_SamplerContext.hDx12Dll;
        if (hDx12Dll == NULL)
        {
            std::cerr << "[ERROR] Can't load " << d3d12DllName.c_str() << "\n";
            return 1;
        }

        bool dxgiInit = false;
        const auto InitializeDXGI = (PFN_SamplerD3D12_InitializeDXGI)GetProcAddress(hDx12Dll, "SamplerD3D12_InitializeDXGI");
        if (!InitializeDXGI)
            std::cerr << "[ERROR] Failed to resolve BvhvSampler_InitializeDXGI from " << d3d12DllName.c_str() << "\n";
        else
            dxgiInit = InitializeDXGI(&g_SamplerContext.connection);

        if(!dxgiInit)
            std::cerr << "[ERROR] Failed to initialize DXGI"<< "\n";

        if (hDx12Dll && dxgiInit)
        {
            const char* funcName = "CBTProc";
            HOOKPROC hProc = (HOOKPROC)GetProcAddress(hDx12Dll, "CBTProc");
            if (!hProc)
            {
                char msg[512];
                sprintf_s(msg, "[ERROR] Could not find hook (%d) for function %s", WH_CBT, funcName);
                OutputDebugStringA(msg);
                std::cerr << msg << "\n";
            }
            else
            {
                g_SamplerContext.hook = SetWindowsHookEx(WH_CBT, hProc, hDx12Dll, 0);
                if (!g_SamplerContext.hook)
                    std::cerr << "[ERROR] Failed to set Window Hook:" << funcName <<"\n";
                else
                    std::cout << "[INFO] D3D12 is ready\n";
            }
        }
    }

    while(true)
    { // check connections here
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(g_SamplerContext.connection.GetLog())
            g_SamplerContext.connection.GetLog()->PrintPosted();
    }

    return 0;
}