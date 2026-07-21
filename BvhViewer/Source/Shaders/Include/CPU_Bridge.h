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

#if !defined( __cplusplus )
    #define uint32_t uint
    #define CPU_BRIDGE_INLINE
#else 
    #define CPU_BRIDGE_INLINE inline
#endif // __cplusplus

////////////////////////////////////////////////////////////////////////

#if !defined( CPU_BRIDGE_INCLUDE )
#define CPU_BRIDGE_INCLUDE

#define INST_COPY_SHDR_X 256
#define INST_COPY_SHDR_Y 1

#define OP_DISPATCH_SHDR_X 256
#define OP_DISPATCH_SHDR_Y 1

#define OP_PARSE_SHDR_X 256
#define OP_PARSE_SHDR_Y 1

#define CLAS_PARSE_SHDR_X 256
#define CLAS_PARSE_SHDR_Y 1

#define MI_RESOLVE_SHDR_X 1
#define MI_RESOLVE_SHDR_Y 1

#define BVH_VIZ_SHDR_X 4
#define BVH_VIZ_SHDR_Y 4

struct ConstantBufferLayout
{
    float4x4 viewToWorld;
    float4x4 worldToClip;

    float4 frustum;

    float2 renderSize;
    float2 invRenderSize;

    uint2 mousePos;
    uint32_t visualizationMode;
    uint32_t numBlasDescs;

    uint32_t flagMask;
    uint32_t lastSelectedBlasHash;
    uint32_t lastSelectedGeometryId;
    float heatmapScaling;

    uint32_t instanceInclusionMask;
    uint32_t flagFilterMask;
    uint32_t frameId;
    uint32_t geometryTypeMask;

    uint32_t packedBools;
    uint16_t numSamples;
    uint16_t numBufferedFrames;
    uint32_t blasMapCapacity;
    uint32_t debugUint;
};

////////////////////////////////////////////////////////////////////////
struct TlasInstanceDesc
{
    float Transform[ 3 ][ 4 ];
    uint32_t Mask8InstanceID24Instance;
    uint32_t Flags8InstanceContributionToHitGroupIndex24;
    uint64_t virtualGpuAddress;
};

////////////////////////////////////////////////////////////////////////
struct ViewerBlasDescGPU
{
    uint32_t flags;
    uint32_t updateFrameId;
    uint32_t geometryOffset;
#if defined( VIEWER_API_VK ) || defined( GLSL )
    uint32_t pad;
#endif
};

////////////////////////////////////////////////////////////////////////
struct ClusteredBlasDescGPU
{
    uint32_t numClusters;
    uint32_t numOmmClusters;
    uint32_t numTriangles;
    uint32_t numVertices;
    uint32_t flags;
    uint32_t updateFrameId;
};

////////////////////////////////////////////////////////////////////////
struct FeedbackBufferLayout
{
    uint32_t blasHash;
    uint32_t data;
    #if defined ( VIEWER_API_D3D12 ) || defined( HLSL )
    uint64_t clusteredBlasAddr;
    ClusteredBlasDescGPU clusteredBlas;
    #endif // defined( HLSL )
};

////////////////////////////////////////////////////////////////////////
struct IndirectOpDesc
{
    uint32_t type;
    uint32_t count;
    uint64_t data;
    uint64_t stride;
};

struct UpdateInstanceOpDesc
{
    uint32_t instanceIndex;
    uint32_t instanceContributionToHitGroupIndex;
    uint64_t accelerationStructure;
};

struct WriteInstanceOpDesc
{
    float transform[ 3 ][ 4 ];
    float userAABB[ 6 ];
    uint32_t instanceID;
    uint32_t instanceMask;
    uint32_t instanceContributionToHitGroupIndex;
    uint32_t instanceFlags;
    uint32_t instanceIndex;
    uint32_t partitionIndex;
    uint64_t accelerationStructure;
};

////////////////////////////////////////////////////////////////////////
#if defined( VIEWER_API_VK ) || defined( GLSL )
    #define PUSH_CONSTANTS_LAYOUT\
        uint64_t instanceDescBufferAddress;\
        uint64_t indirectOpsBufferAddress;\
        uint64_t indirectOpsCountAddress;\
        uint32_t numInstances;
#else
    #define PUSH_CONSTANTS_LAYOUT\
        uint32_t numInstances;
#endif

struct PushConstantsLayout
{
    PUSH_CONSTANTS_LAYOUT
};

////////////////////////////////////////////////////////////////////////
#if defined( VIEWER_API_D3D12 ) || defined( HLSL )
struct IndirectOpsParserArgs
{
    uint32_t type;
    uint32_t count;
    uint64_t stride;
    uint64_t data;
    uint32_t threadCountX;
    uint32_t threadCountY;
    uint32_t threadCountZ;
    uint32_t pad;
};

#define PTLAS_INDIRECT_PUSH_CONSTANTS_LAYOUT\
    uint32_t type;\
    uint32_t count;\
    uint64_t stride;

struct PtlasIndirectPushConstantsLayout
{
    PTLAS_INDIRECT_PUSH_CONSTANTS_LAYOUT
};

////////////////////////////////////////////////////////////////////////
struct ClasBuildDescGPU
{
    uint32_t triangleCount : 15;
    uint32_t vertexCount : 15;
    uint32_t hasOmm : 1;
};

#define MULTI_INDIRECT_COPY_OUT_OP_SIZE             ( 4 * 1024 * 1024 )
#define MULTI_INDIRECT_IS_INDIRECTED_COUNT          uint32_t( 1 << 0 )
#define MULTI_INDIRECT_IS_INDIRECTED_INPUT          uint32_t( 1 << 1 )
#define MULTI_INDIRECT_IS_INDIRECTED_DESTINATION    uint32_t( 1 << 2 )

CPU_BRIDGE_INLINE bool _IsIndirectedCount( uint16_t packedBools )
{ return ( packedBools & MULTI_INDIRECT_IS_INDIRECTED_COUNT ) != 0; }

CPU_BRIDGE_INLINE bool _IsIndirectedInput( uint16_t packedBools )
{ return ( packedBools & MULTI_INDIRECT_IS_INDIRECTED_INPUT ) != 0; }

CPU_BRIDGE_INLINE bool _IsIndirectedDestination( uint16_t packedBools )
{ return ( packedBools & MULTI_INDIRECT_IS_INDIRECTED_DESTINATION ) != 0; }

CPU_BRIDGE_INLINE uint16_t _PackIndirectionBools( bool isIndirectedCount, bool isIndirectedInput, bool isIndirectedDestination )
{
    return uint16_t( isIndirectedCount ? MULTI_INDIRECT_IS_INDIRECTED_COUNT : 0 ) |
           uint16_t( isIndirectedInput ? MULTI_INDIRECT_IS_INDIRECTED_INPUT : 0 ) |
           uint16_t( isIndirectedDestination ? MULTI_INDIRECT_IS_INDIRECTED_DESTINATION : 0 );
}

#define MULTI_INDIRECT_PUSH_CONSTANTS\
    uint64_t originalCountBufferAddr;\
    uint64_t originalInputBufferAddr;\
    uint64_t originalDestinationBufferAddr;\
    uint64_t internalCountBufferAddr;\
    uint32_t clasMapCapacity;\
    uint32_t templateMapCapacity;\
    uint32_t blasMapCapacity;\
    uint16_t packedBools;\
    uint16_t buildFlags;\
    uint32_t maxArgCount;\
    uint32_t maxClasPerBlas;\
    uint16_t inputBufferStride;\
    uint16_t destinationBufferStride;\
    uint32_t frameId;\

struct MultiIndirectPushConstantsLayout
{
    MULTI_INDIRECT_PUSH_CONSTANTS
};

#define MULTI_INDIRECT_BLAS_BUILD_PARSING_CONSTANTS\
    uint64_t dstBlasAddr;\
    uint32_t clasCount;\
    uint32_t dispatchId;\

struct MultiIndirectBlasBuildArgs
{
    uint64_t clasBufferAddr;

    MULTI_INDIRECT_BLAS_BUILD_PARSING_CONSTANTS

    uint32_t threadCountX;
    uint32_t threadCountY;
    uint32_t threadCountZ;
    uint32_t pad;
};

struct MultiIndirectBlasBuildPushConstantsLayout
{
    MULTI_INDIRECT_BLAS_BUILD_PARSING_CONSTANTS
};

struct MultiIndirectResloveInputsArgs
{
    uint64_t countBufferAddr; // root arg
    uint64_t inputBufferAddr; // root arg
    uint64_t destinationBufferAddr; // root arg
    uint32_t threadCountX;
    uint32_t threadCountY;
    uint32_t threadCountZ;
    uint32_t pad;
};
#endif // VIEWER_API_D3D12 || HLSL

////////////////////////////////////////////////////////////////////////
#if !defined( __cplusplus )
TlasInstanceDesc _TlasInstanceDesc()
{
    TlasInstanceDesc i;
    i.Transform[ 0 ][ 0 ] = 0.0;
    i.Transform[ 0 ][ 1 ] = 0.0;
    i.Transform[ 0 ][ 2 ] = 0.0;
    i.Transform[ 0 ][ 3 ] = 0.0;
    
    i.Transform[ 1 ][ 0 ] = 0.0;
    i.Transform[ 1 ][ 1 ] = 0.0;
    i.Transform[ 1 ][ 2 ] = 0.0;
    i.Transform[ 1 ][ 3 ] = 0.0;
    
    i.Transform[ 2 ][ 0 ] = 0.0;
    i.Transform[ 2 ][ 1 ] = 0.0;
    i.Transform[ 2 ][ 2 ] = 0.0;
    i.Transform[ 2 ][ 3 ] = 0.0;
    
    i.Mask8InstanceID24Instance = 0;
    i.Flags8InstanceContributionToHitGroupIndex24 = 0;
    i.virtualGpuAddress = 0;
    return i;
}

ViewerBlasDescGPU _ViewerBlasDescGPU()
{
    ViewerBlasDescGPU b;
    b.flags = 0;
    b.updateFrameId = 0;
    b.geometryOffset = 0;
    return b;
}

ViewerBlasDescGPU ViewerBlasDescGPU_Init( uint3 a )
{
    ViewerBlasDescGPU blasDesc;
    blasDesc.flags = a.x;
    blasDesc.updateFrameId = a.y;
    blasDesc.geometryOffset = a.z;
    return blasDesc;
}

#if defined( HLSL )
ClusteredBlasDescGPU _ClusteredBlasDescGPU()
{
    ClusteredBlasDescGPU r;
    r.numClusters = 0;
    r.numTriangles = 0;
    r.numVertices = 0;
    r.flags = 0;
    r.numOmmClusters = 0;
    r.updateFrameId = 0;
    return r;
}
#endif // HLSL

FeedbackBufferLayout _FeedbackBufferLayout()
{
    FeedbackBufferLayout r;
    r.blasHash = 0;
    r.data = 0;
    #if defined( HLSL )
    r.clusteredBlasAddr = 0;
    r.clusteredBlas = _ClusteredBlasDescGPU();
    #endif // defined( HLSL )
    return r;
}

#if defined( HLSL )
ClasBuildDescGPU _ClasBuildDescGPU()
{
    ClasBuildDescGPU r;
    r.hasOmm = uint32_t( 0 );
    r.triangleCount = uint32_t( 0 );
    r.vertexCount = uint32_t( 0 );
    return r;
};

MultiIndirectBlasBuildArgs _MultiIndirectBlasBuildArgs()
{
    MultiIndirectBlasBuildArgs r;
    r.clasBufferAddr = uint64_t( 0 );
    r.dstBlasAddr = uint64_t( 0 );
    r.clasCount = 0;
    r.dispatchId = 0;
    r.threadCountX = 0;
    r.threadCountY = 0;
    r.threadCountZ = 0;
    r.pad = 0;
    return r;
};

IndirectOpsParserArgs _IndirectOpsParserArgs()
{
    IndirectOpsParserArgs r;
    r.type = 0;
    r.count = 0;
    r.data = 0;
    r.stride = 0;
    r.threadCountX = 0;
    r.threadCountY = 0;
    r.threadCountZ = 0;
    r.pad = 0;
    return r;
};

MultiIndirectResloveInputsArgs _MultiIndirectResloveInputsArgs()
{
    MultiIndirectResloveInputsArgs r;
    r.countBufferAddr = 0;
    r.inputBufferAddr = 0;
    r.destinationBufferAddr = 0;
    r.threadCountX = 0;
    r.threadCountY = 0;
    r.threadCountZ = 0;
    r.pad = 0;
    return r;
}
#endif // HLSL

uint64_t UtoU64( uint high, uint low )
{
    return ( uint64_t( low ) << 32 ) | uint64_t( high );
}

uint64_t U2toU64( uint2 a )
{
    return UtoU64( a.x, a.y );
}

uint16_t UtoU16High( uint a )
{
    return uint16_t( ( a >> 16 ) & 0xFFFF );
}

uint16_t UtoU16Low( uint a )
{
    return uint16_t( ( a ) & 0xFFFF );
}

uint U16toU( uint16_t high, uint16_t low )
{
    return ( uint( low ) << 16 ) | uint( high );
}

uint2 U64toU2( uint64_t a )
{
    return uint2( uint( a & 0x00000000FFFFFFFF ), uint( ( a >> 32 ) & 0x00000000FFFFFFFF ) );
}

uint DivideRoundUp( uint a, uint b )
{
    return ( a + b - 1 ) / b;
}
#endif // not __cplusplus

////////////////////////////////////////////////////////////////////////
#define FLAG_VIS_MODE_HIGHLIGHT uint32_t( 0 )
#define FLAG_VIS_MODE_SHOW      uint32_t( 1 )

#define FLAG_INC_MODE_INCLUDE uint( 0 )
#define FLAG_INC_MODE_EXCLUDE uint( 1 )

#define FLAG_LOGIC_OP_AND uint( 0 )
#define FLAG_LOGIC_OP_OR  uint( 1 )

CPU_BRIDGE_INLINE uint32_t _GetFlagFilterVisualizationMode( uint32_t packedFlags ) { return ( packedFlags >> 16 ) & 0xFF; }
CPU_BRIDGE_INLINE uint32_t _GetFlagFilterInclusionMode( uint32_t packedFlags ) { return ( packedFlags >> 8 ) & 0xFF ; }
CPU_BRIDGE_INLINE uint32_t _GetFlagFilterLogicalMode( uint32_t packedFlags ) { return ( packedFlags ) & 0xFF ; }
CPU_BRIDGE_INLINE uint32_t _PackFilterFlags( uint32_t visualizationMode, uint32_t inclusionMode, uint32_t logicalOp )
{
    return ( ( visualizationMode & 0xFF ) << 16 )
        |  ( ( inclusionMode     & 0xFF ) << 8 )
        |  ( ( logicalOp         & 0xFF ) << 0 );
}

////////////////////////////////////////////////////////////////////////
#define PACKED_BOOL_ENABLE_BARY     uint32_t( 1 << 0 )
#define PACKED_BOOL_ENABLE_WIRE     uint32_t( 1 << 1 )
#define PACKED_BOOL_ENABLE_FRAME    uint32_t( 1 << 2 )
#define PACKED_BOOL_CULL_OPAQUE     uint32_t( 1 << 3 )
#define PACKED_BOOL_CULL_NON_OPAQUE uint32_t( 1 << 4 )
#define PACKED_BOOL_CULL_BACK_FACE  uint32_t( 1 << 5 )
#define PACKED_BOOL_CULL_FRONT_FACE uint32_t( 1 << 6 )

CPU_BRIDGE_INLINE bool _GetPackedBool( uint32_t packedBools, uint32_t entry ) {  return ( packedBools & entry ) != 0; }


////////////////////////////////////////////////////////////////////////
// has to match CPU side
#define VIS_MODE_BLAS_ADDRESS              0
#define VIS_MODE_PRIMITIVE_INDEX           1
#define VIS_MODE_GEOMETRY_INDEX            2
#define VIS_MODE_INSTANCE_INDEX            3
#define VIS_MODE_INSTANCE_ID               4
#define VIS_MODE_CONTRIBUTION_TO_HIT_GROUP 5
#define VIS_MODE_CLUSTER_ID                6
#define VIS_MODE_INSTANCE_MASK             7
#define VIS_MODE_INSTANCE_FLAGS            8
#define VIS_MODE_UPDATE_STATE              9
#define VIS_MODE_FLAT_NORMAL               10
#define VIS_MODE_HEAT_MAP                  11 // expected to be the last in the list
#define VIS_MODE_NUM                       12


////////////////////////////////////////////////////////////////////////
#define AS_BUILD_FLAG_ALLOW_UPDATE                             uint32_t( 1 << 0 )
#define AS_BUILD_FLAG_ALLOW_COMPACTION                         uint32_t( 1 << 1 )
#define AS_BUILD_FLAG_PREFER_FAST_TRACE                        uint32_t( 1 << 2 )
#define AS_BUILD_FLAG_PREFER_FAST_BUILD                        uint32_t( 1 << 3 )
#define AS_BUILD_FLAG_MINIMIZE_MEMORY                          uint32_t( 1 << 4 )

#if defined( D3D12_API ) || defined( VIEWER_API_D3D12 )
    #define AS_BUILD_FLAG_PERFORM_UPDATE                       uint32_t( 1 << 5 )
    #define AS_BUILD_FLAG_COMPACTED                            uint32_t( 1 << 6 )
    #define AS_BUILD_FLAG_CLUSTERED                            uint32_t( 1 << 7 )
    #define AS_BUILD_FLAG_HAS_OMM_CLUSTERS                     uint32_t( 1 << 8 )
    #define AS_BUILD_FLAG_NUM                                  uint32_t( 9 )
#else // VK
    #define AS_BUILD_FLAG_MOTION                               uint32_t( 1 << 5 )
    #define AS_BUILD_FLAG_ALLOW_OMM_UPDATE                     uint32_t( 1 << 6 )
    #define AS_BUILD_FLAG_ALLOW_OMM_DISABLE                    uint32_t( 1 << 7 )
    #define AS_BUILD_FLAG_ALLOW_OMM_DATA_UPDATE                uint32_t( 1 << 8 )
    #define AS_BUILD_FLAG_ALLOW_DMM_UPDATE                     uint32_t( 1 << 9 )
    #define AS_BUILD_FLAG_CLUSTERED                            uint32_t( 1 << 10 )
    #define AS_BUILD_FLAG_ALLOW_DATA_ACCESS                    uint32_t( 1 << 11 )
    #define AS_BUILD_FLAG_PERFORM_UPDATE                       uint32_t( 1 << 12 )
    #define AS_BUILD_FLAG_COMPACTED                            uint32_t( 1 << 13 )
    #define AS_BUILD_FLAG_HAS_OMM_CLUSTERS                     uint32_t( 1 << 14 )
    #define AS_BUILD_FLAG_NUM                                  uint32_t( 15 )
#endif
#define AS_BUILD_FLAG_UNKNOWN                                  uint32_t( 1 << 31 )


////////////////////////////////////////////////////////////////////////
#define GEOMETRY_FLAG_OPAQUE                                   uint32_t( 1 << 0 )
#define GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION           uint32_t( 1 << 1 )
#define GEOMETRY_FLAG_NUM                                      uint32_t( 2 )


////////////////////////////////////////////////////////////////////////
#define INSTANCE_FLAG_TRIANGLE_CULL_DISABLE                     uint32_t( 1 << 0 )
#define INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE           uint32_t( 1 << 1 )
#define INSTANCE_FLAG_FORCE_OPAQUE                              uint32_t( 1 << 2 )
#define INSTANCE_FLAG_FORCE_NON_OPAQUE                          uint32_t( 1 << 3 )
#define INSTANCE_FLAG_FORCE_OMM_2_STATE_EX                      uint32_t( 1 << 4 )
#define INSTANCE_FLAG_DISABLE_OMMS_EX                           uint32_t( 1 << 5 )
#define INSTANCE_FLAG_NUM                                       uint32_t( 6 )


////////////////////////////////////////////////////////////////////////
#define MAKE_BIT_MASK( n )                                      uint32_t( ( uint32_t( 1 ) << uint32_t( n ) ) - uint32_t( 1 ) )
#define AS_BUILD_FLAG_MASK                                      MAKE_BIT_MASK( AS_BUILD_FLAG_NUM )
#define GEOMETRY_FLAG_MASK                                      MAKE_BIT_MASK( GEOMETRY_FLAG_NUM )
#define INSTANCE_FLAG_MASK                                      MAKE_BIT_MASK( INSTANCE_FLAG_NUM )

#define AS_BUILD_FLAG_MASK_SHIFT  ( uint32_t( 0 ) )
#define GEOMETRY_FLAG_MASK_SHIFT  ( uint32_t( AS_BUILD_FLAG_NUM ) )
#define INSTANCE_FLAG_MASK_SHIFT  ( uint32_t( AS_BUILD_FLAG_NUM + GEOMETRY_FLAG_NUM ) )

CPU_BRIDGE_INLINE uint32_t _PackGeometryFlagMask( uint32_t instanceFlags, uint32_t blasFlags, uint32_t geometryFlags )
{
    return ( ( blasFlags     & AS_BUILD_FLAG_MASK ) << AS_BUILD_FLAG_MASK_SHIFT )
         | ( ( geometryFlags & GEOMETRY_FLAG_MASK ) << GEOMETRY_FLAG_MASK_SHIFT )
         | ( ( instanceFlags & INSTANCE_FLAG_MASK ) << INSTANCE_FLAG_MASK_SHIFT );
}

////////////////////////////////////////////////////////////////////////
#define GEOMETRY_TYPE_NONE                                      uint32_t( 0 << 0 )
#define GEOMETRY_TYPE_TRIANGLE                                  uint32_t( 1 << 0 )
#define GEOMETRY_TYPE_OMM_TRIANGLE                              uint32_t( 1 << 1 )
#define GEOMETRY_TYPE_AABB                                      uint32_t( 1 << 2 )
#define GEOMETRY_TYPE_LSS                                       uint32_t( 1 << 3 )
#define GEOMETRY_TYPE_SPHERE                                    uint32_t( 1 << 4 )
#define GEOMETRY_TYPE_CLUSTER_TRIANGLE                          uint32_t( 1 << 5 )
#define GEOMETRY_TYPE_ALL                                       uint32_t( GEOMETRY_TYPE_TRIANGLE        |\
                                                                          GEOMETRY_TYPE_OMM_TRIANGLE    |\
                                                                          GEOMETRY_TYPE_AABB            |\
                                                                          GEOMETRY_TYPE_LSS             |\
                                                                          GEOMETRY_TYPE_SPHERE          |\
                                                                          GEOMETRY_TYPE_CLUSTER_TRIANGLE )

#endif // !CPU_BRIDGE_INCLUDE
