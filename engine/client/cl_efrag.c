/*
gl_refrag.c - store entity fragments
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
#include "entity_types.h"
#include "studio.h"
#include "world.h" // BOX_ON_PLANE_SIDE
#include "client.h"
#include "xash3d_mathlib.h"

/*
===============================================================================

			ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

#define NUM_EFRAGS_ALLOC 64 // alloc 64 efrags (1-2kb each alloc)

static efrag_t	**lastlink;
static mnode_t	*r_pefragtopnode;
static vec3_t	r_emins, r_emaxs;
static cl_entity_t	*r_addent;
static int cl_efrags_num;
static efrag_t *cl_efrags;

static efrag_t *CL_AllocEfrags( int num )
{
	int i;
	efrag_t *efrags;

	if( !cl.worldmodel )
	{
		Host_Error( "%s: called with NULL world\n", __func__ );
		return NULL;
	}

	if( num == 0 )
		return NULL;

	// set world to be the owner, so it will get automatically cleaned up
	efrags = Mem_Calloc( cl.worldmodel->mempool, sizeof( *efrags ) * num );

	// initialize linked list
	for( i = 0; i < num - 1; i++ )
		efrags[i].entnext = &efrags[i + 1];

	cl_efrags_num += num;

	return efrags;
}

/*
==============
CL_ClearEfrags
==============
*/
void CL_ClearEfrags( void )
{
	cl_efrags_num = 0;
	cl_efrags = NULL;
}

/*
===================
R_SplitEntityOnNode
===================
*/
static void R_SplitEntityOnNode( mnode_t *node )
{
	efrag_t	*ef;
	mleaf_t	*leaf;
	int	sides;

	if( node->contents == CONTENTS_SOLID )
		return;

	// add an efrag if the node is a leaf
	if( node->contents < 0 )
	{
		if( !r_pefragtopnode )
			r_pefragtopnode = node;

		leaf = (mleaf_t *)node;

		// grab an efrag off the free list
		ef = cl_efrags;
		if( !ef )
			ef = CL_AllocEfrags( NUM_EFRAGS_ALLOC );

		cl_efrags = ef->entnext;
		ef->entity = r_addent;

		// add the entity link
		*lastlink = ef;
		lastlink = &ef->entnext;
		ef->entnext = NULL;

		// set the leaf links
		ef->leaf = leaf;
		ef->leafnext = leaf->efrags;
		leaf->efrags = ef;
		return;
	}

	// NODE_MIXED
	sides = BOX_ON_PLANE_SIDE( r_emins, r_emaxs, node->plane );

	if( sides == 3 )
	{
		// split on this plane
		// if this is the first splitter of this bmodel, remember it
		if( !r_pefragtopnode ) r_pefragtopnode = node;
	}

	// recurse down the contacted sides
	if( sides & 1 )
		R_SplitEntityOnNode( node_child( node, 0, cl.worldmodel ));
	if( sides & 2 )
		R_SplitEntityOnNode( node_child( node, 1, cl.worldmodel ));
}

/*
===========
R_AddEfrags
===========
*/
void R_AddEfrags( cl_entity_t *ent )
{
	matrix3x4	transform;
	vec3_t	outmins, outmaxs;
	int	i;

	if( !ent->model )
		return;

	r_addent = ent;
	lastlink = &ent->efrag;
	r_pefragtopnode = NULL;

	// handle entity rotation for right bbox expanding
	Matrix3x4_CreateFromEntity( transform, ent->angles, vec3_origin, 1.0f );
	Matrix3x4_TransformAABB( transform, ent->model->mins, ent->model->maxs, outmins, outmaxs );

	for( i = 0; i < 3; i++ )
	{
		r_emins[i] = ent->origin[i] + outmins[i];
		r_emaxs[i] = ent->origin[i] + outmaxs[i];
	}

	R_SplitEntityOnNode( cl.worldmodel->nodes );
	ent->topnode = r_pefragtopnode;
}

/*
================
R_StoreEfrags

================
*/
void R_StoreEfrags( efrag_t **ppefrag, int framecount )
{
	efrag_t *pefrag;
	cl_entity_t *pent;
	model_t *clmodel;

	while(( pefrag = *ppefrag ) != NULL )
	{
		pent = pefrag->entity;
		clmodel = pent->model;

		// how this could happen?
		if( unlikely( clmodel->type < mod_brush || clmodel->type > mod_studio ))
			continue;

		if( pent->visframe != framecount )
		{
			if( CL_AddVisibleEntity( pent, ET_FRAGMENTED ))
			{
				// mark that we've recorded this entity for this frame
				pent->curstate.messagenum = cl.parsecount;
				pent->visframe = framecount;
			}
		}

		ppefrag = &pefrag->leafnext;
	}
}
