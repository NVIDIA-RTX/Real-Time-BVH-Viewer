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
#include "LogBuffer.h"

constexpr char c_ConnectionFileName[ ] = "BvhvSampler";

constexpr size_t c_PathMax = 2048;
constexpr size_t c_NameMax = 256;

struct SamplerConnection
{
public:
    struct ViewerDesc
    {
        uint32_t width;
        uint32_t height;
        bool inlineRendering;
        bool forcePositionFetchExt;
    };

    struct HostDesc
    {
        const wchar_t* hostPath;
        const wchar_t* targetName;
        const wchar_t* processFilterPath;
        ViewerDesc viewerSettings;
        uint32_t hostPID;
    };

    struct DxgiDesc
    {
        uintptr_t Present;
        uintptr_t Present1;
        uintptr_t CreateSwapChainForHwnd;
    };


public:
    bool Open(SamplerConnection::HostDesc* desc);
    void Close();

    void SetVkLayerState(bool isEnabled);
    bool GetVkLayerState();

    const wchar_t* GetHostExecutablePath();
    const wchar_t* GetTargetName();
    uint32_t GetHostPID();

    const DxgiDesc& GetDxgiDesc();
    void SetDxgiDesc(DxgiDesc desc);

    const ViewerDesc& GetViewerDesc();
    void SetViewerDesc(ViewerDesc desc);

    bool IsOpened() const 
    { return m_Data != nullptr; }

    LogBuffer* GetLog();

    ~SamplerConnection()
    { Close(); }

private:

    struct DataLayout
    {
        wchar_t hostExecutablePath[c_PathMax];
        wchar_t processFilterPath[c_PathMax];
        wchar_t targetName[c_NameMax];

        DxgiDesc DXGI = {};
        ViewerDesc viewerDesc = {};
        LogBuffer logBuffer = {};

        uint32_t hostPID;
        bool vkLayerState;
    };

    DataLayout* m_Data = nullptr;
    void* m_File = nullptr;
    void* m_MappedBuffer = nullptr;
};