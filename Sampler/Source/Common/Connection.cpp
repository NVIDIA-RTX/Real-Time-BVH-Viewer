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
#include <string>
#include <stdint.h>
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "Connection.h"

bool SamplerConnection::Open(SamplerConnection::HostDesc* desc)
{
    { // allocate the buffer, open shared buffer mapping
        size_t size = sizeof(DataLayout);
        const std::string path = std::string(c_ConnectionFileName);
        const std::wstring pathW(path.begin(), path.end());
        const std::wstring fullPath = L"Local\\" + pathW;

        uint32_t sizeHigh = 0;
        const uint32_t sizeLow = (uint32_t)(size & 0xFFFFFFFF);
        if constexpr (sizeof(size_t) > sizeof(uint32_t))
            sizeHigh = (uint32_t)(uint64_t(size) >> 32);

        m_File = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, sizeHigh, sizeLow, fullPath.c_str());

        if (m_File == nullptr)
        {
            Close();
            return false;
        }

        m_MappedBuffer = MapViewOfFile(m_File, FILE_MAP_ALL_ACCESS, 0, 0, size);

        if (m_MappedBuffer == nullptr)
        {
            Close();
            return false;
        }

        m_Data = (DataLayout*)m_MappedBuffer;
    }

    if (desc)
    {// init host data
        ZeroMemory(m_Data, sizeof(DataLayout));

        ((DataLayout*)m_Data)->hostPID = desc->hostPID;

        auto copyWstr = [](const wchar_t* src, const wchar_t* dst, const size_t maxLength)
            {
                size_t size = wcslen(src);
                size = std::min<size_t>(size, maxLength) * sizeof(wchar_t);
                memcpy((void*)dst, src, size);
            };

        copyWstr(desc->hostPath, ((DataLayout*)m_Data)->hostExecutablePath, c_PathMax);

        if(desc->targetName)
            copyWstr(desc->targetName, ((DataLayout*)m_Data)->targetName, c_NameMax);

        if(desc->processFilterPath)
            copyWstr(desc->processFilterPath, ((DataLayout*)m_Data)->processFilterPath, c_PathMax);

        SetVkLayerState(false);
        SetViewerDesc(desc->viewerSettings);

        m_Data->logBuffer.InitHost();
    }
    else
    { // check that a host exists
        uint32_t pid = GetHostPID();
        HANDLE hHost = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
        if(hHost == nullptr)
        { // you've been hosted ;(
            Close();
            return false;
        }

        wchar_t allegedHostPath[c_PathMax] = {};
        DWORD size = DWORD(c_PathMax);
        QueryFullProcessImageNameW(hHost, 0, allegedHostPath, &size);
        CloseHandle(hHost);

        if(wcscmp(GetHostExecutablePath(), allegedHostPath) != 0)
        { // host PID no longer matches — stale connection
            Close();
            return false;
        }

        wchar_t wBuffer[c_PathMax] = {};
        if (GetModuleFileName(NULL, wBuffer, c_PathMax) == 0)
        {
            Close();
            return false;
        }

        std::filesystem::path exePath = std::filesystem::path(wBuffer);
        std::filesystem::path exeName = exePath.filename();
        std::filesystem::path filterPath = std::filesystem::path(m_Data->processFilterPath);

        if(wcslen(m_Data->targetName) > 0)
        {
            std::filesystem::path targetName = std::filesystem::path(m_Data->targetName);
            if(exeName.has_extension() ^ targetName.has_extension())
            {
                exeName = targetName.has_extension() ? exeName / targetName.extension() : exeName;
                targetName = exeName.has_extension() ^ targetName.has_extension() ? targetName / exeName.extension() : targetName;
            }

            if(exeName != targetName)
            {
                Close();
                return false;
            }
        }
        else if(std::filesystem::exists(filterPath))
        {
            std::wfstream fileStream = {};
            fileStream.open(filterPath.wstring());

            std::wstring lowerExeName = exeName.c_str();
            std::transform(lowerExeName.begin(), lowerExeName.end(), lowerExeName.begin(), ::tolower);

            std::wstring line = {};
            while (fileStream.is_open() && std::getline(fileStream, line))
            {
                std::transform(line.begin(), line.end(), line.begin(), ::tolower);
                if (lowerExeName == line)
                {
                    Close();
                    return false;
                }
            }
        }
    }

    return true;
}

void SamplerConnection::Close()
{
    if (m_MappedBuffer)
    {
        UnmapViewOfFile(m_MappedBuffer);
        m_MappedBuffer = nullptr;
    }

    if(m_File)
    {
        CloseHandle(m_File);
        m_File = nullptr;
    }

    m_Data = nullptr;
}

void SamplerConnection::SetVkLayerState(bool isEnabled)
{
    ((DataLayout*)m_Data)->vkLayerState = isEnabled;
}
bool SamplerConnection::GetVkLayerState()
{ 
    return ((DataLayout*)m_Data)->vkLayerState;
}

const wchar_t* SamplerConnection::GetHostExecutablePath()
{ 
    return ((DataLayout*)m_Data)->hostExecutablePath;
}

const wchar_t* SamplerConnection::GetTargetName()
{
    return ((DataLayout*)m_Data)->targetName;
}

uint32_t SamplerConnection::GetHostPID()
{
    return ((DataLayout*)m_Data)->hostPID;
}

const SamplerConnection::DxgiDesc& SamplerConnection::GetDxgiDesc()
{
    return ((DataLayout*)m_Data)->DXGI;
}

void SamplerConnection::SetDxgiDesc(SamplerConnection::DxgiDesc desc)
{
    ((DataLayout*)m_Data)->DXGI = desc;
}

const SamplerConnection::ViewerDesc& SamplerConnection::GetViewerDesc()
{
    return ((DataLayout*)m_Data)->viewerDesc;
}

void SamplerConnection::SetViewerDesc(SamplerConnection::ViewerDesc desc)
{
    ((DataLayout*)m_Data)->viewerDesc = desc;
}

LogBuffer* SamplerConnection::GetLog() 
{
    return m_Data ? &m_Data->logBuffer : nullptr;
}