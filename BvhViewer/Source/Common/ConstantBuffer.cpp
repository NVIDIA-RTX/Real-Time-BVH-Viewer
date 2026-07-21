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

#include <imgui.h>
#include "ConstantBuffer.h"
#include "Descs.h"

void ConstantBufferHelper::Update(UpdateDesc& desc)
{
    if(desc.camera == nullptr || desc.settings == nullptr)
        return;

    Camera& camera = *desc.camera;
    ExposedSettings& settings = *desc.settings;
    float renderWidth = static_cast<float>(desc.renderWidth);
    float renderHeight = static_cast<float>(desc.renderHeight);

    uint32_t id = (m_CurrentId + 1) % c_Buffering;
    ConstantBufferLayout& layout = m_Data[id];

    const CameraState& state = camera.state;
    layout.viewToWorld = state.mViewToWorld;
    layout.worldToClip = state.mViewToClip * state.mWorldToView;
    layout.frustum = state.frustum;
    layout.renderSize = float2(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
    layout.invRenderSize = float2(1.0f / float(renderWidth), 1.0f / float(renderHeight));
    layout.visualizationMode = uint32_t(settings.visualizationMode);
    layout.numBlasDescs = desc.numBlasDescs;

    layout.flagMask = 0;
    {
        uint32_t instanceFlags = 0;
        for (uint32_t i = 0; i < INSTANCE_FLAG_NUM; ++i)
            instanceFlags |= settings.instanceFlagStates[i] ? g_RtInstanceFlagDescs[i].val : 0;

        uint32_t blasFlags = 0;
        for (uint32_t i = 0; i < AS_BUILD_FLAG_NUM; ++i)
            blasFlags |= settings.buildFlagStates[i] ? g_RtBuildFlagDesc[i].val : 0;

        uint32_t geometryFlags = 0;
        for (uint32_t i = 0; i < GEOMETRY_FLAG_NUM; ++i)
            geometryFlags |= settings.geometryFlagStates[i] ? g_RtGeometryFlagDescs[i].val : 0;

        layout.flagMask = _PackGeometryFlagMask(instanceFlags, blasFlags, geometryFlags);
    }

    ImGuiIO& io = ImGui::GetIO();
    layout.mousePos.x = static_cast<uint32_t>(io.MousePos.x);
    layout.mousePos.y = static_cast<uint32_t>(io.MousePos.y);

    layout.lastSelectedBlasHash = settings.lastSelectedBlasHash;
    layout.lastSelectedGeometryId = settings.lastSelectedGeometryId;
    layout.heatmapScaling = settings.heatmapScaling;
    layout.instanceInclusionMask = settings.instanceInclusionMask;

    layout.flagFilterMask = _PackFilterFlags(settings.flagVisualizationMode, settings.flagInclusionMode, settings.flagLogicalOperator);
    layout.frameId = desc.frameId;
    layout.numBufferedFrames = static_cast<uint16_t>(desc.numBufferedFrames);
    layout.blasMapCapacity = desc.blasMapCapacity;

    layout.geometryTypeMask = 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::Triangle)]        ? GEOMETRY_TYPE_TRIANGLE : 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::OmmTriangle)]     ? GEOMETRY_TYPE_OMM_TRIANGLE : 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::AABB)]            ? GEOMETRY_TYPE_AABB : 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::LSS)]             ? GEOMETRY_TYPE_LSS : 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::Sphere)]          ? GEOMETRY_TYPE_SPHERE : 0;
    layout.geometryTypeMask |= settings.primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::ClusterTriangle)] ? GEOMETRY_TYPE_CLUSTER_TRIANGLE : 0;

    layout.packedBools = 0;
    layout.packedBools |= settings.enableBarycentricShading ? PACKED_BOOL_ENABLE_BARY     : 0;
    layout.packedBools |= settings.enableWireShading        ? PACKED_BOOL_ENABLE_WIRE     : 0;
    layout.packedBools |= settings.enableWireframeMode      ? PACKED_BOOL_ENABLE_FRAME    : 0;
    layout.packedBools |= settings.cullOpaque               ? PACKED_BOOL_CULL_OPAQUE     : 0;
    layout.packedBools |= settings.cullNonOpaque            ? PACKED_BOOL_CULL_NON_OPAQUE : 0;
    layout.packedBools |= settings.cullBackFace             ? PACKED_BOOL_CULL_BACK_FACE  : 0;
    layout.packedBools |= settings.cullFrontFace            ? PACKED_BOOL_CULL_FRONT_FACE : 0;
    layout.numSamples = static_cast<uint16_t>(settings.numSamples);
    layout.debugUint = settings.debugUint;
    m_CurrentId = id;
}