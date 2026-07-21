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

#include <Windows.h>

#include "NvBvhViewerD3D12.h"
#include "NvBvhViewerCommonUtils.h"
#include "NvBvhViewerDispatchTableD3D12.h"
#include "imgui_impl_dx12.h"

#if defined(BVHVIEWER_ENABLE_NVAPI)
#include "nvapi.h"
#endif // defined(BVHVIEWER_ENABLE_NVAPI)

#if defined(BVHVIEWER_ENABLE_AMD_AGS)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#endif
#include "amd_ags.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif // defined(BVHVIEWER_ENABLE_AMD_AGS)

#include <cassert>

#define LOG_BAD_HRESULT(hr, msg) \
if((hr) != S_OK && g_LogMessageCallback) \
{ \
    const size_t messageBufferSize = 128;\
    char messageBuffer[messageBufferSize] = {};\
    snprintf(messageBuffer, messageBufferSize, "%s failed: hresult(0x%08X)", msg, int(hr));\
    g_LogMessageCallback(NvBvhViewerCommon::MessageType::Error, messageBuffer);\
} \

#define RETURN_BAD_HRESULT(hr, msg, returnValue) \
{ \
    HRESULT hresult = hr; \
    LOG_BAD_HRESULT(hresult, msg) \
    if ((hresult) != S_OK) \
    { \
    assert(false && TEXT(msg)); \
    return returnValue; \
    } \
} \

#define ReleaseThenZero(obj) \
if(obj) \
{ \
    obj->Release();\
    obj = nullptr; \
}; \

static constexpr uint32_t c_NodeMask = 0x1;

struct ViewerBuffer
{
    ID3D12Resource* ptr = nullptr;
    uint64_t size = 0;
    uint64_t stride = 0;
    uint32_t numElementCapacity = 0;
    uint32_t staleFrameId = 0;
};

struct ViewerImage
{
    ID3D12Resource* ptr = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t staleFrameId = 0;
};

struct TlasDesc
{
    D3D12_GPU_VIRTUAL_ADDRESS address;
    D3D12_GPU_VIRTUAL_ADDRESS instanceDescsBufferAddress;
    D3D12_GPU_VIRTUAL_ADDRESS indirectOpsCount;
    D3D12_GPU_VIRTUAL_ADDRESS indirectOps;
    ID3D12GraphicsCommandList4* updateCommandList; // commandlist to which the update was recorded by the app

    uint32_t numInstances;
    uint32_t flags;
public:
    void Reset()
    {
        address = 0;
        instanceDescsBufferAddress = 0;
        updateCommandList = nullptr; // commandlist to which the update was recorded by the app
        numInstances = 0;
        flags = 0;
    }
};

struct MultiIndirectTracker
{
public:
    enum class ReservedBuffers : uint32_t
    {
        IndirectArgs,
        IndirectArgsSmall,
        IndirectCounts,
        Count
    };

    const uint64_t c_TileSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    struct ReservedBuffer
    {
        ViewerBuffer buffer;
        std::vector<ID3D12Heap*> memories;
        D3D12_HEAP_TYPE heapType;
        uint64_t numTilesPerHeap;
        uint64_t committedSize;

        void Destroy()
        {
            for (auto& m : memories)
                ReleaseThenZero(m);
            memories.resize(0); memories.shrink_to_fit();

            ReleaseThenZero(buffer.ptr);
            committedSize = 0;
            buffer = ViewerBuffer();
        }
    };

    enum class HashMaps
    { Tempate, Clas, Blas, Count };

    struct GpuHashMap
    {
        ViewerBuffer buffer;
        uint64_t dataOffset;
        uint32_t capacity;
    };

    struct OpImmidiateContext
    {
        MultiIndirectPushConstantsLayout pushConstants = {};
        uint64_t inputsAddr = 0;
        uint64_t countAddr = 0;
        uint64_t destinationAddr = 0;
        uint64_t reservedOffsets[uint32_t(ReservedBuffers::Count)] = {};
    };

public:
    bool Initialize(ID3D12Device* device, const uint32_t numBufferedFrames);
    void Reset(const uint32_t bufferedFrameId);
    void Destroy();

    OpImmidiateContext GetImmidiateContext(const void* desc, const uint32_t bufferedFrameId);
    void Append(ID3D12GraphicsCommandList4* commandList, const void* opDesc, const uint32_t bufferedFrameId);
    bool IsKnownCommandList(ID3D12CommandList* commandList);
    void CommitMemory(ID3D12CommandQueue* queue, const uint32_t bufferedFrameId);

    uint64_t GetResourceAddress(BvhViewerResources resource, const OpImmidiateContext& immidiateContext, const uint32_t bufferedFrameId);

    ReservedBuffer& Get(const ReservedBuffers type)
    { return m_ReservedBuffers[uint32_t(type)]; }

    GpuHashMap& Get(const HashMaps type)
    { return m_GpuHashMaps[uint32_t(type)]; }


public:
    ReservedBuffer m_ReservedBuffers[uint32_t(ReservedBuffers::Count)] = {};
    std::vector<uint64_t> m_ReservedBufferOffsets[uint32_t(ReservedBuffers::Count)] = {};
    GpuHashMap m_GpuHashMaps[uint32_t(HashMaps::Count)] = {};

private:
    struct ReservedBufferCreationDesc
    {
        uint64_t stride;
        uint64_t numTilesPerHeap;
        uint64_t maxSize;
        D3D12_HEAP_TYPE heapType;
        D3D12_RESOURCE_STATES initialState;
    };

private:
    bool CreateReservedBuffer(ReservedBuffer& buffer, ReservedBufferCreationDesc desc);
    bool CreateHashMap(HashMaps mapType, const uint64_t budget, const uint64_t dataStride, uint64_t& inOutOffset);
    ID3D12Heap* CreateHeap(D3D12_HEAP_TYPE type, uint64_t size);
    void CommitReservedBufferMemory(ID3D12CommandQueue* queue, ReservedBuffer& buffer, const uint64_t offset);

private:
    std::set<uint64_t> m_KnownCommandLists;
    ID3D12Heap* m_HashMapHeap = nullptr;
    ID3D12Device* m_Device = nullptr;
    uint32_t m_NumBufferedFrames = 0;
};

uint64_t MultiIndirectTracker::GetResourceAddress(BvhViewerResources resource, const OpImmidiateContext& immidiateContext, const uint32_t bufferedFrameId)
{
    auto GetReservedGPUVA = [&](ReservedBuffers bufferType)
        {
            return Get(bufferType).buffer.ptr->GetGPUVirtualAddress() + m_ReservedBufferOffsets[uint32_t(bufferType)][bufferedFrameId];
        };

    auto GetHashGPUVA = [&](HashMaps bufferType, bool isKey)
        {
            uint64_t gpuva = Get(bufferType).buffer.ptr->GetGPUVirtualAddress();
            return isKey ? gpuva : gpuva + Get(bufferType).dataOffset;
        };

    switch( resource )
    {
    // reserved buffers
    case BvhViewerResources::MiSmallArgBuffer:
        return GetReservedGPUVA(ReservedBuffers::IndirectArgsSmall);
    case BvhViewerResources::MiCountBufferInternal:
        return GetReservedGPUVA(ReservedBuffers::IndirectCounts);
    case BvhViewerResources::MiArgBuffer:
        return GetReservedGPUVA(ReservedBuffers::IndirectArgs);

    // extenal buffers
    case BvhViewerResources::MiOriginalDestinationBuffer: 
        return immidiateContext.destinationAddr;
    case BvhViewerResources::MiOriginalCountBuffer: 
        return immidiateContext.countAddr;
    case BvhViewerResources::MiOriginalInputBuffer: 
        return immidiateContext.inputsAddr;

    // hash maps
    case BvhViewerResources::MiTemplateKeysBuffer:
        return GetHashGPUVA(HashMaps::Tempate, true);
    case BvhViewerResources::MiTemplateDataBuffer:
        return GetHashGPUVA(HashMaps::Tempate, false);

    case BvhViewerResources::MiClasKeysBuffer:
        return GetHashGPUVA(HashMaps::Clas, true);
    case BvhViewerResources::MiClasDataBuffer:
        return GetHashGPUVA(HashMaps::Clas, false);

    case BvhViewerResources::MiBlasKeysBuffer:
        return GetHashGPUVA(HashMaps::Blas, true);
    case BvhViewerResources::MiBlasDataBuffer:
        return GetHashGPUVA(HashMaps::Blas, false);

    // always set via command signature
    case BvhViewerResources::MiResolvedCountBuffer:
        return 0;
    case BvhViewerResources::MiResolvedInputBuffer:
        return 0;

    default: break;
    }

    return 0;
}

bool MultiIndirectTracker::IsKnownCommandList(ID3D12CommandList* commandList)
{
    uint64_t key = uint64_t(commandList);
    auto it = m_KnownCommandLists.find(key);
    return it != m_KnownCommandLists.end();
}

MultiIndirectTracker::OpImmidiateContext MultiIndirectTracker::GetImmidiateContext(const void* opDesc, const uint32_t frameId)
{
    (void)opDesc;
    (void)frameId;
    OpImmidiateContext result = {};

#if defined( BVHVIEWER_ENABLE_NVAPI )
    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC& desc = *reinterpret_cast<const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC*>(opDesc);
    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs = desc.inputs;
    const uint32_t bufferedFrameId = frameId % m_NumBufferedFrames;

    result.countAddr = desc.indirectArgCount;
    result.inputsAddr = desc.indirectArgArray.StartAddress;
    result.destinationAddr = desc.destinationAddressArray.StartAddress;

    const bool isCountPtrToPtr = (desc.addressResolutionFlags & NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_INDIRECTED_INDIRECT_ARG_COUNT) != 0;
    const bool isArgsPtrToPtr = (desc.addressResolutionFlags & NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_INDIRECTED_INDIRECT_ARG_ARRAY) != 0;
    const bool isDstPtrToPtr = (desc.addressResolutionFlags & NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_ADDRESS_RESOLUTION_FLAG_INDIRECTED_DESTINATION_ADDRESS_ARRAY) != 0;

    MultiIndirectPushConstantsLayout& pushConstants = result.pushConstants;
    {
        pushConstants.originalCountBufferAddr = desc.indirectArgCount;
        pushConstants.originalInputBufferAddr = desc.indirectArgArray.StartAddress;
        pushConstants.originalDestinationBufferAddr = desc.destinationAddressArray.StartAddress;
        pushConstants.internalCountBufferAddr = Get(ReservedBuffers::IndirectCounts).buffer.ptr->GetGPUVirtualAddress() + m_ReservedBufferOffsets[uint32_t(ReservedBuffers::IndirectCounts)][bufferedFrameId];
        pushConstants.packedBools = _PackIndirectionBools(isCountPtrToPtr, isArgsPtrToPtr, isDstPtrToPtr);
        pushConstants.inputBufferStride = uint16_t(desc.indirectArgArray.StrideInBytes);
        pushConstants.destinationBufferStride = uint16_t(desc.destinationAddressArray.StrideInBytes);
        pushConstants.maxArgCount = inputs.maxArgCount;
        pushConstants.maxClasPerBlas = 0;
        pushConstants.templateMapCapacity = Get(HashMaps::Tempate).capacity;
        pushConstants.clasMapCapacity = Get(HashMaps::Clas).capacity;
        pushConstants.blasMapCapacity = Get(HashMaps::Blas).capacity;
        pushConstants.frameId = frameId;

        if (inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS)
            pushConstants.maxClasPerBlas = desc.inputs.clasDesc.maxClasCountPerArg;

        if((desc.inputs.flags & NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE) != 0)
            pushConstants.buildFlags = AS_BUILD_FLAG_PREFER_FAST_TRACE;

        if((desc.inputs.flags & NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_BUILD) != 0)
            pushConstants.buildFlags = AS_BUILD_FLAG_PREFER_FAST_BUILD;
    }

    if(inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT)
    {
        switch(desc.inputs.movesDesc.type)
        {
            case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_BOTTOM_LEVEL_ACCELERATION_STRUCTURE:
                pushConstants.templateMapCapacity = 0;
                pushConstants.clasMapCapacity = 0;
                break;
            case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_CLUSTER_LEVEL_ACCELERATION_STRUCTURE:
                pushConstants.templateMapCapacity = 0;
                pushConstants.blasMapCapacity = 0;
                break;
            case NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE_TEMPLATE:
                pushConstants.clasMapCapacity = 0;
                pushConstants.blasMapCapacity = 0;
                break;
        }
    }

    if(inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES)
        pushConstants.templateMapCapacity = 0;

    if (inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES)
        pushConstants.clasMapCapacity = 0;

    for(uint32_t i = 0; i < uint32_t(ReservedBuffers::Count); ++i)
        result.reservedOffsets[i] = m_ReservedBufferOffsets[i][bufferedFrameId];

#endif // BVHVIEWER_ENABLE_NVAPI
    return result;
}

void MultiIndirectTracker::Append(ID3D12GraphicsCommandList4* commandList, const void* opDesc, const uint32_t bufferedFrameId)
{
    (void)commandList;
    (void)opDesc;
    (void)bufferedFrameId;

#if defined( BVHVIEWER_ENABLE_NVAPI )
    m_KnownCommandLists.insert(uint64_t(commandList));

    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC& desc = *reinterpret_cast<const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC*>(opDesc);
    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs = desc.inputs;
    const bool isBlasFromClas = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;

    if (isBlasFromClas)
    {
        const size_t argStride = sizeof(MultiIndirectBlasBuildArgs);
        m_ReservedBufferOffsets[uint32_t(ReservedBuffers::IndirectArgs)][bufferedFrameId] += argStride * inputs.maxArgCount;
    }
    m_ReservedBufferOffsets[uint32_t(ReservedBuffers::IndirectCounts)][bufferedFrameId] += sizeof(uint64_t);
    m_ReservedBufferOffsets[uint32_t(ReservedBuffers::IndirectArgsSmall)][bufferedFrameId] += sizeof(MultiIndirectResloveInputsArgs);
#endif // BVHVIEWER_ENABLE_NVAPI
}

ID3D12Heap* MultiIndirectTracker::CreateHeap(D3D12_HEAP_TYPE type, uint64_t size)
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = type;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = c_NodeMask;
    heapProps.VisibleNodeMask = c_NodeMask;

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = size;
    heapDesc.Properties = heapProps;
    heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heapDesc.Flags = D3D12_HEAP_FLAG_NONE;

    ID3D12Heap* heap = nullptr;
    RETURN_BAD_HRESULT(m_Device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)), "MultiIndirectTracker::CommitMemory::CreateHeap", nullptr);
    return heap;
}

void MultiIndirectTracker::CommitReservedBufferMemory(ID3D12CommandQueue* queue, ReservedBuffer& buffer, const uint64_t offset)
{
    if(!offset || offset < buffer.committedSize)
        return;

    const uint64_t sizeRequested = offset - buffer.committedSize;
    const uint64_t maxHeapSize = buffer.numTilesPerHeap * c_TileSize;
    const uint64_t numHeapsRequested = DivideRoundUp(sizeRequested, maxHeapSize);
    const uint64_t maxResrvedBufferSize = buffer.buffer.size;

    if (buffer.committedSize + sizeRequested >= maxResrvedBufferSize)
    {
        LogMessage(NvBvhViewerCommon::MessageType::Error, "NvBvhViewer::MultiIndirectTracker::CommitReservedBufferMemory: Exceeded max reserved buffer size, unable to allocate memory. Crash in 3..2..1");
        return;
    }

    uint64_t allocationOffset = buffer.committedSize;
    std::vector<ID3D12Heap*> newMemories;
    for (uint64_t i = 0; i < numHeapsRequested && buffer.committedSize < maxResrvedBufferSize; ++i)
    {
        newMemories.push_back(CreateHeap(buffer.heapType, buffer.numTilesPerHeap * c_TileSize));
        buffer.committedSize += maxHeapSize;
    }

    for (uint64_t i = 0; i < numHeapsRequested; ++i)
    {
        const uint32_t tileOffset = uint32_t(allocationOffset / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
        D3D12_TILED_RESOURCE_COORDINATE resourceCoodinate = {};
        resourceCoodinate.X = tileOffset;
        D3D12_TILE_REGION_SIZE resourceRegionSizes = {};
        resourceRegionSizes.NumTiles = uint32_t(maxHeapSize / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
        queue->UpdateTileMappings(buffer.buffer.ptr, 1, &resourceCoodinate, &resourceRegionSizes, newMemories[i], 1, nullptr, 0, 0, D3D12_TILE_MAPPING_FLAG_NONE);
        allocationOffset += maxHeapSize;
    }

    for(size_t i = 0; i < newMemories.size(); ++i)
        buffer.memories.push_back(newMemories[i]);

    return;
}

void MultiIndirectTracker::CommitMemory(ID3D12CommandQueue* queue, const uint32_t bufferedFrameId)
{
    if(queue == nullptr)
        return;

    for(uint32_t bufferId = 0; bufferId < uint32_t(ReservedBuffers::Count); ++bufferId)
        CommitReservedBufferMemory(queue, Get(ReservedBuffers(bufferId)), m_ReservedBufferOffsets[bufferId][bufferedFrameId]);
}

bool MultiIndirectTracker::CreateReservedBuffer(ReservedBuffer& buffer, ReservedBufferCreationDesc desc)
{
    const D3D12_RESOURCE_FLAGS resourceFlag = desc.heapType == D3D12_HEAP_TYPE_DEFAULT ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Flags = resourceFlag;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Height = 1;
    resourceDesc.Width = desc.maxSize;
    RETURN_BAD_HRESULT(m_Device->CreateReservedResource(&resourceDesc, desc.initialState, nullptr, IID_PPV_ARGS(&buffer.buffer.ptr)), "NvBvhViewer::MultiIndirectTracker::Initialize::CreateReservedResource", false);

    buffer.heapType = desc.heapType;
    buffer.committedSize = 0;
    buffer.numTilesPerHeap = desc.numTilesPerHeap;
    buffer.buffer.stride = desc.stride;
    buffer.buffer.size = desc.maxSize;
    buffer.buffer.numElementCapacity = uint32_t(desc.maxSize / desc.stride);
    return true;
}

bool MultiIndirectTracker::CreateHashMap(HashMaps mapType, const uint64_t budget, const uint64_t dataStride, uint64_t& inOutOffset)
{
    GpuHashMap& map = m_GpuHashMaps[uint32_t(mapType)];
    const uint64_t count = std::min<uint64_t>(budget / (sizeof(uint64_t) + dataStride), uint64_t(1) << 31);
    map.capacity = static_cast<uint32_t>(count);
    const uint64_t keysSize = count * sizeof(uint64_t);
    map.dataOffset = keysSize;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Width = budget;
    resourceDesc.Height = 1;
    HRESULT hr = m_Device->CreatePlacedResource(m_HashMapHeap, inOutOffset, &resourceDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map.buffer.ptr));
    RETURN_BAD_HRESULT(hr, "NvBvhViewer::MultiIndirectTracker::Initialize::CreateHashMap::CreatePlacedResource", false);

    inOutOffset += budget;

    return true;
}

bool MultiIndirectTracker::Initialize(ID3D12Device* device, const uint32_t numBufferedFrames)
{
    if(device == nullptr)
        return false;

    if(numBufferedFrames == 0)
        return false;

    if(m_Device != nullptr)
        return true;

    m_Device = device;
    m_NumBufferedFrames = numBufferedFrames;

    bool r = true;
    {
        const size_t indirectArgStride = std::max<size_t>(sizeof(MultiIndirectResloveInputsArgs), sizeof(MultiIndirectBlasBuildArgs));
        struct CreationHelper
        {
            ReservedBuffer& buffer;
            ReservedBufferCreationDesc desc;
        };

        const uint64_t Mb = 1024 * 1024;
        const CreationHelper perBufferDesc[ ] =
        {
            { { Get(ReservedBuffers::IndirectCounts)},    { sizeof(uint64_t),   1, 64 * Mb, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS }},
            { { Get(ReservedBuffers::IndirectArgs)},      { indirectArgStride, 64, 64 * Mb, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS }},
            { { Get(ReservedBuffers::IndirectArgsSmall)}, { indirectArgStride,  1, 64 * Mb, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS }},
        };
        static_assert(std::size(perBufferDesc) == size_t(ReservedBuffers::Count));

        for (uint32_t i = 0; i < uint32_t(ReservedBuffers::Count); ++i)
        {
            m_ReservedBufferOffsets[i].resize(m_NumBufferedFrames);
            r = r && CreateReservedBuffer(perBufferDesc[i].buffer, perBufferDesc[i].desc);
        }
    }

    const uint64_t budget = 64 * 1024 * 1024;
    const uint64_t blasBudget = Align(budget >> 1, uint64_t(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
    const uint64_t templateBudget = Align(budget >> 3, uint64_t(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
    const uint64_t clasBudget = budget - templateBudget - blasBudget;

    m_HashMapHeap = CreateHeap(D3D12_HEAP_TYPE_DEFAULT, budget);
    r = r && (m_HashMapHeap != nullptr);

    if(m_HashMapHeap)
    {
        uint64_t offset = 0;
        r = r && CreateHashMap(HashMaps::Blas, blasBudget, sizeof(ClusteredBlasDescGPU), offset);
        r = r && CreateHashMap(HashMaps::Tempate, templateBudget, sizeof(ClasBuildDescGPU), offset);
        r = r && CreateHashMap(HashMaps::Clas, clasBudget, sizeof(ClasBuildDescGPU), offset);
    }

    return r;
}

void MultiIndirectTracker::Reset(const uint32_t bufferedFrameId)
{
    for (uint32_t bufferId = 0; bufferId < uint32_t(ReservedBuffers::Count); ++bufferId)
        m_ReservedBufferOffsets[bufferId][bufferedFrameId] = 0;
}

void MultiIndirectTracker::Destroy()
{ // sync externally
    for(uint32_t i = 0; i < m_NumBufferedFrames; ++i)
        Reset(i);

    for (uint32_t i = 0; i < uint32_t(ReservedBuffers::Count); ++i)
    {
        m_ReservedBufferOffsets[i].resize(0); m_ReservedBufferOffsets[i].shrink_to_fit();
        m_ReservedBuffers[i].Destroy();
    }

    m_Device = nullptr;
}

struct FrameDesc
{
    std::array<TlasDesc, c_MaxTlasPerFrame> tlases;
    MultiIndirectTracker::OpImmidiateContext multiIndirectCtx;
    uint64_t renderFenceValue;
    uint64_t presentFenceValue;
    uint32_t tlasOffset;
    uint32_t numBlasDescs;
    uint32_t commandBufferOffset;

public:
    void Reset()
    {
        for (uint32_t i = 0; i < tlasOffset && i < c_MaxTlasPerFrame; ++i)
            tlases[i].Reset();
        tlasOffset = 0;
        numBlasDescs = 0;
        commandBufferOffset = 0;
    }

    void Destroy()
    {
        renderFenceValue = 0;
        presentFenceValue = 0;
        Reset();
    }
};

struct CommandBuffer
{
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList4* list = nullptr;
    ID3D12GraphicsCommandList_DT* dt = nullptr;
};

DXGI_FORMAT GetDxgiFormat(ResourceFormats viewerFormat)
{
    switch (viewerFormat)
    {
    case ResourceFormats::Unknown:
        return DXGI_FORMAT_UNKNOWN;
    case ResourceFormats::R32_Uint:
        return DXGI_FORMAT_R32_UINT;
    case ResourceFormats::Rgb32_Uint:
        return DXGI_FORMAT_R32G32B32_UINT;
    case ResourceFormats::R8_Uint:
        return DXGI_FORMAT_R8_UINT;
    case ResourceFormats::Rgb10A2_Unorm:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case ResourceFormats::Count:
        return DXGI_FORMAT_UNKNOWN;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

enum class DescriptorRegisterType
{
    Srv,
    Uav,
    Cbv,
    PushConstants,
    Count
};

struct DescriptorRangeDesc
{
    std::vector<DescriptorBindingDesc> bindings;
    uint32_t baseBindingId = 0;
    uint32_t numBindings = 0;
    uint32_t rootParameterId = 0;
    uint32_t descriptorTableOffset = 0;
    DescriptorRegisterType type = DescriptorRegisterType::Count;
    bool isRootParameter = false;
};

struct PipelineDescD3D12
{
    std::vector<DescriptorRangeDesc> descriptorRanges;
    std::vector<uint32_t> rangeIdPerRootParameter;
    ID3D12RootSignature* rootSignature = {};
    ID3D12PipelineState* pipelineState = {};
    ID3D12CommandSignature* commandSignature = {};
    uint32_t descriptorTableRootParameterId = uint32_t(~0);
    uint32_t descriptorTableHeapOffset = 0;
};

inline void UavBarrier(ID3D12GraphicsCommandList4* list4, ID3D12Resource* ptr)
{
    if (list4 == nullptr)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.UAV.pResource = ptr;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    list4->ResourceBarrier(1, &barrier);
}

inline void GlobalBarrier(ID3D12GraphicsCommandList4* list4)
{
    return UavBarrier(list4, nullptr);
}

inline D3D12_RESOURCE_BARRIER FillTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

inline D3D12_RESOURCE_BARRIER FillTransitionBarrier(ViewerBuffer& buffer, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    return FillTransitionBarrier(buffer.ptr, before, after);
}

struct InternalContext
{
public:
    uint32_t GetFrameId() const { return m_FrameId.load(std::memory_order_acquire); }
    uint32_t GetBufferedFrameId() const { return GetFrameId() % m_NumBufferedFrames; }
    ViewerBuffer* GetBuffer(BvhViewerResources resource, uint32_t bufferedFrameId, uint32_t tlasId);
    ViewerImage* GetImage(uint32_t bufferedFrameId, uint32_t tlasId);

    void CreateOrGrowBuffer(BvhViewerResources resource, uint32_t bufferedFrameId, uint32_t tlasId, uint64_t newSize);
    bool CreatePipelines();
    bool CreateBackend();
    bool CreatePresenter(void* windowHandle, uint32_t outputWidth, uint32_t outputHeight);
    void UpdateObjectPool(bool force);
    void DestroyBackend();

    void UpdateConstantBuffer(uint32_t bufferedFrameId, uint32_t tlasId);
    void PreparePipeline(Pipelines pipelineType, CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId);
    void CopyFeedbackBuffer(CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId);
    void CopyToBackbufferPopulateCommandList(ID3D12GraphicsCommandList4* list, ViewerImage& texture, uint32_t backBufferId);
    void UiPopulateCommandList(CommandBuffer& commandBuffer, uint32_t backBufferId);

    void ResizeSwapChain(uint32_t outputWidth, uint32_t outputHeight);
    void ResizeImage(uint32_t bufferedFrameId, uint32_t tlasId, uint32_t width, uint32_t height);

    void RenderPresenter(void* windowHandle, uint32_t outputWidth, uint32_t outputHeight, std::atomic_flag& lock);
    void RenderTlas(CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId);
    void RenderTlasDeferred(CommandBuffer& commandBuffer, ViewerQueueType queueType, uint32_t bufferedFrameId, uint32_t tlasId);
    bool RenderTlasInline(ID3D12GraphicsCommandList4* commandList, uint32_t bufferedFrameId, uint32_t tlasId);

    Vendor GetVendor(ID3D12Device* device);

public:
    BlasTracker m_BlasTracker = {};
    MultiIndirectTracker m_MultiIndirectTracker = {};
    Camera m_Camera = {};
    ConstantBufferHelper m_ConstantBufferHelper = {};
    ExposedSettings m_Settings = {};
    D3D12DispatchTable m_DispatchTable = {};

    struct
    {
        std::vector<ViewerBuffer> buffers;
        std::vector<ViewerImage> images;
        std::vector<ID3D12DescriptorHeap*> descriptorHeaps;
        std::vector<CommandBuffer> commandBufferPools[uint32_t(ViewerQueueType::Count)];
        uint32_t perBufferTypeOffsets[uint32_t(BvhViewerResources::NumBuffers)];

        std::vector<ViewerBuffer> staleBuffers;
        std::vector<ViewerImage> staleImages;
    } m_ObjectPool = {};

    std::vector<FrameDesc> m_Frames;
    std::vector<CommandBuffer> m_PresentCommandListPool;
    std::vector<ID3D12Resource*> m_BackBuffers;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_BackBufferRtvDescriptors;

    ID3D12Device* m_Device = nullptr;
    ID3D12CommandQueue* m_CommandQueue = nullptr;
    IDXGISwapChain3* m_SwapChain = nullptr;
    ID3D12Fence* m_PresentFence = nullptr;
    ID3D12Fence* m_RenderFence = nullptr;
    ID3D12DescriptorHeap* m_UiDescriptorHeap = nullptr;
    ID3D12DescriptorHeap* m_RtvDescriptorHeap = nullptr;
    IDXGIFactory4* m_DxgiFactory = nullptr;

    #if defined(BVHVIEWER_ENABLE_AMD_AGS)
    AGSContext* m_AgsContext = nullptr;
    #endif // defined(BVHVIEWER_ENABLE_AMD_AGS)

    PipelineDescD3D12 m_Pipelines[uint32_t(Pipelines::Count)] = {};

    uint64_t m_PresentFenceValue = 0;
    uint64_t m_RenderFenceValue = 0;
    int64_t m_FrameTimestamp = 0;
    std::atomic<uint32_t> m_FrameId = 0;

    uint32_t* m_OutputWidth = nullptr;
    uint32_t* m_OutputHeight = nullptr;

    uint32_t m_NumBufferedFrames = 0;
    uint32_t m_SwapChainWidth = 0;
    uint32_t m_SwapChainHeight = 0;

    const DXGI_FORMAT c_OutputFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
    Vendor m_Vendor = Vendor::Unknown;
    bool m_PresenterReady = false;
    bool m_Inline = false;
    bool m_MultiIndirect = false;
    bool m_EnablePositionFetch = false;
    bool m_EnableLss = false;
};

void UiSrvDescriptorAllocateCallback(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
{
    InternalContext* self = (InternalContext*)info->UserData;
    ID3D12DescriptorHeap* heap = self->m_UiDescriptorHeap;
    *out_cpu_desc_handle = heap->GetCPUDescriptorHandleForHeapStart();
    *out_gpu_desc_handle = heap->GetGPUDescriptorHandleForHeapStart();
    return;
}

void UiSrvDescriptorFreeCallback(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle)
{ //TODO:
    (void)info;
    (void)cpu_desc_handle;
    (void)gpu_desc_handle;
    return;
}

bool InternalContext::CreatePresenter(void* window, uint32_t outputWidth, uint32_t outputHeight)
{
    m_SwapChainWidth = outputWidth;
    m_SwapChainHeight = outputHeight;

    { // Create swapchain
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_DxgiFactory));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateDXGIFactory2", false);

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.BufferCount = m_NumBufferedFrames;
        desc.Width = m_SwapChainWidth;
        desc.Height = m_SwapChainHeight;
        desc.Format = c_OutputFormat;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.SampleDesc.Count = 1;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        IDXGISwapChain1* swapChain1;
        HWND windowHandle = GetWindowHandle(window);
        hr = m_DispatchTable.DXGI.CreateSwapChainForHwnd(m_DxgiFactory, m_CommandQueue, windowHandle, &desc, nullptr, nullptr, &swapChain1);
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateSwapChainForHwnd", false);

        { // legacy WAR, if IDXGIFactory2CreateSwapChainForHwnd points to the native dxgi.dll func, this should not be required
            IDXGISwapChain1* swapChain1Check = nullptr;
            struct DECLSPEC_UUID("ADEC44E2-61F0-45C3-AD9F-1B37379284FF") StreamlineRetrieveBaseInterface : IUnknown {};
            hr = swapChain1->QueryInterface(__uuidof(StreamlineRetrieveBaseInterface), (void**)&swapChain1Check);
            if (hr == S_OK) // if the app is using streamline, this will grant us native swapchain interface from dxgi.dll
            {
                swapChain1->Release();
                swapChain1 = swapChain1Check;
            }
        }

        hr = swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_SwapChain);
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::QueryInterface::IDXGISwapChain3", false);
        swapChain1->Release();

        hr = m_DxgiFactory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::MakeWindowAssociation", false);

        hr = m_SwapChain->ResizeBuffers(desc.BufferCount, desc.Width, desc.Height, desc.Format, desc.Flags);
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::ResizeBuffers", false);

        D3D12_DESCRIPTOR_HEAP_DESC dhDesc = {};
        dhDesc.NodeMask = c_NodeMask;
        dhDesc.NumDescriptors = m_NumBufferedFrames;
        dhDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        dhDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_Device->CreateDescriptorHeap(&dhDesc, IID_PPV_ARGS(&m_RtvDescriptorHeap));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateDescriptorHeap", false);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        rtvDesc.Format = c_OutputFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        UINT stride = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

        m_BackBuffers.resize(m_NumBufferedFrames);
        m_BackBufferRtvDescriptors.resize(m_NumBufferedFrames);
        for (uint32_t i = 0; i < m_NumBufferedFrames; ++i)
        {
            m_BackBufferRtvDescriptors[i].ptr = heapStart.ptr + stride * i;
            m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
            m_Device->CreateRenderTargetView(m_BackBuffers[i], &rtvDesc, m_BackBufferRtvDescriptors[i]);
        }
    }

    { // UI
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_UiDescriptorHeap)) != S_OK)
            return false;

        ImGui_ImplDX12_InitInfo initDesc = ImGui_ImplDX12_InitInfo();
        initDesc.Device = m_Device;
        initDesc.CommandQueue = m_CommandQueue;
        initDesc.NumFramesInFlight = m_NumBufferedFrames;
        initDesc.RTVFormat = c_OutputFormat;
        initDesc.DSVFormat = DXGI_FORMAT_D16_UNORM;
        initDesc.UserData = (void*)this;
        initDesc.SrvDescriptorHeap = m_UiDescriptorHeap;
        initDesc.SrvDescriptorAllocFn = UiSrvDescriptorAllocateCallback;
        initDesc.SrvDescriptorFreeFn = UiSrvDescriptorFreeCallback;

        if (!ImGui_ImplDX12_Init(&initDesc))
            return false;
        if (!ImGui_ImplDX12_CreateDeviceObjects())
            return false;
    }

    m_PresenterReady = true;
    return true;
}

void InternalContext::ResizeSwapChain(uint32_t outputWidth, uint32_t outputHeight)
{
    if (!outputWidth || !outputHeight)
        return;

    if (outputWidth == m_SwapChainWidth && outputHeight == m_SwapChainHeight)
        return;

    while (m_PresentFence->GetCompletedValue() < m_PresentFenceValue);

    for (uint32_t i = 0; i < m_NumBufferedFrames; ++i)
        m_BackBuffers[i]->Release();

    m_SwapChain->ResizeBuffers(m_NumBufferedFrames, outputWidth, outputHeight, c_OutputFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    rtvDesc.Format = c_OutputFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    UINT stride = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < m_NumBufferedFrames; ++i)
    {
        m_BackBufferRtvDescriptors[i].ptr = heapStart.ptr + stride * i;
        m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_BackBuffers[i]));
        m_Device->CreateRenderTargetView(m_BackBuffers[i], &rtvDesc, m_BackBufferRtvDescriptors[i]);
    }

    m_SwapChainWidth = outputWidth;
    m_SwapChainHeight = outputHeight;
}

void InternalContext::UiPopulateCommandList(CommandBuffer& commandBuffer, uint32_t backBufferId)
{
    ID3D12Resource* backBuffer = m_BackBuffers[backBufferId];
    D3D12_RESOURCE_BARRIER backBufferBarrier = FillTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandBuffer.list->ResourceBarrier(1, &backBufferBarrier);
    commandBuffer.list->OMSetRenderTargets(1, &m_BackBufferRtvDescriptors[backBufferId], FALSE, nullptr);
    commandBuffer.dt->SetDescriptorHeaps(commandBuffer.list, 1, &m_UiDescriptorHeap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandBuffer.list);
    backBufferBarrier = FillTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandBuffer.list->ResourceBarrier(1, &backBufferBarrier);
}

void InternalContext::RenderPresenter(void* window, uint32_t outputWidth, uint32_t outputHeight, std::atomic_flag& lock)
{
    if (IsIconic(GetWindowHandle(window)) == TRUE || m_PresenterReady == false)
        return;

    static uint32_t lastAppFrameId = 0;
    ID3D12CommandList* cmdList = nullptr;
    if (m_PresentFenceValue >= m_NumBufferedFrames)
        while (m_PresentFence->GetCompletedValue() < 1 + m_PresentFenceValue - m_NumBufferedFrames);

    {
        BvhViewerScopedLock scopedLock(lock);

        uint32_t appFrameId = GetFrameId() - 1;
        uint32_t bufferedFrameId = appFrameId % m_NumBufferedFrames;
        FrameDesc& frame = m_Frames[bufferedFrameId];
        const bool isNewAppFrame = lastAppFrameId != appFrameId && m_RenderFence->GetCompletedValue() >= frame.renderFenceValue;

        if (SkipPresent(isNewAppFrame, m_Settings.enableFpsLimit))
            return;

        ResizeSwapChain(outputWidth, outputHeight);

        uint32_t tlasId = 0;
        if (frame.tlasOffset)
        {
            const uint32_t requestedTlasId = static_cast<uint32_t>(std::max(m_Settings.tlasId, 0));
            uint32_t selectedId = requestedTlasId >= frame.tlasOffset ? frame.tlasOffset - 1 : requestedTlasId;
            tlasId = selectedId;
            if (m_Settings.tlasSortMode == int(TlasSortModes::InstanceNum))
            {
                struct TlasShortDesc { uint32_t nativeId; TlasDesc* ptr; };
                std::vector< TlasShortDesc > sortVector; sortVector.reserve(c_MaxTlasPerFrame);
                for (uint32_t i = 0; i < frame.tlasOffset && i < c_MaxTlasPerFrame; ++i)
                    sortVector.push_back({i, &frame.tlases[i]});

                std::sort(sortVector.begin(), sortVector.end(), [](TlasShortDesc& a, TlasShortDesc& b) { return a.ptr->numInstances > b.ptr->numInstances; });
                tlasId = sortVector[selectedId].nativeId;
            }
        }
        TlasDesc& tlasDesc = frame.tlases[tlasId];

        const ViewerBuffer* readbackBuffer = GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
        void* readbackPtr = nullptr;

        if (isNewAppFrame)
        {
            UpdateObjectPool(false);

            if (readbackBuffer && readbackBuffer->ptr)
            { // parse instance data
                uint32_t totalInstanceNum = 0;
                for (uint32_t i = 0; i < frame.tlasOffset; ++i)
                    totalInstanceNum += frame.tlases[i].numInstances;
                readbackBuffer->ptr->Map(0, nullptr, &readbackPtr);
                const uint64_t parseSize = uint64_t(sizeof(FeedbackBufferLayout) + sizeof(TlasInstanceDesc) * tlasDesc.numInstances);
                if (readbackPtr && readbackBuffer->size >= parseSize)
                {
                    void* mappedData = (void*)((uint8_t*)readbackPtr + sizeof(FeedbackBufferLayout));
                    m_BlasTracker.ParseInstanceDescs(mappedData, tlasDesc.numInstances, totalInstanceNum, &m_Camera.state.globalPosition);
                    readbackBuffer->ptr->Unmap(0, nullptr);
                }
            }
            lastAppFrameId = appFrameId;
        }

        { // prepare new frame
            if (readbackBuffer && readbackBuffer->ptr)
                readbackBuffer->ptr->Map(0, nullptr, &readbackPtr);

            PresenterNewFrameDesc newFrameDesc = {};
            newFrameDesc.cbUpdateDesc.camera = &m_Camera;
            newFrameDesc.cbUpdateDesc.settings = &m_Settings;
            newFrameDesc.cbUpdateDesc.renderWidth = m_SwapChainWidth;
            newFrameDesc.cbUpdateDesc.renderHeight = m_SwapChainHeight;
            newFrameDesc.cbUpdateDesc.frameId = appFrameId;
            newFrameDesc.cbUpdateDesc.numBufferedFrames = m_NumBufferedFrames;
            newFrameDesc.cbUpdateDesc.numBlasDescs = m_BlasTracker.GetBlasNumber();
            newFrameDesc.cbUpdateDesc.blasMapCapacity = m_MultiIndirectTracker.Get(MultiIndirectTracker::HashMaps::Blas).capacity;
            newFrameDesc.constantBufferHelper = &m_ConstantBufferHelper;

            newFrameDesc.blasTracker = &m_BlasTracker;
            newFrameDesc.feedbackData = readbackPtr;
            newFrameDesc.window = window;
            newFrameDesc.tlasAddress = tlasDesc.address;
            newFrameDesc.tlasId = tlasId;
            newFrameDesc.tlasFlags = tlasDesc.flags;
            newFrameDesc.numTlasesInFrame = frame.tlasOffset;
            newFrameDesc.numInstancesInTlas = tlasDesc.numInstances;
            newFrameDesc.enablePositionFetch = m_EnablePositionFetch;
            PresenterNewFrame(newFrameDesc);

            if (readbackBuffer && readbackBuffer->ptr)
                readbackBuffer->ptr->Unmap(0, nullptr);
        }

        uint32_t backBufferId = m_SwapChain->GetCurrentBackBufferIndex();
        CommandBuffer& commandBuffer = m_PresentCommandListPool[backBufferId];
        RETURN_BAD_HRESULT(commandBuffer.allocator->Reset(), "NvBvhViewerD3D12::Impl::RenderPresenter: Failed to reset command allocator", );
        RETURN_BAD_HRESULT(commandBuffer.dt->Reset(commandBuffer.list, commandBuffer.allocator, nullptr), "NvBvhViewerD3D12::Impl::RenderPresenter: Failed to reset command list", );

        if (m_Settings.allowLastKnownBvhRender && !isNewAppFrame && tlasDesc.numInstances)
        {
            while (m_PresentFence->GetCompletedValue() < m_PresentFenceValue);

            UpdateConstantBuffer(bufferedFrameId, tlasId);
            ResizeImage(bufferedFrameId, tlasId, outputWidth, outputHeight);
            CopyFeedbackBuffer(commandBuffer, bufferedFrameId, tlasId);

            PreparePipeline(Pipelines::BvhVisualization, commandBuffer, bufferedFrameId, tlasId);
            uint32_t x = DivideRoundUp(outputWidth, BVH_VIZ_SHDR_X);
            uint32_t y = DivideRoundUp(outputHeight, BVH_VIZ_SHDR_Y);
            commandBuffer.list->Dispatch(x, y, 1);
        }

        ViewerImage& tlasTexture = *GetImage(bufferedFrameId, tlasId);
        CopyToBackbufferPopulateCommandList(commandBuffer.list, tlasTexture, backBufferId);
        UiPopulateCommandList(commandBuffer, backBufferId);
        RETURN_BAD_HRESULT(commandBuffer.dt->Close(commandBuffer.list), "NvBvhViewerD3D12::Impl::RenderPresenter: Failed to close command list", );

        if(frame.renderFenceValue)
            m_DispatchTable.CmdQueue.Wait(m_CommandQueue, m_RenderFence, frame.renderFenceValue);
        frame.presentFenceValue = m_PresentFenceValue + 1;
        cmdList = (ID3D12CommandList*)commandBuffer.list;
    }

    m_DispatchTable.CmdQueue.ExecuteCommandLists(m_CommandQueue, cmdList ? 1 : 0, &cmdList);
    m_DispatchTable.DXGI.Present(m_SwapChain, 0, DXGI_PRESENT_ALLOW_TEARING);
    m_DispatchTable.CmdQueue.Signal(m_CommandQueue, m_PresentFence, ++m_PresentFenceValue);
    BusyWait(m_Settings.fpsLimit, m_Settings.enableFpsLimit);
}

uint32_t NvBvhViewerD3D12::PresentThread(void* parameter)
{
    NvBvhViewerD3D12* self = static_cast<NvBvhViewerD3D12*>(parameter);

    while (self->m_BackendInitialized == false)
    {
        Sleep(1);
        if (self->m_Exit)
            return 0;
    }

    InternalContext& ctx = *self->m_Context;

    { // init
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        if (self->CreateViewerWindow("D3D12") == false)
            return 0;
        if (ctx.CreatePresenter(self->m_Window, self->m_OutputWidth, self->m_OutputHeight) == false)
            return 0;
    }

    while (self->m_Exit == false)
    {
        if (PollWindowEvents(self->m_Window) == false)
        {
            self->NotifyWindowClose();
            return 0;
        }
        ctx.RenderPresenter(self->m_Window, self->m_OutputWidth, self->m_OutputHeight, self->m_Lock);
    }

    return 0;
}

ViewerBuffer* InternalContext::GetBuffer(BvhViewerResources resource, uint32_t bufferedFrameId, uint32_t tlasId)
{
    uint32_t linearId = GetBufferResourceLinearId(resource);

    if (linearId >= uint32_t(BvhViewerResources::NumBuffers))
        return nullptr;

    ViewerBuffer* buffers = m_ObjectPool.buffers.data() + m_ObjectPool.perBufferTypeOffsets[linearId];

    const BufferGroup bufferGroup = GetBufferGroup(resource);
    if (bufferGroup == BufferGroup::Unknown)
        return nullptr;

    const bool perFrame = bufferGroup == BufferGroup::PerFrame;
    const bool perTlas = bufferGroup == BufferGroup::PerTlas;
    const bool perTlasPerFrame = bufferGroup == BufferGroup::PerTlasPerFrame;

    uint32_t bufferId = 0;
    bufferId = perFrame ? bufferedFrameId : bufferId;
    bufferId = perTlas ? tlasId : bufferId;
    bufferId = perTlasPerFrame ? bufferedFrameId * c_MaxTlasPerFrame + tlasId : bufferId;

    return &buffers[bufferId];
}

ViewerImage* InternalContext::GetImage(uint32_t bufferedFrameId, uint32_t tlasId)
{
    const uint32_t imageId = c_MaxTlasPerFrame * bufferedFrameId + tlasId;
    if(uint32_t(m_ObjectPool.images.size()) > imageId)
        return &m_ObjectPool.images[imageId];
    return nullptr;
}

void InternalContext::CreateOrGrowBuffer(BvhViewerResources resource, uint32_t bufferedFrameId, uint32_t tlasId, uint64_t newSize)
{
    ViewerBuffer* bufferPtr = GetBuffer(resource, bufferedFrameId, tlasId);
    if (bufferPtr == nullptr)
        return;

    ViewerBuffer& b = *bufferPtr;
    if (b.size >= newSize || newSize == 0)
        return;

    if(b.size != 0)
    {
        b.staleFrameId = GetFrameId();
        m_ObjectPool.staleBuffers.push_back(b);
        b = {};
    }

    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    const D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_MEMORY_POOL memoryPool = D3D12_MEMORY_POOL_UNKNOWN;
    D3D12_CPU_PAGE_PROPERTY cpuPageProps = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;

    uint64_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    bool isDynamicBuffer = true;
    switch (resource)
    {
    case BvhViewerResources::FeedbackBuffer:
    {
        b.stride = uint64_t(sizeof(FeedbackBufferLayout));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        isDynamicBuffer = false;
        break;
    }
    case BvhViewerResources::InstanceDescInternal:
    {
        b.stride = uint64_t(sizeof(TlasInstanceDesc));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::BlasDescs:
    {
        b.stride = uint64_t(sizeof(ViewerBlasDescGPU));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::GeometryDescs:
    {
        b.stride = uint64_t(sizeof(ViewerRtGeometryDescGPU));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::BlasHashes:
    {
        b.stride = uint64_t(sizeof(uint32_t));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::UploadBuffer:
    {
        heapType = D3D12_HEAP_TYPE_UPLOAD;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        b.stride = uint64_t(sizeof(uint8_t));
        break;
    }
    case BvhViewerResources::ReadbackBuffer:
    {
        heapType = D3D12_HEAP_TYPE_READBACK;
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        b.stride = uint64_t(sizeof(uint8_t));
        break;
    }
    case BvhViewerResources::ConstantBuffer:
    {
        isDynamicBuffer = false;
        heapType = D3D12_HEAP_TYPE_UPLOAD;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        b.stride = uint64_t(sizeof(ConstantBufferLayout));
        alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        break;
    }
    case BvhViewerResources::IndirectOpsCountInternal:
    {
        isDynamicBuffer = false;
        b.stride = uint64_t(sizeof(uint32_t));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::IndirectOpsArgs:
    {
        b.stride = uint64_t(sizeof(IndirectOpsParserArgs));
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        break;
    }
    case BvhViewerResources::OutputTexture:
    case BvhViewerResources::RenderPushConstants:
    case BvhViewerResources::PtlasIndirectPushConstants:
    case BvhViewerResources::Tlas:
    case BvhViewerResources::InstanceDescExternal:
    case BvhViewerResources::IndirectOpsBufferExternal:
    case BvhViewerResources::IndirectOpsCountExternal:
    case BvhViewerResources::IndirectOpsData:
    case BvhViewerResources::GpuVendorReserved:
    case BvhViewerResources::Count:
        break;
    default:
        break;
    }

    if (isDynamicBuffer)
        newSize = newSize + (newSize >> 2); // grow it a bit more than requested. to reduce the number of reallocations

    b.size = Align(newSize, alignment);
    b.numElementCapacity = static_cast<uint32_t>(b.size / b.stride);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;
    heapProps.CPUPageProperty = cpuPageProps;
    heapProps.MemoryPoolPreference = memoryPool;
    heapProps.CreationNodeMask = c_NodeMask;
    heapProps.VisibleNodeMask = c_NodeMask;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Flags = resourceFlags;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Width = b.size;
    resourceDesc.Height = 1;

    HRESULT hr = m_Device->CreateCommittedResource(&heapProps, flags, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&b.ptr));
    LOG_BAD_HRESULT(hr, "NvBvhViewer::Utils::CreateCommittedResource");
}

void InternalContext::CopyToBackbufferPopulateCommandList(ID3D12GraphicsCommandList4* list, ViewerImage& texture, uint32_t backBufferId)
{
    if (texture.ptr == nullptr)
        return;

    ID3D12Resource* backBuffer = m_BackBuffers[backBufferId];

    D3D12_RESOURCE_BARRIER barriers0[ ] =
    {
        FillTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        FillTransitionBarrier(texture.ptr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE)
    };
    list->ResourceBarrier(std::size(barriers0), barriers0);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = backBuffer;
    dst.SubresourceIndex = 0;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture.ptr;
    src.SubresourceIndex = 0;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_BOX srcRect = {0, 0, 0, std::min(m_SwapChainWidth, texture.width), std::min(m_SwapChainHeight, texture.height), 1};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcRect);

    D3D12_RESOURCE_BARRIER barriers1[ ] =
    {
        FillTransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
        FillTransitionBarrier(texture.ptr, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    list->ResourceBarrier(std::size(barriers1), barriers1);
}

void InternalContext::PreparePipeline(Pipelines pipelineType, CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId)
{
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];
    MultiIndirectTracker& clasTracker = m_MultiIndirectTracker;
    const PipelineDescD3D12& pipeline = m_Pipelines[uint32_t(pipelineType)];
    commandBuffer.dt->SetComputeRootSignature(commandBuffer.list, pipeline.rootSignature);
    commandBuffer.dt->SetPipelineState(commandBuffer.list, pipeline.pipelineState);

    for (uint32_t i = 0; i < uint32_t(pipeline.descriptorRanges.size()); ++i)
    { // set root parameters
        const DescriptorRangeDesc& range = pipeline.descriptorRanges[i];
        if (range.isRootParameter == false)
            continue;

        for (uint32_t bindingId = 0; bindingId < uint32_t(range.bindings.size()); ++bindingId)
        {
            const DescriptorBindingDesc& bindingDesc = range.bindings[bindingId];
            D3D12_GPU_VIRTUAL_ADDRESS virtualGpuAddress = 0;
            ID3D12Resource* resourcePtr = nullptr;

            const uint32_t resourceType = uint32_t(bindingDesc.resource);
            switch (resourceType)
            {
            case uint32_t(BvhViewerResources::InstanceDescExternal):
                virtualGpuAddress = tlas.instanceDescsBufferAddress;
                break;
            case uint32_t(BvhViewerResources::IndirectOpsCountExternal):
                virtualGpuAddress = tlas.indirectOpsCount;
                break;
            case uint32_t(BvhViewerResources::IndirectOpsBufferExternal):
                virtualGpuAddress = tlas.indirectOps;
                break;
            default:
            {
                ViewerBuffer* bufferPtr = GetBuffer(bindingDesc.resource, bufferedFrameId, tlasId);
                if (bufferPtr)
                    resourcePtr = bufferPtr->ptr;
                else
                    virtualGpuAddress = clasTracker.GetResourceAddress(bindingDesc.resource, frame.multiIndirectCtx, bufferedFrameId);
                break;
            }
            }

            if (resourcePtr)
                virtualGpuAddress = resourcePtr->GetGPUVirtualAddress();

            switch (range.type)
            {
            case DescriptorRegisterType::Srv:
                commandBuffer.dt->SetComputeRootShaderResourceView(commandBuffer.list, range.rootParameterId, virtualGpuAddress);
                break;
            case DescriptorRegisterType::Uav:
                commandBuffer.dt->SetComputeRootUnorderedAccessView(commandBuffer.list, range.rootParameterId, virtualGpuAddress);
                break;
            case DescriptorRegisterType::Cbv:
                commandBuffer.dt->SetComputeRootConstantBufferView(commandBuffer.list, range.rootParameterId, virtualGpuAddress);
                break;
            case DescriptorRegisterType::PushConstants:
            {
                if(bindingDesc.resource == BvhViewerResources::RenderPushConstants)
                {
                    uint32_t numConstants = uint32_t(bindingDesc.stride / sizeof(uint32_t));
                    PushConstantsLayout data = {};
                    data.numInstances = tlas.numInstances;
                    commandBuffer.dt->SetComputeRoot32BitConstants(commandBuffer.list, range.rootParameterId, numConstants, (void*)&data, 0);
                }
                else if(bindingDesc.resource == BvhViewerResources::MiPushConstants)
                {
                    uint32_t numConstants = uint32_t(bindingDesc.stride / sizeof(uint32_t));
                    MultiIndirectPushConstantsLayout& data = frame.multiIndirectCtx.pushConstants;
                    commandBuffer.dt->SetComputeRoot32BitConstants(commandBuffer.list, range.rootParameterId, numConstants, (void*)&data, 0);
                }
                break;
            }
            default: break;
            }
        }
    }

    if (pipeline.descriptorTableRootParameterId == uint32_t(~0))
        return;

    const uint32_t descriptorHeapId = bufferedFrameId * c_MaxTlasPerFrame + tlasId;
    ID3D12DescriptorHeap*& descriptorHeap = m_ObjectPool.descriptorHeaps[descriptorHeapId];
    if (descriptorHeap == nullptr)
    { //  create descriptor heap
        uint32_t numDescriptors = 0;
        for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
        {
            PipelineDescD3D12& d3d12Pipeline = m_Pipelines[i];
            for (uint32_t j = 0; j < uint32_t(d3d12Pipeline.descriptorRanges.size()); ++j)
            {
                const DescriptorRangeDesc& range = d3d12Pipeline.descriptorRanges[j];
                numDescriptors += range.isRootParameter ? 0 : range.numBindings;
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        heapDesc.NodeMask = c_NodeMask;
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        HRESULT hr = m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
        LOG_BAD_HRESULT(hr, "NvBvhViewer::CreateDescriptorHeap");
    }

    const size_t descriptorHeapStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const size_t descriptorStride = size_t(m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    commandBuffer.dt->SetDescriptorHeaps(commandBuffer.list, 1, &descriptorHeap);

    for (uint32_t i = 0; i < uint32_t(pipeline.descriptorRanges.size()); ++i)
    { // update descriptor table
        const DescriptorRangeDesc& range = pipeline.descriptorRanges[i];
        if (range.isRootParameter)
            continue;

        for (uint32_t bindingId = 0; bindingId < uint32_t(range.bindings.size()); ++bindingId)
        {
            const DescriptorBindingDesc& bindingDesc = range.bindings[bindingId];
            D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = {};
            descriptorHandle.ptr = descriptorHeapStart + descriptorStride * size_t(range.descriptorTableOffset + bindingId);
            ViewerBuffer* bufferPtr = GetBuffer(bindingDesc.resource, bufferedFrameId, tlasId);
            switch (bindingDesc.type)
            {
            case DescriptorTypes::Tlas:
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC tlasView = {};
                tlasView.Format = DXGI_FORMAT_UNKNOWN;
                tlasView.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                tlasView.RaytracingAccelerationStructure.Location = tlas.address;
                tlasView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                m_Device->CreateShaderResourceView(nullptr, &tlasView, descriptorHandle);
                break;
            }
            case DescriptorTypes::StorageTexture:
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.Format = c_OutputFormat;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = 0;
                uavDesc.Texture2D.PlaneSlice = 0;
                ViewerImage& image = *GetImage(bufferedFrameId, tlasId);
                m_Device->CreateUnorderedAccessView(image.ptr, nullptr, &uavDesc, descriptorHandle);
                break;
            }
            case DescriptorTypes::StructuredBuffer:
            case DescriptorTypes::TypedBuffer:
            {
                if (bufferPtr)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
                    srvDesc.Buffer.FirstElement = 0;
                    srvDesc.Format = GetDxgiFormat(bindingDesc.format);
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Buffer.NumElements = bufferPtr->numElementCapacity;
                    m_Device->CreateShaderResourceView(bufferPtr->ptr, &srvDesc, descriptorHandle);
                }
                break;
            }
            case DescriptorTypes::RawBuffer:
            {
                if (bufferPtr)
                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                    srvDesc.Buffer.FirstElement = 0;
                    srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Buffer.NumElements = static_cast<UINT>(bufferPtr->size / sizeof(uint32_t));
                    m_Device->CreateShaderResourceView(bufferPtr->ptr, &srvDesc, descriptorHandle);
                }
                break;
            }
            case DescriptorTypes::RawBufferStorage:
            {
                if (bufferPtr)
                {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    uavDesc.Buffer.FirstElement = 0;
                    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                    uavDesc.Buffer.NumElements = static_cast<UINT>(bufferPtr->size / sizeof(uint32_t));
                    m_Device->CreateUnorderedAccessView(bufferPtr->ptr, nullptr, &uavDesc, descriptorHandle);
                }
                break;
            }
            case DescriptorTypes::StorageTypedBuffer:
            case DescriptorTypes::StorageStructuredBuffer:
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = 0;
                uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
                uavDesc.Buffer.StructureByteStride = 1;
                uavDesc.Buffer.NumElements = 1;
                ID3D12Resource* resource = nullptr;
                if (bufferPtr)
                {
                    uavDesc.Buffer.StructureByteStride = static_cast<UINT>(bindingDesc.stride);
                    uavDesc.Buffer.NumElements = bufferPtr->numElementCapacity;
                    resource = bufferPtr->ptr;
                }
                m_Device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, descriptorHandle);
                break;
            }
            case DescriptorTypes::ConstantBuffer:
            {
                if (bufferPtr)
                {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
                    cbvDesc.BufferLocation = bufferPtr->ptr->GetGPUVirtualAddress();
                    cbvDesc.SizeInBytes = static_cast<UINT>(Align(sizeof(ConstantBufferLayout), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
                    m_Device->CreateConstantBufferView(&cbvDesc, descriptorHandle);
                }
                break;
            }
            case DescriptorTypes::PushConstants:
            case DescriptorTypes::Count:
                break;
            default: break;
            }
        }
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle.ptr += descriptorStride * size_t(pipeline.descriptorTableHeapOffset);
    commandBuffer.dt->SetComputeRootDescriptorTable(commandBuffer.list, pipeline.descriptorTableRootParameterId, gpuHandle);
}

void InternalContext::ResizeImage(uint32_t bufferedFrameId, uint32_t tlasId, uint32_t width, uint32_t height)
{
    ViewerImage& image = *GetImage(bufferedFrameId, tlasId);
    if (image.width == width && image.height == height && image.ptr)
        return;

    if(image.ptr)
    {
        image.staleFrameId = GetFrameId();
        m_ObjectPool.staleImages.push_back(image);
        image = {};
    }

    image.width = std::max(width, 1u);
    image.height = std::max(height, 1u);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = c_NodeMask;
    heapProps.VisibleNodeMask = c_NodeMask;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resourceDesc.Format = c_OutputFormat;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Width = image.width;
    resourceDesc.Height = image.height;

    const D3D12_HEAP_FLAGS flags = D3D12_HEAP_FLAG_NONE;
    const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    HRESULT hr = m_Device->CreateCommittedResource(&heapProps, flags, &resourceDesc, initialState, nullptr, IID_PPV_ARGS(&image.ptr));
    LOG_BAD_HRESULT(hr, "NvBvhViewer::CreateCommittedResource::texture");
}

void InternalContext::CopyFeedbackBuffer(CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId)
{
    CreateOrGrowBuffer(BvhViewerResources::FeedbackBuffer, bufferedFrameId, tlasId, sizeof(FeedbackBufferLayout));
    CreateOrGrowBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId, sizeof(FeedbackBufferLayout));

    ViewerBuffer* readbackBuffer = GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
    ViewerBuffer* feedbackBuffer = GetBuffer(BvhViewerResources::FeedbackBuffer, bufferedFrameId, tlasId);

    D3D12_RESOURCE_BARRIER beforeCopyBarrier = FillTransitionBarrier(*feedbackBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_RESOURCE_BARRIER afterCopyBarrier = FillTransitionBarrier(*feedbackBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandBuffer.list->ResourceBarrier(1, &beforeCopyBarrier);
    commandBuffer.list->CopyBufferRegion(readbackBuffer->ptr, 0, feedbackBuffer->ptr, 0, sizeof(FeedbackBufferLayout));
    commandBuffer.list->ResourceBarrier(1, &afterCopyBarrier);
}

void InternalContext::UpdateConstantBuffer(uint32_t bufferedFrameId, uint32_t tlasId)
{
    CreateOrGrowBuffer(BvhViewerResources::ConstantBuffer, bufferedFrameId, tlasId, sizeof(ConstantBufferLayout));

    void* mappedData = nullptr;
    ViewerBuffer& constantBuffer = *GetBuffer(BvhViewerResources::ConstantBuffer, bufferedFrameId, tlasId);
    HRESULT hr = constantBuffer.ptr->Map(0, nullptr, &mappedData);
    LOG_BAD_HRESULT(hr, "NvBvhViewer::Map::constantBuffer");

    const ConstantBufferLayout& layout = m_ConstantBufferHelper.GetConstantBuffer();
    memcpy(mappedData, (void*)&layout, sizeof(ConstantBufferLayout));
    constantBuffer.ptr->Unmap(0, nullptr);
}

void InternalContext::RenderTlas(CommandBuffer& commandBuffer, uint32_t bufferedFrameId, uint32_t tlasId)
{
    FrameDesc& frame = m_Frames[bufferedFrameId];
    TlasDesc& tlas = frame.tlases[tlasId];

    { // update constant buffer
        UpdateConstantBuffer(bufferedFrameId, tlasId);
    }
    { // grow readback buffer
        const uint64_t readbackRequiredSize = sizeof(FeedbackBufferLayout) + tlas.numInstances * sizeof(TlasInstanceDesc);
        CreateOrGrowBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId, readbackRequiredSize);
    }
    const uint64_t uploadSize = m_BlasTracker.PrepareData();
    if (uploadSize)
    { 
        CreateOrGrowBuffer(BvhViewerResources::UploadBuffer, bufferedFrameId, tlasId, uploadSize);
        ViewerBuffer& uploadBuffer = *GetBuffer(BvhViewerResources::UploadBuffer, bufferedFrameId, tlasId);

        BlasTracker::UploadDesc uploadDesc = {};
        void* mappedUploadBuffer = nullptr;
        uploadBuffer.ptr->Map(0, nullptr, &mappedUploadBuffer);

        uploadDesc.mappedUploadBuffer = mappedUploadBuffer;
        m_BlasTracker.UploadData(uploadDesc);
        uploadBuffer.ptr->Unmap(0, nullptr);

        CreateOrGrowBuffer(BvhViewerResources::BlasDescs, bufferedFrameId, tlasId, uploadDesc.outBlasDescRange.size);
        CreateOrGrowBuffer(BvhViewerResources::BlasHashes, bufferedFrameId, tlasId, uploadDesc.outHashesRange.size);
        CreateOrGrowBuffer(BvhViewerResources::GeometryDescs, bufferedFrameId, tlasId, uploadDesc.outGeometryDescRange.size);

        ViewerBuffer& blasDescs = *GetBuffer(BvhViewerResources::BlasDescs, bufferedFrameId, tlasId);
        ViewerBuffer& blasHashes = *GetBuffer(BvhViewerResources::BlasHashes, bufferedFrameId, tlasId);
        ViewerBuffer& geometryDescs = *GetBuffer(BvhViewerResources::GeometryDescs, bufferedFrameId, tlasId);

        const D3D12_RESOURCE_BARRIER preCopyBarriers[ ] =
        {
            FillTransitionBarrier(blasDescs, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE , D3D12_RESOURCE_STATE_COPY_DEST),
            FillTransitionBarrier(blasHashes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE , D3D12_RESOURCE_STATE_COPY_DEST),
            FillTransitionBarrier(geometryDescs, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE , D3D12_RESOURCE_STATE_COPY_DEST),
        };
        const D3D12_RESOURCE_BARRIER afterCopyBarriers[ ] =
        {
            FillTransitionBarrier(blasDescs, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            FillTransitionBarrier(blasHashes, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            FillTransitionBarrier(geometryDescs, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        };

        commandBuffer.list->ResourceBarrier(std::size(preCopyBarriers), preCopyBarriers);
        commandBuffer.list->CopyBufferRegion(blasDescs.ptr, 0, uploadBuffer.ptr, uploadDesc.outBlasDescRange.offset, uploadDesc.outBlasDescRange.size);
        commandBuffer.list->CopyBufferRegion(blasHashes.ptr, 0, uploadBuffer.ptr, uploadDesc.outHashesRange.offset, uploadDesc.outHashesRange.size);
        commandBuffer.list->CopyBufferRegion(geometryDescs.ptr, 0, uploadBuffer.ptr, uploadDesc.outGeometryDescRange.offset, uploadDesc.outGeometryDescRange.size);
        commandBuffer.list->ResourceBarrier(std::size(afterCopyBarriers), afterCopyBarriers);
    }
    { // copy feedback buffer
        CopyFeedbackBuffer(commandBuffer, bufferedFrameId, tlasId);
    }
    { // copy instance descs
        CreateOrGrowBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId, uint64_t(tlas.numInstances * sizeof(TlasInstanceDesc)));

        if (tlas.instanceDescsBufferAddress)
        {
            PreparePipeline(Pipelines::InstanceCopy, commandBuffer, bufferedFrameId, tlasId);

            const D3D12_RESOURCE_BARRIER beforeBarrier =
                FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_RESOURCE_BARRIER afterBarrier =
                FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            commandBuffer.list->ResourceBarrier(1, &beforeBarrier);
            commandBuffer.list->Dispatch(DivideRoundUp(tlas.numInstances, INST_COPY_SHDR_X), 1, 1);
            commandBuffer.list->ResourceBarrier(1, &afterBarrier);
        }
        else if(tlas.indirectOps && tlas.indirectOpsCount)
        {
            CreateOrGrowBuffer(BvhViewerResources::IndirectOpsArgs, bufferedFrameId, tlasId, uint64_t(tlas.numInstances * sizeof(IndirectOpsParserArgs)));
            CreateOrGrowBuffer(BvhViewerResources::IndirectOpsCountInternal, bufferedFrameId, tlasId, uint64_t(sizeof(uint32_t)));

            { // Indirect ops dispatcher to read PTLAS update params and spawn parser threads
                PreparePipeline(Pipelines::IndirectOpsDispatcher, commandBuffer, bufferedFrameId, tlasId);

                const D3D12_RESOURCE_BARRIER beforeBarriers[ ] =
                {
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::IndirectOpsCountInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::IndirectOpsArgs, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                };
                const D3D12_RESOURCE_BARRIER afterBarriers[ ] =
                {
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::IndirectOpsCountInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::IndirectOpsArgs, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT),
                };

                commandBuffer.list->ResourceBarrier(std::size(beforeBarriers), beforeBarriers);
                commandBuffer.list->Dispatch(DivideRoundUp(tlas.numInstances, OP_DISPATCH_SHDR_X), 1, 1);
                commandBuffer.list->ResourceBarrier(std::size(afterBarriers), afterBarriers);
            }
            { // Indirect Ops parser. Read update operations, update instance description buffer
                PreparePipeline(Pipelines::IndirectOpsParser, commandBuffer, bufferedFrameId, tlasId);

                const D3D12_RESOURCE_BARRIER beforeBarrier =
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS );
                const D3D12_RESOURCE_BARRIER afterBarrier =
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                commandBuffer.list->ResourceBarrier(1, &beforeBarrier);
                ViewerBuffer* argBuffer = GetBuffer(BvhViewerResources::IndirectOpsArgs, bufferedFrameId, tlasId);
                ViewerBuffer* countBuffer = GetBuffer(BvhViewerResources::IndirectOpsCountInternal, bufferedFrameId, tlasId);
                ID3D12CommandSignature* commandSignature = m_Pipelines[uint32_t(Pipelines::IndirectOpsParser)].commandSignature;
                commandBuffer.list->ExecuteIndirect(commandSignature, tlas.numInstances, argBuffer->ptr, 0, countBuffer->ptr, 0);
                commandBuffer.list->ResourceBarrier(1, &afterBarrier);
            }
        }
        {
            ViewerBuffer& instanceDescs = *GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId);
            ViewerBuffer& readbackBuffer = *GetBuffer(BvhViewerResources::ReadbackBuffer, bufferedFrameId, tlasId);
            const uint64_t copySize = tlas.numInstances * sizeof(TlasInstanceDesc);
            if (tlas.numInstances > 0)
            { // readback instance descs
                const D3D12_RESOURCE_BARRIER preCopyBarrier =
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
                const D3D12_RESOURCE_BARRIER afterBarrier = 
                    FillTransitionBarrier(*GetBuffer(BvhViewerResources::InstanceDescInternal, bufferedFrameId, tlasId), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

                commandBuffer.list->ResourceBarrier(1, &preCopyBarrier);
                commandBuffer.list->CopyBufferRegion(readbackBuffer.ptr, uint64_t(sizeof(FeedbackBufferLayout)), instanceDescs.ptr, 0, copySize);
                commandBuffer.list->ResourceBarrier(1, &afterBarrier);
            }
        }
    }
    { // render tlas
        ResizeImage(bufferedFrameId, tlasId, m_SwapChainWidth, m_SwapChainHeight);
        ViewerImage& image = *GetImage(bufferedFrameId, tlasId);
        PreparePipeline(Pipelines::BvhVisualization, commandBuffer, bufferedFrameId, tlasId);
        uint32_t x = DivideRoundUp(image.width, BVH_VIZ_SHDR_X);
        uint32_t y = DivideRoundUp(image.height, BVH_VIZ_SHDR_Y);
        commandBuffer.list->Dispatch(x, y, 1);
    }
}

bool InternalContext::RenderTlasInline(ID3D12GraphicsCommandList4* commandList, uint32_t bufferedFrameId, uint32_t tlasId)
{
    if (m_Inline == false)
        return false;

    GlobalBarrier(commandList);
    {
        CommandBuffer commandBuffer = {};
        commandBuffer.list = commandList;
        commandBuffer.dt = &m_DispatchTable.GetCmdList(commandList->GetType());
        RenderTlas(commandBuffer, bufferedFrameId, tlasId);
    }
    GlobalBarrier(commandList);

    return true;
}

void InternalContext::RenderTlasDeferred(CommandBuffer& commandBuffer, ViewerQueueType queueType, uint32_t bufferedFrameId, uint32_t tlasId)
{
    FrameDesc& frame = m_Frames[bufferedFrameId];
    if (commandBuffer.list == nullptr)
    { // fetch a cmd list from command pool
        const uint32_t cmdBufferId = bufferedFrameId * c_MaxTlasPerFrame + frame.commandBufferOffset++;
        commandBuffer = m_ObjectPool.commandBufferPools[uint32_t(queueType)][cmdBufferId];

        HRESULT hr = commandBuffer.allocator->Reset();
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CommandAllocator::Reset", );
        hr = commandBuffer.dt->Reset(commandBuffer.list, commandBuffer.allocator, nullptr);
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CommandList::Reset", );
    }

    GlobalBarrier(commandBuffer.list);
    RenderTlas(commandBuffer, bufferedFrameId, tlasId);
}

NvBvhViewerD3D12::OnEclResult NvBvhViewerD3D12::OnExecuteCommandLists(ID3D12CommandQueue* inCommandQueue, ID3D12CommandList* const* inCommandLists, uint32_t inNumCommandLists)
{
    OnEclResult eclResult = {};

    if (IsActive() == false)
        return eclResult;

    if(inCommandLists == nullptr || inNumCommandLists == 0)
        return eclResult;

    if(IsInternalQueue(inCommandQueue))
        return eclResult;

    if(IsPresentThread())
        return eclResult;

    InternalContext& ctx = *m_Context;
    uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frame = ctx.m_Frames[bufferedFrameId];
    MultiIndirectTracker& clasTracker = ctx.m_MultiIndirectTracker;

    bool renderSubmitted = false;
    bool hasClasBuild = false;
    CommandBuffer commandBuffer = {};
    BvhViewerScopedLock scopedLock(m_Lock);
    for (uint32_t commandListId = 0; commandListId < inNumCommandLists; ++commandListId)
    {
        for (uint32_t tlasId = 0; tlasId < frame.tlasOffset && tlasId < c_MaxTlasPerFrame; ++tlasId)
        {
            if (inCommandLists[commandListId] != frame.tlases[tlasId].updateCommandList)
                continue;

            renderSubmitted = true;
            if (ctx.m_Inline == false && frame.commandBufferOffset < c_MaxTlasPerFrame)
            {
                const D3D12_COMMAND_LIST_TYPE d3d12CmdListType = inCommandQueue->GetDesc().Type;
                const ViewerQueueType queueType = d3d12CmdListType == uint32_t(D3D12_COMMAND_LIST_TYPE_DIRECT) ? ViewerQueueType::Graphics : ViewerQueueType::Compute;
                ctx.RenderTlasDeferred(commandBuffer, queueType, bufferedFrameId, tlasId);
            }

            if (eclResult.outFence == nullptr)
            {
                eclResult.outFence = ctx.m_RenderFence;
                eclResult.outSignalValue = ++ctx.m_RenderFenceValue;
            }
        }

        hasClasBuild = hasClasBuild || clasTracker.IsKnownCommandList(inCommandLists[commandListId]);
    }

    if(hasClasBuild)
    {
        clasTracker.CommitMemory(inCommandQueue, bufferedFrameId);
    }

    if (renderSubmitted)
    {
        if(frame.presentFenceValue)
            ctx.m_DispatchTable.CmdQueue.Wait(inCommandQueue, ctx.m_PresentFence, frame.presentFenceValue);
        if(frame.renderFenceValue)
            ctx.m_DispatchTable.CmdQueue.Wait(inCommandQueue, ctx.m_RenderFence, frame.renderFenceValue);
        frame.renderFenceValue = ctx.m_RenderFenceValue;
    }

    if (commandBuffer.list != nullptr)
    {
        eclResult.outCommandList = commandBuffer.list;
        commandBuffer.dt->Close(commandBuffer.list);
    }

    return eclResult;
}

void NvBvhViewerD3D12::OnCopyRaytracingAccelerationStructure(D3D12_GPU_VIRTUAL_ADDRESS dst, D3D12_GPU_VIRTUAL_ADDRESS src, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    const bool isCompaction = mode == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT;
    const bool isClone = mode == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE;

    if (isClone == false && isCompaction == false)
        return;

    BvhViewerScopedLock scopedLock(m_Lock);
    if (IsInitialized() == false)
        return;

    InternalContext& ctx = *m_Context;
    ctx.m_BlasTracker.ProcessBlasCopy(dst, src, isCompaction);
}

bool NvBvhViewerD3D12::OnBuildRaytracingPartitionedTlasIndirect(ID3D12GraphicsCommandList4* commandList, const void* pParams)
{
    (void)commandList;
    (void)pParams;
    bool modifiedPipelineState = false;
#if defined(BVHVIEWER_ENABLE_NVAPI)
    BvhViewerScopedLock scopedLock(m_Lock);

    if (IsInitialized() == false)
        return false;

    InternalContext& ctx = *m_Context;
    if(ctx.m_Inline == false)
    {
        LOG_WARNING_ONCE("BuildRaytracingPartitionedTlasIndirect requires inline rendering enabled. All further calls will be ignored.");
        return false;
    }

    uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frameDesc = ctx.m_Frames[bufferedFrameId];

    const NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS& params = *(NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS*)pParams;
    const NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_DESC& desc = *params.pDesc;

    if (frameDesc.tlasOffset >= c_MaxTlasPerFrame)
        return false;

    const uint32_t tlasId = frameDesc.tlasOffset++;
    TlasDesc& tlasDesc = frameDesc.tlases[tlasId];
    tlasDesc.address = desc.destAccelerationStructureData;
    tlasDesc.updateCommandList = commandList;
    tlasDesc.numInstances = desc.inputs.instanceCount;
    tlasDesc.instanceDescsBufferAddress = 0;
    tlasDesc.flags = desc.inputs.flags;
    tlasDesc.indirectOps = desc.indirectOps;
    tlasDesc.indirectOpsCount = desc.indirectOpCount;

    if (IsActive())
        modifiedPipelineState = ctx.RenderTlasInline(commandList, bufferedFrameId, tlasId);
#endif // defined(BVHVIEWER_ENABLE_NVAPI)
    return modifiedPipelineState;
}

bool NvBvhViewerD3D12::OnExecuteMultiIndirectClusterOperation(ID3D12GraphicsCommandList4* commandList, const void* pParams)
{
    (void)commandList;
    (void)pParams;
    bool result = false;
#if defined(BVHVIEWER_ENABLE_NVAPI)
    BvhViewerScopedLock scopedLock(m_Lock);

    InternalContext& ctx = *m_Context;

    if (IsActive() == false || ctx.m_MultiIndirect == false)
        return false;

    if (ctx.m_Inline == false)
    {
        LOG_WARNING_ONCE("OnExecuteMultiIndirectClusterOperation requires inline rendering enabled. All further calls will be ignored.");
        return false;
    }

    const NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS* params = (NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS*)pParams;
    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC& desc = *params->pDesc;
    const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs = desc.inputs;

    if(inputs.mode == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_GET_SIZES)
        return false;

    if (desc.indirectArgArray.StartAddress == 0 || desc.indirectArgArray.StrideInBytes == 0)
        return false;

    if(desc.destinationAddressArray.StartAddress == 0 || desc.destinationAddressArray.StrideInBytes == 0)
        return false;

    if(inputs.maxArgCount == 0)
        return false;

    const bool isClusterCopy            = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_MOVE_CLUSTER_OBJECT;
    const bool isClasFromTriangles      = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES;
    const bool isTemplatesFromTriangles = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES;
    const bool isInstantiateTemplates   = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES;
    const bool isBlasFromClas           = inputs.type == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;

    const uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frame = ctx.m_Frames[bufferedFrameId];
    MultiIndirectTracker& clasTracker = ctx.m_MultiIndirectTracker;

    CommandBuffer commandBuffer = {};
    commandBuffer.list = commandList;
    commandBuffer.dt = &ctx.m_DispatchTable.GetCmdList(commandList->GetType());

    result = isClasFromTriangles || isTemplatesFromTriangles || isInstantiateTemplates || isBlasFromClas || isClusterCopy;
    if(!result) return result;

    frame.multiIndirectCtx = clasTracker.GetImmidiateContext(&desc, ctx.GetFrameId());

    //// lambda
    auto BeforeExecuteIndirectBarriers = [&commandList](ID3D12Resource* argBuffer, ID3D12Resource* countBuffer)
        {
            std::vector<D3D12_RESOURCE_BARRIER> barriers = {};
            if(argBuffer)
                barriers.push_back(FillTransitionBarrier(argBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
            if(countBuffer)
                barriers.push_back(FillTransitionBarrier(countBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            if(barriers.empty() == false)
                commandList->ResourceBarrier(uint32_t(barriers.size()), barriers.data());
        };
    auto AfterExecuteIndirectBarriers = [&commandList](ID3D12Resource* argBuffer, ID3D12Resource* countBuffer)
        {
            std::vector<D3D12_RESOURCE_BARRIER> barriers = {};
            if (argBuffer)
                barriers.push_back(FillTransitionBarrier(argBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            if (countBuffer)
                barriers.push_back(FillTransitionBarrier(countBuffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
            if (barriers.empty() == false)
                commandList->ResourceBarrier(uint32_t(barriers.size()), barriers.data());
        };
    ////

    if (inputs.mode == NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS)
        GlobalBarrier(commandList);

    ID3D12Resource* countBuffer = clasTracker.Get(MultiIndirectTracker::ReservedBuffers::IndirectCounts).buffer.ptr;
    ID3D12Resource* argsBuffer = clasTracker.Get(MultiIndirectTracker::ReservedBuffers::IndirectArgs).buffer.ptr;
    ID3D12Resource* smallArgsBuffer = clasTracker.Get(MultiIndirectTracker::ReservedBuffers::IndirectArgsSmall).buffer.ptr;

    const uint64_t argBufferOffset = frame.multiIndirectCtx.reservedOffsets[uint32_t(MultiIndirectTracker::ReservedBuffers::IndirectArgs)];
    const uint64_t smallArgBufferOffset = frame.multiIndirectCtx.reservedOffsets[uint32_t(MultiIndirectTracker::ReservedBuffers::IndirectArgsSmall)];;

    { // step one: run 1 thread that will prepare indirect arg that bind resoved counts and input buffers. will run it always, even when no indirected indirection is used, to have one code path
        ctx.PreparePipeline(Pipelines::MiResolveIndirection, commandBuffer, bufferedFrameId, frame.tlasOffset);
        commandList->Dispatch(1, 1, 1);
        BeforeExecuteIndirectBarriers(smallArgsBuffer, countBuffer);
    }

    if(isClasFromTriangles || isTemplatesFromTriangles)
    { // append dispatch that will copy out descriptions of clases and cluster templates
        ctx.PreparePipeline(Pipelines::MiParseClasBuild, commandBuffer, bufferedFrameId, frame.tlasOffset);
        ID3D12CommandSignature* commandSignature = ctx.m_Pipelines[uint32_t(Pipelines::MiParseClasBuild)].commandSignature;
        commandList->ExecuteIndirect(commandSignature, 1, smallArgsBuffer, smallArgBufferOffset, nullptr, 0);
    }
    else if(isInstantiateTemplates)
    { // append dispatch that will copy out instantiation operations to then correlate template descriptions with produced clases
        ctx.PreparePipeline(Pipelines::MiParseInstantiation, commandBuffer, bufferedFrameId, frame.tlasOffset);
        ID3D12CommandSignature* commandSignature = ctx.m_Pipelines[uint32_t(Pipelines::MiParseInstantiation)].commandSignature;
        commandList->ExecuteIndirect(commandSignature, 1, smallArgsBuffer, smallArgBufferOffset, nullptr, 0);
    }
    else if(isClusterCopy)
    {
        ctx.PreparePipeline(Pipelines::MiParseMove, commandBuffer, bufferedFrameId, frame.tlasOffset);
        ID3D12CommandSignature* commandSignature = ctx.m_Pipelines[uint32_t(Pipelines::MiParseMove)].commandSignature;
        commandList->ExecuteIndirect(commandSignature, 1, smallArgsBuffer, smallArgBufferOffset, nullptr, 0);
    }
    else if(isBlasFromClas)
    { 
        { // append dispatch that will copy out arrays of clas pointers to then parse them
            ctx.PreparePipeline(Pipelines::MiBlasBuildDispatcher, commandBuffer, bufferedFrameId, frame.tlasOffset);
            ID3D12CommandSignature* commandSignature = ctx.m_Pipelines[uint32_t(Pipelines::MiBlasBuildDispatcher)].commandSignature;
            commandList->ExecuteIndirect(commandSignature, 1, smallArgsBuffer, smallArgBufferOffset, nullptr, 0);
            UavBarrier(commandList, clasTracker.Get(MultiIndirectTracker::HashMaps::Blas).buffer.ptr);
        }
        { // indirect dispatches: 1 dispatch per blas. copy out clas poiunters into data buffer.
            ctx.PreparePipeline(Pipelines::MiBlasBuildParser, commandBuffer, bufferedFrameId, frame.tlasOffset);
            ID3D12CommandSignature* commandSignature = ctx.m_Pipelines[uint32_t(Pipelines::MiBlasBuildParser)].commandSignature;
            BeforeExecuteIndirectBarriers(argsBuffer, nullptr);
            commandList->ExecuteIndirect(commandSignature, inputs.maxArgCount, argsBuffer, argBufferOffset, nullptr, 0);
            AfterExecuteIndirectBarriers(argsBuffer, nullptr);
        }
    }
    AfterExecuteIndirectBarriers(smallArgsBuffer, countBuffer);
    clasTracker.Append(commandList, &desc, bufferedFrameId);
    frame.multiIndirectCtx = {}; // clean up immidiate context
#endif
    return result;
}

bool NvBvhViewerD3D12::OnBuildRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const void* pParams)
{
    (void)commandList;
    (void)pParams;
    bool modifiedPipelineState = false;
#if defined(BVHVIEWER_ENABLE_NVAPI)
    BvhViewerScopedLock scopedLock(m_Lock);

    if (IsInitialized() == false)
        return false;

    InternalContext& ctx = *m_Context;
    uint32_t frameId = ctx.GetFrameId();
    uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frameDesc = ctx.m_Frames[bufferedFrameId];

    const NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS* params = (NVAPI_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_EX_PARAMS*)pParams;
    const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC_EX* pDesc = params->pDesc;
    const NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX& inputs = pDesc->inputs;

    if (pDesc->inputs.type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
    {
        if (frameDesc.tlasOffset >= c_MaxTlasPerFrame)
            return modifiedPipelineState;

        if (pDesc->destAccelerationStructureData && inputs.numDescs && inputs.instanceDescs)
        {
            const uint32_t tlasId = frameDesc.tlasOffset++;
            TlasDesc& tlasDesc = frameDesc.tlases[tlasId];
            tlasDesc.address = pDesc->destAccelerationStructureData;
            tlasDesc.updateCommandList = commandList;
            tlasDesc.numInstances = inputs.numDescs;
            tlasDesc.instanceDescsBufferAddress = inputs.instanceDescs;
            tlasDesc.flags = inputs.flags;

            if (IsActive())
                modifiedPipelineState = ctx.RenderTlasInline(commandList, bufferedFrameId, tlasId);
        }
    }
    else
    {
        ViewerBlasDesc viewerBlasDesc = { };
        viewerBlasDesc.address = pDesc->destAccelerationStructureData;
        viewerBlasDesc.hash = HashUint64(pDesc->destAccelerationStructureData);
        viewerBlasDesc.updateFrameId = frameId;
        viewerBlasDesc.buildFlags = uint32_t(inputs.flags);
        viewerBlasDesc.geometries.resize(pDesc->inputs.numDescs);

        bool isArrayOfPtr = inputs.descsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;

        for (uint32_t i = 0; i < inputs.numDescs; ++i)
        {
            ViewerGeometryDesc& geometryDesc = viewerBlasDesc.geometries[i];
            const NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX& d3d12GeometryDesc = isArrayOfPtr ? *inputs.ppGeometryDescs[i] : inputs.pGeometryDescs[i];
            geometryDesc.flags = uint16_t(d3d12GeometryDesc.flags);

            switch (d3d12GeometryDesc.type)
            {
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::Triangles);
                const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = d3d12GeometryDesc.triangles;
                geometryDesc.data[0] = triangles.VertexCount;
                geometryDesc.data[1] = triangles.IndexCount;
                break;
            }
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::OmmTriangles);
                const NVAPI_D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC& ommTriangles = d3d12GeometryDesc.ommTriangles;
                const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = ommTriangles.triangles;
                geometryDesc.data[0] = triangles.VertexCount;
                geometryDesc.data[1] = triangles.IndexCount;
                break;
            }
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_DMM_TRIANGLES_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::DmmTriangles);
                const NVAPI_D3D12_RAYTRACING_GEOMETRY_DMM_TRIANGLES_DESC& dmmTriangles = d3d12GeometryDesc.dmmTriangles;
                const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = dmmTriangles.triangles;
                geometryDesc.data[0] = triangles.VertexCount;
                geometryDesc.data[1] = triangles.IndexCount;
                break;
            }
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::AABB);
                const D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& aabbs = d3d12GeometryDesc.aabbs;
                geometryDesc.data[0] = static_cast<uint32_t>(aabbs.AABBCount);
                break;
            }
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_LSS_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::LSS);
                const NVAPI_D3D12_RAYTRACING_GEOMETRY_LSS_DESC& lssDesc = d3d12GeometryDesc.lss;
                geometryDesc.data[0] = lssDesc.vertexCount;
                geometryDesc.data[1] = lssDesc.indexCount;
                geometryDesc.data[2] = lssDesc.primitiveCount;
                break;
            }
            case NVAPI_D3D12_RAYTRACING_GEOMETRY_TYPE_SPHERES_EX:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::Spheres);
                const NVAPI_D3D12_RAYTRACING_GEOMETRY_SPHERES_DESC& sphereDesc = d3d12GeometryDesc.spheres;
                geometryDesc.data[0] = sphereDesc.vertexCount;
                geometryDesc.data[1] = sphereDesc.indexCount;
                break;
            }
            default: break;
            }
        }
        ctx.m_BlasTracker.Insert(viewerBlasDesc);
    }
#endif // defined(BVHVIEWER_ENABLE_NVAPI)
    return modifiedPipelineState;
}

bool NvBvhViewerD3D12::OnBuildRaytracingAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* pDesc)
{
    BvhViewerScopedLock scopedLock(m_Lock);

    if (IsInitialized() == false)
        return false;

    InternalContext& ctx = *m_Context;
    uint32_t frameId = ctx.GetFrameId();
    uint32_t bufferedFrameId = ctx.GetBufferedFrameId();
    FrameDesc& frameDesc = ctx.m_Frames[bufferedFrameId];

    bool modifiedPipelineState = false;
    if (pDesc->Inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL)
    {
        if (frameDesc.tlasOffset >= c_MaxTlasPerFrame || !IsActive())
            return modifiedPipelineState;

        bool check = bool(pDesc->DestAccelerationStructureData);
        check = check && bool(pDesc->Inputs.NumDescs);
        check = check && bool(pDesc->Inputs.InstanceDescs);

        if (check)
        {
            const uint32_t tlasId = frameDesc.tlasOffset++;
            TlasDesc& tlasDesc = frameDesc.tlases[tlasId];
            tlasDesc.address = pDesc->DestAccelerationStructureData;
            tlasDesc.updateCommandList = commandList;
            tlasDesc.numInstances = pDesc->Inputs.NumDescs;
            tlasDesc.instanceDescsBufferAddress = pDesc->Inputs.InstanceDescs;
            tlasDesc.flags = pDesc->Inputs.Flags;

            if (IsActive())
                modifiedPipelineState = ctx.RenderTlasInline(commandList, bufferedFrameId, tlasId);
        }
    }
    else
    {
        uint32_t hash = HashUint64(uint64_t(pDesc->DestAccelerationStructureData));
        ViewerBlasDesc viewerBlasDesc = {};
        viewerBlasDesc.address = pDesc->DestAccelerationStructureData;
        viewerBlasDesc.hash = hash;
        viewerBlasDesc.updateFrameId = frameId;
        viewerBlasDesc.buildFlags = uint32_t(pDesc->Inputs.Flags);
        viewerBlasDesc.geometries.resize(pDesc->Inputs.NumDescs);

        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs = pDesc->Inputs;
        bool isArrayOfPtr = inputs.DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;

        for (uint32_t i = 0; i < inputs.NumDescs; ++i)
        {
            ViewerGeometryDesc& geometryDesc = viewerBlasDesc.geometries[i];
            const D3D12_RAYTRACING_GEOMETRY_DESC& d3d12GeometryDesc = isArrayOfPtr ? *inputs.ppGeometryDescs[i] : inputs.pGeometryDescs[i];
            geometryDesc.flags = uint16_t(d3d12GeometryDesc.Flags);

            switch(d3d12GeometryDesc.Type)
            {
            case D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::Triangles);
                const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = d3d12GeometryDesc.Triangles;
                geometryDesc.data[0] = triangles.VertexCount;
                geometryDesc.data[1] = triangles.IndexCount;
                break;
            }
            case D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::AABB);
                const D3D12_RAYTRACING_GEOMETRY_AABBS_DESC& aabbs = d3d12GeometryDesc.AABBs;
                geometryDesc.data[0] = static_cast<uint32_t>(aabbs.AABBCount);
                break;
            }
            case D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES:
            {
                geometryDesc.type = uint16_t(RtGeometryTypes::OmmTriangles);
                const D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC& ommTriangles = d3d12GeometryDesc.OmmTriangles;
                const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& triangles = *ommTriangles.pTriangles;
                geometryDesc.data[0] = triangles.VertexCount;
                geometryDesc.data[1] = triangles.IndexCount;
                break;
            }
            default: break;
            }
        }
        ctx.m_BlasTracker.Insert(viewerBlasDesc);
    }
    return modifiedPipelineState;
}

void NvBvhViewerD3D12::OnNewFrame()
{
    BvhViewerScopedLock scopedLock(m_Lock);

    if (IsActive() == false)
        return;

    InternalContext& ctx = *m_Context;
    const uint32_t currentId = ctx.GetFrameId();
    const uint32_t currentBufferedId = currentId % ctx.m_NumBufferedFrames;
    if (ctx.m_Frames[currentBufferedId].tlasOffset == 0)
        return;

    const uint32_t nextFrameId = currentId + 1;
    const uint32_t nextBufferedFrameId = nextFrameId % ctx.m_NumBufferedFrames;
    FrameDesc& nextFrame = ctx.m_Frames[nextBufferedFrameId];

    scopedLock.Clear();
    {
        while (ctx.m_RenderFence->GetCompletedValue() < nextFrame.renderFenceValue);
    }
    scopedLock.Set();

    ctx.m_Frames[nextBufferedFrameId].Reset();
    ctx.m_FrameId.store(nextFrameId, std::memory_order_relaxed);
    ctx.m_MultiIndirectTracker.Reset(nextBufferedFrameId);
}

void InternalContext::UpdateObjectPool(bool force)
{
    const uint32_t frameId = GetFrameId();
    for (size_t i = m_ObjectPool.staleBuffers.size(); i > 0;)
    {
        ViewerBuffer& buffer = m_ObjectPool.staleBuffers[--i];
        bool ready = (frameId - buffer.staleFrameId) > m_NumBufferedFrames * 2;
        if (ready || force)
        {
            ReleaseThenZero(buffer.ptr);
            m_ObjectPool.staleBuffers.erase(m_ObjectPool.staleBuffers.begin() + i);
        }

    }
    for (size_t i = m_ObjectPool.staleImages.size(); i > 0;)
    {
        ViewerImage& image = m_ObjectPool.staleImages[--i];
        bool ready = (frameId - image.staleFrameId) > m_NumBufferedFrames * 2;
        if (ready || force)
        {
            ReleaseThenZero(image.ptr);
            m_ObjectPool.staleImages.erase(m_ObjectPool.staleImages.begin() + i);
        }
    }

}

void InternalContext::DestroyBackend()
{
    while (m_RenderFence && m_RenderFence->GetCompletedValue() < m_RenderFenceValue);

    for (auto& frameDesc : m_Frames)
        frameDesc.Destroy();

    auto DestroyCommandListPool = [&](std::vector< CommandBuffer >& pool)
        {
            for (uint32_t i = 0; i < uint32_t(pool.size()); ++i)
            {
                CommandBuffer& commandBuffer = pool[i];
                ReleaseThenZero(commandBuffer.list);
                ReleaseThenZero(commandBuffer.allocator);
            }
            pool.resize(0); pool.shrink_to_fit();
        };

    DestroyCommandListPool(m_PresentCommandListPool);
    for(uint32_t queueId = 0; queueId < uint32_t(ViewerQueueType::Count); ++queueId)
        DestroyCommandListPool(m_ObjectPool.commandBufferPools[queueId]);

    for (auto& obj : m_ObjectPool.buffers)
        ReleaseThenZero(obj.ptr);
    m_ObjectPool.buffers.resize(0); m_ObjectPool.buffers.shrink_to_fit();

    for (auto& obj : m_ObjectPool.images)
        ReleaseThenZero(obj.ptr);
    m_ObjectPool.images.resize(0); m_ObjectPool.images.shrink_to_fit();

    for (auto& obj : m_ObjectPool.descriptorHeaps)
        ReleaseThenZero(obj);
    m_ObjectPool.descriptorHeaps.resize(0); m_ObjectPool.descriptorHeaps.shrink_to_fit();

    for (uint32_t i = 0; i < uint32_t(BvhViewerResources::NumBuffers); ++i)
        m_ObjectPool.perBufferTypeOffsets[i] = 0;

    for (size_t i = 0; i < m_BackBuffers.size(); ++i)
    {
        m_BackBuffers[i]->Release();
        m_BackBuffers[i] = nullptr;
    }
    m_BackBuffers.resize(0); m_BackBuffers.shrink_to_fit();

    for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
    {
        m_Pipelines[i].descriptorRanges.resize(0);
        m_Pipelines[i].descriptorRanges.shrink_to_fit();
        ReleaseThenZero(m_Pipelines[i].pipelineState);
        ReleaseThenZero(m_Pipelines[i].rootSignature);
        ReleaseThenZero(m_Pipelines[i].commandSignature);
    }

    UpdateObjectPool(true);
    ReleaseThenZero(m_SwapChain);
    ReleaseThenZero(m_DxgiFactory);
    ReleaseThenZero(m_CommandQueue);
    ReleaseThenZero(m_PresentFence);
    ReleaseThenZero(m_RenderFence);
    ReleaseThenZero(m_UiDescriptorHeap);
    ReleaseThenZero(m_RtvDescriptorHeap);

    m_MultiIndirectTracker.Destroy();

    if(m_PresenterReady) ImGui_ImplDX12_Shutdown();

    for (auto& descriptor : m_BackBufferRtvDescriptors)
        descriptor = {};

    #if defined(BVHVIEWER_ENABLE_NVAPI)
    if (m_Vendor == Vendor::NV)
        NvAPI_Unload();
    #endif // BVHVIEWER_ENABLE_NVAPI

    #if defined(BVHVIEWER_ENABLE_AMD_AGS)
    if(m_Vendor == Vendor::AMD && m_AgsContext)
        agsDeInitialize(m_AgsContext);
    m_AgsContext = nullptr;
    #endif // defined(BVHVIEWER_ENABLE_AMD_AGS)

    m_RenderFenceValue = 0;
    m_PresentFenceValue = 0;
    m_FrameId.store(0);
    m_Vendor = Vendor::Unknown;
    m_PresenterReady = false;
}

inline D3D12_ROOT_PARAMETER_TYPE GetRootParameterType(DescriptorTypes type)
{
    switch (type)
    {
    case DescriptorTypes::Tlas:
    case DescriptorTypes::TypedBuffer:
    case DescriptorTypes::StructuredBuffer:
    case DescriptorTypes::RawBuffer:
        return D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_SRV;

    case DescriptorTypes::StorageTexture:
    case DescriptorTypes::StorageTypedBuffer:
    case DescriptorTypes::StorageStructuredBuffer:
    case DescriptorTypes::RawBufferStorage:
        return D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_UAV;

    case DescriptorTypes::ConstantBuffer:
        return D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_CBV;
    case DescriptorTypes::PushConstants:
        return D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    case DescriptorTypes::Count:
        return D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_SRV;
    default: return D3D12_ROOT_PARAMETER_TYPE::D3D12_ROOT_PARAMETER_TYPE_SRV;
    }
}

inline bool IsRootParameter(BvhViewerResources resource, Vendor vendor)
{
    switch (resource)
    {
    // base
    case BvhViewerResources::RenderPushConstants:
    case BvhViewerResources::InstanceDescInternal:
    case BvhViewerResources::InstanceDescExternal:

    //PTLAS
    case BvhViewerResources::IndirectOpsCountExternal:
    case BvhViewerResources::IndirectOpsBufferExternal:
    case BvhViewerResources::PtlasIndirectPushConstants:
    case BvhViewerResources::IndirectOpsData:
        return true;

    case BvhViewerResources::GpuVendorReserved:
        return vendor == Vendor::AMD;
    default: break;
    }

    if(resource >= BvhViewerResources::MiStart && resource <= BvhViewerResources::MiEnd)
        return true;

    return false;
}

inline DescriptorRegisterType GetDescriptorRegisterType(DescriptorTypes type)
{
    switch (type)
    {
    case DescriptorTypes::Tlas:
    case DescriptorTypes::TypedBuffer:
    case DescriptorTypes::StructuredBuffer:
    case DescriptorTypes::RawBuffer:
        return DescriptorRegisterType::Srv;

    case DescriptorTypes::StorageTexture:
    case DescriptorTypes::StorageTypedBuffer:
    case DescriptorTypes::StorageStructuredBuffer:
    case DescriptorTypes::RawBufferStorage:
        return DescriptorRegisterType::Uav;

    case DescriptorTypes::ConstantBuffer:
        return DescriptorRegisterType::Cbv;

    case DescriptorTypes::PushConstants:
        return DescriptorRegisterType::PushConstants;

    case DescriptorTypes::Count:
        return DescriptorRegisterType::Srv;
    }
    return DescriptorRegisterType::Srv;
}

inline bool IsVolitileResource(BvhViewerResources resource)
{
    switch(resource)
    {
    default: break;
    case BvhViewerResources::MiArgBuffer:
    case BvhViewerResources::MiSmallArgBuffer:

    case BvhViewerResources::MiCountBufferInternal:
    case BvhViewerResources::MiResolvedInputBuffer:
    case BvhViewerResources::MiResolvedCountBuffer:
    case BvhViewerResources::MiResolvedDestinationBuffer:
        return true;
    }
    return false;
}

bool InternalContext::CreatePipelines()
{
    uint32_t descriptorTableOffset = 0;
    for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
    {
        const CommonPipelineDesc& commonDesc = g_ViewerPipelineDescs[i];
        const uint32_t numBindings = uint32_t(commonDesc.bindings.size());
        PipelineDescD3D12& d3d12Pipeline = m_Pipelines[i];

        uint32_t tRegShift = 0;
        uint32_t uRegShift = 0;
        uint32_t bRegShift = 0;

        std::vector<D3D12_ROOT_PARAMETER1> rootParameters = {};
        std::vector<D3D12_DESCRIPTOR_RANGE1> descriptorRanges = {};

        bool isPrevBindingRootParameter = false;
        DescriptorRegisterType prevBindingType = DescriptorRegisterType(-1);

        #if defined(BVHVIEWER_ENABLE_NVAPI)
        uint32_t vendorSlot = uint32_t(~0);
        #endif // defined(BVHVIEWER_ENABLE_NVAPI)

        for (uint32_t bindingId = 0; bindingId < numBindings; ++bindingId)
        {
            const DescriptorBindingDesc& bindingDesc = commonDesc.bindings[bindingId];
            const bool isRootDescriptor = IsRootParameter(bindingDesc.resource, m_Vendor);
            const DescriptorRegisterType bindingType = GetDescriptorRegisterType(bindingDesc.type);

            uint32_t bindingRegister = uint32_t(~0);
            uint32_t bindingSpace = 0;
            if (bindingType == DescriptorRegisterType::Srv)
                bindingRegister = tRegShift++;
            else if (bindingType == DescriptorRegisterType::Uav)
                bindingRegister = uRegShift++;
            else if (bindingType == DescriptorRegisterType::Cbv || bindingType == DescriptorRegisterType::PushConstants)
                bindingRegister = bRegShift++;

            if(bindingDesc.resource == BvhViewerResources::GpuVendorReserved)
            {
                switch(m_Vendor)
                {
                #if defined(BVHVIEWER_ENABLE_NVAPI)
                case Vendor::NV: vendorSlot = bindingRegister; break;
                #endif // defined(BVHVIEWER_ENABLE_NVAPI)

                #if defined(BVHVIEWER_ENABLE_AMD_AGS)
                case Vendor::AMD: bindingRegister = 0; bindingSpace = uint32_t(AGS_DX12_SHADER_INTRINSICS_SPACE_ID); break;
                #endif // defined(BVHVIEWER_ENABLE_AMD_AGS)

                case Vendor::Unknown: break;
                default: break;
                }
            }

            D3D12_ROOT_PARAMETER1 rootParameter = {};
            if (isRootDescriptor)
            {
                D3D12_ROOT_PARAMETER_TYPE rootParameterType = GetRootParameterType(bindingDesc.type);
                rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParameter.Descriptor.Flags = IsVolitileResource(bindingDesc.resource) ? D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE : D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
                rootParameter.Descriptor.RegisterSpace = bindingSpace;
                rootParameter.Descriptor.ShaderRegister = bindingRegister;
                rootParameter.ParameterType = rootParameterType;

                if (bindingType == DescriptorRegisterType::PushConstants)
                {
                    rootParameter.Constants.RegisterSpace = 0;
                    rootParameter.Constants.ShaderRegister = bindingRegister;
                    rootParameter.Constants.Num32BitValues = static_cast<UINT>(bindingDesc.stride / sizeof(uint32_t));
                }
                else
                    rootParameter.Descriptor.ShaderRegister = bindingRegister;

                rootParameters.push_back(rootParameter);

                d3d12Pipeline.rangeIdPerRootParameter.push_back(uint32_t(d3d12Pipeline.descriptorRanges.size()));
                DescriptorRangeDesc& rangeDesc = d3d12Pipeline.descriptorRanges.emplace_back();
                rangeDesc.type = bindingType;
                rangeDesc.rootParameterId = uint32_t(rootParameters.size()) - 1;
                rangeDesc.isRootParameter = true;
                rangeDesc.baseBindingId = bindingType == DescriptorRegisterType::PushConstants ? rootParameter.Constants.ShaderRegister : rootParameter.Descriptor.ShaderRegister;
                rangeDesc.numBindings = 1;
                rangeDesc.bindings.push_back(bindingDesc);
                isPrevBindingRootParameter = true;
            }
            else
            {
                D3D12_DESCRIPTOR_RANGE1* descriptorRangePtr = nullptr;
                DescriptorRangeDesc* rangePtr = nullptr;
                bool isNewRange = false;
                if (prevBindingType != bindingType || isPrevBindingRootParameter)
                {
                    rangePtr = &d3d12Pipeline.descriptorRanges.emplace_back();
                    descriptorRangePtr = &descriptorRanges.emplace_back();
                    isNewRange = true;
                }
                else
                {
                    descriptorRangePtr = &descriptorRanges.back();
                    rangePtr = &d3d12Pipeline.descriptorRanges.back();
                }

                DescriptorRangeDesc& rangeDesc = *rangePtr;
                D3D12_DESCRIPTOR_RANGE1& descriptorRange = *descriptorRangePtr;
                if (isNewRange)
                {
                    rangeDesc.baseBindingId = bindingRegister;
                    rangeDesc.isRootParameter = false;
                    rangeDesc.numBindings = 0;
                    rangeDesc.descriptorTableOffset = descriptorTableOffset;
                    rangeDesc.type = bindingType;

                    descriptorRange.RegisterSpace = bindingSpace;
                    descriptorRange.BaseShaderRegister = bindingRegister;
                    descriptorRange.NumDescriptors = 0;
                    descriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
                    descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                    if (bindingType == DescriptorRegisterType::Srv)
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    else if (bindingType == DescriptorRegisterType::Uav)
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    else if (bindingType == DescriptorRegisterType::Cbv)
                        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE::D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                }

                ++rangeDesc.numBindings;
                ++descriptorRange.NumDescriptors;
                ++descriptorTableOffset;
                rangeDesc.bindings.push_back(bindingDesc);
                isPrevBindingRootParameter = false;
            }
            prevBindingType = bindingType;
        }

        if (descriptorRanges.empty() == false)
        {
            d3d12Pipeline.descriptorTableRootParameterId = uint32_t(rootParameters.size());
            D3D12_ROOT_PARAMETER1& descriptorTable = rootParameters.emplace_back();
            descriptorTable.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            descriptorTable.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            descriptorTable.DescriptorTable.NumDescriptorRanges = UINT(descriptorRanges.size());
            descriptorTable.DescriptorTable.pDescriptorRanges = descriptorRanges.data();
        }

        D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
        rootSignatureDesc.pParameters = rootParameters.data();
        rootSignatureDesc.NumParameters = UINT(rootParameters.size());
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* blob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc1 = {};
        rootSignatureDesc1.Desc_1_1 = rootSignatureDesc;
        rootSignatureDesc1.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc1, &blob, &errorBlob);
        if(hr != S_OK)
        {
            char* ptr = (char*)errorBlob->GetBufferPointer(); (void)ptr;
            ReleaseThenZero(blob);
            ReleaseThenZero(errorBlob);
        }
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::D3D12SerializeRootSignature", false);

        hr = m_Device->CreateRootSignature(c_NodeMask, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&d3d12Pipeline.rootSignature));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateRootSignature", false);

        #if defined(BVHVIEWER_ENABLE_NVAPI)
        const bool reserveNvApiSlot = vendorSlot != uint32_t(~0) && m_Vendor == Vendor::NV;
        if (reserveNvApiSlot)
        {
            NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(m_Device, vendorSlot, 0); // TODO:
            assert(status == NVAPI_OK && "NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread failed");
            if (status != NVAPI_OK)
                LogMessage(NvBvhViewerCommon::MessageType::Error, "NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread failed");
        }
        #endif // defined(BVHVIEWER_ENABLE_NVAPI)

        const uint32_t permutationId = GetPermutationId(Pipelines(i), m_Vendor);
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = d3d12Pipeline.rootSignature;
        psoDesc.NodeMask = c_NodeMask;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        psoDesc.CS.pShaderBytecode = commonDesc.shaderPermutations[permutationId].code;
        psoDesc.CS.BytecodeLength = commonDesc.shaderPermutations[permutationId].codeSize;
        hr = m_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&d3d12Pipeline.pipelineState));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateComputePipelineState", false);

        #if defined(BVHVIEWER_ENABLE_NVAPI)
        if (reserveNvApiSlot)
        {
            NvAPI_Status status = NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(m_Device, ~0u, 0);
            assert(status == NVAPI_OK && "Failed to NvAPI_D3D12_SetNvShaderExtnSlotSpace");
            if (status != NVAPI_OK)
                LogMessage(NvBvhViewerCommon::MessageType::Error, "Failed to NvAPI_D3D12_SetNvShaderExtnSlotSpace");
        }
        #endif // defined(BVHVIEWER_ENABLE_NVAPI)
    }

    for (uint32_t i = 0; i < uint32_t(Pipelines::Count); ++i)
    {
        PipelineDescD3D12& d3d12Pipeline = m_Pipelines[i];
        for(size_t j = 0; j < d3d12Pipeline.descriptorRanges.size(); ++j)
        {
            DescriptorRangeDesc& range = d3d12Pipeline.descriptorRanges[j];
            if(range.isRootParameter == false)
            {
                d3d12Pipeline.descriptorTableHeapOffset = range.descriptorTableOffset;
                break;
            }
        }
    }

    { // create command signature for PTLAS indirect ops parsing
        std::vector<D3D12_INDIRECT_ARGUMENT_DESC> argDescs = {};
        { // type
            D3D12_INDIRECT_ARGUMENT_DESC typeArg = {};
            typeArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            typeArg.Constant.RootParameterIndex = 0;
            typeArg.Constant.Num32BitValuesToSet = 1;
            typeArg.Constant.DestOffsetIn32BitValues = 0;
            argDescs.push_back(typeArg);
        }
        { // count
            D3D12_INDIRECT_ARGUMENT_DESC countArg = {};
            countArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            countArg.Constant.RootParameterIndex = 0;
            countArg.Constant.Num32BitValuesToSet = 1;
            countArg.Constant.DestOffsetIn32BitValues = 1;
            argDescs.push_back(countArg);
        }
        { // stride 
            D3D12_INDIRECT_ARGUMENT_DESC strideArg = {};
            strideArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            strideArg.Constant.RootParameterIndex = 0;
            strideArg.Constant.Num32BitValuesToSet = 2;
            strideArg.Constant.DestOffsetIn32BitValues = 2;
            argDescs.push_back(strideArg);
        }
        { // data buffer
            D3D12_INDIRECT_ARGUMENT_DESC dataViewArg = {};
            dataViewArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            dataViewArg.ShaderResourceView.RootParameterIndex = 1;
            argDescs.push_back(dataViewArg);
        }
        { // dispatch dimensions
            D3D12_INDIRECT_ARGUMENT_DESC dispatchArg = {};
            dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            argDescs.push_back(dispatchArg);
        }

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.NumArgumentDescs = UINT(argDescs.size());
        commandSignatureDesc.pArgumentDescs = argDescs.data();
        commandSignatureDesc.ByteStride = sizeof(IndirectOpsParserArgs);

        PipelineDescD3D12& pipeline = m_Pipelines[uint32_t(Pipelines::IndirectOpsParser)];
        HRESULT hr = m_Device->CreateCommandSignature(&commandSignatureDesc, pipeline.rootSignature, IID_PPV_ARGS(&pipeline.commandSignature));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateCommandSignature", false);
    }

    { // command signature for Multi Indirect tracker's Blas Build pass
        std::vector<D3D12_INDIRECT_ARGUMENT_DESC> argDescs = {};
        { // uint64_t clas addr buffer
            D3D12_INDIRECT_ARGUMENT_DESC dataViewArg = {};
            dataViewArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            dataViewArg.ShaderResourceView.RootParameterIndex = 0;
            argDescs.push_back(dataViewArg);
        }
        { // uint64_t clas addr buffer
            D3D12_INDIRECT_ARGUMENT_DESC blasAddr = {};
            blasAddr.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            blasAddr.Constant.RootParameterIndex = 1;
            blasAddr.Constant.Num32BitValuesToSet = 2;
            blasAddr.Constant.DestOffsetIn32BitValues = 0;
            argDescs.push_back(blasAddr);
        }
        { // uint32_t clas count
            D3D12_INDIRECT_ARGUMENT_DESC typeArg = {};
            typeArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            typeArg.Constant.RootParameterIndex = 1;
            typeArg.Constant.Num32BitValuesToSet = 1;
            typeArg.Constant.DestOffsetIn32BitValues = 2;
            argDescs.push_back(typeArg);
        }
        { // uint32_t dispatch linear Id
            D3D12_INDIRECT_ARGUMENT_DESC typeArg = {};
            typeArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            typeArg.Constant.RootParameterIndex = 1;
            typeArg.Constant.Num32BitValuesToSet = 1;
            typeArg.Constant.DestOffsetIn32BitValues = 3;
            argDescs.push_back(typeArg);
        }
        { // dispatch dimensions
            D3D12_INDIRECT_ARGUMENT_DESC dispatchArg = {};
            dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            argDescs.push_back(dispatchArg);
        }

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.NumArgumentDescs = UINT(argDescs.size());
        commandSignatureDesc.pArgumentDescs = argDescs.data();
        commandSignatureDesc.ByteStride = sizeof(MultiIndirectBlasBuildArgs);

        PipelineDescD3D12& pipeline = m_Pipelines[uint32_t(Pipelines::MiBlasBuildParser)];
        HRESULT hr = m_Device->CreateCommandSignature(&commandSignatureDesc, pipeline.rootSignature, IID_PPV_ARGS(&pipeline.commandSignature));
        RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateCommandSignature", false);
    }

    { // create command signature for multi indirect indirirected inputs resolve passes
        std::vector<D3D12_INDIRECT_ARGUMENT_DESC> argDescs = {};
        { // count buffer root binding
            D3D12_INDIRECT_ARGUMENT_DESC countBufferView = {};
            countBufferView.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            countBufferView.ShaderResourceView.RootParameterIndex = 0;
            argDescs.push_back(countBufferView);
        }
        { //  argument binding array
            D3D12_INDIRECT_ARGUMENT_DESC argBufferView = {};
            argBufferView.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            argBufferView.ShaderResourceView.RootParameterIndex = 1;
            argDescs.push_back(argBufferView);
        }
        { //  argument binding array
            D3D12_INDIRECT_ARGUMENT_DESC argBufferView = {};
            argBufferView.Type = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW;
            argBufferView.ShaderResourceView.RootParameterIndex = 2;
            argDescs.push_back(argBufferView);
        }
        { // dispatch dimensions
            D3D12_INDIRECT_ARGUMENT_DESC dispatchArg = {};
            dispatchArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            argDescs.push_back(dispatchArg);
        }

        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.NumArgumentDescs = UINT(argDescs.size());
        commandSignatureDesc.pArgumentDescs = argDescs.data();
        commandSignatureDesc.ByteStride = sizeof(MultiIndirectResloveInputsArgs);

        const Pipelines pipelines[] = 
        {
            Pipelines::MiParseClasBuild,
            Pipelines::MiParseInstantiation,
            Pipelines::MiBlasBuildDispatcher,
            Pipelines::MiParseMove,
        };

        for(size_t pipelineId = 0; pipelineId < std::size(pipelines); ++pipelineId)
        {
            PipelineDescD3D12& pipeline = m_Pipelines[uint32_t(pipelines[pipelineId])];
            
            HRESULT hr = m_Device->CreateCommandSignature(&commandSignatureDesc, pipeline.rootSignature, IID_PPV_ARGS(&pipeline.commandSignature));
            RETURN_BAD_HRESULT(hr, "NvBvhViewer::CreateCommandSignature", false);
        }
    }
    return true;
}

Vendor InternalContext::GetVendor(ID3D12Device* device)
{
    LUID deviceLuid = device->GetAdapterLuid();
    Vendor result = Vendor::Unknown;

    IDXGIFactory* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory)))
        return result;

    constexpr uint32_t NvVendorId = 0x10DE;
    constexpr uint32_t AmdVendorId = 0x1002;
    IDXGIAdapter* pAdapter = nullptr;
    for (UINT i = 0; pFactory && pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC adapterDesc = {};
        if (SUCCEEDED(pAdapter->GetDesc(&adapterDesc))
            && adapterDesc.AdapterLuid.HighPart == deviceLuid.HighPart
            && adapterDesc.AdapterLuid.LowPart == deviceLuid.LowPart)
        {
            result = adapterDesc.VendorId == NvVendorId ? Vendor::NV : result;
            result = adapterDesc.VendorId == AmdVendorId ? Vendor::AMD : result;
        }

        if(pAdapter)
        {
            pAdapter->Release();
            pAdapter = nullptr;
        }

        if(result != Vendor::Unknown)
            break;
    }

    if (pAdapter) pAdapter->Release();
    if (pFactory) pFactory->Release();

    return result;
}

bool InternalContext::CreateBackend()
{
    HRESULT hr = {};
    { // Vendor API
        std::vector<IDXGIAdapter*> vAdapters;
        m_Vendor = GetVendor(m_Device);

        #if defined(BVHVIEWER_ENABLE_NVAPI)
        if (m_Vendor == Vendor::NV)
        {
            NvAPI_Status nvapiStatusSuccess = NvAPI_Initialize();
            if (nvapiStatusSuccess == NVAPI_OK)
            {
                m_EnableLss = true;
                m_EnablePositionFetch = true;
            }
            else 
                LogMessage(NvBvhViewerCommon::MessageType::Info, "NvBvhViewerD3D12::Impl::CreateBackend: Failed to initialize NVAPI");
        }
        #endif // defined(BVHVIEWER_ENABLE_NVAPI)
        
        #if defined(BVHVIEWER_ENABLE_AMD_AGS)
        if(m_Vendor == Vendor::AMD)
        {
            AGSReturnCode agsStatus = agsInitialize(AGS_MAKE_VERSION(AMD_AGS_VERSION_MAJOR, AMD_AGS_VERSION_MINOR, AMD_AGS_VERSION_PATCH), nullptr, &m_AgsContext, nullptr);
            if(agsStatus != AGS_SUCCESS)
                LogMessage(NvBvhViewerCommon::MessageType::Info, "NvBvhViewerD3D12::Impl::CreateBackend: Failed to initialize AMDAGS");
        }
        #endif // defined(BVHVIEWER_ENABLE_AMD_AGS)
    }
    { // Create CommandQueue
        D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        hr = m_Device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&m_CommandQueue));
        RETURN_BAD_HRESULT(hr, "NvBvhViewerD3D12::Impl::CreateBackend::CreateCommandQueue", false);
    }
    { // Fences
        hr = m_Device->CreateFence(uint64_t(m_FrameId.load()), D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_PresentFence));
        RETURN_BAD_HRESULT(hr, "NvBvhViewerD3D12::Impl::CreateBackend::CreateFence", false);

        hr = m_Device->CreateFence(m_RenderFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_RenderFence));
        RETURN_BAD_HRESULT(hr, "NvBvhViewerD3D12::Impl::CreateBackend::CreateFence", false);
    }
    { // Camera
        float3 pos = float3(0.0f, 0.0f, 0.0f);
        float3 lookAt = float3(0.0f, 1.0f, 0.0f);
        m_Camera.Initialize(pos, lookAt, false);
        CameraDesc cameraDesc = {};
        cameraDesc.aspectRatio = float(*m_OutputWidth) / float(*m_OutputHeight);
        m_Camera.Update(cameraDesc);
    }
    { // Create command list pools
        auto CreateCommandListPool = [&](std::vector< CommandBuffer >& pool, uint32_t poolSize, ViewerQueueType queueType)
            {
                pool.resize(poolSize, CommandBuffer());
                D3D12_COMMAND_LIST_TYPE commandType = queueType == ViewerQueueType::Graphics ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COMPUTE;
                for (uint32_t i = 0; i < poolSize; ++i)
                {
                    CommandBuffer& commandBuffer = pool[i];
                    commandBuffer.dt = &m_DispatchTable.GetCmdList(commandType);
                    hr = m_Device->CreateCommandAllocator(commandType, IID_PPV_ARGS(&commandBuffer.allocator));
                    RETURN_BAD_HRESULT(hr, "NvBvhViewer::Impl::CreateBackend::CreateCommandAllocator", false);
                    hr = m_Device->CreateCommandList(0, commandType, commandBuffer.allocator, nullptr, IID_PPV_ARGS(&commandBuffer.list));
                    RETURN_BAD_HRESULT(hr, "NvBvhViewer::Impl::CreateBackend::CreateCommandList", false);
                    commandBuffer.dt->Close(commandBuffer.list);
                }
                return true;
            };

        bool commandListStatus = CreateCommandListPool(m_PresentCommandListPool, m_NumBufferedFrames, ViewerQueueType::Graphics);

        for(uint32_t queueId = 0; queueId < uint32_t(ViewerQueueType::Count); ++queueId)
            commandListStatus = commandListStatus && CreateCommandListPool(m_ObjectPool.commandBufferPools[queueId], m_NumBufferedFrames * c_MaxTlasPerFrame, ViewerQueueType(queueId));

        if(commandListStatus != true)
            return false;
    }
    {
        if (CreatePipelines() == false)
            return false;
    }
    const uint32_t tlasCount = m_NumBufferedFrames * c_MaxTlasPerFrame;
    { // prepare object pool
        uint32_t numBuffersTotal = 0;
        for (uint32_t i = 0; i < uint32_t(BvhViewerResources::NumBuffers); ++i)
        { // Buffer Resources
            uint32_t bufferId = i + uint32_t(BvhViewerResources::FirstBuffer);
            uint32_t numBuffers = GetBufferResourceCount(BvhViewerResources(bufferId), m_NumBufferedFrames);
            m_ObjectPool.perBufferTypeOffsets[i] = numBuffersTotal;
            numBuffersTotal += numBuffers;
        }
        m_ObjectPool.buffers.resize(numBuffersTotal, ViewerBuffer());
        m_ObjectPool.images.resize(tlasCount, ViewerImage());
        m_ObjectPool.descriptorHeaps.resize(tlasCount, nullptr);
    }

    if(m_MultiIndirect)
    { // clas trakers
        m_MultiIndirectTracker.Initialize(m_Device, m_NumBufferedFrames);
    }

    return true;
}

bool NvBvhViewerD3D12::IsInternalQueue(const ID3D12CommandQueue* commandQueue) const
{
    if (m_Context)
        return m_Context->m_CommandQueue == commandQueue;
    return false;
}

void NvBvhViewerD3D12::ResizeOutputWindow(uint32_t newWidth, uint32_t newHeight)
{
    if (m_OutputWidth == newWidth && m_OutputHeight == newHeight)
        return;
    if (!newWidth || !newHeight)
        return;

    m_OutputWidth = newWidth;
    m_OutputHeight = newHeight;
}

void NvBvhViewerD3D12::Shutdown()
{
    if (m_BackendInitialized == false)
        return;

    m_Exit = true;
    while (m_Context->m_PresentFence->GetCompletedValue() < m_Context->m_PresentFenceValue);

    DestroyAsyncPresentThread();

    BvhViewerScopedLock scopedLock(m_Lock);

    m_Context->DestroyBackend();
    DestroyViewerWindow();

    m_BackendInitialized = false;
}

bool NvBvhViewerD3D12::Launch()
{
    if (m_Initialized == false)
        return false;
    if (m_BackendInitialized)
        return true;

    m_Exit = false;

    InternalContext& ctx = *m_Context;

    m_BackendInitialized = ctx.CreateBackend();
    if (m_BackendInitialized == false)
    {
        ctx.DestroyBackend();
        return false;
    }

    if (CreatePresentThread(&NvBvhViewerD3D12::PresentThread) == false)
    {
        Shutdown();
        return false;
    }

    return m_BackendInitialized;
}

bool NvBvhViewerD3D12::Deinitialize()
{
    if(m_Initialized == false)
        return true;
    if (m_BackendInitialized == true)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Deinitialize: Viewer has to be destroyed before deinitialization");
        return false;
    }

    m_Context->m_Frames.resize(0); m_Context->m_Frames.shrink_to_fit();

    delete m_Context;
    m_Context = nullptr;

    m_OutputWidth = 0;
    m_OutputHeight = 0;
    m_WindowCloseCallback = nullptr;
    m_WindowResizeCallback = nullptr;
    m_CallbackUserData = nullptr;

    m_Initialized = false;
    return true;
}

bool NvBvhViewerD3D12::Initialize(const NvBvhViewerD3D12::InitDesc& initDesc)
{
    if (m_Initialized)
        return true;

    g_LogMessageCallback = initDesc.logMessageCallback;

    if (initDesc.device == nullptr)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: Null D3D12 device");
        return false;
    }

    if (initDesc.outputWidth == 0 || initDesc.outputHeight == 0)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: window dimensions can't be zero");
        return false;
    }

    if(initDesc.dispatchTable.IsInitialized() == false)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: Dispatch Table has to be initialized beforehand");
        return false;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (FAILED(initDesc.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: ID3D12Device::CheckFeatureSupport D3D12_FEATURE_D3D12_OPTIONS5 failed");
        return false;
    }

    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: D3D12_RAYTRACING_TIER_1_1 or above is required");
        return false;
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_5 };
    if (FAILED(initDesc.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(D3D12_FEATURE_DATA_SHADER_MODEL))))
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: ID3D12Device::CheckFeatureSupport D3D12_FEATURE_SHADER_MODEL failed");
        return false;
    }

    if(shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5)
    {
        LogMessage(MessageType::Error, "NvBvhViewerD3D12::Initialize: Provided D3D12 Device doesn't support D3D_SHADER_MODEL_6_5");
        return false;
    }

    m_OutputWidth = initDesc.outputWidth;
    m_OutputHeight = initDesc.outputHeight;
    m_WindowCloseCallback = initDesc.windowCloseCallback;
    m_WindowResizeCallback = initDesc.windowResizeCallback;
    m_CallbackUserData = initDesc.callbackUserData;

    m_Context = new InternalContext();
    InternalContext& ctx = *m_Context;
    ctx.m_Device = initDesc.device;
    ctx.m_NumBufferedFrames = initDesc.numBufferedFrames;
    ctx.m_DispatchTable = initDesc.dispatchTable;
    ctx.m_Frames.resize(ctx.m_NumBufferedFrames, FrameDesc());
    ctx.m_Inline = initDesc.enableInlineRendering;
    ctx.m_MultiIndirect = initDesc.enableClusterGeometryTracking;
    ctx.m_OutputWidth = &m_OutputWidth;
    ctx.m_OutputHeight = &m_OutputHeight;

    m_Initialized = true;
    return m_Initialized;
}
