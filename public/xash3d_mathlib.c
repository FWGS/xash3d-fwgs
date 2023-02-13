/*
xash3d_mathlib.c - internal mathlib
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "port.h"
#include "xash3d_types.h"
#include "const.h"
#include "com_model.h"
#include "xash3d_mathlib.h"
#include "eiface.h"

#define NUM_HULL_ROUNDS	ARRAYSIZE( hull_table )
#define HULL_PRECISION	4

vec3_t vec3_origin = { 0, 0, 0 };

static word hull_table[] = { 2, 4, 6, 8, 12, 16, 18, 24, 28, 32, 36, 40, 48, 54, 56, 60, 64, 72, 80, 112, 120, 128, 140, 176 };

int boxpnt[6][4] =
{
{ 0, 4, 6, 2 }, // +X
{ 0, 1, 5, 4 }, // +Y
{ 0, 2, 3, 1 }, // +Z
{ 7, 5, 1, 3 }, // -X
{ 7, 3, 2, 6 }, // -Y
{ 7, 6, 4, 5 }, // -Z
};

// pre-quantized table normals from Quake1
const float m_bytenormals[NUMVERTEXNORMALS][3] =
{
#include "anorms.h"
};

/*
=================
anglemod
=================
*/
float anglemod( float a )
{
	a = (360.0f / 65536) * ((int)(a*(65536/360.0f)) & 65535);
	return a;
}

/*
=================
SimpleSpline

NOTE: ripped from hl2 source
hermite basis function for smooth interpolation
Similar to Gain() above, but very cheap to call
value should be between 0 & 1 inclusive
=================
*/
float SimpleSpline( float value )
{
	float	valueSquared = value * value;

	// nice little ease-in, ease-out spline-like curve
	return (3.0f * valueSquared - 2.0f * valueSquared * value);
}

word FloatToHalf( float v )
{
	unsigned int	i = *((unsigned int *)&v);
	unsigned int	e = (i >> 23) & 0x00ff;
	unsigned int	m = i & 0x007fffff;
	unsigned short	h;

	if( e <= 127 - 15 )
		h = ((m | 0x00800000) >> (127 - 14 - e)) >> 13;
	else h = (i >> 13) & 0x3fff;

	h |= (i >> 16) & 0xc000;

	return h;
}

float HalfToFloat( word h )
{
	unsigned int	f = (h << 16) & 0x80000000;
	unsigned int	em = h & 0x7fff;

	if( em > 0x03ff )
	{
		f |= (em << 13) + ((127 - 15) << 23);
	}
	else
	{
		unsigned int m = em & 0x03ff;

		if( m != 0 )
		{
			unsigned int e = (em >> 10) & 0x1f;

			while(( m & 0x0400 ) == 0 )
			{
				m <<= 1;
				e--;
			}

			m &= 0x3ff;
			f |= ((e + (127 - 14)) << 23) | (m << 13);
		}
	}

	return *((float *)&f);
}

/*
=================
RoundUpHullSize

round the hullsize to nearest 'right' value
=================
*/
void RoundUpHullSize( vec3_t size )
{
	int	i, j;

	for( i = 0; i < 3; i++)
	{
		qboolean	negative = false;
		float	result, value;

		value = size[i];
		if( value < 0.0f ) negative = true;
		value = Q_ceil( fabs( value ));
		result = Q_ceil( size[i] );

		// lookup hull table to find nearest supposed value
		for( j = 0; j < NUM_HULL_ROUNDS; j++ )
		{
			if( value > hull_table[j] )
				continue;	// ceil only

			if( negative )
			{
				result = ( value - hull_table[j] );
				if( result <= HULL_PRECISION )
				{
					result = -hull_table[j];
					break;
				}
			}
			else
			{
				result = ( value - hull_table[j] );
				if( result <= HULL_PRECISION )
				{
					result = hull_table[j];
					break;
				}
			}
		}

		size[i] = result;
	}
}

/*
=================
SignbitsForPlane

fast box on planeside test
=================
*/
int SignbitsForPlane( const vec3_t normal )
{
	int	bits, i;

	for( bits = i = 0; i < 3; i++ )
		if( normal[i] < 0.0f ) bits |= 1<<i;
	return bits;
}

/*
=================
PlaneTypeForNormal
=================
*/
int PlaneTypeForNormal( const vec3_t normal )
{
	if( normal[0] == 1.0f )
		return PLANE_X;
	if( normal[1] == 1.0f )
		return PLANE_Y;
	if( normal[2] == 1.0f )
		return PLANE_Z;
	return PLANE_NONAXIAL;
}

/*
=================
PlanesGetIntersectionPoint

=================
*/
qboolean PlanesGetIntersectionPoint( const mplane_t *plane1, const mplane_t *plane2, const mplane_t *plane3, vec3_t out )
{
	vec3_t	n1, n2, n3;
	vec3_t	n1n2, n2n3, n3n1;
	float	denom;

	VectorNormalize2( plane1->normal, n1 );
	VectorNormalize2( plane2->normal, n2 );
	VectorNormalize2( plane3->normal, n3 );

	CrossProduct( n1, n2, n1n2 );
	CrossProduct( n2, n3, n2n3 );
	CrossProduct( n3, n1, n3n1 );

	denom = DotProduct( n1, n2n3 );
	VectorClear( out );

	// check if the denominator is zero (which would mean that no intersection is to be found
	if( denom == 0.0f )
	{
		// no intersection could be found, return <0,0,0>
		return false;
	}

	// compute intersection point
#if 0
	VectorMAMAM( plane1->dist, n2n3, plane2->dist, n3n1, plane3->dist, n1n2, out );
#else
	VectorMA( out, plane1->dist, n2n3, out );
	VectorMA( out, plane2->dist, n3n1, out );
	VectorMA( out, plane3->dist, n1n2, out );
#endif
	VectorScale( out, ( 1.0f / denom ), out );

	return true;
}

/*
=================
NearestPOW
=================
*/
int NearestPOW( int value, qboolean roundDown )
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

// remap a value in the range [A,B] to [C,D].
float RemapVal( float val, float A, float B, float C, float D )
{
	return C + (D - C) * (val - A) / (B - A);
}

float ApproachVal( float target, float value, float speed )
{
	float	delta = target - value;

	if( delta > speed )
		value += speed;
	else if( delta < -speed )
		value -= speed;
	else value = target;

	return value;
}

/*
=================
rsqrt
=================
*/
float rsqrt( float number )
{
	int	i;
	float	x, y;

	if( number == 0.0f )
		return 0.0f;

	x = number * 0.5f;
	i = *(int *)&number;	// evil floating point bit level hacking
	i = 0x5f3759df - (i >> 1);	// what the fuck?
	y = *(float *)&i;
	y = y * (1.5f - (x * y * y));	// first iteration

	return y;
}

/*
=================
SinCos
=================
*/
void SinCos( float radians, float *sine, float *cosine )
{
#if _MSC_VER == 1200
	_asm
	{
		fld	dword ptr [radians]
		fsincos

		mov edx, dword ptr [cosine]
		mov eax, dword ptr [sine]

		fstp dword ptr [edx]
		fstp dword ptr [eax]
	}
#else
	*sine = sin(radians);
	*cosine = cos(radians);
#endif
}

/*
==============
VectorCompareEpsilon

==============
*/
qboolean VectorCompareEpsilon( const vec3_t vec1, const vec3_t vec2, vec_t epsilon )
{
	vec_t	ax, ay, az;

	ax = fabs( vec1[0] - vec2[0] );
	ay = fabs( vec1[1] - vec2[1] );
	az = fabs( vec1[2] - vec2[2] );

	if(( ax <= epsilon ) && ( ay <= epsilon ) && ( az <= epsilon ))
		return true;
	return false;
}

float VectorNormalizeLength2( const vec3_t v, vec3_t out )
{
	float	length, ilength;

	length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	length = sqrt( length );

	if( length )
	{
		ilength = 1.0f / length;
		out[0] = v[0] * ilength;
		out[1] = v[1] * ilength;
		out[2] = v[2] * ilength;
	}

	return length;
}

void VectorVectors( const vec3_t forward, vec3_t right, vec3_t up )
{
	float	d;

	right[0] = forward[2];
	right[1] = -forward[0];
	right[2] = forward[1];

	d = DotProduct( forward, right );
	VectorMA( right, -d, forward, right );
	VectorNormalize( right );
	CrossProduct( right, forward, up );
	VectorNormalize( up );
}

/*
=================
AngleVectors

=================
*/
void GAME_EXPORT AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up )
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

/*
=================
VectorAngles

=================
*/
void GAME_EXPORT VectorAngles( const float *forward, float *angles )
{
	float	tmp, yaw, pitch;

	if( !forward || !angles )
	{
		if( angles ) VectorClear( angles );
		return;
	}

	if( forward[1] == 0 && forward[0] == 0 )
	{
		// fast case
		yaw = 0;
		if( forward[2] > 0 )
			pitch = 90.0f;
		else pitch = 270.0f;
	}
	else
	{
		yaw = ( atan2( forward[1], forward[0] ) * 180 / M_PI_F );
		if( yaw < 0 ) yaw += 360;

		tmp = sqrt( forward[0] * forward[0] + forward[1] * forward[1] );
		pitch = ( atan2( forward[2], tmp ) * 180 / M_PI_F );
		if( pitch < 0 ) pitch += 360;
	}

	VectorSet( angles, pitch, yaw, 0 );
}

/*
=================
VectorsAngles

=================
*/
void VectorsAngles( const vec3_t forward, const vec3_t right, const vec3_t up, vec3_t angles )
{
	float	pitch, cpitch, yaw, roll;

	pitch = -asin( forward[2] );
	cpitch = cos( pitch );

	if( fabs( cpitch ) > EQUAL_EPSILON )	// gimball lock?
	{
		cpitch = 1.0f / cpitch;
		pitch = RAD2DEG( pitch );
		yaw = RAD2DEG( atan2( forward[1] * cpitch, forward[0] * cpitch ));
		roll = RAD2DEG( atan2( -right[2] * cpitch, up[2] * cpitch ));
	}
	else
	{
		pitch = forward[2] > 0 ? -90.0f : 90.0f;
		yaw = RAD2DEG( atan2( right[0], -right[1] ));
		roll = 180.0f;
	}

	angles[PITCH] = pitch;
	angles[YAW] = yaw;
	angles[ROLL] = roll;
}

//
// bounds operations
//
/*
=================
ClearBounds
=================
*/
void ClearBounds( vec3_t mins, vec3_t maxs )
{
	// make bogus range
	mins[0] = mins[1] = mins[2] =  999999.0f;
	maxs[0] = maxs[1] = maxs[2] = -999999.0f;
}

/*
=================
AddPointToBounds
=================
*/
void AddPointToBounds( const vec3_t v, vec3_t mins, vec3_t maxs )
{
	float	val;
	int	i;

	for( i = 0; i < 3; i++ )
	{
		val = v[i];
		if( val < mins[i] ) mins[i] = val;
		if( val > maxs[i] ) maxs[i] = val;
	}
}

/*
=================
ExpandBounds
=================
*/
void ExpandBounds( vec3_t mins, vec3_t maxs, float offset )
{
	mins[0] -= offset;
	mins[1] -= offset;
	mins[2] -= offset;
	maxs[0] += offset;
	maxs[1] += offset;
	maxs[2] += offset;
}

/*
=================
BoundsIntersect
=================
*/
qboolean BoundsIntersect( const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2 )
{
	if( mins1[0] > maxs2[0] || mins1[1] > maxs2[1] || mins1[2] > maxs2[2] )
		return false;
	if( maxs1[0] < mins2[0] || maxs1[1] < mins2[1] || maxs1[2] < mins2[2] )
		return false;
	return true;
}

/*
=================
BoundsAndSphereIntersect
=================
*/
qboolean BoundsAndSphereIntersect( const vec3_t mins, const vec3_t maxs, const vec3_t origin, float radius )
{
	if( mins[0] > origin[0] + radius || mins[1] > origin[1] + radius || mins[2] > origin[2] + radius )
		return false;
	if( maxs[0] < origin[0] - radius || maxs[1] < origin[1] - radius || maxs[2] < origin[2] - radius )
		return false;
	return true;
}

/*
=================
SphereIntersect
=================
*/
qboolean SphereIntersect( const vec3_t vSphereCenter, float fSphereRadiusSquared, const vec3_t vLinePt, const vec3_t vLineDir )
{
	float	a, b, c, insideSqr;
	vec3_t	p;

	// translate sphere to origin.
	VectorSubtract( vLinePt, vSphereCenter, p );

	a = DotProduct( vLineDir, vLineDir );
	b = 2.0f * DotProduct( p, vLineDir );
	c = DotProduct( p, p ) - fSphereRadiusSquared;

	insideSqr = b * b - 4.0f * a * c;
	if( insideSqr <= 0.000001f )
		return false;
	return true;
}

/*
=================
PlaneIntersect

find point where ray
was intersect with plane
=================
*/
void PlaneIntersect( const mplane_t *plane, const vec3_t p0, const vec3_t p1, vec3_t out )
{
	float distToPlane = PlaneDiff( p0, plane );
	float planeDotRay = DotProduct( plane->normal, p1 );
	float sect = -(distToPlane) / planeDotRay;

	VectorMA( p0, sect, p1, out );
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds( const vec3_t mins, const vec3_t maxs )
{
	vec3_t	corner;
	int	i;

	for( i = 0; i < 3; i++ )
	{
		corner[i] = fabs( mins[i] ) > fabs( maxs[i] ) ? fabs( mins[i] ) : fabs( maxs[i] );
	}
	return VectorLength( corner );
}

//
// studio utils
//
/*
====================
AngleQuaternion

====================
*/
void AngleQuaternion( const vec3_t angles, vec4_t q, qboolean studio )
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

/*
====================
QuaternionAngle

====================
*/
void QuaternionAngle( const vec4_t q, vec3_t angles )
{
	matrix3x4	mat;
	Matrix3x4_FromOriginQuat( mat, q, vec3_origin );
	Matrix3x4_AnglesFromMatrix( mat, angles );
}

/*
====================
QuaternionAlign

make sure quaternions are within 180 degrees of one another,
if not, reverse q
====================
*/
void QuaternionAlign( const vec4_t p, const vec4_t q, vec4_t qt )
{
	// decide if one of the quaternions is backwards
	float	a = 0.0f;
	float	b = 0.0f;
	int	i;

	for( i = 0; i < 4; i++ )
	{
		a += (p[i] - q[i]) * (p[i] - q[i]);
		b += (p[i] + q[i]) * (p[i] + q[i]);
	}

	if( a > b )
	{
		for( i = 0; i < 4; i++ )
			qt[i] = -q[i];
	}
	else
	{
		for( i = 0; i < 4; i++ )
			qt[i] = q[i];
	}
}

/*
====================
QuaternionSlerpNoAlign
====================
*/
void QuaternionSlerpNoAlign( const vec4_t p, const vec4_t q, float t, vec4_t qt )
{
	float	omega, cosom, sinom, sclp, sclq;
	int	i;

	// 0.0 returns p, 1.0 return q.
	cosom = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];

	if(( 1.0f + cosom ) > 0.000001f )
	{
		if(( 1.0f - cosom ) > 0.000001f )
		{
			omega = acos( cosom );
			sinom = sin( omega );
			sclp = sin( (1.0f - t) * omega) / sinom;
			sclq = sin( t * omega ) / sinom;
		}
		else
		{
			sclp = 1.0f - t;
			sclq = t;
		}

		for( i = 0; i < 4; i++ )
		{
			qt[i] = sclp * p[i] + sclq * q[i];
		}
	}
	else
	{
		qt[0] = -q[1];
		qt[1] = q[0];
		qt[2] = -q[3];
		qt[3] = q[2];
		sclp = sin(( 1.0f - t ) * ( 0.5f * M_PI_F ));
		sclq = sin( t * ( 0.5f * M_PI_F ));

		for( i = 0; i < 3; i++ )
		{
			qt[i] = sclp * p[i] + sclq * qt[i];
		}
	}
}

/*
====================
QuaternionSlerp

Quaternion sphereical linear interpolation
====================
*/
void QuaternionSlerp( const vec4_t p, const vec4_t q, float t, vec4_t qt )
{
	vec4_t	q2;

	// 0.0 returns p, 1.0 return q.
	// decide if one of the quaternions is backwards
	QuaternionAlign( p, q, q2 );

	QuaternionSlerpNoAlign( p, q2, t, qt );
}

/*
====================
V_CalcFov
====================
*/
float V_CalcFov( float *fov_x, float width, float height )
{
	float	x, half_fov_y;

	if( *fov_x < 1.0f || *fov_x > 179.0f )
		*fov_x = 90.0f; // default value

	x = width / tan( DEG2RAD( *fov_x ) * 0.5f );
	half_fov_y = atan( height / x );

	return RAD2DEG( half_fov_y ) * 2;
}

/*
====================
V_AdjustFov
====================
*/
void V_AdjustFov( float *fov_x, float *fov_y, float width, float height, qboolean lock_x )
{
	float x, y;

	if( width * 3 == 4 * height || width * 4 == height * 5 )
	{
		// 4:3 or 5:4 ratio
		return;
	}

	if( lock_x )
	{
		*fov_y = 2 * atan((width * 3) / (height * 4) * tan( *fov_y * M_PI_F / 360.0f * 0.5f )) * 360 / M_PI_F;
		return;
	}

	y = V_CalcFov( fov_x, 640, 480 );
	x = *fov_x;

	*fov_x = V_CalcFov( &y, height, width );
	if( *fov_x < x ) *fov_x = x;
	else *fov_y = y;
}

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSide( const vec3_t emins, const vec3_t emaxs, const mplane_t *p )
{
	float	dist1, dist2;
	int	sides = 0;

	// general case
	switch( p->signbits )
	{
	case 0:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 1:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		break;
	case 2:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 3:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		break;
	case 4:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 5:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2];
		break;
	case 6:
		dist1 = p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	case 7:
		dist1 = p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2];
		dist2 = p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2];
		break;
	default:
		// shut up compiler
		dist1 = dist2 = 0;
		break;
	}

	if( dist1 >= p->dist )
		sides = 1;
	if( dist2 < p->dist )
		sides |= 2;

	return sides;
}

/*
====================
StudioSlerpBones

====================
*/
void R_StudioSlerpBones( int numbones, vec4_t q1[], float pos1[][3], const vec4_t q2[], const float pos2[][3], float s )
{
	int	i;

	s = bound( 0.0f, s, 1.0f );

	for( i = 0; i < numbones; i++ )
	{
		QuaternionSlerp( q1[i], q2[i], s, q1[i] );
		VectorLerp( pos1[i], s, pos2[i], pos1[i] );
	}
}

/*
====================
StudioCalcBoneQuaternion

====================
*/
void R_StudioCalcBoneQuaternion( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const float *adj, vec4_t q )
{
	vec3_t	angles1;
	vec3_t	angles2;
	int	j, k;

	for( j = 0; j < 3; j++ )
	{
		if( !panim || panim->offset[j+3] == 0 )
		{
			angles2[j] = angles1[j] = pbone->value[j+3]; // default;
		}
		else
		{
			mstudioanimvalue_t *panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j+3]);

			k = frame;

			// debug
			if( panimvalue->num.total < panimvalue->num.valid )
				k = 0;

			// find span of values that includes the frame we want
			while( panimvalue->num.total <= k )
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

				// debug
				if( panimvalue->num.total < panimvalue->num.valid )
					k = 0;
			}

			// bah, missing blend!
			if( panimvalue->num.valid > k )
			{
				angles1[j] = panimvalue[k+1].value;

				if( panimvalue->num.valid > k + 1 )
				{
					angles2[j] = panimvalue[k+2].value;
				}
				else
				{
					if( panimvalue->num.total > k + 1 )
						angles2[j] = angles1[j];
					else angles2[j] = panimvalue[panimvalue->num.valid+2].value;
				}
			}
			else
			{
				angles1[j] = panimvalue[panimvalue->num.valid].value;
				if( panimvalue->num.total > k + 1 )
					angles2[j] = angles1[j];
				else angles2[j] = panimvalue[panimvalue->num.valid+2].value;
			}

			angles1[j] = pbone->value[j+3] + angles1[j] * pbone->scale[j+3];
			angles2[j] = pbone->value[j+3] + angles2[j] * pbone->scale[j+3];
		}

		if( pbone->bonecontroller[j+3] != -1 && adj != NULL )
		{
			angles1[j] += adj[pbone->bonecontroller[j+3]];
			angles2[j] += adj[pbone->bonecontroller[j+3]];
		}
	}

	if( !VectorCompare( angles1, angles2 ))
	{
		vec4_t	q1, q2;

		AngleQuaternion( angles1, q1, true );
		AngleQuaternion( angles2, q2, true );
		QuaternionSlerp( q1, q2, s, q );
	}
	else
	{
		AngleQuaternion( angles1, q, true );
	}
}

/*
====================
StudioCalcBonePosition

====================
*/
void R_StudioCalcBonePosition( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const float *adj, vec3_t pos )
{
	vec3_t	origin1;
	vec3_t	origin2;
	int	j, k;

	for( j = 0; j < 3; j++ )
	{
		if( !panim || panim->offset[j] == 0 )
		{
			origin2[j] = origin1[j] = pbone->value[j]; // default;
		}
		else
		{
			mstudioanimvalue_t	*panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j]);

			k = frame;

			// debug
			if( panimvalue->num.total < panimvalue->num.valid )
				k = 0;

			// find span of values that includes the frame we want
			while( panimvalue->num.total <= k )
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

				// debug
				if( panimvalue->num.total < panimvalue->num.valid )
					k = 0;
			}

			// bah, missing blend!
			if( panimvalue->num.valid > k )
			{
				origin1[j] = panimvalue[k+1].value;

				if( panimvalue->num.valid > k + 1 )
				{
					origin2[j] = panimvalue[k+2].value;
				}
				else
				{
					if( panimvalue->num.total > k + 1 )
						origin2[j] = origin1[j];
					else origin2[j] = panimvalue[panimvalue->num.valid+2].value;
				}
			}
			else
			{
				origin1[j] = panimvalue[panimvalue->num.valid].value;
				if( panimvalue->num.total > k + 1 )
					origin2[j] = origin1[j];
				else origin2[j] = panimvalue[panimvalue->num.valid+2].value;
			}

			origin1[j] = pbone->value[j] + origin1[j] * pbone->scale[j];
			origin2[j] = pbone->value[j] + origin2[j] * pbone->scale[j];
		}

		if( pbone->bonecontroller[j] != -1 && adj != NULL )
		{
			origin1[j] += adj[pbone->bonecontroller[j]];
			origin2[j] += adj[pbone->bonecontroller[j]];
		}
	}

	if( !VectorCompare( origin1, origin2 ))
	{
		VectorLerp( origin1, s, origin2, pos );
	}
	else
	{
		VectorCopy( origin1, pos );
	}
}
