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

#include "Camera.h"

void Camera::Initialize(const float3& position, const float3& lookAt, bool isRelative)
{
    float3 dir = normalize(lookAt - position);

    float3 rot;
    rot.x = atan2(dir.y, dir.x);
    rot.y = asin(dir.z);
    rot.z = 0.0f;

    state.globalPosition = double3(position);
    state.rotation = degrees(rot);

    m_IsRelative = isRelative;
}

void Camera::Update(const CameraDesc& desc)
{
    uint32_t projFlags = desc.isProjectionReversed ? PROJ_REVERSED_Z : 0;
    projFlags |= desc.isLeftHanded ? PROJ_LEFT_HANDED : 0;

    // Position
    {
        const float3 vRight = state.mWorldToView.Row(0).xyz;
        const float3 vUp = state.mWorldToView.Row(1).xyz;
        const float3 vForward = state.mWorldToView.Row(2).xyz;

        float3 delta = float3(desc.dLocalSide, desc.dLocalVertical, desc.dLocalForward);
        delta = delta * desc.timeScale;
        delta.z *= desc.isLeftHanded ? 1.0f : -1.0f;

        state.globalPosition += double3(vRight * delta.x);
        state.globalPosition += double3(vUp * delta.y);
        state.globalPosition += double3(vForward * delta.z);
        state.globalPosition += double3(desc.dUser);
    }

    if (desc.limits.IsValid())
        state.globalPosition = clamp(state.globalPosition, double3(desc.limits.vMin), double3(desc.limits.vMax));

    if (desc.isCustomMatrixSet)
    {
        const float3 vCustomRight = desc.customMatrix.Row(3).xyz;
        state.globalPosition = double3(vCustomRight);
    }

    if (m_IsRelative)
    {
        state.position = float3::Zero();
        statePrev.position = float3(statePrev.globalPosition - state.globalPosition);
        statePrev.mWorldToView.PreTranslation(-statePrev.position);
    }
    else
    {
        state.position = float3(state.globalPosition);
        statePrev.position = float3(statePrev.globalPosition);
    }

    // Rotation
    float angularSpeed = 0.03f * saturate(desc.horizontalFov * 0.5f / 90.0f);

    {
        state.rotation.x += desc.dYaw * angularSpeed * (desc.flipX ? -1.0f : 1.0f);
        state.rotation.y += desc.dPitch * angularSpeed;

        state.rotation.x = fmod(state.rotation.x, 360.0f);
        state.rotation.y = clamp(state.rotation.y, -90.0f, 90.0f);
    }

    if (desc.isCustomMatrixSet)
    {
        state.mViewToWorld = desc.customMatrix;

        state.rotation = degrees(state.mViewToWorld.GetRotationYPR());
        state.rotation.z = 0.0f;
    }
    else
    {
        state.mViewToWorld.SetupByRotationYPR(radians(state.rotation.x), radians(state.rotation.y), radians(state.rotation.z));
    }

    if (desc.up == CameraUp::Z)
    {
        float4x4 rotationMat = {};
        rotationMat.SetupByRotationYPR(radians(0.0f), radians(-90.0f), radians(0.0f));
        state.mViewToWorld = rotationMat * state.mViewToWorld;
    }

    if (desc.flipX)
    {
        float4x4 scaleMat = {};
        scaleMat.SetupByScale(float3(-1.0f, 1.0f, 1.0f));
        state.mViewToWorld = state.mViewToWorld * scaleMat;
    }

    state.mWorldToView = state.mViewToWorld;
    state.mWorldToView.WorldToView(projFlags);
    state.mWorldToView.PreTranslation(-state.position);

    // Projection
    if (desc.orthoRange > 0.0f)
    {
        float x = desc.orthoRange;
        float y = desc.orthoRange / desc.aspectRatio;
        state.mViewToClip.SetupByOrthoProjection(-x, x, -y, y, desc.nearZ, desc.farZ, projFlags);
    }
    else
    {
        if (desc.farZ == 0.0f)
            state.mViewToClip.SetupByHalfFovxInf(0.5f * radians(desc.horizontalFov), desc.aspectRatio, desc.nearZ, projFlags);
        else
            state.mViewToClip.SetupByHalfFovx(0.5f * radians(desc.horizontalFov), desc.aspectRatio, desc.nearZ, desc.farZ, projFlags);
    }

    // Other
    state.mViewToWorld = state.mWorldToView;
    state.mViewToWorld.InvertOrtho();

    uint32_t flags = 0;
    DecomposeProjection(STYLE_D3D, STYLE_D3D, state.mViewToClip, &flags, nullptr, nullptr, state.frustum.a, nullptr, nullptr);
    m_IsOrtho = (flags & PROJ_ORTHO) == 0 ? 0.0f : -1.0f;

    // Previous other
    statePrev.mViewToWorld = statePrev.mWorldToView;
    statePrev.mViewToWorld.InvertOrtho();
}