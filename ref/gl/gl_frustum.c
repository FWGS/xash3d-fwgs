/*
gl_frustum.cpp - frustum test implementation
Copyright (C) 2016 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "xash3d_mathlib.h"

static void GL_FrustumSetPlane( gl_frustum_t *out, int side, const vec3_t vecNormal, float flDist )
{
	Assert( side >= 0 && side < FRUSTUM_PLANES );

	out->planes[side].type = PlaneTypeForNormal( vecNormal );
	out->planes[side].signbits = SignbitsForPlane( vecNormal );
	VectorCopy( vecNormal, out->planes[side].normal );
	out->planes[side].dist = flDist;

	SetBits( out->clipFlags, BIT( side ));
}

void GL_FrustumInitProj( gl_frustum_t *out, float flZNear, float flZFar, float flFovX, float flFovY )
{
	float	xs, xc;
	vec3_t	farpoint, nearpoint;
	vec3_t	normal, iforward;

	// horizontal fov used for left and right planes
	SinCos( DEG2RAD( flFovX ) * 0.5f, &xs, &xc );

	// setup left plane
	VectorMAM( xs, RI.cull_vforward, -xc, RI.cull_vright, normal );
	GL_FrustumSetPlane( out, FRUSTUM_LEFT, normal, DotProduct( RI.cullorigin, normal ));

	// setup right plane
	VectorMAM( xs, RI.cull_vforward, xc, RI.cull_vright, normal );
	GL_FrustumSetPlane( out, FRUSTUM_RIGHT, normal, DotProduct( RI.cullorigin, normal ));

	// vertical fov used for top and bottom planes
	SinCos( DEG2RAD( flFovY ) * 0.5f, &xs, &xc );
	VectorNegate( RI.cull_vforward, iforward );

	// setup bottom plane
	VectorMAM( xs, RI.cull_vforward, -xc, RI.cull_vup, normal );
	GL_FrustumSetPlane( out, FRUSTUM_BOTTOM, normal, DotProduct( RI.cullorigin, normal ));

	// setup top plane
	VectorMAM( xs, RI.cull_vforward, xc, RI.cull_vup, normal );
	GL_FrustumSetPlane( out, FRUSTUM_TOP, normal, DotProduct( RI.cullorigin, normal ));

	// setup far plane
	VectorMA( RI.cullorigin, flZFar, RI.cull_vforward, farpoint );
	GL_FrustumSetPlane( out, FRUSTUM_FAR, iforward, DotProduct( iforward, farpoint ));

	// no need to setup backplane for general view.
	if( flZNear == 0.0f ) return;

	// setup near plane
	VectorMA( RI.cullorigin, flZNear, RI.cull_vforward, nearpoint );
	GL_FrustumSetPlane( out, FRUSTUM_NEAR, RI.cull_vforward, DotProduct( RI.cull_vforward, nearpoint ));
}

void GL_FrustumInitOrtho( gl_frustum_t *out, float xLeft, float xRight, float yTop, float yBottom, float flZNear, float flZFar )
{
	vec3_t	iforward, iright, iup;

	// setup the near and far planes
	float orgOffset = DotProduct( RI.cullorigin, RI.cull_vforward );
	VectorNegate( RI.cull_vforward, iforward );

	// because quake ortho is inverted and far and near should be swaped
	GL_FrustumSetPlane( out, FRUSTUM_FAR, iforward, -flZNear - orgOffset );
	GL_FrustumSetPlane( out, FRUSTUM_NEAR, RI.cull_vforward, flZFar + orgOffset );

	// setup left and right planes
	orgOffset = DotProduct( RI.cullorigin, RI.cull_vright );
	VectorNegate( RI.cull_vright, iright );

	GL_FrustumSetPlane( out, FRUSTUM_LEFT, RI.cull_vright, xLeft + orgOffset );
	GL_FrustumSetPlane( out, FRUSTUM_RIGHT, iright, -xRight - orgOffset );

	// setup top and buttom planes
	orgOffset = DotProduct( RI.cullorigin, RI.cull_vup );
	VectorNegate( RI.cull_vup, iup );

	GL_FrustumSetPlane( out, FRUSTUM_TOP, RI.cull_vup, yTop + orgOffset );
	GL_FrustumSetPlane( out, FRUSTUM_BOTTOM, iup, -yBottom - orgOffset );
}

// cull methods
qboolean GL_FrustumCullBox( const gl_frustum_t *out, const vec3_t mins, const vec3_t maxs, int userClipFlags )
{
	int	iClipFlags;
	int	i, bit;

	if( r_nocull.value )
		return false;

	if( userClipFlags != 0 )
		iClipFlags = userClipFlags;
	else iClipFlags = out->clipFlags;

	for( i = FRUSTUM_PLANES, bit = 1; i > 0; i--, bit <<= 1 )
	{
		const mplane_t	*p = &out->planes[FRUSTUM_PLANES - i];

		if( !FBitSet( iClipFlags, bit ))
			continue;

		switch( p->signbits )
		{
		case 0:
			if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 1:
			if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 2:
			if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 3:
			if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * maxs[2] < p->dist )
				return true;
			break;
		case 4:
			if( p->normal[0] * maxs[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 5:
			if( p->normal[0] * mins[0] + p->normal[1] * maxs[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 6:
			if( p->normal[0] * maxs[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		case 7:
			if( p->normal[0] * mins[0] + p->normal[1] * mins[1] + p->normal[2] * mins[2] < p->dist )
				return true;
			break;
		default:
			return false;
		}
	}

	return false;
}

qboolean GL_FrustumCullSphere( const gl_frustum_t *out, const vec3_t center, float radius, int userClipFlags )
{
	int	iClipFlags;
	int	i, bit;

	if( r_nocull.value )
		return false;

	if( userClipFlags != 0 )
		iClipFlags = userClipFlags;
	else iClipFlags = out->clipFlags;

	for( i = FRUSTUM_PLANES, bit = 1; i > 0; i--, bit <<= 1 )
	{
		const mplane_t *p = &out->planes[FRUSTUM_PLANES - i];

		if( !FBitSet( iClipFlags, bit ))
			continue;

		if( DotProduct( center, p->normal ) - p->dist <= -radius )
			return true;
	}

	return false;
}
