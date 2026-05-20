/*
pm_surface.c - surface tracing
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

#include "common.h"
#include "xash3d_mathlib.h"
#include "pm_local.h"
#include "ref_common.h"

#undef FRAC_EPSILON
#define FRAC_EPSILON	(1.0f / 32.0f)

typedef struct
{
	float		fraction;
	int		contents;
	msurface_t	*surface;
} linetrace_t;

/*
==============
fix_coord

converts the reletive tex coords to absolute
==============
*/
static uint fix_coord( vec_t in, uint width )
{
	if( in > 0 ) return (uint)in % width;
	return width - ((uint)fabs( in ) % width);
}

/*
=============
SampleMiptex

fence texture testing
=============
*/
static int PM_SampleMiptex( const msurface_t *surf, const vec3_t point )
{
	mextrasurf_t	*info = surf->info;
	mfacebevel_t	*fb = info->bevel;
	int		contents;

	// fill the default contents
	if( fb ) contents = fb->contents;
	else contents = CONTENTS_SOLID;

	if( !surf->texinfo || !surf->texinfo->texture )
		return contents;

	mtexinfo_t *tx = surf->texinfo;
	texture_t *mt = tx->texture;

	if( mt->name[0] != '{' )
		return contents;

	// TODO: this won't work under dedicated
	// should we bring up imagelib and keep original buffers?
#if !XASH_DEDICATED
	if( !Host_IsDedicated() )
	{
		const byte *data = ref.dllFuncs.R_GetTextureOriginalBuffer( mt->gl_texturenum );

		if( !data ) return contents; // original doesn't kept

		vec_t ds = DotProduct( point, tx->vecs[0] ) + tx->vecs[0][3];
		vec_t dt = DotProduct( point, tx->vecs[1] ) + tx->vecs[1][3];

		// convert ST to real pixels position
		int x = fix_coord( ds, mt->width - 1 );
		int y = fix_coord( dt, mt->height - 1 );

		ASSERT( x >= 0 && y >= 0 );

		if( data[(mt->width * y) + x] == 255 )
			return CONTENTS_EMPTY;
		return CONTENTS_SOLID;
	}
#endif // !XASH_DEDICATED

	return contents;
}

/*
==================
PM_RecursiveSurfCheck

==================
*/
msurface_t *PM_RecursiveSurfCheck( model_t *mod, mnode_t *node, vec3_t p1, vec3_t p2 )
{
loc0:
	if( node->contents < 0 )
		return NULL;

	float t1 = PlaneDiff( p1, node->plane );
	float t2 = PlaneDiff( p2, node->plane );

	if( t1 >= -FRAC_EPSILON && t2 >= -FRAC_EPSILON )
	{
		node = node_child( node, 0, mod );
		goto loc0;
	}

	if( t1 < FRAC_EPSILON && t2 < FRAC_EPSILON )
	{
		node = node_child( node, 1, mod );
		goto loc0;
	}

	int side = (t1 < 0.0f);
	float frac = t1 / ( t1 - t2 );
	frac = bound( 0.0f, frac, 1.0f );

	vec3_t mid;
	VectorLerp( p1, frac, p2, mid );

	msurface_t *surf = PM_RecursiveSurfCheck( mod, node_child( node, side, mod ), p1, mid );
	if( surf != NULL )
		return surf;

	// walk through real faces
	int numsurfaces = node_numsurfaces( node, mod );
	int firstsurface = node_firstsurface( node, mod );
	for( int i = 0; i < numsurfaces; i++ )
	{
		msurface_t	*surf = &mod->surfaces[firstsurface + i];
		mextrasurf_t	*info = surf->info;
		mfacebevel_t	*fb = info->bevel;
		vec3_t		delta;

		if( !fb ) continue;	// ???

		VectorSubtract( mid, fb->origin, delta );
		if( DotProduct( delta, delta ) >= fb->radius )
			continue;	// no intersection

		int j;
		for( j = 0; j < fb->numedges; j++ )
		{
			if( PlaneDiff( mid, &fb->edges[j] ) > FRAC_EPSILON )
				break; // outside the bounds
		}

		if( j != fb->numedges )
			continue; // we are outside the bounds of the facet

		// hit the surface
		int contents = PM_SampleMiptex( surf, mid );

		if( contents != CONTENTS_EMPTY )
			return surf;
		return NULL; // through the fence
	}

	return PM_RecursiveSurfCheck( mod, node_child( node, side^1, mod ), mid, p2 );
}

/*
==================
PM_TraceTexture

find the face where the traceline hit
assume physentity is valid
==================
*/
msurface_t *PM_TraceSurface( physent_t *pe, vec3_t start, vec3_t end )
{
	model_t *bmodel = pe->model;

	if( !bmodel || bmodel->type != mod_brush )
		return NULL;

	hull_t *hull = &pe->model->hulls[0];
	vec3_t offset;
	VectorSubtract( hull->clip_mins, vec3_origin, offset );
	VectorAdd( offset, pe->origin, offset );

	vec3_t start_l, end_l;
	VectorSubtract( start, offset, start_l );
	VectorSubtract( end, offset, end_l );

	// rotate start and end into the models frame of reference
	if( !VectorIsNull( pe->angles ))
	{
		matrix4x4 matrix;
		Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
		Matrix4x4_VectorITransform( matrix, start, start_l );
		Matrix4x4_VectorITransform( matrix, end, end_l );
	}

	return PM_RecursiveSurfCheck( bmodel, &bmodel->nodes[hull->firstclipnode], start_l, end_l );
}

/*
==================
PM_TestLine_r

optimized trace for light gathering
==================
*/
static int PM_TestLine_r( model_t *mod, mnode_t *node, vec_t p1f, vec_t p2f, const vec3_t start, const vec3_t stop, linetrace_t *trace )
{
loc0:
	if( node->contents < 0 )
	{
		// water, slime or lava interpret as empty
		if( node->contents == CONTENTS_SOLID )
			return CONTENTS_SOLID;
		if( node->contents == CONTENTS_SKY )
			return CONTENTS_SKY;
		trace->fraction = 1.0f;
		return CONTENTS_EMPTY;
	}

	float front = PlaneDiff( start, node->plane );
	float back = PlaneDiff( stop, node->plane );

	if( front >= -FRAC_EPSILON && back >= -FRAC_EPSILON )
	{
		node = node_child( node, 0, mod );
		goto loc0;
	}

	if( front < FRAC_EPSILON && back < FRAC_EPSILON )
	{
		node = node_child( node, 1, mod );
		goto loc0;
	}

	int side = (front < 0);
	float frac = front / (front - back);
	frac = bound( 0.0f, frac, 1.0f );

	vec3_t mid;
	VectorLerp( start, frac, stop, mid );
	float midf = p1f + ( p2f - p1f ) * frac;

	int r = PM_TestLine_r( mod, node_child( node, side, mod ), p1f, midf, start, mid, trace );

	if( r != CONTENTS_EMPTY )
	{
		if( trace->surface == NULL )
			trace->fraction = midf;
		trace->contents = r;
		return r;
	}

	// walk through real faces
	int numsurfaces = node_numsurfaces( node, mod );
	int firstsurface = node_firstsurface( node, mod );
	for( int i = 0; i < numsurfaces; i++ )
	{
		msurface_t	*surf = &mod->surfaces[firstsurface + i];
		mextrasurf_t	*info = surf->info;
		mfacebevel_t	*fb = info->bevel;
		vec3_t		delta;

		if( !fb ) continue;

		VectorSubtract( mid, fb->origin, delta );
		if( DotProduct( delta, delta ) >= fb->radius )
			continue;	// no intersection

		int j;
		for( j = 0; j < fb->numedges; j++ )
		{
			if( PlaneDiff( mid, &fb->edges[j] ) > FRAC_EPSILON )
				break; // outside the bounds
		}

		if( j != fb->numedges )
			continue; // we are outside the bounds of the facet

		// hit the surface
		int contents = PM_SampleMiptex( surf, mid );

		// fill the trace and out
		trace->contents = contents;
		trace->fraction = midf;

		if( contents != CONTENTS_EMPTY )
			trace->surface = surf;

		return contents;
	}

	return PM_TestLine_r( mod, node_child( node, !side, mod ), midf, p2f, mid, stop, trace );
}

int PM_TestLineExt( playermove_t *pmove, physent_t *ents, int numents, const vec3_t start, const vec3_t end, int flags )
{
	linetrace_t trace;

	trace.contents = CONTENTS_EMPTY;
	trace.fraction = 1.0f;
	trace.surface = NULL;

	for( int i = 0; i < numents; i++ )
	{
		physent_t *pe = &ents[i];

		if( i != 0 && FBitSet( flags, PM_WORLD_ONLY ))
			break;

		if( !pe->model || pe->model->type != mod_brush || pe->solid != SOLID_BSP )
			continue;

		if( FBitSet( flags, PM_GLASS_IGNORE ) && pe->rendermode != kRenderNormal )
			continue;

		hull_t *hull = &pe->model->hulls[0];
		vec3_t offset;

		hull = PM_HullForBsp( pe, pmove, offset );

		qboolean rotated;
		if( pe->solid == SOLID_BSP && !VectorIsNull( pe->angles ))
			rotated = true;
		else rotated = false;

		vec3_t start_l, end_l;
		if( rotated )
		{
			matrix4x4 matrix;
			Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
			Matrix4x4_VectorITransform( matrix, start, start_l );
			Matrix4x4_VectorITransform( matrix, end, end_l );
		}
		else
		{
			VectorSubtract( start, pe->origin, start_l );
			VectorSubtract( end, pe->origin, end_l );
		}

		linetrace_t trace_bbox;
		trace_bbox.contents = CONTENTS_EMPTY;
		trace_bbox.fraction = 1.0f;
		trace_bbox.surface = NULL;

		PM_TestLine_r( pe->model, &pe->model->nodes[hull->firstclipnode], 0.0f, 1.0f, start_l, end_l, &trace_bbox );

		if( trace_bbox.contents != CONTENTS_EMPTY || trace_bbox.fraction < trace.fraction )
		{
			trace = trace_bbox;
		}
	}

	return trace.contents;
}
