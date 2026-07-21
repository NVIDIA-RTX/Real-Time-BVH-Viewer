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
#include <atomic>

struct BvhViewerScopedLock
{
    void Clear()
    {
        m_Lock.clear(std::memory_order_release);
    }

    void Set()
    {
        while (m_Lock.test_and_set(std::memory_order_acquire))
        {
            while (m_Lock.test(std::memory_order_relaxed));
        }
    }

    BvhViewerScopedLock(std::atomic_flag& lock) : m_Lock(lock)
    {
        Set();
    };

    ~BvhViewerScopedLock()
    {
        if(m_Lock.test(std::memory_order_relaxed))
            Clear();
    };

    std::atomic_flag& m_Lock;
};