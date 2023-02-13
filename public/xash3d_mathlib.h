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

#include "build.h"
#include "com_model.h"
#include "studio.h"

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

#ifdef XASH_IRIX
#undef isnan
#endif
#ifdef isnan // check for C99 isnan
#define IS_NAN isnan
#else
#define IS_NAN(x)		(((*(int *)&x) & (255<<23)) == (255<<23))
#endif

#define ALIGN( x, a )	((( x ) + (( size_t )( a ) - 1 )) & ~(( size_t )( a ) - 1 ))

#define VectorIsNAN(v) (IS_NAN(v[0]) || IS_NAN(v[1]) || IS_NAN(v[2]))
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define DotProductAbs(x,y) (abs((x)[0]*(y)[0])+abs((x)[1]*(y)[1])+abs((x)[2]*(y)[2]))
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
#define VectorSet(v, x, y, z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define Vector4Set(v, a, b, c, d) ((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3] = (d))
#define VectorClear(x) ((x)[0]=(x)[1]=(x)[2]=0)
#define Vector2Lerp( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]))
#define VectorLerp( v1, lerp, v2, c ) ((c)[0] = (v1)[0] + (lerp) * ((v2)[0] - (v1)[0]), (c)[1] = (v1)[1] + (lerp) * ((v2)[1] - (v1)[1]), (c)[2] = (v1)[2] + (lerp) * ((v2)[2] - (v1)[2]))
#define VectorNormalize( v ) { float ilength = (float)sqrt(DotProduct(v, v));if (ilength) ilength = 1.0f / ilength;v[0] *= ilength;v[1] *= ilength;v[2] *= ilength; }
#define VectorNormalize2( v, dest ) {float ilength = (float)sqrt(DotProduct(v,v));if (ilength) ilength = 1.0f / ilength;dest[0] = v[0] * ilength;dest[1] = v[1] * ilength;dest[2] = v[2] * ilength; }
#define VectorNormalizeFast( v ) {float	ilength = (float)rsqrt(DotProduct(v,v)); v[0] *= ilength; v[1] *= ilength; v[2] *= ilength; }
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

void R_StudioSlerpBones( int numbones, vec4_t q1[], float pos1[][3], const vec4_t q2[], const float pos2[][3], float s );
void R_StudioCalcBoneQuaternion( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const float *adj, vec4_t q );
void R_StudioCalcBonePosition( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const vec3_t adj, vec3_t pos );

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

