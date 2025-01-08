/*
mod_dbghulls.c - loading & handling world and brushmodels
Copyright (C) 2016 Uncle Mike
Copyright (C) 2005 Kevin Shanahan
Copyright (C) 1996-1997 Id Software, Inc.

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
#include "client.h"
#include "mod_local.h"
#include "xash3d_mathlib.h"
#include "world.h"
#include "eiface.h" // offsetof

#define MAX_CLIPNODE_DEPTH		256	// should never exceeds

#define list_entry( ptr, type, member ) \
	((type *)((char *)(ptr) - (size_t)(&((type *)0)->member)))

// iterate over each entry in the list
#define list_for_each_entry( pos, head, member )			\
	for( pos = list_entry( (head)->next, winding_t, member );	\
	     &pos->member != (head);				\
	     pos = list_entry( pos->member.next, winding_t, member ))

// iterate over the list, safe for removal of entries
#define list_for_each_entry_safe( pos, n, head, member )		\
	for( pos = list_entry( (head)->next, winding_t, member ),	\
	     n = list_entry( pos->member.next, winding_t, member );	\
	     &pos->member != (head);				\
	     pos = n, n = list_entry( n->member.next, winding_t, member ))

#define LIST_HEAD_INIT( name ) { &(name), &(name) }

_inline void list_add__( hullnode_t *new, hullnode_t *prev, hullnode_t *next )
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

// add the new entry after the give list entry
_inline void list_add( hullnode_t *newobj, hullnode_t *head )
{
	list_add__( newobj, head, head->next );
}

// add the new entry before the given list entry (list is circular)
_inline void list_add_tail( hullnode_t *newobj, hullnode_t *head )
{
	list_add__( newobj, head->prev, head );
}

_inline void list_del( hullnode_t *entry )
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static winding_t * winding_alloc( uint numpoints )
{
	return (winding_t *)malloc( offsetof( winding_t, p[numpoints] ) );
}

static void free_winding( winding_t *w )
{
	// simple sentinel by Carmack
	if( *(unsigned *)w == 0xDEADC0DE )
		Host_Error( "%s: freed a freed winding\n", __func__ );
	*(unsigned *)w = 0xDEADC0DE;
	free( w );
}

static winding_t *winding_copy( winding_t *w )
{
	winding_t	*neww;

	neww = winding_alloc( w->numpoints );
	memcpy( neww, w, offsetof( winding_t, p[w->numpoints] ) );

	return neww;
}

static void winding_reverse( winding_t *w )
{
	vec3_t	point;
	int	i;

	for( i = 0; i < w->numpoints / 2; i++ )
	{
		VectorCopy( w->p[i], point );
		VectorCopy( w->p[w->numpoints - i - 1], w->p[i] );
		VectorCopy( point, w->p[w->numpoints - i - 1] );
	}
}

/*
 * winding_shrink
 *
 * Takes an over-allocated winding and allocates a new winding with just the
 * required number of points. The input winding is freed.
 */
static winding_t *winding_shrink( winding_t *w )
{
	winding_t	*neww = winding_alloc( w->numpoints );
	memcpy( neww, w, offsetof( winding_t, p[w->numpoints] ));
	free_winding( w );

	return neww;
}

/*
====================
winding_for_plane
====================
*/
static winding_t *winding_for_plane( const mplane_t *p )
{
	vec3_t	org, vright, vup;
	int	i, axis;
	vec_t	max, v;
	winding_t	*w;

	// find the major axis
	max = -BOGUS_RANGE;
	axis = -1;

	for( i = 0; i < 3; i++ )
	{
		v = fabs( p->normal[i] );
		if( v > max )
		{
			axis = i;
			max = v;
		}
	}

	VectorClear( vup );
	switch( axis )
	{
	case 0:
	case 1:
		vup[2] = 1;
		break;
	case 2:
		vup[0] = 1;
		break;
	default:
		Host_Error( "%s: no axis found\n", __func__ );
		return NULL;
	}

	v = DotProduct( vup, p->normal );
	VectorMA( vup, -v, p->normal, vup );
	VectorNormalize( vup );
	VectorScale( p->normal, p->dist, org );
	CrossProduct( vup, p->normal, vright );
	VectorScale( vup, BOGUS_RANGE, vup );
	VectorScale( vright, BOGUS_RANGE, vright );

	// project a really big axis aligned box onto the plane
	w = winding_alloc( 4 );
	memset( w->p, 0, sizeof( vec3_t ) * 4 );
	w->numpoints = 4;
	w->plane = p;

	VectorSubtract( org, vright, w->p[0] );
	VectorAdd( w->p[0], vup, w->p[0] );
	VectorAdd( org, vright, w->p[1] );
	VectorAdd( w->p[1], vup, w->p[1] );
	VectorAdd( org, vright, w->p[2] );
	VectorSubtract( w->p[2], vup, w->p[2] );
	VectorSubtract( org, vright, w->p[3] );
	VectorSubtract( w->p[3], vup, w->p[3] );

	return w;
}

/*
 * ===========================
 * Helper for for the clipping functions
 *  (winding_clip, winding_split)
 * ===========================
 */
static void CalcSides( const winding_t *in, const mplane_t *split, int *sides, vec_t *dists, int counts[3], vec_t epsilon )
{
	const vec_t	*p;
	int		i;

	counts[0] = counts[1] = counts[2] = 0;

	switch( split->type )
	{
	case PLANE_X:
	case PLANE_Y:
	case PLANE_Z:
		p = in->p[0] + split->type;
		for( i = 0; i < in->numpoints; i++, p += 3 )
		{
			const vec_t dot = *p - split->dist;

			dists[i] = dot;
			if( dot > epsilon )
				sides[i] = SIDE_FRONT;
			else if( dot < -epsilon )
				sides[i] = SIDE_BACK;
			else sides[i] = SIDE_ON;
			counts[sides[i]]++;
		}
		break;
	default:
		p = in->p[0];
		for( i = 0; i < in->numpoints; i++, p += 3 )
		{
			const vec_t dot = DotProduct( split->normal, p ) - split->dist;

			dists[i] = dot;
			if( dot > epsilon )
				sides[i] = SIDE_FRONT;
			else if( dot < -epsilon )
				sides[i] = SIDE_BACK;
			else sides[i] = SIDE_ON;
			counts[sides[i]]++;
		}
		break;
	}

	sides[i] = sides[0];
	dists[i] = dists[0];
}

static void PushToPlaneAxis( vec_t *v, const mplane_t *p )
{
	const int	t = p->type % 3;

	v[t] = (p->dist - p->normal[(t + 1) % 3] * v[(t + 1) % 3] - p->normal[(t + 2) % 3] * v[(t + 2) % 3]) / p->normal[t];
}

/*
==================
winding_clip

Clips the winding to the plane, returning the new winding on 'side'.
Frees the input winding.
If keepon is true, an exactly on-plane winding will be saved, otherwise
  it will be clipped away.
==================
*/
static winding_t *winding_clip( winding_t *in, const mplane_t *split, qboolean keepon, int side, vec_t epsilon )
{
	vec_t	*dists;
	int	*sides;
	int	counts[3];
	vec_t	dot;
	int	i, j;
	winding_t *neww;
	vec_t	*p1, *p2, *mid;
	int	maxpts;

	dists = (vec_t *)malloc(( in->numpoints + 1 ) * sizeof( vec_t ));
	sides = (int *)malloc(( in->numpoints + 1 ) * sizeof( int ));
	CalcSides( in, split, sides, dists, counts, epsilon );

	if( keepon && !counts[SIDE_FRONT] && !counts[SIDE_BACK] )
	{
		neww = in;
		goto out_free;
	}

	if( !counts[side] )
	{
		free_winding( in );
		neww = NULL;
		goto out_free;
	}

	if( !counts[side ^ 1] )
	{
		neww = in;
		goto out_free;
	}

	maxpts = in->numpoints + 4;
	neww = winding_alloc( maxpts );
	neww->numpoints = 0;
	neww->plane = in->plane;

	for( i = 0; i < in->numpoints; i++ )
	{
		p1 = in->p[i];

		if( sides[i] == SIDE_ON )
		{
			VectorCopy( p1, neww->p[neww->numpoints] );
			neww->numpoints++;
			continue;
		}

		if( sides[i] == side )
		{
			VectorCopy( p1, neww->p[neww->numpoints] );
			neww->numpoints++;
		}

		if( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
			continue;

		// generate a split point
		p2 = in->p[(i + 1) % in->numpoints];
		mid = neww->p[neww->numpoints++];

		dot = dists[i] / (dists[i] - dists[i + 1]);
		for( j = 0; j < 3; j++ )
		{
			// avoid round off error when possible
			if( in->plane->normal[j] == 1.0f )
				mid[j] = in->plane->dist;
			else if( in->plane->normal[j] == -1.0f )
				mid[j] = -in->plane->dist;
			else if( split->normal[j] == 1.0f )
				mid[j] = split->dist;
			else if( split->normal[j] == -1.0f )
				mid[j] = -split->dist;
			else mid[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

		if( in->plane->type < 3 )
			PushToPlaneAxis( mid, in->plane );
	}

	// free the original winding
	free_winding( in );

	// Shrink the winding back to just what it needs...
	neww = winding_shrink(neww);
out_free:
	free( dists );
	free( sides );

	return neww;
}

/*
==================
winding_split

Splits a winding by a plane, producing one or two windings.  The
original winding is not damaged or freed.  If only on one side, the
returned winding will be the input winding.  If on both sides, two
new windings will be created.
==================
*/
static void winding_split( winding_t *in, const mplane_t *split, winding_t **pfront, winding_t **pback )
{
	vec_t	*dists;
	int	*sides;
	int	counts[3];
	vec_t	dot;
	int	i, j;
	winding_t	*front, *back;
	vec_t	*p1, *p2, *mid;
	int	maxpts;

	dists = (vec_t *)malloc(( in->numpoints + 1 ) * sizeof( vec_t ));
	sides = (int *)malloc(( in->numpoints + 1 ) * sizeof( int ));
	CalcSides(in, split, sides, dists, counts, 0.04f );

	if( !counts[0] && !counts[1] )
	{
		// winding on the split plane - return copies on both sides
		*pfront = winding_copy( in );
		*pback = winding_copy( in );
		goto out_free;
	}

	if( !counts[0] )
	{
		*pfront = NULL;
		*pback = in;
		goto out_free;
	}

	if( !counts[1] )
	{
		*pfront = in;
		*pback = NULL;
		goto out_free;
	}

	maxpts = in->numpoints + 4;
	front = winding_alloc( maxpts );
	front->numpoints = 0;
	front->plane = in->plane;
	back = winding_alloc( maxpts );
	back->numpoints = 0;
	back->plane = in->plane;

	for( i = 0; i < in->numpoints; i++ )
	{
		p1 = in->p[i];

		if( sides[i] == SIDE_ON )
		{
			VectorCopy( p1, front->p[front->numpoints] );
			VectorCopy( p1, back->p[back->numpoints] );
			front->numpoints++;
			back->numpoints++;
			continue;
		}

		if( sides[i] == SIDE_FRONT )
		{
			VectorCopy( p1, front->p[front->numpoints] );
			front->numpoints++;
		}
		else if( sides[i] == SIDE_BACK )
		{
			VectorCopy( p1, back->p[back->numpoints] );
			back->numpoints++;
		}

		if( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
			continue;

		// generate a split point
		p2 = in->p[(i + 1) % in->numpoints];
		mid = front->p[front->numpoints++];

		dot = dists[i] / (dists[i] - dists[i + 1]);
		for( j = 0; j < 3; j++ )
		{
			// avoid round off error when possible
			if( in->plane->normal[j] == 1.0f )
				mid[j] = in->plane->dist;
			else if( in->plane->normal[j] == -1.0f )
				mid[j] = -in->plane->dist;
			else if( split->normal[j] == 1.0f )
				mid[j] = split->dist;
			else if( split->normal[j] == -1.0f )
				mid[j] = -split->dist;
			else mid[j] = p1[j] + dot * (p2[j] - p1[j]);
		}

		if( in->plane->type < 3 )
			PushToPlaneAxis( mid, in->plane );
		VectorCopy( mid, back->p[back->numpoints] );
		back->numpoints++;
	}

	*pfront = winding_shrink( front );
	*pback = winding_shrink( back );
out_free:
	free( dists );
	free( sides );
}

/* ------------------------------------------------------------------------- */

/*
 * This is a stack of the clipnodes we have traversed
 * "sides" indicates which side we went down each time
 */
static int	node_stack[MAX_CLIPNODE_DEPTH];
static int	side_stack[MAX_CLIPNODE_DEPTH];
static uint	node_stack_depth;

static void push_node( int nodenum, int side )
{
	if( node_stack_depth == MAX_CLIPNODE_DEPTH )
		Host_Error( "node stack overflow\n" );

	node_stack[node_stack_depth] = nodenum;
	side_stack[node_stack_depth] = side;
	node_stack_depth++;
}

static void pop_node( void )
{
	if( !node_stack_depth )
		Host_Error( "node stack underflow\n" );
	node_stack_depth--;
}

static void free_hull_polys( hullnode_t *hull_polys )
{
	winding_t	*w, *next;

	list_for_each_entry_safe( w, next, hull_polys, chain )
	{
		list_del( &w->chain );
		free_winding( w );
	}
}

static void hull_windings_r( hull_t *hull, int nodenum, hullnode_t *polys, hull_model_t *model );

static void do_hull_recursion( hull_t *hull, int nodenum, int side, hullnode_t *polys, hull_model_t *model )
{
	winding_t	*w, *next;
	int childnum;

	if( world.version == QBSP2_VERSION )
		childnum = hull->clipnodes32[nodenum].children[side];
	else
		childnum = hull->clipnodes16[nodenum].children[side];

	if( childnum >= 0 )
	{
		push_node( nodenum, side );
		hull_windings_r( hull, childnum, polys, model );
		pop_node();
	}
	else
	{
		switch( childnum )
		{
		case CONTENTS_EMPTY:
		case CONTENTS_WATER:
		case CONTENTS_SLIME:
		case CONTENTS_LAVA:
			list_for_each_entry_safe( w, next, polys, chain )
			{
				list_del( &w->chain );
				list_add( &w->chain, &model->polys );
			}
			break;
		case CONTENTS_SOLID:
		case CONTENTS_SKY:
			// throw away polys...
			list_for_each_entry_safe( w, next, polys, chain )
			{
				if( w->pair )
					w->pair->pair = NULL;
				list_del( &w->chain );
				free_winding( w );
				model->num_polys--;
			}
			break;
		default:
			Host_Error( "bad contents: %i\n", childnum );
			break;
		}
	}
}

static void hull_windings_r( hull_t *hull, int nodenum, hullnode_t *polys, hull_model_t *model )
{
	mplane_t *plane;
	hullnode_t	frontlist = LIST_HEAD_INIT( frontlist );
	hullnode_t	backlist = LIST_HEAD_INIT( backlist );
	winding_t		*w, *next, *front, *back;
	int	i;

	if( world.version == QBSP2_VERSION )
		plane = hull->planes + hull->clipnodes32[nodenum].planenum;
	else
		plane = hull->planes + hull->clipnodes16[nodenum].planenum;

	list_for_each_entry_safe( w, next, polys, chain )
	{
		// PARANIOA - PAIR CHECK
		ASSERT( !w->pair || w->pair->pair == w );

		list_del( &w->chain );
		winding_split( w, plane, &front, &back );
		if( front ) list_add( &front->chain, &frontlist );
		if( back ) list_add( &back->chain, &backlist );

		if( front && back )
		{
			if( w->pair )
			{
				// split the paired poly, preserve pairing
				winding_t	*front2, *back2;

				winding_split( w->pair, plane, &front2, &back2 );

				front2->pair = front;
				front->pair = front2;
				back2->pair = back;
				back->pair = back2;

				list_add( &front2->chain, &w->pair->chain );
				list_add( &back2->chain, &w->pair->chain );
				list_del( &w->pair->chain );
				free_winding( w->pair );
				model->num_polys++;
			}
			else
			{
				front->pair = NULL;
				back->pair = NULL;
			}

			model->num_polys++;
			free_winding( w );
		}
	}

	w = winding_for_plane(plane);

	for( i = 0; w && i < node_stack_depth; i++ )
	{
		mplane_t *p;

		if( world.version == QBSP2_VERSION )
			p = hull->planes + hull->clipnodes32[node_stack[i]].planenum;
		else
			p = hull->planes + hull->clipnodes16[node_stack[i]].planenum;

		w = winding_clip( w, p, false, side_stack[i], 0.00001 );
	}

	if( w )
	{
		winding_t *tmp = winding_copy( w );
		winding_reverse( tmp );

		w->pair = tmp;
		tmp->pair = w;

		list_add( &w->chain, &frontlist );
		list_add( &tmp->chain, &backlist );

		// PARANIOA - PAIR CHECK
		ASSERT( !w->pair || w->pair->pair == w );
		model->num_polys += 2;
	}
	else
	{
		Con_Printf( S_WARN "new winding was clipped away!\n" );
	}

	do_hull_recursion( hull, nodenum, 0, &frontlist, model );
	do_hull_recursion( hull, nodenum, 1, &backlist, model );
}

static void remove_paired_polys( hull_model_t *model )
{
	winding_t	*w, *next;

	list_for_each_entry_safe( w, next, &model->polys, chain )
	{
		if( w->pair )
		{
			list_del( &w->chain );
			free_winding( w );
			model->num_polys--;
		}
	}
}

static void make_hull_windings( hull_t *hull, hull_model_t *model )
{
	hullnode_t head = LIST_HEAD_INIT( head );

	Con_Reportf( "%i clipnodes...\n", hull->lastclipnode - hull->firstclipnode );

	node_stack_depth = 0;
	model->num_polys = 0;

	if( hull->planes != NULL )
	{
		hull_windings_r( hull, hull->firstclipnode, &head, model );
		remove_paired_polys( model );
	}
	Con_Reportf( "%i hull polys\n", model->num_polys );
}

static void Mod_InitDebugHulls( model_t *loadmodel )
{
	int	i;

	world.hull_models = Mem_Calloc( loadmodel->mempool, sizeof( hull_model_t ) * loadmodel->numsubmodels );
	world.num_hull_models = loadmodel->numsubmodels;

	// initialize list
	for( i = 0; i < world.num_hull_models; i++ )
	{
		hullnode_t *poly = &world.hull_models[i].polys;
		poly->next = poly;
		poly->prev = poly;
	}
}

static void Mod_CreatePolygonsForHull( int hullnum )
{
	model_t	*mod = cl.worldmodel;
	double	start, end;
	char	name[8];
	int	i;

	if( hullnum < 0 || hullnum >= MAX_MAP_HULLS )
		return;

	if( !world.num_hull_models )
		Mod_InitDebugHulls( mod ); // FIXME: build hulls for separate bmodels (shells, medkits etc)

	Con_Printf( "generating polygons for hull %u...\n", hullnum );
	start = Sys_DoubleTime();

	// rebuild hulls list
	for( i = 0; i < world.num_hull_models; i++ )
	{
		hull_model_t *model = &world.hull_models[i];
		free_hull_polys( &model->polys );
		make_hull_windings( &mod->hulls[hullnum], model );
		Q_snprintf( name, sizeof( name ), "*%i", i + 1 );
		mod = Mod_FindName( name, false );
	}
	end = Sys_DoubleTime();
	Con_Printf( "build time %.3f secs\n", end - start );
}

static void R_DrawHull( hull_model_t *hull )
{
	winding_t *poly;

	ref.dllFuncs.GL_Bind( XASH_TEXTURE0, R_GetBuiltinTexture( REF_WHITE_TEXTURE ));
	ref.dllFuncs.TriRenderMode( kRenderNormal );
	list_for_each_entry( poly, &hull->polys, chain )
	{
		int i;

		srand((unsigned int)poly );
		ref.dllFuncs.Color4ub( rand() & 255, rand() & 255, rand() & 255, 255 );

		ref.dllFuncs.Begin( TRI_POLYGON );
		for( i = 0; i < poly->numpoints; i++ )
			ref.dllFuncs.Vertex3fv( poly->p[i] );
		ref.dllFuncs.End();
	}
}

void R_DrawWorldHull( void )
{
	if( r_showhull.value <= 0.0f )
		return;

	if( FBitSet( r_showhull.flags, FCVAR_CHANGED ))
	{
		int val = r_showhull.value;
		if( val > 3 ) val = 0;
		Mod_CreatePolygonsForHull( val );
		ClearBits( r_showhull.flags, FCVAR_CHANGED );
	}

	R_DrawHull( &world.hull_models[0] );
}

void R_DrawModelHull( model_t *mod )
{
	int i;

	if( r_showhull.value <= 0.0f )
		return;

	if( !mod || mod->name[0] != '*' )
		return;

	i = atoi( mod->name + 1 );
	if( i < 1 || i >= world.num_hull_models )
		return;

	R_DrawHull( &world.hull_models[i] );
}

void Mod_ReleaseHullPolygons( void )
{
	int	i;

	// release ploygons
	for( i = 0; i < world.num_hull_models; i++ )
	{
		hull_model_t *model = &world.hull_models[i];
		free_hull_polys( &model->polys );
	}
	world.num_hull_models = 0;
}
