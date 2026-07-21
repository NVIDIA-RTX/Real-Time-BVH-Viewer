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

#include "NvBvhViewerD3D12.h"

struct ID3D12Device;
struct IDXGISwapChain;
struct SamplerConnection;
extern SamplerConnection g_Connection;

struct Dx12Layer
{
public:
    void OnNewFrame(IDXGISwapChain* pSwapChain);
    void InitializeDx12Device(ID3D12Device* device);
    void LoadLibraryCommon(const char* nameA, const wchar_t* nameW, HMODULE hModule);
    bool CreateDxgiHooks(HMODULE dxgi, HMODULE d3d12);
    bool CreateLoadLibraryHooks(HMODULE kernel32);
    ~Dx12Layer();

public:

    NvBvhViewerD3D12 m_BvhViewer = {};
    HMODULE m_hDxgi = NULL;
    HMODULE m_hD3d12 = NULL;
    HMODULE m_Self = NULL;
    ID3D12Device* m_Device = nullptr;
    std::atomic_flag m_ReflexMarkerState = ATOMIC_FLAG_INIT;
    bool m_UserClosedTheViewer = false;
    bool m_Terminating = false;
};

extern Dx12Layer g_Layer;