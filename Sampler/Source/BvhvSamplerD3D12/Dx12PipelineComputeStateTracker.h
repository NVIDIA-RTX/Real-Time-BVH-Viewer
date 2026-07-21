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
#include <map>
#include <vector>
#include <atomic>
#include <NvBvhViewerDispatchTableD3D12.h>

class PipelineComputeStateTracker
{
    // The maximum size of a root signature is 64 DWORDs.
    // ipovarov: overhead of tracking all 64 params is expected, but tracker is only used in pair with BvhViewer and performance is not a concern
    static const uint32_t c_RootParameterNumMax = 64;

    enum class BindingType
    {
        None,
        DescriptorTable,
        Constant,
        ConstantBufferView,
        ShaderResourceView,
        UnorderedAccessView,
        Count
    };

    struct Binding
    {
        BindingType type;

        union
        {
            struct DescriptorTable
            {
                D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor;
            } DescriptorTable;

            struct ConstantBufferView
            {
                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
            } ConstantBufferView;

            struct ShaderResourceView
            {
                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
            } ShaderResourceView;

            struct UnorderedAccessView
            {
                D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
            } UnorderedAccessView;
        };
    };

    struct State
    {
        State()
        {
            m_RootSignature = nullptr;
            m_DescriptorHeaps.clear();
            m_PipelineState = nullptr;
            m_StateObject = nullptr;
            memset(m_Bindings, 0, sizeof(m_Bindings));
            for (auto& binding : m_Bindings)
            {
                binding.ConstantBufferView.BufferLocation = 0;
                binding.type = BindingType::None;
            }

            for (uint32_t i = 0; i < c_RootParameterNumMax; ++i)
                m_RootConstantMaps[i].clear();
        }
        ID3D12RootSignature* m_RootSignature = nullptr;
        ID3D12PipelineState* m_PipelineState = nullptr;
        ID3D12StateObject* m_StateObject = nullptr;
        Binding m_Bindings[c_RootParameterNumMax] = {};
        std::map<UINT, UINT> m_RootConstantMaps[c_RootParameterNumMax] = {};
        std::vector<ID3D12DescriptorHeap*> m_DescriptorHeaps = {};
    };

    std::atomic_flag m_WriteLock = ATOMIC_FLAG_INIT;
    std::atomic_uint m_ReadLock = 0;
    std::map<ID3D12CommandList*, State> m_States = {};
    D3D12DispatchTable D3D12DT = {};

public:
    inline bool IsSupportedType(ID3D12GraphicsCommandList* pCommandList)
    {
        const D3D12_COMMAND_LIST_TYPE type = pCommandList->GetType();
        return type == D3D12_COMMAND_LIST_TYPE_DIRECT || type == D3D12_COMMAND_LIST_TYPE_COMPUTE;
    }

    void Initialize(D3D12DispatchTable dispatchTable);
    bool CanRestore(ID3D12GraphicsCommandList* pCommandList);
    void Open(ID3D12GraphicsCommandList* pCommandList, ID3D12PipelineState* pipelineState);
    void Close(ID3D12GraphicsCommandList* pCommandList);
    void SetComputeRootSignature(ID3D12GraphicsCommandList* pCommandList, ID3D12RootSignature* pRootSignature);
    void UpdateDescriptorTable(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor);
    void UpdateConstant(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues);
    void UpdateConstantBufferView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void UpdateShaderResourceView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void UpdateUnorderedAccessView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation);
    void UpdatePipelineState(ID3D12GraphicsCommandList* pCommandList, ID3D12StateObject* stateObject);
    void UpdatePipelineState(ID3D12GraphicsCommandList* pCommandList, ID3D12PipelineState* pipelineState);
    void UpdateDescriptorHeaps(ID3D12GraphicsCommandList* pCommandList, UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps);
    void Restore(ID3D12GraphicsCommandList* pCommandList);
};
