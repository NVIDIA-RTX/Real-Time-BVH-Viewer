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
#include <atomic>
#include <thread>

enum class ViewerQueueType : uint32_t
{
    Graphics,
    Compute,
    Count
};

class NvBvhViewerCommon
{
public:
    virtual void ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight) = 0;

    void NotifyWindowClose() const
    {
        if(m_WindowCloseCallback)
            m_WindowCloseCallback(m_CallbackUserData);
    };

    void NotifyWindowResize() const
    {
        if (m_WindowResizeCallback)
            m_WindowResizeCallback(m_OutputWidth, m_OutputHeight, m_CallbackUserData);
    };

    typedef void(_stdcall* PFN_WindowCloseCallback)(void* userData);
    typedef void(_stdcall* PFN_WindowResizeCallback)(uint32_t newWidth, uint32_t newHeight, void* userData);

    enum class MessageType : int32_t { Info, Warning, Error };
    typedef void(_stdcall* PFN_LogMessageCallback)(MessageType type, const char* message);

    bool IsPresentThread() const { return std::this_thread::get_id() == m_PresentThread.get_id(); }

protected:
    bool CreateViewerWindow(const char* api);
    void DestroyViewerWindow();

    bool CreatePresentThread(uint32_t(*presentImpl)(void*));
    void DestroyAsyncPresentThread();
    void LogMessage(MessageType type, const char* message);

protected:
    void* m_Window = nullptr;
    std::thread m_PresentThread = {};
    uint32_t m_OutputWidth = 0;
    uint32_t m_OutputHeight = 0;

    PFN_WindowCloseCallback m_WindowCloseCallback = nullptr;
    PFN_WindowResizeCallback m_WindowResizeCallback = nullptr;
    void* m_CallbackUserData = nullptr;
    std::atomic_flag m_Lock = ATOMIC_FLAG_INIT;
};