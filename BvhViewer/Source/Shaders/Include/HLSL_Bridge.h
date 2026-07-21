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

#define TLAS_BINDING( name, slot )\
RaytracingAccelerationStructure name          : register ( t##slot );

#define TYPED_BUFFER_BINDING( format, name, slot )\
Buffer<format>  name                          : register ( t##slot );

#define STRUCTURED_BUFFER_BINDING( format, name, slot )\
StructuredBuffer<format> name                 : register ( t##slot );

#define RAW_BUFFER_BINDING( name, slot )\
ByteAddressBuffer name                        : register ( t##slot );

#define RAW_BUFFER_STORAGE_BINDING( name, slot )\
RWByteAddressBuffer name                      : register ( u##slot );

#define TYPED_STORAGE_BUFFER_BINDING( format, name, slot )\
RWBuffer<format> name                         : register ( u##slot );

#define STORAGE_TEXTURE_BINDING( format, name, slot )\
RWTexture2D< format > name                    : register ( u##slot );

#define STORAGE_STRUCTURED_BUFFER_BINDING( format, name, slot )\
RWStructuredBuffer< format > name             : register ( u##slot );


#define CONSTANT_BUFFER_BINDING( format, name, slot )\
cbuffer name##_cb                             : register ( b##slot )\
{\
    format name;\
};

#define PUSH_CONSTANTS_BINDING( constantsLayout, name, slot )\
struct name##_layout\
{\
    constantsLayout\
};\
CONSTANT_BUFFER_BINDING( name##_layout, name, slot )

////////////////////////////////////////////////////////////////////////
#define DECLARE_THREAD_GROUP_SIZE( x, y, z )\
[ numthreads( x, y, z ) ]

#define INIT_DISPATCH_THREAD_ID_SEMANTIC( type, name )\
type name : SV_DispatchThreadId

#define BEGIN_MAIN( THREAD_GROUP, SEMANTICS )\
THREAD_GROUP \
void main( SEMANTICS ) {

#define END_MAIN }

////////////////////////////////////////////////////////////////////////
#define GROUP_BARRIER()         GroupMemoryBarrierWithGroupSync()
#define WAVE_SUM( x )           WaveActiveSum( x )
#define WAVE_ANY_TRUE( x )      WaveActiveAnyTrue( x )
#define WAVE_IS_FIRST_LANE()    WaveIsFirstLane()
#define ATOMIC_ADD( dest, x )   InterlockedAdd( dest, x )
#define ATOMIC_OR( dest, x )    InterlockedOr( dest, x )

////////////////////////////////////////////////////////////////////////
#define RAY_QUERY RayQuery< RAY_FLAG_NONE >
#define RayQueryTraceRayInline( rayQuery, tlas, rayFlags, instanceMask, rayDesc )\
rayQuery.TraceRayInline( tlas, rayFlags, instanceMask, rayDesc )

#define RayQueryAbort( rayQuery )                                        rayQuery.Abort()
#define RayQueryProceed( rayQuery )                                      rayQuery.Proceed()
#define RayQueryCandidateType( rayQuery  )                               rayQuery.CandidateType()
#define RayQueryCommittedStatus( rayQuery )                              rayQuery.CommittedStatus()
#define RayQueryWorldRayOrigin( rayQuery )                               rayQuery.WorldRayOrigin()
#define RayQueryWorldRayDirection( rayQuery )                            rayQuery.WorldRayDirection()

#define RayQueryCandidateRayT( rayQuery )                                rayQuery.CandidateTriangleRayT()
#define RayQueryCandidatePrimitiveIndex( rayQuery )                      rayQuery.CandidatePrimitiveIndex()
#define RayQueryCandidateInstanceIndex( rayQuery )                       rayQuery.CandidateInstanceIndex()
#define RayQueryCandidateInstanceContributionToHitGroupIndex( rayQuery ) rayQuery.CandidateInstanceContributionToHitGroupIndex()
#define RayQueryCandidateGeometryIndex( rayQuery )                       rayQuery.CandidateGeometryIndex()
#define RayQueryCandidateInstanceID( rayQuery )                          rayQuery.CandidateInstanceID()
#define RayQueryCandidateTriangleBarycentrics( rayQuery )                rayQuery.CandidateTriangleBarycentrics()
#define RayQueryCandidateObjectToWorld( rayQuery )                       rayQuery.CandidateObjectToWorld3x4()

#define RayQueryCommitNonOpaqueTriangleHit( rayQuery )                   rayQuery.CommitNonOpaqueTriangleHit()
#define RayQueryCommittedRayT( rayQuery )                                rayQuery.CommittedRayT()
#define RayQueryCommittedPrimitiveIndex( rayQuery )                      rayQuery.CommittedPrimitiveIndex()
#define RayQueryCommittedInstanceIndex( rayQuery )                       rayQuery.CommittedInstanceIndex()
#define RayQueryCommittedInstanceContributionToHitGroupIndex( rayQuery ) rayQuery.CommittedInstanceContributionToHitGroupIndex()
#define RayQueryCommittedGeometryIndex( rayQuery )                       rayQuery.CommittedGeometryIndex()
#define RayQueryCommittedInstanceID( rayQuery )                          rayQuery.CommittedInstanceID()
#define RayQueryCommittedTriangleBarycentrics( rayQuery )                rayQuery.CommittedTriangleBarycentrics()
#define RayQueryCommittedObjectToWorld( rayQuery )                       rayQuery.CommittedObjectToWorld3x4()

#if defined( NV_GPU )
    #define RayQueryCandidateTrianglePositions( rayQuery )                NvRtCandidateTriangleObjectPositions( rayQuery )
    #define RayQueryCandidateIsLss( rayQuery )                            NvRtCandidateIsNonOpaqueLss( rayQuery )
    #define RayQueryCandidateIsSphere( rayQuery )                         NvRtCandidateIsNonOpaqueSphere( rayQuery )
    #define RayQueryCandidateLssPositionsAndRadii( rayQuery )             NvRtCandidateLssObjectPositionsAndRadii( rayQuery )
    #define RayQueryCandidateGetClusterIndex( rayQuery )                  NvRtGetCandidateClusterID( rayQuery )

    #define RayQueryCommittedTrianglePositions( rayQuery )                NvRtCommittedTriangleObjectPositions( rayQuery )
    #define RayQueryCommittedIsLss( rayQuery )                            NvRtCommittedIsLss( rayQuery )
    #define RayQueryCommittedIsSphere( rayQuery )                         NvRtCommittedIsSphere( rayQuery )
    #define RayQueryCommittedLssPositionsAndRadii( rayQuery )             NvRtCommittedLssObjectPositionsAndRadii( rayQuery )
    #define RayQueryCommittedGetClusterIndex( rayQuery )                  NvRtGetCommittedClusterID( rayQuery )
#endif

////////////////////////////////////////////////////////////////////////
#define Image2dStore( image, addr, x ) image[ addr ] = x;