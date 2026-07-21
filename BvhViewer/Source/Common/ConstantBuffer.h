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
#include "Camera.h"
#include <ml.h>
#include "CPU_Bridge.h"

struct ConstantBufferHelper
{ // buffer cb data to avoid read/write conflicts of game and present threads
    struct UpdateDesc
    {
        Camera* camera;
        ExposedSettings* settings;
        uint32_t renderWidth;
        uint32_t renderHeight;
        uint32_t numBlasDescs;
        uint32_t frameId;
        uint32_t numBufferedFrames;
        uint32_t blasMapCapacity;
    };

    void Update(Camera& camera, ExposedSettings& settings, uint32_t renderWidth, uint32_t renderHeight, uint32_t numBlasDescs, uint32_t frameId);
    void Update(UpdateDesc& desc);
    const ConstantBufferLayout& GetConstantBuffer() { return m_Data[m_CurrentId]; }
private:
    static constexpr uint32_t c_Buffering = 2;
    uint32_t m_CurrentId = 0;
    ConstantBufferLayout m_Data[c_Buffering];
};