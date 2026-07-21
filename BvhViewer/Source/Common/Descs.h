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
#include <algorithm>
#include <vector>
#include <stdint.h>
#include <ml.h>
#include <CPU_Bridge.h>

static constexpr uint32_t c_MaxTlasPerFrame = 2;

struct RtFlagDesc
{
    const char* str;
    uint32_t val;
};

constexpr RtFlagDesc g_RtInstanceFlagDescs[] = 
{
    { "TriangleCullDisable",           INSTANCE_FLAG_TRIANGLE_CULL_DISABLE },
    { "TriangleFrontCounterClockwise", INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE },
    { "ForceOpaque",                   INSTANCE_FLAG_FORCE_OPAQUE },
    { "ForceNonOpaque",                INSTANCE_FLAG_FORCE_NON_OPAQUE },
    { "Force2StateOMM",                INSTANCE_FLAG_FORCE_OMM_2_STATE_EX },
    { "DisableOMM",                    INSTANCE_FLAG_DISABLE_OMMS_EX },
};

constexpr RtFlagDesc g_RtGeometryFlagDescs[ ] =
{
    { "Opaque",            GEOMETRY_FLAG_OPAQUE },
    { "NoDuplicateAnyhit", GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION },
};

constexpr RtFlagDesc g_RtBuildFlagDesc[ ] =
{
    // d3d12 and vk matched flags
    { "AllowUpdate",        AS_BUILD_FLAG_ALLOW_UPDATE },
    { "AllowCompaction",    AS_BUILD_FLAG_ALLOW_COMPACTION },
    { "FastTrace",          AS_BUILD_FLAG_PREFER_FAST_TRACE },
    { "FastBuild",          AS_BUILD_FLAG_PREFER_FAST_BUILD },
    { "MinimizeMemory",     AS_BUILD_FLAG_MINIMIZE_MEMORY },
    { "Compacted",          AS_BUILD_FLAG_COMPACTED },
    { "PerformUpdate",      AS_BUILD_FLAG_PERFORM_UPDATE },
#if defined( VIEWER_API_VK )
    { "Motion",             AS_BUILD_FLAG_MOTION },
    { "AllowOmmUpdate",     AS_BUILD_FLAG_ALLOW_OMM_UPDATE },
    { "AllowOmmDisable",    AS_BUILD_FLAG_ALLOW_OMM_DISABLE },
    { "AllowOmmDataUpdate", AS_BUILD_FLAG_ALLOW_OMM_DATA_UPDATE },
    { "AllowDmmUpdate",     AS_BUILD_FLAG_ALLOW_DMM_UPDATE },
    { "AllowDataAccess",    AS_BUILD_FLAG_ALLOW_DATA_ACCESS },
#endif
    { "HasOmmClusters",     AS_BUILD_FLAG_HAS_OMM_CLUSTERS },
    { "Clustered",          AS_BUILD_FLAG_CLUSTERED },
};

static_assert(INSTANCE_FLAG_NUM == std::size(g_RtInstanceFlagDescs));
static_assert(GEOMETRY_FLAG_NUM == std::size(g_RtGeometryFlagDescs));
static_assert(AS_BUILD_FLAG_NUM == std::size(g_RtBuildFlagDesc));

enum class RtGeometryTypes : uint16_t
{
    None,
    Triangles,
    OmmTriangles,
    AABB,
    LSS,
    Spheres,
    DmmTriangles,
    Count
};

struct ViewerGeometryDesc
{
    uint16_t type;
    uint16_t flags;
    uint32_t data[3];
};

struct ViewerRtGeometryDescGPU
{ // packed to 8 bit
#if defined( VIEWER_API_VK )
    uint32_t desc; // glsl structure alignment..
#else
    uint8_t desc;
#endif
};

struct ViewerBlasDesc
{ // cpu side representation for tracking
    std::vector<ViewerGeometryDesc> geometries;
    uint64_t address;
    uint32_t hash;
    uint32_t updateFrameId;
    uint32_t lastReferenceFrameId;
    uint32_t buildFlags;
};

enum class CameraUp : int32_t
{
    Y,
    Z,
    Count
};

constexpr const char* g_CameraUpNames[ ] =
{
    "Y Up",
    "Z Up"
};
static_assert(std::size(g_CameraUpNames) == size_t(CameraUp::Count));

enum class TlasSortModes
{
    InstanceNum,
    UpdateOrder,
    Count
};

constexpr const char* g_TlasSortModeNames[ ] =
{
    "InstanceNum",
    "UpdateOrder"
};
static_assert(std::size(g_TlasSortModeNames) == size_t(TlasSortModes::Count));

constexpr const char* g_VisualizationModeNames[ ] =
{
    "BlasAddress",
    "PrimitiveIndex",
    "GeometryIndex",
    "InstanceIndex",
    "InstanceId",
    "ContributionToHitGroupIndex",
    "ClusterId",
    "InstanceMask",
    "InstanceFlags",
    "UpdateState",
    "Flat Normal",
    "Heatmap",
};
static_assert(std::size(g_VisualizationModeNames) == size_t(VIS_MODE_NUM));

constexpr const char* g_FlagVisualizationModes[ ] =
{
    "Highlight if",
    "Show if",
};
constexpr const char* g_FlagInclusionModes[ ] =
{
    "has",
    "doesn't have"
};
constexpr const char* g_FlagLogicalOperatorModes[ ] =
{
    "all of selected flags",
    "any of selected flags"
};

enum class RtGeometryPrimitiveTypes
{
    Triangle,
    OmmTriangle,
    ClusterTriangle,
    AABB,
    LSS,
    Sphere,
    Count,
};

constexpr const char* g_GeometryPrimitiveNames[ ] =
{
    "Triangle",
    "OmmTriangle",
    "ClusterTriangle",
    "AABB",
    "LSS",
    "Sphere"
};

enum class Vendor
{
    NV,
    AMD,
    Unknown
};

struct ExposedSettings
{
    int visualizationMode = VIS_MODE_BLAS_ADDRESS;
    int tlasSortMode = int(TlasSortModes::InstanceNum);
    int tlasId = 0;

    int flagVisualizationMode = 0;
    int flagInclusionMode = 0;
    int flagLogicalOperator = 0;

    uint32_t lastSelectedBlasHash = 0xFFFFFFFF;
    uint32_t lastSelectedGeometryId = 0;
    float heatmapScaling = 1.0f;
    float fpsLimit = 60.0f;
    float mouseSensitivity = 3.0f;
    bool flipCameraX = false;
    bool enableFpsLimit = false;
    bool allowLastKnownBvhRender = false;
    bool windowOnTop = false;
    bool primitiveTypeToggles[uint32_t(RtGeometryPrimitiveTypes::Count)] = {};

    bool instanceFlagStates[INSTANCE_FLAG_NUM] = {};
    bool buildFlagStates[AS_BUILD_FLAG_NUM] = {};
    bool geometryFlagStates[GEOMETRY_FLAG_NUM] = {};

    bool perBitInstanceMaskValue[8] = {};
    bool enableBarycentricShading = false;
    bool enableWireShading = true;
    bool enableWireframeMode = false;
    bool cullOpaque = false;
    bool cullNonOpaque = false;
    bool cullBackFace = false;
    bool cullFrontFace = false;
    uint8_t instanceInclusionMask = 0xFF;
    int32_t numSamples = 1;
    int32_t debugUint = 0;
    CameraUp cameraUp = CameraUp::Y;

    ExposedSettings()
    {
        for(uint32_t i = 0; i < uint32_t(RtGeometryPrimitiveTypes::Count); ++i)
            primitiveTypeToggles[i] = true;
    }
};
