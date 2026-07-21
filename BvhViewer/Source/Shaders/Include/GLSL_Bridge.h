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

#if !defined( GLSL )
    #define GLSL
#endif

#define uint2                  uvec2
#define uint3                  uvec3
#define uint4                  uvec4
#define int2                   ivec2
#define int3                   ivec3
#define int4                   ivec4
#define float4                 vec4
#define float3                 vec3
#define float2                 vec2
#define float2x4               mat2x4
#define float3x3               mat3x3
#define float3x4               mat3x4
#define float4x3               mat4x3
#define float4x4               mat4x4
#define saturate( x )          clamp( x, 0.0, 1.0 )
#define lerp                   mix
#define static

////////////////////////////////////////////////////////////////////////
float4 mul( float4x4 m, float4 v ) { return v * transpose( m ); }
float3 mul( float3x4 m, float4 v ) { return transpose( m ) * v; };
float3 mul( float3x3 m, float3 v ) { return v * transpose( m ); };

float   asfloat( uint x )   { return uintBitsToFloat( x ); }
float2  asfloat( uint2 x )  { return uintBitsToFloat( x ); }
float3  asfloat( uint3 x )  { return uintBitsToFloat( x ); }
float4  asfloat( uint4 x )  { return uintBitsToFloat( x ); }

float   asfloat( int x )    { return intBitsToFloat( x ); }
float2  asfloat( int2 x )   { return intBitsToFloat( x ); }
float3  asfloat( int3 x )   { return intBitsToFloat( x ); }
float4  asfloat( int4 x )   { return intBitsToFloat( x ); }

uint    asuint( float x )   { return floatBitsToUint( x ); }
uint2   asuint( float2 x )  { return floatBitsToUint( x ); }
uint3   asuint( float3 x )  { return floatBitsToUint( x ); }
uint4   asuint( float4 x )  { return floatBitsToUint( x ); }

int     asint( float x )    { return floatBitsToInt( x ); }
int2    asint( float2 x )   { return floatBitsToInt( x ); }
int3    asint( float3 x )   { return floatBitsToInt( x ); }
int4    asint( float4 x )   { return floatBitsToInt( x ); }


////////////////////////////////////////////////////////////////////////
#define tRegShift( slot ) slot + 0
#define uRegShift( slot ) slot + 10
#define bRegShift( slot ) slot + 20
#define sRegShift( slot ) slot + 30

#define TLAS_BINDING( name, slot )\
layout( set = 0, binding = tRegShift( slot ) ) uniform accelerationStructureEXT name;
//..................................................................................

#define TYPED_BUFFER_BINDING( format, name, slot )\
layout( set = 0, binding = tRegShift( slot ), std430 ) buffer name##_buffer\
{ restrict readonly format name[ ]; };
//..................................................................................

#define STRUCTURED_BUFFER_BINDING( format, name, slot )\
layout( set = 0, binding = tRegShift( slot ), std430 ) buffer name##_buffer\
{ restrict readonly format name[ ]; };
//..................................................................................

#define STORAGE_TEXTURE_BINDING( format, name, slot )\
layout( set = 0, binding = uRegShift( slot ), rgba16f ) uniform image2D name;
//..................................................................................

#define STORAGE_STRUCTURED_BUFFER_BINDING( format, name, slot )\
layout( set = 0, binding = uRegShift( slot ), std430 ) buffer name##_buffer\
{ format name[ ]; };
//..................................................................................

#define CONSTANT_BUFFER_BINDING( format, name, slot )\
layout( set = 0, binding = bRegShift( slot ) ) uniform name##_cb { format name; };
//..................................................................................

#define PUSH_CONSTANTS_BINDING( constantsLayout, name, slot )\
layout( push_constant ) uniform name##_pc\
{ constantsLayout } name;
//..................................................................................

#define BUFFER_REFERENCE( type, name, align )\
layout(buffer_reference, std430, buffer_reference_align = align ) readonly buffer type##_br\
{ type data[]; }; type##_br name;
//..................................................................................

#define INIT_BUFFER_REFERENCE( type, name, addr )\
name = type##_br( addr );
//..................................................................................

////////////////////////////////////////////////////////////////////////
#define DECLARE_THREAD_GROUP_SIZE( x, y, z )\
layout( local_size_x = x, local_size_y = y, local_size_z = z ) in;

#define INIT_DISPATCH_THREAD_ID_SEMANTIC( type, name )\
const type name = gl_GlobalInvocationID;

#define BEGIN_MAIN( THREAD_GROUP, SEMANTICS )\
THREAD_GROUP \
void main() { SEMANTICS

#define END_MAIN }

////////////////////////////////////////////////////////////////////////
#define GROUP_BARRIER()         groupMemoryBarrier(); barrier();
#define WAVE_SUM( x )           subgroupAdd( x )
#define WAVE_ANY_TRUE( x )      subgroupAny( x )
#define WAVE_IS_FIRST_LANE()    subgroupElect()
#define ATOMIC_ADD( dest, x )   atomicAdd( dest, x )
#define ATOMIC_OR( dest, x )    atomicOr( dest, x )

////////////////////////////////////////////////////////////////////////
struct RayDesc
{
    float3 Origin;
    float3 Direction;
    float TMin;
    float TMax;
};

#define RAY_FLAG_NONE                             gl_RayFlagsNoneEXT
#define RAY_FLAG_FORCE_OPAQUE                     gl_RayFlagsOpaqueEXT
#define RAY_FLAG_FORCE_NON_OPAQUE                 gl_RayFlagsNoOpaqueEXT
#define RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH  gl_RayFlagsTerminateOnFirstHitEXT
#define RAY_FLAG_SKIP_CLOSEST_HIT_SHADER          gl_RayFlagsSkipClosestHitShaderEXT
#define RAY_FLAG_CULL_BACK_FACING_TRIANGLES       gl_RayFlagsCullBackFacingTrianglesEXT
#define RAY_FLAG_CULL_FRONT_FACING_TRIANGLES      gl_RayFlagsCullFrontFacingTrianglesEXT
#define RAY_FLAG_CULL_OPAQUE                      gl_RayFlagsCullOpaqueEXT
#define RAY_FLAG_CULL_NON_OPAQUE                  gl_RayFlagsCullNoOpaqueEXT
#define RAY_FLAG_SKIP_TRIANGLES                   gl_RayFlagsSkipTrianglesEXT 
#define RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES       gl_RayFlagsSkipAABBEXT 

#define COMMITTED_NOTHING                         gl_RayQueryCommittedIntersectionNoneEXT
#define COMMITTED_TRIANGLE_HIT                    gl_RayQueryCommittedIntersectionTriangleEXT
#define COMMITTED_PROCEDURAL_PRIMITIVE_HIT        gl_RayQueryCommittedIntersectionGeneratedEXT
#define CANDIDATE_NON_OPAQUE_TRIANGLE             gl_RayQueryCandidateIntersectionTriangleEXT
#define CANDIDATE_PROCEDURAL_PRIMITIVE            gl_RayQueryCandidateIntersectionAABBEXT

#define RAY_QUERY rayQueryEXT
#define RayQueryTraceRayInline( rayQuery, tlas, rayFlags, instanceMask, rayDesc )\
rayQueryInitializeEXT( rayQuery, tlas, rayFlags, instanceMask, rayDesc.Origin, rayDesc.TMin, rayDesc.Direction, rayDesc.TMax )

#ifdef ENABLE_RAYQUERY_IMPL

    float3x4 _RayQueryCandidateObjectToWorld( RAY_QUERY rayQuery )
    {
        float4x3 outMatrix = rayQueryGetIntersectionObjectToWorldEXT( rayQuery, false );
        return transpose( outMatrix );
    }

    float3x4 _RayQueryCommittedObjectToWorld( RAY_QUERY rayQuery )
    {
        float4x3 outMatrix = rayQueryGetIntersectionObjectToWorldEXT( rayQuery, true );
        return transpose( outMatrix );
    }

#if defined( ENABLE_POSITION_FETCH )
    #define _RayQueryTrianglePositionsImpl( rayQuery, isCommitted )\
    {\
        float3 positions[3];\
        rayQueryGetIntersectionTriangleVertexPositionsEXT( rayQuery, isCommitted, positions );\
        float3x3 outMatrix;\
        outMatrix[0] = positions[0];\
        outMatrix[1] = positions[1];\
        outMatrix[2] = positions[2];\
        return outMatrix;\
    }

    float3x3 _RayQueryCandidateTrianglePositions( RAY_QUERY rayQuery )
    {
        _RayQueryTrianglePositionsImpl( rayQuery, false );
    }

    float3x3 _RayQueryCommittedTrianglePositions( RAY_QUERY rayQuery )
    {
        _RayQueryTrianglePositionsImpl( rayQuery, true );
    }
#endif // ENABLE_POSITION_FETCH

#if defined( ENABLE_LSS )
    #define _RayQueryLssPositionsAndRadiiImpl( rayQuery, isCommitted )\
    {\
        float3 lssPositions[2];\
        float lssRadii[2];\
        rayQueryGetIntersectionLSSPositionsNV(rayQuery, isCommitted, lssPositions);\
        rayQueryGetIntersectionLSSRadiiNV(rayQuery, isCommitted, lssRadii);\
        float2x4 outMatrix;\
        outMatrix[0] = float4( lssPositions[ 0 ].xyz, lssRadii[ 0 ].x );\
        outMatrix[1] = float4( lssPositions[ 1 ].xyz, lssRadii[ 1 ].x );\
        return outMatrix;\
    }

    float2x4 _RayQueryCandidateLssPositionsAndRadii( RAY_QUERY rayQuery )
    { _RayQueryLssPositionsAndRadiiImpl( rayQuery, false ); }

    float2x4 _RayQueryCommittedLssPositionsAndRadii( RAY_QUERY rayQuery )
    { _RayQueryLssPositionsAndRadiiImpl( rayQuery, true ); }
#endif // ENABLE_LSS

#endif // ENABLE_RAYQUERY_IMPL

#define RayQueryAbort( rayQuery )                                        rayQueryTerminateEXT( rayQuery )
#define RayQueryProceed( rayQuery )                                      rayQueryProceedEXT( rayQuery )
#define RayQueryCandidateType( rayQuery  )                               rayQueryGetIntersectionTypeEXT( rayQuery, false )
#define RayQueryCommittedStatus( rayQuery )                              rayQueryGetIntersectionTypeEXT( rayQuery, true )
#define RayQueryWorldRayOrigin( rayQuery )                               rayQueryGetWorldRayOriginEXT( rayQuery )
#define RayQueryWorldRayDirection( rayQuery )                            rayQueryGetWorldRayDirectionEXT( rayQuery )

#define RayQueryCandidateRayT( rayQuery )                                rayQueryGetIntersectionTEXT( rayQuery, false )
#define RayQueryCandidatePrimitiveIndex( rayQuery )                      rayQueryGetIntersectionPrimitiveIndexEXT( rayQuery, false )
#define RayQueryCandidateInstanceIndex( rayQuery )                       rayQueryGetIntersectionInstanceIdEXT( rayQuery, false )
#define RayQueryCandidateInstanceContributionToHitGroupIndex( rayQuery ) rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT( rayQuery, false )
#define RayQueryCandidateGeometryIndex( rayQuery )                       rayQueryGetIntersectionGeometryIndexEXT( rayQuery, false )
#define RayQueryCandidateInstanceID( rayQuery )                          rayQueryGetIntersectionInstanceCustomIndexEXT( rayQuery, false )
#define RayQueryCandidateTriangleBarycentrics( rayQuery )                rayQueryGetIntersectionBarycentricsEXT( rayQuery, false )
#define RayQueryCandidateTrianglePositions( rayQuery )                   _RayQueryCandidateTrianglePositions ( rayQuery )
#define RayQueryCandidateObjectToWorld( rayQuery )                       _RayQueryCandidateObjectToWorld( rayQuery )

#define RayQueryCommitNonOpaqueTriangleHit( rayQuery )                   rayQueryConfirmIntersectionEXT( rayQuery )
#define RayQueryCommittedRayT( rayQuery )                                rayQueryGetIntersectionTEXT( rayQuery, true )
#define RayQueryCommittedPrimitiveIndex( rayQuery )                      rayQueryGetIntersectionPrimitiveIndexEXT( rayQuery, true )
#define RayQueryCommittedInstanceIndex( rayQuery )                       rayQueryGetIntersectionInstanceIdEXT( rayQuery, true )
#define RayQueryCommittedInstanceContributionToHitGroupIndex( rayQuery ) rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT( rayQuery, true )
#define RayQueryCommittedGeometryIndex( rayQuery )                       rayQueryGetIntersectionGeometryIndexEXT( rayQuery, true )
#define RayQueryCommittedInstanceID( rayQuery )                          rayQueryGetIntersectionInstanceCustomIndexEXT( rayQuery, true )
#define RayQueryCommittedTriangleBarycentrics( rayQuery )                rayQueryGetIntersectionBarycentricsEXT( rayQuery, true )
#define RayQueryCommittedTrianglePositions( rayQuery )                   _RayQueryCommittedTrianglePositions( rayQuery )
#define RayQueryCommittedObjectToWorld( rayQuery )                       _RayQueryCommittedObjectToWorld( rayQuery )

// LSS and spheres
#define RayQueryCandidateIsLss( rayQuery )                               rayQueryIsLSSHitNV( rayQuery, false )
#define RayQueryCandidateIsSphere( rayQuery )                            rayQueryIsSphereHitNV( rayQuery, false )
#define RayQueryCandidateLssPositionsAndRadii( rayQuery )                _RayQueryCandidateLssPositionsAndRadii( rayQuery )

#define RayQueryCommittedIsLss( rayQuery )                                rayQueryIsLSSHitNV( rayQuery, true )
#define RayQueryCommittedIsSphere( rayQuery )                             rayQueryIsSphereHitNV( rayQuery, true )
#define RayQueryCommittedLssPositionsAndRadii( rayQuery )                 _RayQueryCommittedLssPositionsAndRadii( rayQuery )

////////////////////////////////////////////////////////////////////////
#define Image2dStore( image, addr, x ) imageStore( image, int2( addr ), x );
