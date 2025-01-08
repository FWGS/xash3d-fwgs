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
	vec_t		ds, dt;
	int		x, y;
	mtexinfo_t	*tx;
	texture_t		*mt;

	// fill the default contents
	if( fb ) contents = fb->contents;
	else contents = CONTENTS_SOLID;

	if( !surf->texinfo || !surf->texinfo->texture )
		return contents;

	tx = surf->texinfo;
	mt = tx->texture;

	if( mt->name[0] != '{' )
		return contents;

	// TODO: this won't work under dedicated
	// should we bring up imagelib and keep original buffers?
#if !XASH_DEDICATED
	if( !Host_IsDedicated() )
	{
		const byte		*data;

		data = ref.dllFuncs.R_GetTextureOriginalBuffer( mt->gl_texturenum );

		if( !data ) return contents; // original doesn't kept

		ds = DotProduct( point, tx->vecs[0] ) + tx->vecs[0][3];
		dt = DotProduct( point, tx->vecs[1] ) + tx->vecs[1][3];

		// convert ST to real pixels position
		x = fix_coord( ds, mt->width - 1 );
		y = fix_coord( dt, mt->height - 1 );

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
	float		t1, t2, frac;
	int		i, side;
	msurface_t	*surf;
	vec3_t		mid;
	mnode_t *children[2];
	int numsurfaces, firstsurface;

loc0:
	if( node->contents < 0 )
		return NULL;

	t1 = PlaneDiff( p1, node->plane );
	t2 = PlaneDiff( p2, node->plane );

	node_children( children, node, mod );

	if( t1 >= -FRAC_EPSILON && t2 >= -FRAC_EPSILON )
	{
		node = children[0];
		goto loc0;
	}

	if( t1 < FRAC_EPSILON && t2 < FRAC_EPSILON )
	{
		node = children[1];
		goto loc0;
	}

	side = (t1 < 0.0f);
	frac = t1 / ( t1 - t2 );
	frac = bound( 0.0f, frac, 1.0f );

	VectorLerp( p1, frac, p2, mid );

	if(( surf = PM_RecursiveSurfCheck( mod, children[side], p1, mid )) != NULL )
		return surf;

	// walk through real faces
	numsurfaces = node_numsurfaces( node, mod );
	firstsurface = node_firstsurface( node, mod );
	for( i = 0; i < numsurfaces; i++ )
	{
		msurface_t	*surf = &mod->surfaces[firstsurface + i];
		mextrasurf_t	*info = surf->info;
		mfacebevel_t	*fb = info->bevel;
		int		j, contents;
		vec3_t		delta;

		if( !fb ) continue;	// ???

		VectorSubtract( mid, fb->origin, delta );
		if( DotProduct( delta, delta ) >= fb->radius )
			continue;	// no intersection

		for( j = 0; j < fb->numedges; j++ )
		{
			if( PlaneDiff( mid, &fb->edges[j] ) > FRAC_EPSILON )
				break; // outside the bounds
		}

		if( j != fb->numedges )
			continue; // we are outside the bounds of the facet

		// hit the surface
		contents = PM_SampleMiptex( surf, mid );

		if( contents != CONTENTS_EMPTY )
			return surf;
		return NULL; // through the fence
	}

	return PM_RecursiveSurfCheck( mod, children[side^1], mid, p2 );
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
	matrix4x4		matrix;
	model_t		*bmodel;
	hull_t		*hull;
	vec3_t		start_l, end_l;
	vec3_t		offset;

	bmodel = pe->model;

	if( !bmodel || bmodel->type != mod_brush )
		return NULL;

	hull = &pe->model->hulls[0];
	VectorSubtract( hull->clip_mins, vec3_origin, offset );
	VectorAdd( offset, pe->origin, offset );

	VectorSubtract( start, offset, start_l );
	VectorSubtract( end, offset, end_l );

	// rotate start and end into the models frame of reference
	if( !VectorIsNull( pe->angles ))
	{
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
	float	front, back;
	float	frac, midf;
	int	i, r, side;
	vec3_t	mid;
	mnode_t *children[2];
	int numsurfaces, firstsurface;

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

	front = PlaneDiff( start, node->plane );
	back = PlaneDiff( stop, node->plane );

	node_children( children, node, mod );

	if( front >= -FRAC_EPSILON && back >= -FRAC_EPSILON )
	{
		node = children[0];
		goto loc0;
	}

	if( front < FRAC_EPSILON && back < FRAC_EPSILON )
	{
		node = children[1];
		goto loc0;
	}

	side = (front < 0);
	frac = front / (front - back);
	frac = bound( 0.0f, frac, 1.0f );

	VectorLerp( start, frac, stop, mid );
	midf = p1f + ( p2f - p1f ) * frac;

	r = PM_TestLine_r( mod, children[side], p1f, midf, start, mid, trace );

	if( r != CONTENTS_EMPTY )
	{
		if( trace->surface == NULL )
			trace->fraction = midf;
		trace->contents = r;
		return r;
	}

	// walk through real faces
	numsurfaces = node_numsurfaces( node, mod );
	firstsurface = node_firstsurface( node, mod );
	for( i = 0; i < numsurfaces; i++ )
	{
		msurface_t	*surf = &mod->surfaces[firstsurface + i];
		mextrasurf_t	*info = surf->info;
		mfacebevel_t	*fb = info->bevel;
		int		j, contents;
		vec3_t		delta;

		if( !fb ) continue;

		VectorSubtract( mid, fb->origin, delta );
		if( DotProduct( delta, delta ) >= fb->radius )
			continue;	// no intersection

		for( j = 0; j < fb->numedges; j++ )
		{
			if( PlaneDiff( mid, &fb->edges[j] ) > FRAC_EPSILON )
				break; // outside the bounds
		}

		if( j != fb->numedges )
			continue; // we are outside the bounds of the facet

		// hit the surface
		contents = PM_SampleMiptex( surf, mid );

		// fill the trace and out
		trace->contents = contents;
		trace->fraction = midf;

		if( contents != CONTENTS_EMPTY )
			trace->surface = surf;

		return contents;
	}

	return PM_TestLine_r( mod, children[!side], midf, p2f, mid, stop, trace );
}

int PM_TestLineExt( playermove_t *pmove, physent_t *ents, int numents, const vec3_t start, const vec3_t end, int flags )
{
	linetrace_t	trace, trace_bbox;
	matrix4x4		matrix;
	hull_t		*hull = NULL;
	vec3_t		offset, start_l, end_l;
	qboolean		rotated;
	physent_t		*pe;
	int		i;

	trace.contents = CONTENTS_EMPTY;
	trace.fraction = 1.0f;
	trace.surface = NULL;

	for( i = 0; i < numents; i++ )
	{
		pe = &ents[i];

		if( i != 0 && FBitSet( flags, PM_WORLD_ONLY ))
			break;

		if( !pe->model || pe->model->type != mod_brush || pe->solid != SOLID_BSP )
			continue;

		if( FBitSet( flags, PM_GLASS_IGNORE ) && pe->rendermode != kRenderNormal )
			continue;

		hull = &pe->model->hulls[0];

		hull = PM_HullForBsp( pe, pmove, offset );

		if( pe->solid == SOLID_BSP && !VectorIsNull( pe->angles ))
			rotated = true;
		else rotated = false;

		if( rotated )
		{
			Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
			Matrix4x4_VectorITransform( matrix, start, start_l );
			Matrix4x4_VectorITransform( matrix, end, end_l );
		}
		else
		{
			VectorSubtract( start, pe->origin, start_l );
			VectorSubtract( end, pe->origin, end_l );
		}

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
