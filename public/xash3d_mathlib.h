/*
xash3d_mathlib.h - base math functions
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef XASH3D_MATHLIB_H
#define XASH3D_MATHLIB_H

#include <stdlib.h>
#include <math.h>
#if HAVE_TGMATH_H
#include <tgmath.h>
#endif

#include "port.h"
#include "com_model.h"

#ifdef XASH_MSVC
#pragma warning(disable : 4201)	// nonstandard extension used
#endif

// euler angle order
#define PITCH		0
#define YAW		1
#define ROLL		2

#ifndef M_PI
#define M_PI		(double)3.14159265358979323846
#endif

#ifndef M_PI2
#define M_PI2		((double)(M_PI * 2))
#endif

#define M_PI_F		((float)(M_PI))
#define M_PI2_F		((float)(M_PI2))

#define RAD2DEG( x )	((double)(x) * (double)(180.0 / M_PI))
#define DEG2RAD( x )	((double)(x) * (double)(M_PI / 180.0))

#define NUMVERTEXNORMALS	162

#define BOGUS_RANGE		((vec_t)114032.64)	// world.size * 1.74

#define SIDE_FRONT		0
#define SIDE_BACK		1
#define SIDE_ON		2
#define SIDE_CROSS		-2

#define PLANE_X		0	// 0 - 2 are axial planes
#define PLANE_Y		1	// 3 needs alternate calc
#define PLANE_Z		2
#define PLANE_NONAXIAL	3

#define EQUAL_EPSILON	0.001f
#define STOP_EPSILON	0.1f
#define ON_EPSILON		0.1f

#define RAD_TO_STUDIO	(32768.0 / M_PI)
#define STUDIO_TO_RAD	(M_PI / 32768.0)

#define INV127F		( 1.0f / 127.0f )
#define INV255F		( 1.0f / 255.0f )
#define MAKE_SIGNED( x )	((( x ) * INV127F ) - 1.0f )

#define Q_min( a, b )	(((a) < (b)) ? (a) : (b))
#define Q_max( a, b )	(((a) > (b)) ? (a) : (b))
#define Q_equal( a, b ) (((a) > ((b) - EQUAL_EPSILON)) && ((a) < ((b) + EQUAL_EPSILON)))
#define Q_recip( a )	((float)(1.0f / (float)(a)))
#define Q_floor( a )	((float)(int)(a))
#define Q_ceil( a )		((float)(int)((a) + 1))
#define Q_round( x, y )	(floor( x / y + 0.5f ) * y )
#define Q_rint(x)		((x) < 0.0f ? ((int)((x)-0.5f)) : ((int)((x)+0.5f)))

#define ALIGN( x, a )	((( x ) + (( size_t )( a ) - 1 )) & ~(( size_t )( a ) - 1 ))

#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define DotProductAbs(x,y) (abs((x)[0]*(y)[0])+abs((x)[1]*(y)[1])+abs((x)[2]*(y)[2]))
#define DotProductFabs(x,y) (fabs((x)[0]*(y)[0])+fabs((x)[1]*(y)[1])+fabs((x)[2]*(y)[2]))
#define DotProductPrecise(x,y) ((double)(x)[0]*(double)(y)[0]+(double)(x)[1]*(double)(y)[1]+(double)(x)[2]*(double)(y)[2])
#define CrossProduct_(a,b,c) ((c)[0]=(a)[1]*(b)[2]-(a)[2]*(b)[1],(c)[1]=(a)[2]*(b)[0]-(a)[0]*(b)[2],(c)[2]=(a)[0]*(b)[1]-(a)[1]*(b)[0])
#define Vector2Subtract_(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1])
#define VectorSubtract_(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define Vector2Add_(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1])
#define VectorAdd_(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorAddScalar_(a,b,c) ((c)[0]=(a)[0]+(b),(c)[1]=(a)[1]+(b),(c)[2]=(a)[2]+(b))
#define Vector2Copy_(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1])
#define VectorCopy_(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define Vector4Copy_(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])
#define VectorScale_(in, scale, out) ((out)[0] = (in)[0] * (scale),(out)[1] = (in)[1] * (scale),(out)[2] = (in)[2] * (scale))
#define VectorCompare(v1,v2)	((v1)[0]==(v2)[0] && (v1)[1]==(v2)[1] && (v1)[2]==(v2)[2])
#define VectorDivide_( in, d, out ) VectorScale_( in, (1.0f / (d)), out )
#define VectorMax(a) ( Q_max((a)[0], Q_max((a)[1], (a)[2])) )
#define VectorAvg(a) ( ((a)[0] + (a)[1] + (a)[2]) / 3 )
#define VectorLength(a) ( sqrt( DotProduct( a, a )))
#define VectorLength2(a) (DotProduct( a, a ))
#define VectorDistance(a, b) (sqrt( VectorDistance2( a, b )))
#define VectorDistance2(a, b) (((a)[0] - (b)[0]) * ((a)[0] - (b)[0]) + ((a)[1] - (b)[1]) * ((a)[1] - (b)[1]) + ((a)[2] - (b)[2]) * ((a)[2] - (b)[2]))
#define Vector2Average_(a,b,o)	((o)[0]=((a)[0]+(b)[0])*0.5f,(o)[1]=((a)[1]+(b)[1])*0.5f)
#define VectorAverage_(a,b,o)	((o)[0]=((a)[0]+(b)[0])*0.5f,(o)[1]=((a)[1]+(b)[1])*0.5f,(o)[2]=((a)[2]+(b)[2])*0.5f)
#define Vector2Set_(v, x, y) ((v)[0]=(x),(v)[1]=(y))
#define VectorSet_(v, x, y, z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define Vector4Set_(v, a, b, c, d) ((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3] = (d))
#define VectorClear(x) ((x)[0]=(x)[1]=(x)[2]=0)
#define Vector2Lerp_( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]))
#define VectorLerp_( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]), (c)[2] = (v1)[2] + (lerp) * ((v2)[2] - (v1)[2]))
#define VectorNormalize_( v ) { float ilength = (float)sqrt(DotProduct(v, v));if (ilength) ilength = 1.0f / ilength;v[0] *= ilength;v[1] *= ilength;v[2] *= ilength; }
#define VectorNormalize2_( v, dest ) {float ilength = (float)sqrt(DotProduct(v,v));if (ilength) ilength = 1.0f / ilength;dest[0] = v[0] * ilength;dest[1] = v[1] * ilength;dest[2] = v[2] * ilength; }
#define VectorNormalizeFast_( v ) {float	ilength = (float)rsqrt(DotProduct(v,v)); v[0] *= ilength; v[1] *= ilength; v[2] *= ilength; }
#define VectorNormalizeLength( v ) VectorNormalizeLength2((v), (v))
#define VectorNegate_(x, y) ((y)[0] = -(x)[0], (y)[1] = -(x)[1], (y)[2] = -(x)[2])
#define VectorM_(scale1, b1, c) ((c)[0] = (scale1) * (b1)[0],(c)[1] = (scale1) * (b1)[1],(c)[2] = (scale1) * (b1)[2])
#define VectorMA_(a, scale, b, c) ((c)[0] = (a)[0] + (scale) * (b)[0],(c)[1] = (a)[1] + (scale) * (b)[1],(c)[2] = (a)[2] + (scale) * (b)[2])
#define VectorMAM_(scale1, b1, scale2, b2, c) ((c)[0] = (scale1) * (b1)[0] + (scale2) * (b2)[0],(c)[1] = (scale1) * (b1)[1] + (scale2) * (b2)[1],(c)[2] = (scale1) * (b1)[2] + (scale2) * (b2)[2])
#define VectorMAMAM_(scale1, b1, scale2, b2, scale3, b3, c) ((c)[0] = (scale1) * (b1)[0] + (scale2) * (b2)[0] + (scale3) * (b3)[0],(c)[1] = (scale1) * (b1)[1] + (scale2) * (b2)[1] + (scale3) * (b3)[1],(c)[2] = (scale1) * (b1)[2] + (scale2) * (b2)[2] + (scale3) * (b3)[2])
#define VectorIsNull( v ) ((v)[0] == 0.0f && (v)[1] == 0.0f && (v)[2] == 0.0f)
#define MakeRGBA( out, x, y, z, w ) Vector4Set_( out, x, y, z, w )
#define PlaneDist(point,plane) ((plane)->type < 3 ? (point)[(plane)->type] : DotProduct((point), (plane)->normal))
#define PlaneDiff(point,plane) (((plane)->type < 3 ? (point)[(plane)->type] : DotProduct((point), (plane)->normal)) - (plane)->dist)
#define bound( min, num, max ) ((num) >= (min) ? ((num) < (max) ? (num) : (max)) : (min))

#define PARANOID_MATH 0 // kills performance, only for heavy debug, requires C11

#if PARANOID_MATH

FORCEINLINE int IS_NAN_STUB( int x ) { return 0; } // stub function for _Generic
#define IS_NAN(x) _Generic( x, \
	float: isnan, \
	double: isnan, \
	long double: isnan, \
	default: IS_NAN_STUB)(x)

// separated these functions to see the difference between scalar and vector checks in debugger
// but may get optimized out
FORCEINLINE int TNAN_S_( float v )        { abort(); return 1; }
FORCEINLINE int TNAN_V_( const float *v ) { abort(); return 1; }

#define TNAN_S( s )    do { if( IS_NAN( s ) ) TNAN_S_( s ); } while(0);
#define TNAN_V( v, f ) do { if( f( v ) ) TNAN_V_(( const float * )( v )); } while(0);
#define TNAN_V2( v ) TNAN_V( v, Vector2IsNAN )
#define TNAN_V3( v ) TNAN_V( v, Vector3IsNAN )
#define TNAN_V4( v ) TNAN_V( v, Vector4IsNAN )

#define TNAN_BEGIN do {
#define TNAN_END   } while( 0 )

#define TNAN_T( T1, v1 ) ;TNAN_##T1( v1 ); // never add unneeded semicolons to not break existing code
#define TNAN_TT( T1, T2, v1, v2 ) TNAN_T( T1, v1 ) TNAN_T( T2, v2 )
#define TNAN_TTT( T1, T2, T3, v1, v2, v3 ) TNAN_TT( T1, T2, v1, v2 ) TNAN_T( T3, v3 );
#define TNAN_TTTT( T1, T2, T3, T4, v1, v2, v3, v4 ) \
	TNAN_TTT( T1, T2, T3, v1, v2, v3 ) \
	TNAN_T( T4, v4 )
#define TNAN_TTTTT( T1, T2, T3, T4, T5, v1, v2, v3, v4, v5 ) \
	TNAN_TTTT( T1, T2, T3, T4, v1, v2, v3, v4 ) \
	TNAN_T( T5, v5 )
#define TNAN_TTTTTT( T1, T2, T3, T4, T5, T6, v1, v2, v3, v4, v5, v6 ) \
	TNAN_TTTTT( T1, T2, T3, T4, T5, v1, v2, v3, v4, v5 ) \
	TNAN_T( T6, v6 )
#else

#ifdef isnan // check for C99 isnan
#define IS_NAN isnan
#else
#define IS_NAN( x ) ((( *( int * )&( x )) & ( 255 << 23 )) == ( 255<<23 ))
#endif

#define TNAN_S( s )
#define TNAN_V( v, f )

#define TNAN_BEGIN
#define TNAN_END
#define TNAN_T( T1, v1 )
#define TNAN_TT( T1, T2, v1, v2 )
#define TNAN_TTT( T1, T2, T3, v1, v2, v3 )
#define TNAN_TTTT( T1, T2, T3, T4, v1, v2, v3, v4 )
#define TNAN_TTTTT( T1, T2, T3, T4, T5, v1, v2, v3, v4, v5 )
#define TNAN_TTTTTT( T1, T2, T3, T4, T5, T6, v1, v2, v3, v4, v5, v6 )
#endif

#define Vector2IsNAN( v ) ( IS_NAN(( v )[0] ) || IS_NAN(( v )[1]))
#define Vector3IsNAN( v ) ( Vector2IsNAN( v ) || IS_NAN(( v )[2]))
#define Vector4IsNAN( v ) ( Vector3IsNAN( v ) || IS_NAN(( v )[3]))

#define TNAN_INPLACE( F, T1, v1 ) \
	TNAN_BEGIN \
	TNAN_T( T1, v1 ) \
	F ## _( v1 ) \
	TNAN_T( T1, v1 ) \
	TNAN_END

#define TNAN_I_O( F, T1, TOUT, v1, vOut ) \
	TNAN_BEGIN \
	TNAN_T( T1, v1 ) \
	F ## _( v1, vOut ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_II_O( F, T1, T2, TOUT, v1, v2, vOut ) \
	TNAN_BEGIN \
	TNAN_TT( T1, T2, v1, v2 ) \
	F ## _( v1, v2, vOut ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_III_O( F, T1, T2, T3, TOUT, v1, v2, v3, vOut ) \
	TNAN_BEGIN \
	TNAN_TTT( T1, T2, T3, v1, v2, v3 ) \
	F ## _( v1, v2, v3, vOut ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_IIII_O( F, T1, T2, T3, T4, TOUT, v1, v2, v3, v4, vOut ) \
	TNAN_BEGIN \
	TNAN_TTTT( T1, T2, T3, T4, v1, v2, v3, v4 ) \
	F ## _( v1, v2, v3, v4, vOut ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_IIIIII_O( F, T1, T2, T3, T4, T5, T6, TOUT, v1, v2, v3, v4, v5, v6, vOut ) \
	TNAN_BEGIN \
	TNAN_TTTTTT( T1, T2, T3, T4, T5, T6, v1, v2, v3, v4, v5, v6 ) \
	F ## _( v1, v2, v3, v4, v5, v6, vOut ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_O_II( F, TOUT, T1, T2, vOut, v1, v2 ) \
	TNAN_BEGIN \
	TNAN_TT( T1, T2, v1, v2 ) \
	F ## _( vOut, v1, v2 ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_O_III( F, TOUT, T1, T2, T3, vOut, v1, v2, v3 ) \
	TNAN_BEGIN \
	TNAN_TTT( T1, T2, T3, v1, v2, v3 ) \
	F ## _( vOut, v1, v2, v3 ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_O_IIII( F, TOUT, T1, T2, T3, T4, vOut, v1, v2, v3, v4 ) \
	TNAN_BEGIN \
	TNAN_TTTT( T1, T2, T3, T4, v1, v2, v3, v4 ) \
	F ## _( vOut, v1, v2, v3, v4 ) \
	TNAN_T( TOUT, vOut ) \
	TNAN_END

#define TNAN_V_V( F, s, v1, vOut ) TNAN_I_O( F, V##s, V##s, v1, vOut )

#define TNAN_VV_V( F, s, v1, v2, vOut ) TNAN_II_O( F, V##s, V##s, V##s, v1, v2, vOut )
#define TNAN_SV_V( F, s, v1, v2, vOut ) TNAN_II_O( F, S,    V##s, V##s, v1, v2, vOut )
#define TNAN_VS_V( F, s, v1, v2, vOut ) TNAN_II_O( F, V##s, S,    V##s, v1, v2, vOut )
#define TNAN_V_SS( F, s, vOut, v1, v2 ) TNAN_O_II( F, V##s, S,    S,    vOut, v1, v2 )

#define TNAN_VSV_V( F, s, v1, v2, v3, vOut ) TNAN_III_O( F, V##s, S, V##s, V##s, v1, v2, v3, vOut )
#define TNAN_V_SSS( F, s, vOut, v1, v2, v3 ) TNAN_O_III( F, V##s, S, S,    S,    vOut, v1, v2, v3 )

#define TNAN_SVSV_V( F, s, v1, v2, v3, v4, vOut ) TNAN_IIII_O( F, S,    V##s, S, V##s, V##s, v1, v2, v3, v4, vOut )
#define TNAN_V_SSSS(F, s, vOut, v1, v2, v3, v4)   TNAN_O_IIII( F, V##s, S,    S, S,    S,    vOut, v1, v2, v3, v4 )

#define TNAN_SVSVSV_V( F, s, v1, v2, v3, v4, v5, v6, vOut ) \
	TNAN_IIIIII_O( F, S, V##s, S, V##s, S, V##s, V##s, v1, v2, v3, v4, v5, v6, vOut )

#define Vector2Set( v, x, y )       TNAN_V_SS( Vector2Set,   2, v, x, y )
#define VectorSet( v, x, y, z )     TNAN_V_SSS( VectorSet,   3, v, x, y, z )
#define Vector4Set( v, x, y, z, w ) TNAN_V_SSSS( Vector4Set, 4, v, x, y, z, w )

#define Vector2Lerp( a, s, b, c ) TNAN_VSV_V( Vector2Lerp, 2, a, s, b, c )
#define VectorLerp( a, s, b, c )  TNAN_VSV_V( VectorLerp,  3, a, s, b, c )
#define VectorMA( a, s, b, c )    TNAN_VSV_V( VectorMA,    3, a, s, b, c )

#define Vector2Subtract( a, b, c)  TNAN_VV_V( Vector2Subtract, 2, a, b, c )
#define Vector2Add( a, b, c )      TNAN_VV_V( Vector2Add,      2, a, b, c )
#define Vector2Average( a, b, o )  TNAN_VV_V( Vector2Average,  2, a, b, o )
#define CrossProduct( a, b, c )    TNAN_VV_V( CrossProduct,    3, a, b, c )
#define VectorSubtract( a, b, c )  TNAN_VV_V( VectorSubtract,  3, a, b, c )
#define VectorAdd( a, b, c )       TNAN_VV_V( VectorAdd,       3, a, b, c )
#define VectorAverage( a, b, o )	  TNAN_VV_V( VectorAverage,   3, a, b, o )
#define VectorAddScalar( a, b, c ) TNAN_VS_V( VectorAddScalar, 3, a, b, c )
#define VectorScale( i, s, o )     TNAN_VS_V( VectorScale,     3, i, s, o )
#define VectorDivide( i, d, o )    TNAN_VS_V( VectorDivide,    3, i, d, o )
#define VectorM( s, a, b)          TNAN_SV_V( VectorM,         3, s, a, b )

#define Vector2Copy( a, b )      TNAN_V_V( Vector2Copy,        2, a, b )
#define VectorCopy( a, b )       TNAN_V_V( VectorCopy,         3, a, b )
#define VectorNegate( x, y )     TNAN_V_V( VectorNegate,       3, x, y )
#define VectorNormalize2( v, d ) TNAN_V_V( VectorNormalize2,   3, v, d )
#define Vector4Copy( a, b )      TNAN_V_V( Vector4Copy,        4, a, b )

#define VectorNormalize( v )     TNAN_INPLACE( VectorNormalize,     V3, v )
#define VectorNormalizeFast( v ) TNAN_INPLACE( VectorNormalizeFast, V3, v )

#define VectorMAM( s1, b1, s2, b2, c ) \
	TNAN_SVSV_V( VectorMAM, 3, s1, b1, s2, b2, c )
#define VectorMAMAM( s1, b1, s2, b2, s3, b3, c ) \
	TNAN_SVSVSV_V( VectorMAMAM, 3, s1, b1, s2, b2, s3, b3, c )

float rsqrt( float number );
float anglemod( float a );
word FloatToHalf( float v );
float HalfToFloat( word h );
float SimpleSpline( float value );
void RoundUpHullSize( vec3_t size );
int SignbitsForPlane( const vec3_t normal );
int PlaneTypeForNormal( const vec3_t normal );
int NearestPOW( int value, qboolean roundDown );
void SinCos( float radians, float *sine, float *cosine );
float VectorNormalizeLength2( const vec3_t v, vec3_t out );
qboolean VectorCompareEpsilon( const vec3_t vec1, const vec3_t vec2, vec_t epsilon );
void VectorVectors( const vec3_t forward, vec3_t right, vec3_t up );
void VectorAngles( const float *forward, float *angles );
void AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up );
void VectorsAngles( const vec3_t forward, const vec3_t right, const vec3_t up, vec3_t angles );
qboolean PlanesGetIntersectionPoint( const struct mplane_s *plane1, const struct mplane_s *plane2, const struct mplane_s *plane3, vec3_t out );
void PlaneIntersect( const struct mplane_s *plane, const vec3_t p0, const vec3_t p1, vec3_t out );

void ClearBounds( vec3_t mins, vec3_t maxs );
void AddPointToBounds( const vec3_t v, vec3_t mins, vec3_t maxs );
qboolean BoundsIntersect( const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2 );
qboolean BoundsAndSphereIntersect( const vec3_t mins, const vec3_t maxs, const vec3_t origin, float radius );
qboolean SphereIntersect( const vec3_t vSphereCenter, float fSphereRadiusSquared, const vec3_t vLinePt, const vec3_t vLineDir );
float RadiusFromBounds( const vec3_t mins, const vec3_t maxs );
void ExpandBounds( vec3_t mins, vec3_t maxs, float offset );

void AngleQuaternion( const vec3_t angles, vec4_t q, qboolean studio );
void QuaternionAngle( const vec4_t q, vec3_t angles );
void QuaternionSlerp( const vec4_t p, const vec4_t q, float t, vec4_t qt );
float RemapVal( float val, float A, float B, float C, float D );
float ApproachVal( float target, float value, float speed );

//
// matrixlib.c
//
#define Matrix3x4_LoadIdentity( mat )		Matrix3x4_Copy( mat, matrix3x4_identity )
#define Matrix3x4_Copy( out, in )		memcpy( out, in, sizeof( matrix3x4 ))

void Matrix3x4_VectorTransform( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorITransform( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorRotate( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorIRotate( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_ConcatTransforms( matrix3x4 out, const matrix3x4 in1, const matrix3x4 in2 );
void Matrix3x4_FromOriginQuat( matrix3x4 out, const vec4_t quaternion, const vec3_t origin );
void Matrix3x4_CreateFromEntity( matrix3x4 out, const vec3_t angles, const vec3_t origin, float scale );
void Matrix3x4_TransformPositivePlane( const matrix3x4 in, const vec3_t normal, float d, vec3_t out, float *dist );
void Matrix3x4_TransformAABB( const matrix3x4 world, const vec3_t mins, const vec3_t maxs, vec3_t absmin, vec3_t absmax );
void Matrix3x4_SetOrigin( matrix3x4 out, float x, float y, float z );
void Matrix3x4_Invert_Simple( matrix3x4 out, const matrix3x4 in1 );
void Matrix3x4_OriginFromMatrix( const matrix3x4 in, float *out );
void Matrix3x4_AnglesFromMatrix( const matrix3x4 in, vec3_t out );
void Matrix3x4_Transpose( matrix3x4 out, const matrix3x4 in1 );

#define Matrix4x4_LoadIdentity( mat )	Matrix4x4_Copy( mat, matrix4x4_identity )
#define Matrix4x4_Copy( out, in )	memcpy( out, in, sizeof( matrix4x4 ))

void Matrix4x4_VectorTransform( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorITransform( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorRotate( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorIRotate( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_ConcatTransforms( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_FromOriginQuat( matrix4x4 out, const vec4_t quaternion, const vec3_t origin );
void Matrix4x4_CreateFromEntity( matrix4x4 out, const vec3_t angles, const vec3_t origin, float scale );
void Matrix4x4_TransformPositivePlane( const matrix4x4 in, const vec3_t normal, float d, vec3_t out, float *dist );
void Matrix4x4_TransformStandardPlane( const matrix4x4 in, const vec3_t normal, float d, vec3_t out, float *dist );
void Matrix4x4_ConvertToEntity( const matrix4x4 in, vec3_t angles, vec3_t origin );
void Matrix4x4_SetOrigin( matrix4x4 out, float x, float y, float z );
void Matrix4x4_Invert_Simple( matrix4x4 out, const matrix4x4 in1 );
void Matrix4x4_OriginFromMatrix( const matrix4x4 in, float *out );
void Matrix4x4_Transpose( matrix4x4 out, const matrix4x4 in1 );
qboolean Matrix4x4_Invert_Full( matrix4x4 out, const matrix4x4 in1 );

float V_CalcFov( float *fov_x, float width, float height );
void V_AdjustFov( float *fov_x, float *fov_y, float width, float height, qboolean lock_x );

int BoxOnPlaneSide( const vec3_t emins, const vec3_t emaxs, const mplane_t *p );
#define BOX_ON_PLANE_SIDE( emins, emaxs, p )			\
	((( p )->type < 3 ) ?				\
	(						\
		((p)->dist <= (emins)[(p)->type]) ?		\
			1				\
		:					\
		(					\
			((p)->dist >= (emaxs)[(p)->type]) ?	\
				2			\
			:				\
				3			\
		)					\
	)						\
	:						\
		BoxOnPlaneSide(( emins ), ( emaxs ), ( p )))

extern vec3_t		vec3_origin;
extern int		boxpnt[6][4];
extern const matrix3x4	matrix3x4_identity;
extern const matrix4x4	matrix4x4_identity;
extern const float		m_bytenormals[NUMVERTEXNORMALS][3];

#endif // XASH3D_MATHLIB_H

