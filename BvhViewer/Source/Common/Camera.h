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
#include "Descs.h"
#include <ml.h>

struct CameraDesc
{
    cBoxf limits = {};
    float3 dUser = float3(0.0f);
    float dLocalForward = 0.0f;
    float dLocalSide = 0.0f;
    float dLocalVertical = 0.0f;
    float dYaw = 0.0f; // deg
    float dPitch = 0.0f; // deg
    float aspectRatio = 1.0f;
    float horizontalFov = 90.0f; // deg
    float nearZ = 0.1f;
    float farZ = 10000.0f;
    float orthoRange = 0.0f;
    float timeScale = 0.5f;
    bool isProjectionReversed = false;
    bool isLeftHanded = true;
    bool isCustomMatrixSet = false;
    bool flipX = false;
    float4x4 customMatrix = float4x4::Identity();
    CameraUp up = CameraUp::Y;
};

struct CameraState
{
    float4x4 mWorldToView = float4x4::Identity();
    float4x4 mViewToWorld = float4x4::Identity();
    float4x4 mViewToClip = float4x4::Identity();

    double3 globalPosition = {};
    float4 frustum = {};
    float3 position = {};
    float3 rotation = {};
    float motionScale = 0.015f;
};

class Camera
{ // from NriFramework
public:
    void Update(const CameraDesc& desc);
    void Initialize(const float3& position, const float3& lookAt, bool isRelative = false);

    inline void SavePreviousState()
    {
        statePrev = state;
    }

    inline const float3 GetRelative(const double3& origin) const
    {
        double3 position = m_IsRelative ? state.globalPosition : double3(0.0);
        return float3(origin - position);
    }

    inline void* GetState()
    {
        return &state;
    }

    static inline uint32_t GetStateSize()
    {
        return sizeof(CameraState);
    }

public:
    CameraState state = {};
    CameraState statePrev = {};
    float m_IsOrtho = 0.0f;
private:
    bool m_IsRelative = false;
};