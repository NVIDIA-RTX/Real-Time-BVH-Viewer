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
#include <filesystem>
#include "Connection.h"
#include "../Common/NvBvhViewerCommon.h"
extern SamplerConnection g_Connection;

static void LogMessage(const char* type, const char* message)
{
    if (g_Connection.IsOpened() == false)
        return;

    if (g_Connection.GetLog() == nullptr)
        return;

    static std::string processName = {};
    if (processName.empty())
    {
        char fullName[4096] = {};
        GetModuleFileNameA(NULL, fullName, 4096);
        std::filesystem::path fullPath = std::filesystem::path(fullName);
        processName = std::string("[");
        processName += fullPath.filename().generic_string();
        processName += "]";
    }

    std::string finalMessage = processName;

    std::string timestamp = {};
    {
        time_t t = std::time(nullptr);
        tm tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "[%d-%m-%Y %H-%M-%S]");
        timestamp = oss.str();
    }
    finalMessage += timestamp;

    std::string typeStr = type;
    finalMessage += typeStr;

    finalMessage += std::string(message);
    g_Connection.GetLog()->Post(finalMessage.c_str());
}

static void LogViewerMessage(NvBvhViewerCommon::MessageType type, const char* message)
{
    std::string typeStr = {};
    switch (type)
    {
    case NvBvhViewerCommon::MessageType::Info: typeStr = "[Info]"; break;
    case NvBvhViewerCommon::MessageType::Warning: typeStr = "[WARNING]"; break;
    case NvBvhViewerCommon::MessageType::Error: typeStr = "[ERROR]"; break;
    default: typeStr = "[Unknown]"; break;
    }
    LogMessage(typeStr.c_str(), message);
}