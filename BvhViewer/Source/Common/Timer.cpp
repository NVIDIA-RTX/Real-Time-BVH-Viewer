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

#include "Timer.h"
#include <chrono>

using namespace std::chrono;

double Timer::GetDeltaMs(int64_t begin, int64_t end)
{
    return double(end - begin) / (1000.0 * 1000.0); //ns to ms
}

double Timer::GetDeltaMs(int64_t timestamp)
{
    return GetDeltaMs(timestamp, GetTimestamp());
}

int64_t Timer::GetTimestamp()
{
    auto nowTp = steady_clock::now();
    return duration_cast<std::chrono::nanoseconds>(nowTp.time_since_epoch()).count();
}