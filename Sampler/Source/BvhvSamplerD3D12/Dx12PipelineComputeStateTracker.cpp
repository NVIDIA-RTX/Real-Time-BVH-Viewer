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

#include "Lock.h"
#include "Dx12PipelineComputeStateTracker.h"

void PipelineComputeStateTracker::Initialize(D3D12DispatchTable dispatchTable)
{
    D3D12DT = dispatchTable;
}

bool PipelineComputeStateTracker::CanRestore(ID3D12GraphicsCommandList* pCommandList)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    return it != m_States.end();
}

void PipelineComputeStateTracker::Open(ID3D12GraphicsCommandList* pCommandList, ID3D12PipelineState* pipelineState)
{
    if (IsSupportedType(pCommandList) == false)
        return;

    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Writer);
    State state = State();
    state.m_PipelineState = pipelineState;
    m_States[pCommandList] = state;
}

void PipelineComputeStateTracker::Close(ID3D12GraphicsCommandList* pCommandList)
{
    if (IsSupportedType(pCommandList) == false)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Writer);
    auto it = m_States.find(pCommandList);
    if (it != m_States.end())
        m_States.erase(it);
}

void PipelineComputeStateTracker::SetComputeRootSignature(ID3D12GraphicsCommandList* pCommandList, ID3D12RootSignature* pRootSignature)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);

    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;

    State& state = it->second;
    if (pRootSignature != state.m_RootSignature)
    { // Preserve DescriptorHeaps and pipeline sate!
        state.m_RootSignature = pRootSignature;
        memset(state.m_Bindings, 0, sizeof(state.m_Bindings));
        for (uint32_t i = 0; i < c_RootParameterNumMax; ++i)
            state.m_RootConstantMaps[i].clear();
    }
}

void PipelineComputeStateTracker::UpdateDescriptorTable(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
    if (RootParameterIndex >= c_RootParameterNumMax)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;

    auto& binding = it->second.m_Bindings[RootParameterIndex];
    binding.type = BindingType::DescriptorTable;
    binding.DescriptorTable.BaseDescriptor = BaseDescriptor;
}

void PipelineComputeStateTracker::UpdateConstant(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues)
{
    if (RootParameterIndex >= c_RootParameterNumMax)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;

    auto& binding = it->second.m_Bindings[RootParameterIndex];
    binding.type = BindingType::Constant;

    auto& rootConstantMap = it->second.m_RootConstantMaps[RootParameterIndex];
    rootConstantMap[DestOffsetIn32BitValues] = SrcData;
}

void PipelineComputeStateTracker::UpdateConstantBufferView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (RootParameterIndex >= c_RootParameterNumMax)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;
    auto& binding = it->second.m_Bindings[RootParameterIndex];
    binding.type = BindingType::ConstantBufferView;
    binding.ConstantBufferView.BufferLocation = BufferLocation;
}

void PipelineComputeStateTracker::UpdateShaderResourceView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (RootParameterIndex >= c_RootParameterNumMax)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;
    auto& binding = it->second.m_Bindings[RootParameterIndex];
    binding.type = BindingType::ShaderResourceView;
    binding.ShaderResourceView.BufferLocation = BufferLocation;
}

void PipelineComputeStateTracker::UpdateUnorderedAccessView(ID3D12GraphicsCommandList* pCommandList, UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
    if (RootParameterIndex >= c_RootParameterNumMax)
        return;
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;
    auto& binding = it->second.m_Bindings[RootParameterIndex];
    binding.type = BindingType::UnorderedAccessView;
    binding.UnorderedAccessView.BufferLocation = BufferLocation;
}

void PipelineComputeStateTracker::UpdatePipelineState(ID3D12GraphicsCommandList* pCommandList, ID3D12StateObject* stateObject)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;
    it->second.m_StateObject = stateObject;
    it->second.m_PipelineState = nullptr;
}

void PipelineComputeStateTracker::UpdatePipelineState(ID3D12GraphicsCommandList* pCommandList, ID3D12PipelineState* pipelineState)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;
    it->second.m_PipelineState = pipelineState;
    it->second.m_StateObject = nullptr;
}

void PipelineComputeStateTracker::UpdateDescriptorHeaps(ID3D12GraphicsCommandList* pCommandList, UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
    auto it = m_States.find(pCommandList);
    if (it == m_States.end())
        return;

    it->second.m_DescriptorHeaps.resize(NumDescriptorHeaps);
    memcpy(it->second.m_DescriptorHeaps.data(), ppDescriptorHeaps, sizeof(ID3D12DescriptorHeap*) * NumDescriptorHeaps);
}

void PipelineComputeStateTracker::Restore(ID3D12GraphicsCommandList* pCommandList)
{
    State state = {};
    {
        RWLock lock(m_ReadLock, m_WriteLock, RWLock::Type::Reader);
        auto it = m_States.find(pCommandList);
        if (it == m_States.end())
            return;
        state = it->second;
    }

    ID3D12GraphicsCommandList_DT& dt = D3D12DT.GetCmdList(pCommandList->GetType());

    if (state.m_DescriptorHeaps.empty() == false)
        dt.SetDescriptorHeaps(pCommandList, UINT(state.m_DescriptorHeaps.size()), state.m_DescriptorHeaps.data());
    if (state.m_RootSignature)
        dt.SetComputeRootSignature(pCommandList, state.m_RootSignature);
    if (state.m_PipelineState)
        dt.SetPipelineState(pCommandList, state.m_PipelineState);
    if (state.m_StateObject)
    {
        ID3D12GraphicsCommandList4* cmdList4 = nullptr;
        pCommandList->QueryInterface(IID_PPV_ARGS(&cmdList4));
        if (cmdList4)
        {
            dt.SetPipelineState1(cmdList4, state.m_StateObject);
            cmdList4->Release();
        }
    }

    const Binding* bindings = state.m_Bindings;
    for (uint32_t i = 0; i < c_RootParameterNumMax; ++i)
    {
        switch (state.m_Bindings[i].type)
        {
        case BindingType::DescriptorTable:
            dt.SetComputeRootDescriptorTable(pCommandList, i, bindings[i].DescriptorTable.BaseDescriptor);
            break;
        case BindingType::ConstantBufferView:
            dt.SetComputeRootConstantBufferView(pCommandList, i, bindings[i].ConstantBufferView.BufferLocation);
            break;
        case BindingType::ShaderResourceView:
            dt.SetComputeRootShaderResourceView(pCommandList, i, bindings[i].ShaderResourceView.BufferLocation);
            break;
        case BindingType::UnorderedAccessView:
            dt.SetComputeRootUnorderedAccessView(pCommandList, i, bindings[i].UnorderedAccessView.BufferLocation);
            break;
        case BindingType::Constant:
        {
            auto& rootConstantMap = state.m_RootConstantMaps[i];
            for (auto entry : rootConstantMap)
                dt.SetComputeRoot32BitConstants(pCommandList, i, 1, &entry.second, entry.first);
            break;
        }
        }
    }
}
