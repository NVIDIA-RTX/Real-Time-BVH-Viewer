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

#include "NvBvhViewerCommonUtils.h"

NvBvhViewerCommon::PFN_LogMessageCallback g_LogMessageCallback = nullptr;
void LogMessage(NvBvhViewerCommon::MessageType type, const char* message)
{
    if (g_LogMessageCallback)
        g_LogMessageCallback(type, message);
}

void LogWarning(const char* message)
{
    return LogMessage(NvBvhViewerCommon::MessageType::Warning, message);
}

void LogError(const char* message)
{
    return LogMessage(NvBvhViewerCommon::MessageType::Error, message);
}

void LogInfo(const char* message)
{
    return LogMessage(NvBvhViewerCommon::MessageType::Info, message);
}

