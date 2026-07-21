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
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <vector>
#include <array>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <set>
#include <thread>
#include <string>
#include <stdint.h>

#include "NvBvhViewerCommon.h"

#include "GLFW/glfw3.h"

#include "ml.h"
#include "CPU_Bridge.h"

#include "ScopedLock.h"
#include "Timer.h"
#include "Descs.h"
#include "Resources.h"

#include "Compiled_Shaders.h"
#include "Pipelines.h"

#include "Camera.h"
#include "ConstantBuffer.h"

#include "BlasTracker.h"
#include "Presenter.h"

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
#else
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#endif // _WIN32

#include "GLFW/glfw3native.h"

#if defined(_WIN32)
inline HWND GetWindowHandle(void* window)
{
    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(window);
    return glfwGetWin32Window(glfwWindow);
}
#endif // _WIN32

template<typename T, typename T1>
inline T Align(T s, T1 a)
{
    T a1 = T(a);
    return ((s + a1 - 1) / a1) * a1;
}

inline uint32_t DivideRoundUp(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

inline uint64_t DivideRoundUp(uint64_t a, uint64_t b)
{
    return (a + b - 1) / b;
}

extern NvBvhViewerCommon::PFN_LogMessageCallback g_LogMessageCallback;
void LogMessage(NvBvhViewerCommon::MessageType type, const char* message);
void LogWarning(const char* message);
void LogError(const char* message);
void LogInfo(const char* message);

#if !defined(LOG_WARNING_ONCE)
#define LOG_WARNING_ONCE(msg) \
    { \
        static bool warned = false;\
        if(!warned)\
        { \
            LogMessage(NvBvhViewerCommon::MessageType::Warning, msg);\
            warned = true;\
        } \
    } \

#endif //!defined(LOG_WARNING_ONCE)