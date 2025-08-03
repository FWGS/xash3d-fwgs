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

#include <math.h>
#if HAVE_TGMATH_H
#include <tgmath.h>
#endif
#include <string.h>

#include "build.h"
#include "xash3d_types.h"

/*
===========================

CONSTANTS AND HELPER MACROS

===========================
*/

// euler angle order
#define PITCH 0
#define YAW   1
#define ROLL  2

#ifndef M_PI
#define M_PI    (double)3.14159265358979323846
#endif

#define M_PI2   ((double)(M_PI * 2))
#define M_PI_F  ((float)(M_PI))
#define M_PI2_F ((float)(M_PI2))

#define RAD2DEG( x ) ((double)(x) * (double)(180.0 / M_PI))
#define DEG2RAD( x ) ((double)(x) * (double)(M_PI / 180.0))

#define NUMVERTEXNORMALS 162

#define BOGUS_RANGE ((vec_t)114032.64)	// world.size * 1.74

#define SIDE_FRONT 0
#define SIDE_BACK  1
#define SIDE_ON    2
#define SIDE_CROSS -2

#define PLANE_X        0 // 0 - 2 are axial planes
#define PLANE_Y        1 // 3 needs alternate calc
#define PLANE_Z        2
#define PLANE_NONAXIAL 3

#define EQUAL_EPSILON 0.001f
#define STOP_EPSILON  0.1f
#define ON_EPSILON    0.1f

#define RAD_TO_STUDIO (32768.0 / M_PI)
#define STUDIO_TO_RAD (M_PI / 32768.0)

#define INV127F          ( 1.0f / 127.0f )
#define INV255F          ( 1.0f / 255.0f )
#define MAKE_SIGNED( x ) ((( x ) * INV127F ) - 1.0f )

#define Q_min( a, b ) (((a) < (b)) ? (a) : (b))
#define Q_max( a, b ) (((a) > (b)) ? (a) : (b))
#define Q_equal_e( a, b, e ) (((a) >= ((b) - (e))) && ((a) <= ((b) + (e))))
#define Q_equal( a, b ) Q_equal_e( a, b, EQUAL_EPSILON )
#define Q_floor( a )    ((float)(int)(a))
#define Q_ceil( a )     ((float)(int)((a) + 1))
#define Q_round( x, y ) (floor( x / y + 0.5f ) * y )
#define Q_rint(x)       ((x) < 0.0f ? ((int)((x)-0.5f)) : ((int)((x)+0.5f)))
#define ALIGN( x, a )   ((( x ) + (( size_t )( a ) - 1 )) & ~(( size_t )( a ) - 1 ))

#define VectorIsNAN(v) (IS_NAN(v[0]) || IS_NAN(v[1]) || IS_NAN(v[2]))
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define DotProductFabs(x,y) (fabs((x)[0]*(y)[0])+fabs((x)[1]*(y)[1])+fabs((x)[2]*(y)[2]))
#define DotProductPrecise(x,y) ((double)(x)[0]*(double)(y)[0]+(double)(x)[1]*(double)(y)[1]+(double)(x)[2]*(double)(y)[2])
#define CrossProduct(a,b,c) ((c)[0]=(a)[1]*(b)[2]-(a)[2]*(b)[1],(c)[1]=(a)[2]*(b)[0]-(a)[0]*(b)[2],(c)[2]=(a)[0]*(b)[1]-(a)[1]*(b)[0])
#define Vector2Subtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1])
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define Vector2Add(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1])
#define VectorAdd(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorAddScalar(a,b,c) ((c)[0]=(a)[0]+(b),(c)[1]=(a)[1]+(b),(c)[2]=(a)[2]+(b))
#define Vector2Copy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1])
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define Vector4Copy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])
#define VectorScale(in, scale, out) ((out)[0] = (in)[0] * (scale),(out)[1] = (in)[1] * (scale),(out)[2] = (in)[2] * (scale))
#define VectorCompare(v1,v2)	((v1)[0]==(v2)[0] && (v1)[1]==(v2)[1] && (v1)[2]==(v2)[2])
#define VectorDivide( in, d, out ) VectorScale( in, (1.0f / (d)), out )
#define VectorMax(a) ( Q_max((a)[0], Q_max((a)[1], (a)[2])) )
#define VectorAvg(a) ( ((a)[0] + (a)[1] + (a)[2]) / 3 )
#define VectorLength(a) ( sqrt( DotProduct( a, a )))
#define VectorLength2(a) (DotProduct( a, a ))
#define VectorDistance(a, b) (sqrt( VectorDistance2( a, b )))
#define VectorDistance2(a, b) (((a)[0] - (b)[0]) * ((a)[0] - (b)[0]) + ((a)[1] - (b)[1]) * ((a)[1] - (b)[1]) + ((a)[2] - (b)[2]) * ((a)[2] - (b)[2]))
#define Vector2Average(a,b,o)	((o)[0]=((a)[0]+(b)[0])*0.5f,(o)[1]=((a)[1]+(b)[1])*0.5f)
#define VectorAverage(a,b,o)	((o)[0]=((a)[0]+(b)[0])*0.5f,(o)[1]=((a)[1]+(b)[1])*0.5f,(o)[2]=((a)[2]+(b)[2])*0.5f)
#define Vector2Set(v, x, y) ((v)[0]=(x),(v)[1]=(y))
#define Vector2Unpack(v, x, y) ((x)=(v)[0],(y)=(v)[1])
#define VectorSet(v, x, y, z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define VectorUnpack(v, x, y, z) ((x)=(v)[0],(y)=(v)[1],(z)=(v)[2])
#define Vector4Set(v, a, b, c, d) ((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))
#define Vector4Unpack(v, a, b, c, d) ((a)=(v)[0],(b)=(v)[1],(c)=(v)[2],(d)=(v)[3])
#define VectorClear(x) ((x)[0]=(x)[1]=(x)[2]=0)
#define Vector2Lerp( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]))
#define VectorLerp( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]), (c)[2] = (v1)[2] + (lerp) * ((v2)[2] - (v1)[2]))
#define VectorNormalize( v ) { float ilength = (float)sqrt(DotProduct(v, v));if (ilength) ilength = 1.0f / ilength;v[0] *= ilength;v[1] *= ilength;v[2] *= ilength; }
#define VectorNormalize2( v, dest ) {float ilength = (float)sqrt(DotProduct(v,v));if (ilength) ilength = 1.0f / ilength;dest[0] = v[0] * ilength;dest[1] = v[1] * ilength;dest[2] = v[2] * ilength; }
#define VectorNormalizeFast( v ) {float ilength = (float)Q_rsqrt(DotProduct(v,v)); v[0] *= ilength; v[1] *= ilength; v[2] *= ilength; }
#define VectorNormalizeLength( v ) VectorNormalizeLength2((v), (v))
#define VectorNegate(x, y) ((y)[0] = -(x)[0], (y)[1] = -(x)[1], (y)[2] = -(x)[2])
#define VectorM(scale1, b1, c) ((c)[0] = (scale1) * (b1)[0],(c)[1] = (scale1) * (b1)[1],(c)[2] = (scale1) * (b1)[2])
#define VectorMA(a, scale, b, c) ((c)[0] = (a)[0] + (scale) * (b)[0],(c)[1] = (a)[1] + (scale) * (b)[1],(c)[2] = (a)[2] + (scale) * (b)[2])
#define VectorMAM(scale1, b1, scale2, b2, c) ((c)[0] = (scale1) * (b1)[0] + (scale2) * (b2)[0],(c)[1] = (scale1) * (b1)[1] + (scale2) * (b2)[1],(c)[2] = (scale1) * (b1)[2] + (scale2) * (b2)[2])
#define VectorMAMAM(scale1, b1, scale2, b2, scale3, b3, c) ((c)[0] = (scale1) * (b1)[0] + (scale2) * (b2)[0] + (scale3) * (b3)[0],(c)[1] = (scale1) * (b1)[1] + (scale2) * (b2)[1] + (scale3) * (b3)[1],(c)[2] = (scale1) * (b1)[2] + (scale2) * (b2)[2] + (scale3) * (b3)[2])
#define VectorIsNull( v ) ((v)[0] == 0.0f && (v)[1] == 0.0f && (v)[2] == 0.0f)
#define MakeRGBA( out, x, y, z, w ) Vector4Set( out, x, y, z, w )
#define PlaneDist(point,plane) ((plane)->type < 3 ? (point)[(plane)->type] : DotProduct((point), (plane)->normal))
#define PlaneDiff(point,plane) (((plane)->type < 3 ? (point)[(plane)->type] : DotProduct((point), (plane)->normal)) - (plane)->dist)
#define bound( min, num, max ) ((num) >= (min) ? ((num) < (max) ? (num) : (max)) : (min))

/*
===========================

CONSTANTS GLOBALS

===========================
*/
// a1ba: we never return pointers to these globals
// so help compiler optimize constants away
#define vec3_origin ((vec3_t){ 0.0f, 0.0f, 0.0f })

extern const int       boxpnt[6][4];
extern const float     m_bytenormals[NUMVERTEXNORMALS][3];


/*
===========================

MATH FUNCTIONS

===========================
*/
typedef struct mplane_s mplane_t;
typedef struct mstudiobone_s mstudiobone_t;
typedef struct mstudioanim_s mstudioanim_t;

float Q_rsqrt( float number );
uint16_t FloatToHalf( float v );
float HalfToFloat( uint16_t h );
void RoundUpHullSize( vec3_t size );
void VectorVectors( const vec3_t forward, vec3_t right, vec3_t up );
void VectorAngles( const float *forward, float *angles );
void VectorsAngles( const vec3_t forward, const vec3_t right, const vec3_t up, vec3_t angles );
void PlaneIntersect( const mplane_t *plane, const vec3_t p0, const vec3_t p1, vec3_t out );
qboolean SphereIntersect( const vec3_t vSphereCenter, float fSphereRadiusSquared, const vec3_t vLinePt, const vec3_t vLineDir );
void QuaternionSlerp( const vec4_t p, const vec4_t q, float t, vec4_t qt );

void R_StudioCalcBones( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const float *adj, vec3_t pos, vec4_t q );
int BoxOnPlaneSide( const vec3_t emins, const vec3_t emaxs, const mplane_t *p );
#define BOX_ON_PLANE_SIDE( emins, emaxs, p )           \
	((( p )->type < 3 ) ?                              \
	(                                                  \
		((p)->dist <= (emins)[(p)->type]) ? 1 :        \
		(                                              \
			((p)->dist >= (emaxs)[(p)->type] ) ? 2 : 3 \
		)                                              \
	) : BoxOnPlaneSide(( emins ), ( emaxs ), ( p )))

//
// matrixlib.c
//
static inline void Matrix3x4_LoadIdentity( matrix3x4 m )
{
	memset( m, 0, sizeof( matrix3x4 ));
	m[0][0] = m[1][1] = m[2][2] = 1.0f;
}
#define Matrix3x4_Copy( out, in )		memcpy( out, in, sizeof( matrix3x4 ))
void Matrix3x4_VectorTransform( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorITransform( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorRotate( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_VectorIRotate( const matrix3x4 in, const float v[3], float out[3] );
void Matrix3x4_ConcatTransforms( matrix3x4 out, const matrix3x4 in1, const matrix3x4 in2 );
void Matrix3x4_FromOriginQuat( matrix3x4 out, const vec4_t quaternion, const vec3_t origin );
void Matrix3x4_CreateFromEntity( matrix3x4 out, const vec3_t angles, const vec3_t origin, float scale );
void Matrix3x4_TransformAABB( const matrix3x4 world, const vec3_t mins, const vec3_t maxs, vec3_t absmin, vec3_t absmax );
void Matrix3x4_AnglesFromMatrix( const matrix3x4 in, vec3_t out );

static inline void Matrix4x4_LoadIdentity( matrix4x4 m )
{
	memset( m, 0, sizeof( matrix4x4 ));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}
#define Matrix4x4_Copy( out, in )	memcpy( out, in, sizeof( matrix4x4 ))
void Matrix4x4_VectorTransform( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorITransform( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorRotate( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_VectorIRotate( const matrix4x4 in, const float v[3], float out[3] );
void Matrix4x4_ConcatTransforms( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_CreateFromEntity( matrix4x4 out, const vec3_t angles, const vec3_t origin, float scale );
void Matrix4x4_TransformPositivePlane( const matrix4x4 in, const vec3_t normal, float d, vec3_t out, float *dist );
void Matrix4x4_ConvertToEntity( const matrix4x4 in, vec3_t angles, vec3_t origin );
void Matrix4x4_Invert_Simple( matrix4x4 out, const matrix4x4 in1 );
qboolean Matrix4x4_Invert_Full( matrix4x4 out, const matrix4x4 in1 );

// horrible cast but helps not breaking strict aliasing in mathlib
// as union type punning should be fine in C but not in C++
// so don't carry over this to C++ code
#ifndef __cplusplus
typedef union
{
	float fl;
	uint32_t u;
	int32_t i;
} float_bits_t;

static inline uint32_t FloatAsUint( float v )
{
	float_bits_t bits = { v };
	return bits.u;
}

static inline int32_t FloatAsInt( float v )
{
	float_bits_t bits = { v };
	return bits.i;
}

static inline float IntAsFloat( int32_t i )
{
	float_bits_t bits;
	bits.i = i;
	return bits.fl;
}

static inline float UintAsFloat( uint32_t u )
{
	float_bits_t bits;
	bits.u = u;
	return bits.fl;
}
#endif // __cplusplus

// isnan implementation is broken on IRIX as reported in https://github.com/FWGS/xash3d-fwgs/pull/1211
#if defined( XASH_IRIX ) || !defined( isnan )
static inline int IS_NAN( float x )
{
	int32_t i = FloatAsInt( x ); // only C
	return i & ( 255 << 23 ) == ( 255 << 23 );
}
#else
#define IS_NAN isnan
#endif

static inline float anglemod( float a )
{
	a = (360.0f / 65536) * ((int)(a*(65536/360.0f)) & 65535);
	return a;
}

static inline void SinCos( float radians, float *sine, float *cosine )
{
	*sine = sin( radians );
	*cosine = cos( radians );
}

static inline int NearestPOW( int value, qboolean roundDown )
{
	int	n = 1;

	if( value <= 0 ) return 1;
	while( n < value ) n <<= 1;

	if( roundDown )
	{
		if( n > value ) n >>= 1;
	}
	return n;
}

static inline qboolean VectorCompareEpsilon( const vec3_t vec1, const vec3_t vec2, vec_t epsilon )
{
	vec_t ax = fabs( vec1[0] - vec2[0] );
	vec_t ay = fabs( vec1[1] - vec2[1] );
	vec_t az = fabs( vec1[2] - vec2[2] );

	return ( ax <= epsilon ) && ( ay <= epsilon ) && ( az <= epsilon ) ? true : false;
}

static inline float VectorNormalizeLength2( const vec3_t v, vec3_t out )
{
	float length = VectorLength( v );

	if( length )
	{
		float ilength = 1.0f / length;
		out[0] = v[0] * ilength;
		out[1] = v[1] * ilength;
		out[2] = v[2] * ilength;
	}

	return length;
}

static inline void GAME_EXPORT AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up )
{
	float	sr, sp, sy, cr, cp, cy;

	SinCos( DEG2RAD( angles[YAW] ), &sy, &cy );
	SinCos( DEG2RAD( angles[PITCH] ), &sp, &cp );
	SinCos( DEG2RAD( angles[ROLL] ), &sr, &cr );

	if( forward )
	{
		forward[0] = cp * cy;
		forward[1] = cp * sy;
		forward[2] = -sp;
	}

	if( right )
	{
		right[0] = (-1.0f * sr * sp * cy + -1.0f * cr * -sy );
		right[1] = (-1.0f * sr * sp * sy + -1.0f * cr * cy );
		right[2] = (-1.0f * sr * cp);
	}

	if( up )
	{
		up[0] = (cr * sp * cy + -sr * -sy );
		up[1] = (cr * sp * sy + -sr * cy );
		up[2] = (cr * cp);
	}
}

static inline void ClearBounds( vec3_t mins, vec3_t maxs )
{
	// make bogus range
	mins[0] = mins[1] = mins[2] =  999999.0f;
	maxs[0] = maxs[1] = maxs[2] = -999999.0f;
}

static inline qboolean BoundsIntersect( const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2 )
{
	if( mins1[0] > maxs2[0] || mins1[1] > maxs2[1] || mins1[2] > maxs2[2] )
		return false;
	if( maxs1[0] < mins2[0] || maxs1[1] < mins2[1] || maxs1[2] < mins2[2] )
		return false;
	return true;
}

static inline qboolean BoundsAndSphereIntersect( const vec3_t mins, const vec3_t maxs, const vec3_t origin, float radius )
{
	if( mins[0] > origin[0] + radius || mins[1] > origin[1] + radius || mins[2] > origin[2] + radius )
		return false;
	if( maxs[0] < origin[0] - radius || maxs[1] < origin[1] - radius || maxs[2] < origin[2] - radius )
		return false;
	return true;
}

static inline float RadiusFromBounds( const vec3_t mins, const vec3_t maxs )
{
	vec3_t corner;
	int i;

	for( i = 0; i < 3; i++ )
	{
		float a = fabs( mins[i] );
		float b = fabs( maxs[i] );
		corner[i] = Q_max( a, b );
	}

	return VectorLength( corner );
}

static inline void AddPointToBounds( const vec3_t v, vec3_t mins, vec3_t maxs )
{
	int i;

	for( i = 0; i < 3; i++ )
	{
		float val = v[i];
		if( val < mins[i] ) mins[i] = val;
		if( val > maxs[i] ) maxs[i] = val;
	}
}

static inline void ExpandBounds( vec3_t mins, vec3_t maxs, float offset )
{
	mins[0] -= offset;
	mins[1] -= offset;
	mins[2] -= offset;
	maxs[0] += offset;
	maxs[1] += offset;
	maxs[2] += offset;
}

static inline int SignbitsForPlane( const vec3_t normal )
{
	int	bits, i;

	for( bits = i = 0; i < 3; i++ )
		if( normal[i] < 0.0f ) bits |= 1<<i;
	return bits;
}

static inline int PlaneTypeForNormal( const vec3_t normal )
{
	if( normal[0] == 1.0f )
		return PLANE_X;
	if( normal[1] == 1.0f )
		return PLANE_Y;
	if( normal[2] == 1.0f )
		return PLANE_Z;
	return PLANE_NONAXIAL;
}

static inline void AngleQuaternion( const vec3_t angles, vec4_t q, qboolean studio )
{
	float	sr, sp, sy, cr, cp, cy;

	if( studio )
	{
		SinCos( angles[ROLL] * 0.5f, &sy, &cy );
		SinCos( angles[YAW] * 0.5f, &sp, &cp );
		SinCos( angles[PITCH] * 0.5f, &sr, &cr );
	}
	else
	{
		SinCos( DEG2RAD( angles[YAW] ) * 0.5f, &sy, &cy );
		SinCos( DEG2RAD( angles[PITCH] ) * 0.5f, &sp, &cp );
		SinCos( DEG2RAD( angles[ROLL] ) * 0.5f, &sr, &cr );
	}

	q[0] = sr * cp * cy - cr * sp * sy; // X
	q[1] = cr * sp * cy + sr * cp * sy; // Y
	q[2] = cr * cp * sy - sr * sp * cy; // Z
	q[3] = cr * cp * cy + sr * sp * sy; // W
}

static inline void Matrix3x4_SetOrigin( matrix3x4 out, float x, float y, float z )
{
	out[0][3] = x;
	out[1][3] = y;
	out[2][3] = z;
}

static inline void Matrix3x4_OriginFromMatrix( const matrix3x4 in, float *out )
{
	out[0] = in[0][3];
	out[1] = in[1][3];
	out[2] = in[2][3];
}

static inline void QuaternionAngle( const vec4_t q, vec3_t angles )
{
	matrix3x4	mat;
	Matrix3x4_FromOriginQuat( mat, q, vec3_origin );
	Matrix3x4_AnglesFromMatrix( mat, angles );
}

static inline void R_StudioSlerpBones( int numbones, vec4_t q1[], float pos1[][3], const vec4_t q2[], const float pos2[][3], float s )
{
	int	i;
	s = bound( 0.0f, s, 1.0f );

	for( i = 0; i < numbones; i++ )
	{
		QuaternionSlerp( q1[i], q2[i], s, q1[i] );
		VectorLerp( pos1[i], s, pos2[i], pos1[i] );
	}
}

#endif // XASH3D_MATHLIB_H
