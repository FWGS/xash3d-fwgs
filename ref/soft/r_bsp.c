/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_bsp.c

#include "r_local.h"

//
// current entity info
//
vec3_t r_entorigin; // the currently rendering entity in world
// coordinates

float  entity_rotation[3][3];

int    r_currentbkey;

typedef enum {touchessolid, drawnode, nodrawnode} solidstate_t;

#define MAX_BMODEL_VERTS 1000                   // 12K
#define MAX_BMODEL_EDGES 2000                   // 24K

static mvertex_t *pbverts;
static bedge_t   *pbedges;
static int       numbverts, numbedges;

static mvertex_t *pfrontenter, *pfrontexit;

static qboolean  makeclippededge;



/*
================
R_ConcatRotations
================
*/
static void R_ConcatRotations( float in1[3][3], float in2[3][3], float out[3][3] )
{
	out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0]
		    + in1[0][2] * in2[2][0];
	out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1]
		    + in1[0][2] * in2[2][1];
	out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2]
		    + in1[0][2] * in2[2][2];
	out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0]
		    + in1[1][2] * in2[2][0];
	out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1]
		    + in1[1][2] * in2[2][1];
	out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2]
		    + in1[1][2] * in2[2][2];
	out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0]
		    + in1[2][2] * in2[2][0];
	out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1]
		    + in1[2][2] * in2[2][1];
	out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2]
		    + in1[2][2] * in2[2][2];
}


// ===========================================================================

/*
================
R_EntityRotate
================
*/
static void R_EntityRotate( vec3_t vec )
{
	vec3_t tvec;

	VectorCopy( vec, tvec );
	vec[0] = DotProduct( entity_rotation[0], tvec );
	vec[1] = DotProduct( entity_rotation[1], tvec );
	vec[2] = DotProduct( entity_rotation[2], tvec );
}


/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel( void )
{
	float angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

// TODO: should use a look-up table
// TODO: should really be stored with the entity instead of being reconstructed
// TODO: could cache lazily, stored in the entity
// TODO: share work with R_SetUpAliasTransform

// yaw
	angle = RI.currententity->angles[YAW];
	angle = angle * M_PI_F * 2 / 360.0f;
	s = sin( angle );
	c = cos( angle );

	temp1[0][0] = c;
	temp1[0][1] = s;
	temp1[0][2] = 0;
	temp1[1][0] = -s;
	temp1[1][1] = c;
	temp1[1][2] = 0;
	temp1[2][0] = 0;
	temp1[2][1] = 0;
	temp1[2][2] = 1;


// pitch
	angle = RI.currententity->angles[PITCH];
	angle = angle * M_PI_F * 2 / 360.0f;
	s = sin( angle );
	c = cos( angle );

	temp2[0][0] = c;
	temp2[0][1] = 0;
	temp2[0][2] = -s;
	temp2[1][0] = 0;
	temp2[1][1] = 1;
	temp2[1][2] = 0;
	temp2[2][0] = s;
	temp2[2][1] = 0;
	temp2[2][2] = c;

	R_ConcatRotations( temp2, temp1, temp3 );

// roll
	angle = RI.currententity->angles[ROLL];
	angle = angle * M_PI_F * 2 / 360.0f;
	s = sin( angle );
	c = cos( angle );

	temp1[0][0] = 1;
	temp1[0][1] = 0;
	temp1[0][2] = 0;
	temp1[1][0] = 0;
	temp1[1][1] = c;
	temp1[1][2] = s;
	temp1[2][0] = 0;
	temp1[2][1] = -s;
	temp1[2][2] = c;

	R_ConcatRotations( temp1, temp3, entity_rotation );

//
// rotate modelorg and the transformation matrix
//
	R_EntityRotate( tr.modelorg );
	R_EntityRotate( RI.vforward );
	R_EntityRotate( RI.vright );
	R_EntityRotate( RI.vup );

	R_TransformFrustum();
}

/*
================
R_RecursiveClipBPoly
================
*/
static void R_RecursiveClipBPoly( model_t *mod, bedge_t *pedges, mnode_t *pnode, msurface_t *psurf )
{
	bedge_t   *psideedges[2], *pnextedge, *ptedge;
	int       i, side, lastside;
	float     dist, frac, lastdist;
	mplane_t  *splitplane, tplane;
	mvertex_t *pvert, *plastvert, *ptvert;
	mnode_t   *pn;

	psideedges[0] = psideedges[1] = NULL;

	makeclippededge = false;

// transform the BSP plane into model space
// FIXME: cache these?
	splitplane = pnode->plane;
	tplane.dist = splitplane->dist
		      - DotProduct( r_entorigin, splitplane->normal );
	tplane.normal[0] = DotProduct( entity_rotation[0], splitplane->normal );
	tplane.normal[1] = DotProduct( entity_rotation[1], splitplane->normal );
	tplane.normal[2] = DotProduct( entity_rotation[2], splitplane->normal );

// clip edges to BSP plane
	for( ; pedges; pedges = pnextedge )
	{
		pnextedge = pedges->pnext;

		// set the status for the last point as the previous point
		// FIXME: cache this stuff somehow?
		plastvert = pedges->v[0];
		lastdist = DotProduct( plastvert->position, tplane.normal )
			   - tplane.dist;

		if( lastdist > 0 )
			lastside = 0;
		else
			lastside = 1;

		pvert = pedges->v[1];

		dist = DotProduct( pvert->position, tplane.normal ) - tplane.dist;

		if( dist > 0 )
			side = 0;
		else
			side = 1;

		if( side != lastside )
		{
			// clipped
			if( numbverts >= MAX_BMODEL_VERTS )
				return;

			// generate the clipped vertex
			frac = lastdist / ( lastdist - dist );
			ptvert = &pbverts[numbverts++];
			ptvert->position[0] = plastvert->position[0]
					      + frac * ( pvert->position[0]
							 - plastvert->position[0] );
			ptvert->position[1] = plastvert->position[1]
					      + frac * ( pvert->position[1]
							 - plastvert->position[1] );
			ptvert->position[2] = plastvert->position[2]
					      + frac * ( pvert->position[2]
							 - plastvert->position[2] );

			// split into two edges, one on each side, and remember entering
			// and exiting points
			// FIXME: share the clip edge by having a winding direction flag?
			if( numbedges >= ( MAX_BMODEL_EDGES - 1 ))
			{
				// gEngfuncs.Con_Printf ("Out of edges for bmodel\n");
				return;
			}

			ptedge = &pbedges[numbedges];
			ptedge->pnext = psideedges[lastside];
			psideedges[lastside] = ptedge;
			ptedge->v[0] = plastvert;
			ptedge->v[1] = ptvert;

			ptedge = &pbedges[numbedges + 1];
			ptedge->pnext = psideedges[side];
			psideedges[side] = ptedge;
			ptedge->v[0] = ptvert;
			ptedge->v[1] = pvert;

			numbedges += 2;

			if( side == 0 )
			{
				// entering for front, exiting for back
				pfrontenter = ptvert;
				makeclippededge = true;
			}
			else
			{
				pfrontexit = ptvert;
				makeclippededge = true;
			}
		}
		else
		{
			// add the edge to the appropriate side
			pedges->pnext = psideedges[side];
			psideedges[side] = pedges;
		}
	}

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
	if( makeclippededge )
	{
		if( numbedges >= ( MAX_BMODEL_EDGES - 2 ))
		{
			// gEngfuncs.Con_Printf ("Out of edges for bmodel\n");
			return;
		}

		ptedge = &pbedges[numbedges];
		ptedge->pnext = psideedges[0];
		psideedges[0] = ptedge;
		ptedge->v[0] = pfrontexit;
		ptedge->v[1] = pfrontenter;

		ptedge = &pbedges[numbedges + 1];
		ptedge->pnext = psideedges[1];
		psideedges[1] = ptedge;
		ptedge->v[0] = pfrontenter;
		ptedge->v[1] = pfrontexit;

		numbedges += 2;
	}

// draw or recurse further
	for( i = 0; i < 2; i++ )
	{
		if( psideedges[i] )
		{
			// draw if we've reached a non-solid leaf, done if all that's left is a
			// solid leaf, and continue down the tree if it's not a leaf
			pn = node_child( pnode, i, mod );

			// we're done with this branch if the node or leaf isn't in the PVS
			if( pn->visframe == tr.visframecount )
			{
				if( pn->contents < 0 )
				{
					if( pn->contents != CONTENTS_SOLID )
					{
						// r_currentbkey = ((mleaf_t *)pn)->cluster;
						r_currentbkey = LEAF_KEY(((mleaf_t *)pn ));
						R_RenderBmodelFace( psideedges[i], psurf );
					}
				}
				else
				{
					R_RecursiveClipBPoly( mod, psideedges[i], pn, psurf );
				}
			}
		}
	}
}

/*
================
R_DrawSolidClippedSubmodelPolygons

Bmodel crosses multiple leafs
================
*/
void R_DrawSolidClippedSubmodelPolygons( model_t *pmodel, mnode_t *topnode )
{
	int        i, j, lindex;
	vec_t      dot;
	msurface_t *psurf;
	int        numsurfaces;
	mplane_t   *pplane;
	mvertex_t  bverts[MAX_BMODEL_VERTS];
	bedge_t    bedges[MAX_BMODEL_EDGES], *pbedge;
	medge16_t  *pedge, *pedges;

// FIXME: use bounding-box-based frustum clipping info?

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;
	pedges = pmodel->edges16;

	for( i = 0; i < numsurfaces; i++, psurf++ )
	{
		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( RI.currententity->curstate.effects, EF_WATERSIDES ))
				continue;
			if( r_entorigin[2] + pmodel->mins[2] + 1.0f >= psurf->plane->dist )
				continue;
		}
		// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct( tr.modelorg, pplane->normal ) - pplane->dist;

		// draw the polygon
		if(( !( psurf->flags & SURF_PLANEBACK ) && ( dot < -BACKFACE_EPSILON ))
		   || (( psurf->flags & SURF_PLANEBACK ) && ( dot > BACKFACE_EPSILON )))
			continue;

		// FIXME: use bounding-box-based frustum clipping info?

		// copy the edges to bedges, flipping if necessary so always
		// clockwise winding
		// FIXME: if edges and vertices get caches, these assignments must move
		// outside the loop, and overflow checking must be done here
		pbverts = bverts;
		pbedges = bedges;
		numbverts = numbedges = 0;
		pbedge = &bedges[numbedges];
		numbedges += psurf->numedges;

		for( j = 0; j < psurf->numedges; j++ )
		{
			lindex = pmodel->surfedges[psurf->firstedge + j];

			if( lindex > 0 )
			{
				pedge = &pedges[lindex];
				pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
				pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
			}
			else
			{
				lindex = -lindex;
				pedge = &pedges[lindex];
				pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
				pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
			}

			pbedge[j].pnext = &pbedge[j + 1];
		}

		pbedge[j - 1].pnext = NULL; // mark end of edges

		// if ( !( psurf->texinfo->flags & ( SURF_TRANS66 | SURF_TRANS33 ) ) )
		R_RecursiveClipBPoly( pmodel, pbedge, topnode, psurf );
		// else
		//	R_RenderBmodelFace( pbedge, psurf );
	}
}


/*
================
R_DrawSubmodelPolygons

All in one leaf
================
*/
void R_DrawSubmodelPolygons( model_t *pmodel, int clipflags, mnode_t *topnode )
{
	int        i;
	vec_t      dot;
	msurface_t *psurf;
	int        numsurfaces;
	mplane_t   *pplane;

// FIXME: use bounding-box-based frustum clipping info?

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;

	for( i = 0; i < numsurfaces; i++, psurf++ )
	{
		if( FBitSet( psurf->flags, SURF_DRAWTURB ) && !ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ))
		{
			if( psurf->plane->type != PLANE_Z && !FBitSet( RI.currententity->curstate.effects, EF_WATERSIDES ))
				continue;
			if( r_entorigin[2] + pmodel->mins[2] + 1.0f >= psurf->plane->dist )
				continue;
		}
		// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct( tr.modelorg, pplane->normal ) - pplane->dist;

		// draw the polygon
		if((( psurf->flags & SURF_PLANEBACK ) && ( dot < -BACKFACE_EPSILON ))
		   || ( !( psurf->flags & SURF_PLANEBACK ) && ( dot > BACKFACE_EPSILON )))
		{
			r_currentkey = LEAF_KEY(((mleaf_t *)topnode ));

			// FIXME: use bounding-box-based frustum clipping info?
			R_RenderFace( psurf, clipflags );
		}
	}
}

#if XASH_LOW_MEMORY
unsigned short r_leafkeys[MAX_MAP_LEAFS];
#else
int r_leafkeys[MAX_MAP_LEAFS];
#endif
/*
================
R_RecursiveWorldNode
================
*/
static void R_RecursiveWorldNode( mnode_t *node, int clipflags )
{
	int        i, c, side, *pindex;
	vec3_t     acceptpt, rejectpt;
	mplane_t   *plane;
	msurface_t *surf, **mark;
	mleaf_t    *pleaf;
	double     d, dot;

	if( node->contents == CONTENTS_SOLID )
		return; // solid

	if( node->visframe != tr.visframecount )
		return;

// cull the clipping planes if not trivial accept
// FIXME: the compiler is doing a lousy job of optimizing here; it could be
//  twice as fast in ASM
	if( clipflags )
	{
		for( i = 0; i < 4; i++ )
		{
			if( !( clipflags & ( 1 << i )))
				continue; // don't need to clip against it

			// generate accept and reject points
			// FIXME: do with fast look-ups or integer tests based on the sign bit
			// of the floating point values

			pindex = qfrustum.pfrustum_indexes[i];

			rejectpt[0] = (float)node->minmaxs[pindex[0]];
			rejectpt[1] = (float)node->minmaxs[pindex[1]];
			rejectpt[2] = (float)node->minmaxs[pindex[2]];

			d = DotProduct( rejectpt, qfrustum.view_clipplanes[i].normal );
			d -= qfrustum.view_clipplanes[i].dist;

			if( d <= 0 )
				return;

			acceptpt[0] = (float)node->minmaxs[pindex[3 + 0]];
			acceptpt[1] = (float)node->minmaxs[pindex[3 + 1]];
			acceptpt[2] = (float)node->minmaxs[pindex[3 + 2]];

			d = DotProduct( acceptpt, qfrustum.view_clipplanes[i].normal );
			d -= qfrustum.view_clipplanes[i].dist;

			if( d >= 0 )
				clipflags &= ~( 1 << i ); // node is entirely on screen
		}
	}

// if a leaf node, draw stuff
	if( node->contents < 0 )
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if( c )
		{
			do
			{
				( *mark )->visframe = tr.framecount;
				mark++;
			}
			while( --c );
		}

		// deal with model fragments in this leaf
		if( pleaf->efrags )
		{
			gEngfuncs.R_StoreEfrags( &pleaf->efrags, tr.realframecount );
		}


		//	pleaf->cluster
		LEAF_KEY( pleaf ) = r_currentkey;
		r_currentkey++; // all bmodels in a leaf share the same key
	}
	else
	{
		mnode_t    *children[2];
		int firstsurface;

		// node is just a decision point, so go down the apropriate sides

		// find which side of the node we are on
		plane = node->plane;

		switch( plane->type )
		{
		case PLANE_X:
			dot = tr.modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = tr.modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = tr.modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct( tr.modelorg, plane->normal ) - plane->dist;
			break;
		}

		if( dot >= 0 )
			side = 0;
		else
			side = 1;

		// recurse down the children, front side first
		node_children( children, node, WORLDMODEL );
		R_RecursiveWorldNode( children[side], clipflags );

		// draw stuff
		c = node_numsurfaces( node, WORLDMODEL );
		firstsurface = node_firstsurface( node, WORLDMODEL );

		if( c )
		{
			surf = WORLDMODEL->surfaces + firstsurface;

			if( dot < -BACKFACE_EPSILON )
			{
				do
				{
					if(( surf->flags & SURF_PLANEBACK )
					   && ( surf->visframe == tr.framecount ))
					{
						R_RenderFace( surf, clipflags );
					}

					surf++;
				}
				while( --c );
			}
			else if( dot > BACKFACE_EPSILON )
			{
				do
				{
					if( !( surf->flags & SURF_PLANEBACK )
					    && ( surf->visframe == tr.framecount ))
					{
						R_RenderFace( surf, clipflags );
					}

					surf++;
				}
				while( --c );
			}

			// all surfaces on the same node share the same sequence number
			r_currentkey++;
		}

		// recurse down the back side
		R_RecursiveWorldNode( children[!side], clipflags );
	}
}

/*
================
R_RenderWorld
================
*/
void R_RenderWorld( void )
{
	if( !RI.drawWorld )
		return;

	// auto cycle the world frame for texture animation
	RI.currententity = CL_GetEntityByIndex( 0 );
	// RI.currententity->frame = (int)(gp_cl->time*2);

	VectorCopy( RI.vieworg, tr.modelorg );
	RI.currentmodel = WORLDMODEL;
	r_pcurrentvertbase = RI.currentmodel->vertexes;

	R_RecursiveWorldNode( RI.currentmodel->nodes, 15 );
}
