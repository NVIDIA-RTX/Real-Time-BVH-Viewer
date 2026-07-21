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

/*
==================================================================================
RNG/MATH etc

Source: https://github.com/NVIDIAGameWorks/MathLib
==================================================================================
*/

//=======================================================================================================================
// SEQUENCE
//=======================================================================================================================
// https://en.wikipedia.org/wiki/Ordered_dithering

uint Sequence_IntegerExplode( uint x ) {
	x = ( x | ( x << 8 ) ) & 0x00FF00FF;
	x = ( x | ( x << 4 ) ) & 0x0F0F0F0F;
	x = ( x | ( x << 2 ) ) & 0x33333333;
	x = ( x | ( x << 1 ) ) & 0x55555555;
	return x;
}

uint Sequence_Zorder( uint2 xy ) {
	return Sequence_IntegerExplode( xy.x ) | ( Sequence_IntegerExplode( xy.y ) << 1 );
}

uint Sequence_Lcg( uint x ) {
	// http://en.wikipedia.org/wiki/Linear_congruential_generator
	return 1664525u * x + 1013904223u;
}

uint Sequence_XorShift( uint x ) {
	// Xorshift algorithm from George Marsaglia's paper
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

// NOTE: Hash( 0 ) = 0! If that's a problem do "Hash32( x + constant )". HashCombine does something similar already
uint Sequence_Hash( uint x ) {
	// This little gem is from https://nullprogram.com/blog/2018/07/31/, "Prospecting for Hash Functions" by Chris Wellons
	#if 1 // faster, higher bias
		x ^= x >> 16;
		x *= 0x7FEB352D;
		x ^= x >> 15;
		x *= 0x846CA68B;
		x ^= x >> 16;

		return x;
	#else // slower, lower bias
		x ^= x >> 17;
		x *= 0xED5AD4BB;
		x ^= x >> 11;
		x *= 0xAC4C1B51;
		x ^= x >> 15;
		x *= 0x31848BAB;
		x ^= x >> 14;

		return x;
	#endif
}

uint Sequence_HashCombine( uint seed, uint value ) {
	return seed ^ ( Sequence_Hash( value ) + 0x9E3779B9 + ( seed << 6 ) + ( seed >> 2 ) );
}

//=======================================================================================================================
// RNG
//=======================================================================================================================

// Procedural white noise generation
#define STL_RNG_LCG 0
#define STL_RNG_XORSHIFT 1
#define STL_RNG_HASH 2

#define STL_RNG_UTOF 0
#define STL_RNG_MANTISSA_BITS 1
#define STL_RNG_NEXT_MODE				STL_RNG_HASH
#define STL_RNG_FLOAT01_MODE			STL_RNG_MANTISSA_BITS

uint _RNG_Next( uint seed ) {
	#if( STL_RNG_NEXT_MODE == STL_RNG_LCG )
		seed = Sequence_Lcg( seed );
	#elif( STL_RNG_NEXT_MODE == STL_RNG_XORSHIFT )
		seed = Sequence_XorShift( seed );
	#else
		seed = Sequence_Hash( seed );
	#endif

	return seed;
}

#if( STL_RNG_FLOAT01_MODE == STL_RNG_UTOF )
	#define _UintToFloat01( x ) ( ( x >> 8 ) * ( 1.0 / float( 1 << 24 ) ) )
#else
	#define _UintToFloat01( x ) ( 2.0 - asfloat( ( x >> 9 ) | 0x3F800000 ) )
#endif

static uint g_HashSeed;

void RNG_Initialize( uint linearIndex, uint frameIndex ) {
	g_HashSeed = Sequence_HashCombine( Sequence_Hash( frameIndex + 0x035F9F29 ), linearIndex );
}

void RNG_Initialize( uint2 samplePos, uint frameIndex ) {
	RNG_Initialize( Sequence_Zorder( samplePos ), frameIndex );
}

uint RNG_GetUint( ) {
	g_HashSeed = _RNG_Next( g_HashSeed );
	return g_HashSeed;
}

// RESULT: u32 range

uint2 RNG_GetUint2( ) {
	return uint2( RNG_GetUint( ), RNG_GetUint( ) );
}

uint3 RNG_GetUint3( ) {
	return uint3( RNG_GetUint(), RNG_GetUint(), RNG_GetUint() );
}

uint4 RNG_GetUint4( ) {
	return uint4( RNG_GetUint2( ), RNG_GetUint2( ) );
}

// RESULT: [0; 1)

float RNG_GetFloat( ) {
	uint x = RNG_GetUint( );
	return _UintToFloat01( x );
}

float2 RNG_GetFloat2( ) {
	uint2 x = RNG_GetUint2( );
	return _UintToFloat01( x );
}

float3 RNG_GetFloat3() {
	uint3 x = RNG_GetUint3();
	return _UintToFloat01( x );
}

float4 RNG_GetFloat4( ) {
	uint4 x = RNG_GetUint4( );
	return _UintToFloat01( x );
}

//=======================================================================================================================
// MATH
//=======================================================================================================================
float3 ReconstructViewPosition( float2 uv, float4 cameraFrustum, float viewZ, float orthoMode )
{
	float3 p;
	p.xy = uv * cameraFrustum.zw + cameraFrustum.xy;
	p.xy *= viewZ * ( 1.0 - abs( orthoMode ) ) + orthoMode;
	p.z = viewZ;

	return p;
}

float3 ReconstructViewPosition( float2 uv, float4 cameraFrustum )
{ return ReconstructViewPosition( uv, cameraFrustum, 1.0, 0.0 ); }

float3 AffineTransform( float4x4 m, float3 p )
{ return mul( m, float4( p, 1.0 ) ).xyz; }

float3 AffineTransform( float3x4 m, float3 p )
{ return mul( m, float4( p, 1.0 ) ); }

float3 AffineTransform( float4x4 m, float4 p )
{ return mul( m, p ).xyz; }

float2 RotateVector( float4 rotator, float2 v )
{ return v.x * rotator.xz + v.y * rotator.yw; }

float3 RotateVector( float4x4 m, float3 v )
{ return mul( float3x3( m[ 0 ].xyz, m[ 1 ].xyz, m[ 2 ].xyz ) , v ); }

float3 RotateVector( float3x3 m, float3 v )
{ return mul( m, v ); }