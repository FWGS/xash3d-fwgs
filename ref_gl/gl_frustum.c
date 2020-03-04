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

void GL_FrustumEnablePlane( gl_frustum_t *out, int side )
{
	Assert( side >= 0 && side < FRUSTUM_PLANES );

	// make sure what plane is ready
	if( !VectorIsNull( out->planes[side].normal ))
		SetBits( out->clipFlags, BIT( side ));
}

void GL_FrustumDisablePlane( gl_frustum_t *out, int side )
{
	Assert( side >= 0 && side < FRUSTUM_PLANES );
	ClearBits( out->clipFlags, BIT( side ));
}

void GL_FrustumSetPlane( gl_frustum_t *out, int side, const vec3_t vecNormal, float flDist )
{
	Assert( side >= 0 && side < FRUSTUM_PLANES );

	out->planes[side].type = PlaneTypeForNormal( vecNormal );
	out->planes[side].signbits = SignbitsForPlane( vecNormal );
	VectorCopy( vecNormal, out->planes[side].normal );
	out->planes[side].dist = flDist;

	SetBits( out->clipFlags, BIT( side ));
}

void GL_FrustumNormalizePlane( gl_frustum_t *out, int side )
{
	float	length;

	Assert( side >= 0 && side < FRUSTUM_PLANES );

	// normalize
	length = VectorLength( out->planes[side].normal );

	if( length )
	{
		float ilength = (1.0f / length);
		out->planes[side].normal[0] *= ilength;
		out->planes[side].normal[1] *= ilength;
		out->planes[side].normal[2] *= ilength;
		out->planes[side].dist *= ilength;
	}

	out->planes[side].type = PlaneTypeForNormal( out->planes[side].normal );
	out->planes[side].signbits = SignbitsForPlane( out->planes[side].normal );

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

void GL_FrustumInitBox( gl_frustum_t *out, const vec3_t org, float radius )
{
	vec3_t	normal;
	int	i;

	for( i = 0; i < FRUSTUM_PLANES; i++ )
	{
		// setup normal for each direction
		VectorClear( normal );
		normal[((i >> 1) + 1) % 3] = (i & 1) ? 1.0f : -1.0f;
		GL_FrustumSetPlane( out, i, normal, DotProduct( org, normal ) - radius );
	}
}

void GL_FrustumInitProjFromMatrix( gl_frustum_t *out, const matrix4x4 projection )
{
	int	i;

	// left
	out->planes[FRUSTUM_LEFT].normal[0] =	projection[0][3] + projection[0][0];
	out->planes[FRUSTUM_LEFT].normal[1] =	projection[1][3] + projection[1][0];
	out->planes[FRUSTUM_LEFT].normal[2] =	projection[2][3] + projection[2][0];
	out->planes[FRUSTUM_LEFT].dist =	-(projection[3][3] + projection[3][0]);

	// right
	out->planes[FRUSTUM_RIGHT].normal[0] =	projection[0][3] - projection[0][0];
	out->planes[FRUSTUM_RIGHT].normal[1] =	projection[1][3] - projection[1][0];
	out->planes[FRUSTUM_RIGHT].normal[2] =	projection[2][3] - projection[2][0];
	out->planes[FRUSTUM_RIGHT].dist =	-(projection[3][3] - projection[3][0]);

	// bottom
	out->planes[FRUSTUM_BOTTOM].normal[0] =	projection[0][3] + projection[0][1];
	out->planes[FRUSTUM_BOTTOM].normal[1] =	projection[1][3] + projection[1][1];
	out->planes[FRUSTUM_BOTTOM].normal[2] =	projection[2][3] + projection[2][1];
	out->planes[FRUSTUM_BOTTOM].dist =	-(projection[3][3] + projection[3][1]);

	// top
	out->planes[FRUSTUM_TOP].normal[0] =	projection[0][3] - projection[0][1];
	out->planes[FRUSTUM_TOP].normal[1] =	projection[1][3] - projection[1][1];
	out->planes[FRUSTUM_TOP].normal[2] =	projection[2][3] - projection[2][1];
	out->planes[FRUSTUM_TOP].dist =	-(projection[3][3] - projection[3][1]);

	// near
	out->planes[FRUSTUM_NEAR].normal[0] =	projection[0][3] + projection[0][2];
	out->planes[FRUSTUM_NEAR].normal[1] =	projection[1][3] + projection[1][2];
	out->planes[FRUSTUM_NEAR].normal[2] =	projection[2][3] + projection[2][2];
	out->planes[FRUSTUM_NEAR].dist =	-(projection[3][3] + projection[3][2]);

	// far
	out->planes[FRUSTUM_FAR].normal[0] =	projection[0][3] - projection[0][2];
	out->planes[FRUSTUM_FAR].normal[1] =	projection[1][3] - projection[1][2];
	out->planes[FRUSTUM_FAR].normal[2] =	projection[2][3] - projection[2][2];
	out->planes[FRUSTUM_FAR].dist =	-(projection[3][3] - projection[3][2]);

	for( i = 0; i < FRUSTUM_PLANES; i++ )
	{
		GL_FrustumNormalizePlane( out, i );
	}
}

void GL_FrustumComputeCorners( gl_frustum_t *out, vec3_t corners[8] )
{
	memset( corners, 0, sizeof( vec3_t ) * 8 );

	PlanesGetIntersectionPoint( &out->planes[FRUSTUM_LEFT], &out->planes[FRUSTUM_TOP], &out->planes[FRUSTUM_FAR], corners[0] );
	PlanesGetIntersectionPoint( &out->planes[FRUSTUM_RIGHT], &out->planes[FRUSTUM_TOP], &out->planes[FRUSTUM_FAR], corners[1] );
	PlanesGetIntersectionPoint( &out->planes[FRUSTUM_LEFT], &out->planes[FRUSTUM_BOTTOM], &out->planes[FRUSTUM_FAR], corners[2] );
	PlanesGetIntersectionPoint( &out->planes[FRUSTUM_RIGHT], &out->planes[FRUSTUM_BOTTOM], &out->planes[FRUSTUM_FAR], corners[3] );

	if( FBitSet( out->clipFlags, BIT( FRUSTUM_NEAR )))
	{
		PlanesGetIntersectionPoint( &out->planes[FRUSTUM_LEFT], &out->planes[FRUSTUM_TOP], &out->planes[FRUSTUM_NEAR], corners[4] );
		PlanesGetIntersectionPoint( &out->planes[FRUSTUM_RIGHT], &out->planes[FRUSTUM_TOP], &out->planes[FRUSTUM_NEAR], corners[5] );
		PlanesGetIntersectionPoint( &out->planes[FRUSTUM_LEFT], &out->planes[FRUSTUM_BOTTOM], &out->planes[FRUSTUM_NEAR], corners[6] );
		PlanesGetIntersectionPoint( &out->planes[FRUSTUM_RIGHT], &out->planes[FRUSTUM_BOTTOM], &out->planes[FRUSTUM_NEAR], corners[7] );
	}
	else
	{
		PlanesGetIntersectionPoint( &out->planes[FRUSTUM_LEFT], &out->planes[FRUSTUM_RIGHT], &out->planes[FRUSTUM_TOP], corners[4] );
		VectorCopy( corners[4], corners[5] );
		VectorCopy( corners[4], corners[6] );
		VectorCopy( corners[4], corners[7] );
	}
}

void GL_FrustumComputeBounds( gl_frustum_t *out, vec3_t mins, vec3_t maxs )
{
	vec3_t	corners[8];
	int	i;

	GL_FrustumComputeCorners( out, corners );

	ClearBounds( mins, maxs );

	for( i = 0; i < 8; i++ )
		AddPointToBounds( corners[i], mins, maxs );
}

void GL_FrustumDrawDebug( gl_frustum_t *out )
{
	vec3_t	bbox[8];
	int	i;

	GL_FrustumComputeCorners( out, bbox );

	// g-cont. frustum must be yellow :-)
	pglColor4f( 1.0f, 1.0f, 0.0f, 1.0f );
	pglDisable( GL_TEXTURE_2D );
	pglBegin( GL_LINES );

	for( i = 0; i < 2; i += 1 )
	{
		pglVertex3fv( bbox[i+0] );
		pglVertex3fv( bbox[i+2] );
		pglVertex3fv( bbox[i+4] );
		pglVertex3fv( bbox[i+6] );
		pglVertex3fv( bbox[i+0] );
		pglVertex3fv( bbox[i+4] );
		pglVertex3fv( bbox[i+2] );
		pglVertex3fv( bbox[i+6] );
		pglVertex3fv( bbox[i*2+0] );
		pglVertex3fv( bbox[i*2+1] );
		pglVertex3fv( bbox[i*2+4] );
		pglVertex3fv( bbox[i*2+5] );
	}

	pglEnd();
	pglEnable( GL_TEXTURE_2D );
}

// cull methods
qboolean GL_FrustumCullBox( gl_frustum_t *out, const vec3_t mins, const vec3_t maxs, int userClipFlags )
{
	int	iClipFlags;
	int	i, bit;

	if( r_nocull->value )
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

qboolean GL_FrustumCullSphere( gl_frustum_t *out, const vec3_t center, float radius, int userClipFlags )
{
	int	iClipFlags;
	int	i, bit;

	if( r_nocull->value )
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
