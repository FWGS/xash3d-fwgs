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
#include "studio.h"

#define NUM_HULL_ROUNDS	ARRAYSIZE( hull_table )
#define HULL_PRECISION	4

static const word hull_table[] = { 2, 4, 6, 8, 12, 16, 18, 24, 28, 32, 36, 40, 48, 54, 56, 60, 64, 72, 80, 112, 120, 128, 140, 176 };

const int boxpnt[6][4] =
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

uint16_t FloatToHalf( float v )
{
	unsigned int	i = FloatAsUint( v );
	unsigned int	e = (i >> 23) & 0x00ff;
	unsigned int	m = i & 0x007fffff;
	unsigned short	h;

	if( e <= 127 - 15 )
		h = ((m | 0x00800000) >> (127 - 14 - e)) >> 13;
	else h = (i >> 13) & 0x3fff;

	h |= (i >> 16) & 0xc000;

	return h;
}

float HalfToFloat( uint16_t h )
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

	return UintAsFloat( f );
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
rsqrt
=================
*/
float Q_rsqrt( float number )
{
	int	i;
	float	x, y;

	if( number == 0.0f )
		return 0.0f;

	x = number * 0.5f;
	i = FloatAsInt( number );	// evil floating point bit level hacking
	i = 0x5f3759df - (i >> 1);	// what the fuck?
	y = IntAsFloat( i );
	y = y * (1.5f - (x * y * y));	// first iteration

	return y;
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

//
// studio utils
//

/*
====================
QuaternionAlign

make sure quaternions are within 180 degrees of one another,
if not, reverse q
====================
*/
static void QuaternionAlign( const vec4_t p, const vec4_t q, vec4_t qt )
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
static void QuaternionSlerpNoAlign( const vec4_t p, const vec4_t q, float t, vec4_t qt )
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

void R_StudioCalcBones( int frame, float s, const mstudiobone_t *pbone, const mstudioanim_t *panim, const float *adj, vec3_t pos, vec4_t q )
{
	float v1[6], v2[6];
	int i, max;

	max = q != NULL ? 6 : 3;

	for( i = 0; i < max; i++ )
	{
		mstudioanimvalue_t *panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[i] );
		int j = frame;
		float fadj = 0.0f;

		if( pbone->bonecontroller[i] >= 0 && adj != NULL )
			fadj = adj[pbone->bonecontroller[i]];

		if( panim->offset[i] == 0 )
		{
			v1[i] = v2[i] = pbone->value[i] + fadj;
			continue;
		}

		if( panimvalue->num.total < panimvalue->num.valid )
			j = 0;

		while( panimvalue->num.total <= j )
		{
			j -= panimvalue->num.total;
			panimvalue += panimvalue->num.valid + 1;

			if( panimvalue->num.total < panimvalue->num.valid )
				j = 0;
		}

		if( panimvalue->num.valid > j )
		{
			v1[i] = panimvalue[j + 1].value;

			if( panimvalue->num.valid > j + 1 )
				v2[i] = panimvalue[j + 2].value;
			else if( panimvalue->num.total > j + 1 )
				v2[i] = v1[i];
			else
				v2[i] = panimvalue[panimvalue->num.valid + 2].value;
		}
		else
		{
			v1[i] = panimvalue[panimvalue->num.valid].value;

			if( panimvalue->num.total > j + 1 )
				v2[i] = v1[i];
			else
				v2[i] = panimvalue[panimvalue->num.valid + 2].value;
		}

		v1[i] = pbone->value[i] + v1[i] * pbone->scale[i] + fadj;
		v2[i] = pbone->value[i] + v2[i] * pbone->scale[i] + fadj;
	}

	if( !VectorCompare( v1, v2 ))
		VectorLerp( v1, s, v2, pos );
	else
		VectorCopy( v1, pos );

	if( q != NULL )
	{
		if( !VectorCompare( &v1[3], &v2[3] ))
		{
			vec4_t q1, q2;

			AngleQuaternion( &v1[3], q1, true );
			AngleQuaternion( &v2[3], q2, true );
			QuaternionSlerp( q1, q2, s, q );
		}
		else
		{
			AngleQuaternion( &v1[3], q, true );
		}
	}
}
