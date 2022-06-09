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
#if XASH_PSP
	float result;
	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.s		S000, %1\n"				// S000 = number
		"vzero.s	S001\n"					// S100 = 0
		"vcmp.s		EZ,   S000\n"			// CC[0] = ( S000 == 0.0f )
		"vrsq.s		S000, S000\n"			// S000 = 1.0 / sqrt( S000 )
		"vcmovt.s	S000, S001, 0\n"		// if ( CC[0] ) S000 = S001
		"sv.s		S000, %0\n"				// result = S000
		".set		pop\n"					// restore assembler option
		:	"=m"( result )
		:	"m"( number )
	);
	return result;
#else
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
#endif
}

/*
=================
SinCos
=================
*/
void SinCos( float radians, float *sine, float *cosine )
{
#if XASH_PSP
	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.s		S000, %2\n"				// S000 = radians
		"vcst.s		S001, VFPU_2_PI\n"		// S001 = VFPU_2_PI = 2 / PI
		"vmul.s		S000, S000, S001\n"		// S000 = S000 * S001
		"vrot.p		C002, S000, [s, c]\n"	// S002 = sin( radians ), S003 = cos( radians )
		"sv.s		S002, %0\n"				// sine = S002
		"sv.s		S003, %1\n"				// cosine = S003
		".set		pop\n"					// restore assembler option
		:	"=m"( *sine), "=m"( *cosine )
		:	"m"( radians )
	);
#else
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
#if XASH_PSP
	vec4_t dst_angles; 

	dst_angles[0] = studio ? angles[PITCH] : DEG2RAD( angles[ROLL] );
	dst_angles[1] = studio ? angles[YAW]   : DEG2RAD( angles[PITCH] );
	dst_angles[2] = studio ? angles[ROLL]  : DEG2RAD( angles[YAW] );

	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.q		C000, %1\n"				// C000 = [PITCH, YAW, ROLL]
		"vcst.s		S010, VFPU_1_PI\n"		// S010 = VFPU_1_PI = 1 / PI
		"vscl.t		C000, C000, S010\n"		// C000 = C000 * S010 = C000 * 0.5f * ( 2 / PI )
		"vcos.t		C010, C000\n"			// C010 = cos( C000 * 0.5f )
		"vsin.t		C000, C000\n"			// C000 = sin( C000 * 0.5f )
		"vcrs.t		C020, C010, C010\n"		// C020 = ( cp*cy, cy*cr, cr*cp )
		"vcrs.t		C030, C000, C000\n"		// C030 = ( sp*sy, sy*sr, sr*sp )
		"vmul.s		S003, S020, S010\n"		// S003 = S020 * S010 = cp*cy*cr
		"vmul.s		S013, S030, S000\n"		// S013 = S030 * S000 = sp*sy*sr
		"vmul.t		C020, C020, C000\n"		// C020 = C020 * C000 = ( cp*cy*sr, cy*cr*sp, cr*cp*sy )
		"vmul.t		C030, C030, C010\n"		// C030 = C030 * C010 = ( sp*sy*cr, sy*sr*cp, sr*sp*cy )
		"vadd.s		S003, S003, S013\n"		// S003 = S003 + S013 = cp*cy*cr + cp*cy*cr
		"vadd.t		C000, C020, C030[-X, Y, -Z]\n"
											// S000 = S020 - C030 = cp*cy*sr - sp*sy*cr
											// S001 = S021 + C031 = cy*cr*sp + sy*sr*cp
											// S002 = S022 - C032 = cr*cp*sy - sr*sp*cy
		"sv.q		C000, %0\n"				// *q   = C000
		".set		pop\n"					// restore assembler option
		: "=m"( *q )
		: "m"( dst_angles )
	);
#else
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
#endif
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
#if XASH_PSP
	int	sides;
	__asm__ (
		".set		push\n"					// save assembler option
		".set		noreorder\n"			// suppress reordering
		"lv.s		S000,  0 + %[normal]\n"	// S000 = p->normal[0]
		"lv.s		S001,  4 + %[normal]\n"	// S001 = p->normal[1]
		"lv.s		S002,  8 + %[normal]\n"	// S002 = p->normal[2]
		"vzero.p	C030\n"					// C030 = [0.0f, 0.0f]
		"lv.s		S032, %[dist]\n"		// S032 = p->dist
		"move		$8,   $0\n"				// $8 = 0
		"beq		%[signbits], $8, 0f\n"	// jump to 0
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 1f\n"	// jump to 1
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 2f\n"	// jump to 2
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 3f\n"	// jump to 3
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 4f\n"	// jump to 4
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 5f\n"	// jump to 5
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 6f\n"	// jump to 6
		"addiu		$8,   $8,   1\n"		// $8 = $8 + 1							( delay slot )
		"beq		%[signbits], $8, 7f\n"	// jump to 7
		"nop\n"								// 										( delay slot )
		"j			9f\n"					// jump to SetSides
		"nop\n"								// 										( delay slot )
	"0:\n"
		"lv.s		S010,  0 + %[emaxs]\n"	// S010 = emaxs[0]
		"lv.s		S011,  4 + %[emaxs]\n"	// S011 = emaxs[1]
		"lv.s		S012,  8 + %[emaxs]\n"	// S012 = emaxs[2]
		"lv.s		S020,  0 + %[emins]\n"	// S020 = emins[0]
		"lv.s		S021,  4 + %[emins]\n"	// S021 = emins[1]
		"lv.s		S022,  8 + %[emins]\n"	// S022 = emins[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"1:\n"
		"lv.s		S010,  0 + %[emins]\n"	// S010 = emins[0]
		"lv.s		S011,  4 + %[emaxs]\n"	// S011 = emaxs[1]
		"lv.s		S012,  8 + %[emaxs]\n"	// S012 = emaxs[2]
		"lv.s		S020,  0 + %[emaxs]\n"	// S020 = emaxs[0]
		"lv.s		S021,  4 + %[emins]\n"	// S021 = emins[1]
		"lv.s		S022,  8 + %[emins]\n"	// S022 = emins[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"2:\n"
		"lv.s		S010,  0 + %[emaxs]\n"	// S010 = emaxs[0]
		"lv.s		S011,  4 + %[emins]\n"	// S011 = emins[1]
		"lv.s		S012,  8 + %[emaxs]\n"	// S012 = emaxs[2]
		"lv.s		S020,  0 + %[emins]\n"	// S020 = emins[0]
		"lv.s		S021,  4 + %[emaxs]\n"	// S021 = emaxs[1]
		"lv.s		S022,  8 + %[emins]\n"	// S022 = emins[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"3:\n"
		"lv.s		S010,  0 + %[emins]\n"	// S010 = emins[0]
		"lv.s		S011,  4 + %[emins]\n"	// S011 = emins[1]
		"lv.s		S012,  8 + %[emaxs]\n"	// S012 = emaxs[2]
		"lv.s		S020,  0 + %[emaxs]\n"	// S020 = emaxs[0]
		"lv.s		S021,  4 + %[emaxs]\n"	// S021 = emaxs[1]
		"lv.s		S022,  8 + %[emins]\n"	// S022 = emins[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"4:\n"
		"lv.s		S010,  0 + %[emaxs]\n"	// S010 = emaxs[0]
		"lv.s		S011,  4 + %[emaxs]\n"	// S011 = emaxs[1]
		"lv.s		S012,  8 + %[emins]\n"	// S012 = emins[2]
		"lv.s		S020,  0 + %[emins]\n"	// S020 = emins[0]
		"lv.s		S021,  4 + %[emins]\n"	// S021 = emins[1]
		"lv.s		S022,  8 + %[emaxs]\n"	// S022 = emaxs[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"5:\n"
		"lv.s		S010,  0 + %[emins]\n"	// S010 = emins[0]
		"lv.s		S011,  4 + %[emaxs]\n"	// S011 = emaxs[1]
		"lv.s		S012,  8 + %[emins]\n"	// S012 = emins[2]
		"lv.s		S020,  0 + %[emaxs]\n"	// S020 = emaxs[0]
		"lv.s		S021,  4 + %[emins]\n"	// S021 = emins[1]
		"lv.s		S022,  8 + %[emaxs]\n"	// S022 = emaxs[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"6:\n"
		"lv.s		S010,  0 + %[emaxs]\n"	// S010 = emaxs[0]
		"lv.s		S011,  4 + %[emins]\n"	// S011 = emins[1]
		"lv.s		S012,  8 + %[emins]\n"	// S012 = emins[2]
		"lv.s		S020,  0 + %[emins]\n"	// S020 = emins[0]
		"lv.s		S021,  4 + %[emaxs]\n"	// S021 = emaxs[1]
		"lv.s		S022,  8 + %[emaxs]\n"	// S022 = emaxs[2]
		"j			8f\n"					// jump to DotProduct
		"nop\n"								// 										( delay slot )
	"7:\n"
		"lv.s		S010,  0 + %[emins]\n"	// S010 = emins[0]
		"lv.s		S011,  4 + %[emins]\n"	// S011 = emins[1]
		"lv.s		S012,  8 + %[emins]\n"	// S012 = emins[2]
		"lv.s		S020,  0 + %[emaxs]\n"	// S020 = emaxs[0]
		"lv.s		S021,  4 + %[emaxs]\n"	// S021 = emaxs[1]
		"lv.s		S022,  8 + %[emaxs]\n"	// S022 = emaxs[2]
	"8:\n"									// DotProduct
		"vdot.t		S030, C000, C010\n"		// S030 = C000 * C010
		"vdot.t		S031, C000, C020\n"		// S031 = C000 * C020
	"9:\n"									// SetSides
		"addiu		%[sides], $0, 0\n"		// sides = 0
		"vcmp.s		LT,   S030, S032\n"		// S030 < S032
		"bvt		0,    10f\n"			// if ( CC[0] == 1 ) jump to 10
		"nop\n"								// 										( delay slot )
		"addiu		%[sides], %[sides], 1\n"// sides = 1
	"10:\n"
		"vcmp.s		GE,   S031, S032\n"		// S031 >= S032
		"bvt		0,    11f\n"			// if ( CC[0] == 1 ) jump to 11
		"nop\n"								// 										( delay slot )
		"addiu		%[sides], %[sides], 2\n"// sides = sides + 2
	"11:\n"
		".set		pop\n"					// restore assembler option
		:	[sides]    "=r" ( sides )
		:	[normal]   "m"  ( p->normal ),
			[emaxs]    "m"  ( *emaxs ),
			[emins]    "m"  ( *emins ),
			[signbits] "r"  ( p->signbits ),
			[dist]     "m"  ( p->dist )
		:	"$8"
	);
	return sides;
#else
	int	sides = 0;
	float	dist1, dist2;

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
#endif
}

