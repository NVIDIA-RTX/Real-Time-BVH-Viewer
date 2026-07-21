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

#include <string>
#include <filesystem>
#include "NvBvhViewerCommon.h"
#include <GLFW/glfw3.h>
#include "imgui_impl_glfw.h"
#include "NvBvhViewerCommonUtils.h"

void NvBvhViewerCommon::LogMessage(MessageType type, const char* message)
{
    if (g_LogMessageCallback)
        g_LogMessageCallback(type, message);
}

void NvBvhViewerCommon::DestroyAsyncPresentThread()
{
    if (!m_PresentThread.native_handle())
        return;

    const std::thread::id currentThreadId = std::this_thread::get_id();
    if(currentThreadId == m_PresentThread.get_id())
        return; // present thread has to be closed from a different thread

    if(m_PresentThread.joinable())
        m_PresentThread.join();

    m_PresentThread = {};
}

bool NvBvhViewerCommon::CreatePresentThread(uint32_t(*presentImpl)(void*))
{
    if (m_PresentThread.joinable())
        return false;

    m_PresentThread = std::thread(presentImpl, this);
    if (!m_PresentThread.joinable())
    {
        LogMessage(NvBvhViewerCommon::MessageType::Error, "NvBvhViewerCommon::CreatePresentThread: CreateThread() failed");
        return false;
    }

    return true;
}

void NvBvhViewerCommon::ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight)
{
    if (m_OutputWidth == newWidth && m_OutputHeight == newHeight)
        return;
    if (!newWidth || !newHeight)
        return;

    m_OutputWidth = newWidth;
    m_OutputHeight = newHeight;
}

void NvBvhViewerCommon::DestroyViewerWindow()
{
    if (!m_Window)
        return;

    GLFWwindow* glfwWindow = static_cast<GLFWwindow*>(m_Window);
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(glfwWindow);
    glfwTerminate();
    m_Window = nullptr;
}

void GlfwWindowResizeCallback(GLFWwindow* window, int width, int height)
{
    NvBvhViewerCommon* ptr = (NvBvhViewerCommon*)glfwGetWindowUserPointer(window);
    if(!ptr) 
        return;
    ptr->ResizeOutputWindow(uint32_t(width), uint32_t(height));
    ptr->NotifyWindowResize();
}

typedef void (*GLFWwindowclosefun)(GLFWwindow* window);
void GlfwWindowCloseCallback(GLFWwindow* window)
{
    NvBvhViewerCommon* ptr = (NvBvhViewerCommon*)glfwGetWindowUserPointer(window);
    if(ptr)
        ptr->NotifyWindowClose();
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void GlfwErrorCallback(int32_t error, const char* message) {
    printf("GLFW error[%d]: %s\n", error, message);
}

bool NvBvhViewerCommon::CreateViewerWindow(const char* api)
{
    if(glfwInit() != GLFW_TRUE)
    {
        LogMessage(MessageType::Error, "NvBvhViewerCommon::CreateViewerWindow: Failed to initialize GLFW");
        return false;
    }

    glfwSetErrorCallback(GlfwErrorCallback);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, 0);
    glfwWindowHint(GLFW_DECORATED, 1);
    glfwWindowHint(GLFW_RESIZABLE, 1);

    std::string title = "NV BVH Viewer";
    title += " [" + std::string(api) + "]";

#if defined(_WIN32)
    {
        const DWORD bufferSize = 1024;
        char buffer[bufferSize] = {};
        GetModuleFileNameA(NULL, buffer, bufferSize);
        std::filesystem::path modulePath = std::filesystem::path(buffer);
        std::string executable = modulePath.filename().generic_string();
        title += "[" + executable + "]";
    }
#endif

    GLFWwindow* window = glfwCreateWindow(int(m_OutputWidth), int(m_OutputHeight), title.c_str(), nullptr, nullptr);

    if (!window)
    {
        glfwTerminate();
        LogMessage(MessageType::Error, "NvBvhViewerCommon::CreateViewerWindow: glfwCreateWindow() failed");
        return false;
    }

    glfwSetWindowUserPointer(window, this);

    { // init imgui
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = NULL;

        #if defined(VIEWER_API_VK)
            ImGui_ImplGlfw_InitForVulkan(window, true);
        #else
            ImGui_ImplGlfw_InitForOther(window, true);
        #endif

        IMGUI_CHECKVERSION();
        ImGui::StyleColorsDark();
    }

    glfwSetFramebufferSizeCallback(window, GlfwWindowResizeCallback);
    glfwSetWindowCloseCallback(window, GlfwWindowCloseCallback);
    glfwShowWindow(window);
    m_Window = window;
    return true;
}