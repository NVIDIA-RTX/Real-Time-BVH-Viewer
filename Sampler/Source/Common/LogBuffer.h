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
#include<memory>
#include<atomic>
#include<cstdint>
#include<cstdio>
#include<cstring>
#include<iostream>
#include<Lock.h>

constexpr uint32_t c_LogLockWaitMs = 10;
constexpr uint32_t c_LogLockStaleMs = 1000;

struct LogBuffer
{
    void InitHost();
    void Post(const char* str);
    void PrintPosted();

private:
    static const size_t c_Length = 4096;
    char m_Data[c_Length];
    uint32_t pointer;
    std::atomic<uint64_t> m_Lock;
};

inline void LogBuffer::InitHost()
{
    memset(m_Data, 0, sizeof(m_Data));
    pointer = 0;
}

inline void LogBuffer::Post(const char* str)
{
    CrossProcessLock lock(m_Lock, c_LogLockWaitMs, c_LogLockStaleMs);
    if(lock.IsAcquired() == false)
        return;

    const size_t length = strlen(str) + 1;
    if(length > c_Length)
    {
        const char* header = "[LOGOVERFLOW]";
        const size_t headerLength = strlen(header);
        const char* adjustedStart = str + (length - c_Length) + headerLength;
        const size_t adjustedLength = strlen(adjustedStart) + 1;
        memcpy(m_Data, header, headerLength);
        memcpy(m_Data + headerLength, adjustedStart, adjustedLength);
        m_Data[c_Length - 1] = '\0';
        pointer = 0;
        return;
    }

    size_t offset = pointer;
    if(c_Length - offset < length)
        offset = 0;

    memcpy(m_Data + offset, str, length);
    pointer = uint32_t(offset + length);
}

inline void LogBuffer::PrintPosted()
{
    char messages[c_Length];
    {
        CrossProcessLock lock(m_Lock, c_LogLockWaitMs, c_LogLockStaleMs);
        if(lock.IsAcquired() == false)
            return;

        if(m_Data[0] == '\0')
            return;

        memcpy(messages, m_Data, c_Length);
        InitHost();
    }
    messages[c_Length - 1] = '\0';

    size_t read = 0;
    while(read < c_Length)
    {
        const char* message = messages + read;
        const size_t l = strlen(message);
        if(l == 0)
            break;

        printf("%s\n", message);
        read += l + 1;
    }
}