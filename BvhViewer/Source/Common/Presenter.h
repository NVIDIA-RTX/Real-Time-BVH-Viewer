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
#include "Descs.h"
#include "ConstantBuffer.h"
#include "BlasTracker.h"

struct UpdateDesc
{
    Camera* camera;
    ExposedSettings* settings;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t numBlasDescs;
    uint32_t frameId;
    uint32_t numBufferedFrames;
};

struct PresenterNewFrameDesc
{
    ConstantBufferHelper* constantBufferHelper = nullptr;
    ConstantBufferHelper::UpdateDesc cbUpdateDesc = {};
    BlasTracker* blasTracker;
    void* feedbackData;
    void* window;
    uint64_t tlasAddress;
    uint32_t tlasId;
    uint32_t tlasFlags;
    uint32_t numTlasesInFrame;
    uint32_t numInstancesInTlas;
    bool enablePositionFetch = true;
};

void PresenterNewFrame(PresenterNewFrameDesc& desc);
void BusyWait(float fpsLimit, bool enableLimit);
bool SkipPresent(bool isNewAppFrame, bool enableFpsLimit);
bool PollWindowEvents(void* window);
