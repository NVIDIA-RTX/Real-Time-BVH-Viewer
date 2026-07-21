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
#include <chrono>
#include <cstdint>
#include <thread>

struct ScopedLock
{
    ScopedLock(std::atomic_flag& lock) :
        m_Lock(lock)
    {
        while (m_Lock.test_and_set(std::memory_order_acquire))
        {
            while (m_Lock.test(std::memory_order_relaxed));
        }
    };

    ~ScopedLock()
    {
        m_Lock.clear(std::memory_order_release);
    };
    std::atomic_flag& m_Lock;
};

struct RWLock
{
    enum class Type { Reader, Writer };
    RWLock(std::atomic_uint& readLock, std::atomic_flag& writeLock, Type type) :
        m_ReadLock(readLock),
        m_WriteLock(writeLock),
        m_Type(type)
    {
        while (m_WriteLock.test_and_set(std::memory_order_acquire))
            while (m_WriteLock.test(std::memory_order_relaxed));

        switch(m_Type)
        {
        case Type::Reader:
        {
            m_ReadLock.fetch_add(1, std::memory_order_relaxed);
            m_WriteLock.clear(std::memory_order_release);
            break;
        }
        case Type::Writer:
        {
            while (m_ReadLock.load(std::memory_order_acquire) != 0);
            break;
        }
        }
    };

    ~RWLock()
    {
        switch (m_Type)
        {
        case Type::Reader:
        {
            m_ReadLock.fetch_sub(1, std::memory_order_release);
            break;
        }
        case Type::Writer:
        {
            m_WriteLock.clear(std::memory_order_release);
            break;
        }
        }
    };
    Type m_Type;
    std::atomic_uint& m_ReadLock;
    std::atomic_flag& m_WriteLock;
};

struct CrossProcessLock
{
    CrossProcessLock(std::atomic<uint64_t>& lock, uint32_t waitMs, uint32_t staleMs) 
        : m_Lock(lock)
    {
        const uint64_t start = Milliseconds();
        do
        {
            const uint64_t now = Milliseconds();
            const uint64_t owner = m_Lock.load();
            const bool isStale = owner != 0 && now >= owner + staleMs;

            const uint64_t token = now != 0 ? now : 1; // 0 is reserved for the unlocked state
            uint64_t expected = isStale ? owner : 0;

            if(m_Lock.compare_exchange_strong(expected, token))
            {
                m_Token = token;
                return;
            }

            std::this_thread::yield();
        } while(Milliseconds() - start < waitMs);
    };

    ~CrossProcessLock()
    {
        uint64_t expected = m_Token;
        if(expected != 0)
            m_Lock.compare_exchange_strong(expected, 0);
    };

    bool IsAcquired() const
    { return m_Token != 0; }

private:
    static uint64_t Milliseconds()
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch();
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(tick).count());
    }

    std::atomic<uint64_t>& m_Lock;
    uint64_t m_Token = 0;
};