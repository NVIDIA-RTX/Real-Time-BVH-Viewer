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

#ifndef HASH_MAP_H
#define HASH_MAP_H

#if !defined( HASH_MAP_MAX_PROBES )
    #define HASH_MAP_MAX_PROBES 64
#endif

#if defined( HLSL )
    #define HASH_MAP_ATOMIC_COMPARE_EXCHANGE( dest, compareValue, value, originalValue )\
        InterlockedCompareExchange( dest, compareValue, value, originalValue )
#else
    #define HASH_MAP_ATOMIC_COMPARE_EXCHANGE( dest, compareValue, value, originalValue )\
        originalValue = atomicCompSwap( dest, compareValue, value )
#endif

static const uint HASH_MAP_INVALID_SLOT = 0xFFFFFFFFu;

uint HashMapHash( uint2 key )
{
    uint hash = key.x ^ ( key.y * 0x9E3779B9u );
    hash ^= hash >> 16;
    hash *= 0x85EBCA6Bu;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35u;
    hash ^= hash >> 16;
    return hash;
}

uint HashMapFirstSlot( uint2 key, uint capacity )
{
    return uint( ( uint64_t( HashMapHash( key ) ) * uint64_t( capacity ) ) >> 32 );
}

uint HashMapNextSlot( uint slot, uint capacity )
{
    return ( slot + 1u == capacity ) ? 0u : slot + 1u;
}

#define HASH_MAP_IMPLEMENTATION( name, keyBuffer, valueBuffer, ValueType, mapCapacity )                     \
uint name##ClaimSlot( uint2 key )                                                                           \
{                                                                                                           \
    const uint2 storedKey = ~key;                                                                           \
    uint slot = HashMapFirstSlot( key, mapCapacity );                                                       \
    for( uint probe = 0u; probe < HASH_MAP_MAX_PROBES; ++probe )                                            \
    {                                                                                                       \
        uint originalLow;                                                                                   \
        HASH_MAP_ATOMIC_COMPARE_EXCHANGE( keyBuffer[ slot * 2u + 0u ], 0u, storedKey.x, originalLow );      \
        if( originalLow == 0u || originalLow == storedKey.x )                                               \
        {                                                                                                   \
            uint originalHigh;                                                                              \
            HASH_MAP_ATOMIC_COMPARE_EXCHANGE( keyBuffer[ slot * 2u + 1u ], 0u, storedKey.y, originalHigh ); \
            if( originalHigh == 0u || originalHigh == storedKey.y )                                         \
                return slot;                                                                                \
        }                                                                                                   \
        slot = HashMapNextSlot( slot, mapCapacity );                                                        \
    }                                                                                                       \
    return HASH_MAP_INVALID_SLOT;                                                                           \
}                                                                                                           \
                                                                                                            \
uint name##FindSlot( uint2 key )                                                                            \
{                                                                                                           \
    const uint2 storedKey = ~key;                                                                           \
    uint slot = HashMapFirstSlot( key, mapCapacity );                                                       \
    for( uint probe = 0u; probe < HASH_MAP_MAX_PROBES; ++probe )                                            \
    {                                                                                                       \
        const uint storedLow = keyBuffer[ slot * 2u + 0u ];                                                 \
        if( storedLow == 0u )                                                                               \
            return HASH_MAP_INVALID_SLOT;                                                                   \
        if( storedLow == storedKey.x && keyBuffer[ slot * 2u + 1u ] == storedKey.y )                        \
            return slot;                                                                                    \
        slot = HashMapNextSlot( slot, mapCapacity );                                                        \
    }                                                                                                       \
    return HASH_MAP_INVALID_SLOT;                                                                           \
}                                                                                                           \
                                                                                                            \
uint name##Insert( uint2 key, ValueType value )                                                             \
{                                                                                                           \
    const uint slot = name##ClaimSlot( key );                                                               \
    if( slot != HASH_MAP_INVALID_SLOT )                                                                     \
        valueBuffer[ slot ] = value;                                                                        \
    return slot;                                                                                            \
}                                                                                                           \
                                                                                                            \
bool name##Find( uint2 key, out ValueType value )                                                           \
{                                                                                                           \
    const uint slot = name##FindSlot( key );                                                                \
    if( slot == HASH_MAP_INVALID_SLOT )                                                                     \
        return false;                                                                                       \
    value = valueBuffer[ slot ];                                                                            \
    return true;                                                                                            \
}                                                                                                           \
                                                                                                            \
bool name##Contains( uint2 key )                                                                            \
{                                                                                                           \
    return name##FindSlot( key ) != HASH_MAP_INVALID_SLOT;                                                  \
}

#endif // HASH_MAP_H
