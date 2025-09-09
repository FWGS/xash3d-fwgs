/*
sv_world.c - world query functions
Copyright (C) 2008 Uncle Mike

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
#include "server.h"
#include "const.h"
#include "pm_local.h"
#include "studio.h"

typedef struct moveclip_s
{
	vec3_t		boxmins, boxmaxs;	// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	const float	*start, *end;
	edict_t		*passedict;
	trace_t		trace;
	int		type;		// move type
	qboolean		ignoretrans;
	qboolean		monsterclip;
} moveclip_t;

/*
===============================================================================

HULL BOXES

===============================================================================
*/

static hull_t        box_hull;
static mplane_t      box_planes[6];

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
static void SV_InitBoxHull( void )
{
	int	i;

	box_hull.clipnodes16 = (mclipnode16_t *)box_clipnodes16;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	for( i = 0; i < 6; i++ )
	{
		box_planes[i].type = i>>1;
		box_planes[i].normal[i>>1] = 1;
		box_planes[i].signbits = 0;
	}

}

/*
====================
StudioPlayerBlend

====================
*/
static void SV_StudioPlayerBlend( mstudioseqdesc_t *pseqdesc, int *pBlend, float *pPitch )
{
	// calc up/down pointing
	*pBlend = (*pPitch * 3);

	if( *pBlend < pseqdesc->blendstart[0] )
	{
		*pPitch -= pseqdesc->blendstart[0] / 3.0f;
		*pBlend = 0;
	}
	else if( *pBlend > pseqdesc->blendend[0] )
	{
		*pPitch -= pseqdesc->blendend[0] / 3.0f;
		*pBlend = 255;
	}
	else
	{
		if( pseqdesc->blendend[0] - pseqdesc->blendstart[0] < 0.1f ) // catch qc error
			*pBlend = 127;
		else *pBlend = 255.0f * (*pBlend - pseqdesc->blendstart[0]) / (pseqdesc->blendend[0] - pseqdesc->blendstart[0]);
		*pPitch = 0;
	}
}

/*
====================
SV_CheckSphereIntersection

check clients only
====================
*/
static qboolean SV_CheckSphereIntersection( edict_t *ent, const vec3_t start, const vec3_t end )
{
	int		i, sequence;
	float		radiusSquared;
	vec3_t		traceOrg, traceDir;
	studiohdr_t	*pstudiohdr;
	mstudioseqdesc_t	*pseqdesc;
	model_t		*mod;

	if( !FBitSet( ent->v.flags, FL_CLIENT|FL_FAKECLIENT ))
		return true;

	if(( mod = SV_ModelHandle( ent->v.modelindex )) == NULL )
		return true;

	if(( pstudiohdr = (studiohdr_t *)Mod_StudioExtradata( mod )) == NULL )
		return true;

	sequence = ent->v.sequence;
	if( sequence < 0 || sequence >= pstudiohdr->numseq )
		sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + sequence;

	VectorCopy( start, traceOrg );
	VectorSubtract( end, start, traceDir );
	radiusSquared = 0.0f;

	for ( i = 0; i < 3; i++ )
		radiusSquared += Q_max( fabs( pseqdesc->bbmin[i] ), fabs( pseqdesc->bbmax[i] ));

	return SphereIntersect( ent->v.origin, radiusSquared, traceOrg, traceDir );
}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
static hull_t *SV_HullForBox( const vec3_t mins, const vec3_t maxs )
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	if( world.version == QBSP2_VERSION )
		box_hull.clipnodes32 = (mclipnode32_t *)box_clipnodes32;
	else
		box_hull.clipnodes16 = (mclipnode16_t *)box_clipnodes16;

	return &box_hull;
}

/*
==================
SV_HullForBsp

forcing to select BSP hull
==================
*/
static hull_t *SV_HullForBsp( edict_t *ent, const vec3_t mins, const vec3_t maxs, vec3_t offset )
{
	hull_t		*hull;
	model_t		*model;
	vec3_t		size;

	if( svgame.physFuncs.SV_HullForBsp != NULL )
	{
		hull = svgame.physFuncs.SV_HullForBsp( ent, mins, maxs, offset );
		if( hull ) return hull;
	}

	// decide which clipping hull to use, based on the size
	model = SV_ModelHandle( ent->v.modelindex );

	if( !model || model->type != mod_brush )
		Host_Error( "Entity %i (%s) SOLID_BSP with a non bsp model %s\n", NUM_FOR_EDICT( ent ), SV_ClassName( ent ), STRING( ent->v.model ));

	VectorSubtract( maxs, mins, size );

#ifdef RANDOM_HULL_NULLIZATION
	// author: The FiEctro
	hull = &model->hulls[COM_RandomLong( 0, 0 )];
#endif
	// g-cont: find a better method to detect quake-maps?
	if( FBitSet( world.flags, FWORLD_SKYSPHERE ))
	{
		// alternate hull select for quake maps
		if( size[0] < 3.0f || ent->v.solid == SOLID_PORTAL )
			hull = &model->hulls[0];
		else if( size[0] <= 32.0f )
			hull = &model->hulls[1];
		else hull = &model->hulls[2];

		VectorSubtract( hull->clip_mins, mins, offset );
	}
	else
	{
		if( size[0] <= 8.0f || ent->v.solid == SOLID_PORTAL )
		{
			hull = &model->hulls[0];
			VectorCopy( hull->clip_mins, offset );
		}
		else
		{
			if( size[0] <= 36.0f )
			{
				if( size[2] <= 36.0f )
					hull = &model->hulls[3];
				else hull = &model->hulls[1];
			}
			else hull = &model->hulls[2];

			VectorSubtract( hull->clip_mins, mins, offset );
		}
	}

	VectorAdd( offset, ent->v.origin, offset );

	return hull;
}

/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
static hull_t *SV_HullForEntity( edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset )
{
	hull_t	*hull;
	vec3_t	hullmins, hullmaxs;

	if( ent->v.solid == SOLID_BSP || ent->v.solid == SOLID_PORTAL )
	{
		if( ent->v.solid != SOLID_PORTAL )
		{
			if( ent->v.movetype != MOVETYPE_PUSH && ent->v.movetype != MOVETYPE_PUSHSTEP )
				Host_Error( "'%s' has SOLID_BSP without MOVETYPE_PUSH or MOVETYPE_PUSHSTEP\n", SV_ClassName( ent ));
		}
		hull = SV_HullForBsp( ent, mins, maxs, offset );
	}
	else
	{
		// create a temp hull from bounding box sizes
		VectorSubtract( ent->v.mins, maxs, hullmins );
		VectorSubtract( ent->v.maxs, mins, hullmaxs );
		hull = SV_HullForBox( hullmins, hullmaxs );

		VectorCopy( ent->v.origin, offset );
	}

	return hull;
}

/*
====================
SV_HullForStudioModel

====================
*/
static hull_t *SV_HullForStudioModel( edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset, int *numhitboxes )
{
	qboolean		useComplexHull;
	float		scale = 0.5f;
	hull_t		*hull = NULL;
	vec3_t		size;
	model_t		*mod;

	if(( mod = SV_ModelHandle( ent->v.modelindex )) == NULL )
	{
		*numhitboxes = 1;
		return SV_HullForEntity( ent, mins, maxs, offset );
	}

	VectorSubtract( maxs, mins, size );
	useComplexHull = false;

	if( VectorIsNull( size ) && !FBitSet( svgame.globals->trace_flags, FTRACE_SIMPLEBOX ))
	{
		useComplexHull = true;

		if( FBitSet( ent->v.flags, FL_CLIENT|FL_FAKECLIENT ))
		{
			if( sv_clienttrace.value == 0.0f )
			{
				// so no way to trace studiomodels by hitboxes
				// use bbox instead
				useComplexHull = false;
			}
			else
			{
				scale = sv_clienttrace.value * 0.5f;
				VectorSet( size, 1.0f, 1.0f, 1.0f );
			}
		}
	}

	if( FBitSet( mod->flags, STUDIO_TRACE_HITBOX ) || useComplexHull )
	{
		VectorScale( size, scale, size );
		VectorClear( offset );

		if( FBitSet( ent->v.flags, FL_CLIENT|FL_FAKECLIENT ))
		{
			studiohdr_t	*pstudio;
			mstudioseqdesc_t	*pseqdesc;
			byte		controller[4];
			byte		blending[2];
			vec3_t		angles;
			int		iBlend;

			pstudio = Mod_StudioExtradata( mod );
			pseqdesc = (mstudioseqdesc_t *)((byte *)pstudio + pstudio->seqindex) + ent->v.sequence;
			VectorCopy( ent->v.angles, angles );

			SV_StudioPlayerBlend( pseqdesc, &iBlend, &angles[PITCH] );

			controller[0] = controller[1] = 0x7F;
			controller[2] = controller[3] = 0x7F;
			blending[0] = (byte)iBlend;
			blending[1] = 0;

			hull = Mod_HullForStudio( mod, ent->v.frame, ent->v.sequence, angles, ent->v.origin, size, controller, blending, numhitboxes, ent );
		}
		else
		{
			hull = Mod_HullForStudio( mod, ent->v.frame, ent->v.sequence, ent->v.angles, ent->v.origin, size, ent->v.controller, ent->v.blending, numhitboxes, ent );
		}
	}

	if( hull ) return hull;

	*numhitboxes = 1;
	return SV_HullForEntity( ent, mins, maxs, offset );
}

/*
===============================================================================

	ENTITY LINKING

===============================================================================
*/
/*
===============
ClearLink

ClearLink is used for new headnodes
===============
*/
static void ClearLink( link_t *l )
{
	l->prev = l->next = l;
}

/*
===============
RemoveLink

remove link from chain
===============
*/
static void RemoveLink( link_t *l )
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

/*
===============
InsertLinkBefore

kept trigger and solid entities seperate
===============
*/
static void InsertLinkBefore( link_t *l, link_t *before )
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/
static int	iTouchLinkSemaphore = 0;	// prevent recursion when SV_TouchLinks is active
areanode_t	sv_areanodes[AREA_NODES];
static int	sv_numareanodes;

/*
===============
SV_CreateAreaNode

builds a uniformly subdivided tree for the given world size
===============
*/
static areanode_t *SV_CreateAreaNode( int depth, vec3_t mins, vec3_t maxs )
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1;
	vec3_t		mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes++];

	ClearLink( &anode->trigger_edicts );
	ClearLink( &anode->solid_edicts );
	ClearLink( &anode->portal_edicts );

	if( depth == AREA_DEPTH )
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}

	VectorSubtract( maxs, mins, size );
	if( size[0] > size[1] )
		anode->axis = 0;
	else anode->axis = 1;

	anode->dist = 0.5f * ( maxs[anode->axis] + mins[anode->axis] );
	VectorCopy( mins, mins1 );
	VectorCopy( mins, mins2 );
	VectorCopy( maxs, maxs1 );
	VectorCopy( maxs, maxs2 );

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;
	anode->children[0] = SV_CreateAreaNode( depth+1, mins2, maxs2 );
	anode->children[1] = SV_CreateAreaNode( depth+1, mins1, maxs1 );

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld( void )
{
	int	i;

	SV_InitBoxHull(); // for box testing

	// clear lightstyles
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		sv.lightstyles[i].value = 256.0f;
		sv.lightstyles[i].time = 0.0f;
	}

	memset( sv_areanodes, 0, sizeof( sv_areanodes ));
	iTouchLinkSemaphore = 0;
	sv_numareanodes = 0;

	SV_CreateAreaNode( 0, sv.worldmodel->mins, sv.worldmodel->maxs );
}

/*
===============
SV_UnlinkEdict
===============
*/
void SV_UnlinkEdict( edict_t *ent )
{
	// not linked in anywhere
	if( !ent->area.prev ) return;

	RemoveLink( &ent->area );
	ent->area.prev = NULL;
	ent->area.next = NULL;
}

/*
====================
SV_TouchLinks
====================
*/
static void SV_TouchLinks( edict_t *ent, areanode_t *node )
{
	link_t	*l, *next;
	edict_t	*touch;
	hull_t	*hull;
	vec3_t	test, offset;
	model_t	*mod;

	// touch linked edicts
	for( l = node->trigger_edicts.next; l != &node->trigger_edicts; l = next )
	{
		next = l->next;
		touch = EDICT_FROM_AREA( l );

		if( svgame.physFuncs.SV_TriggerTouch != NULL )
		{
			// user dll can override trigger checking (Xash3D extension)
			if( !svgame.physFuncs.SV_TriggerTouch( ent, touch ))
				continue;
		}
		else
		{
			if( touch == ent || touch->v.solid != SOLID_TRIGGER ) // disabled ?
				continue;

			if( touch->v.groupinfo && ent->v.groupinfo )
			{
				if( svs.groupop == GROUP_OP_AND && !FBitSet( touch->v.groupinfo, ent->v.groupinfo ))
					continue;

				if( svs.groupop == GROUP_OP_NAND && FBitSet( touch->v.groupinfo, ent->v.groupinfo ))
					continue;
			}

			if( !BoundsIntersect( ent->v.absmin, ent->v.absmax, touch->v.absmin, touch->v.absmax ))
				continue;

			mod = SV_ModelHandle( touch->v.modelindex );

			// check brush triggers accuracy
			if( mod && mod->type == mod_brush )
			{
				// force to select bsp-hull
				hull = SV_HullForBsp( touch, ent->v.mins, ent->v.maxs, offset );

				// support for rotational triggers
				if( FBitSet( mod->flags, MODEL_HAS_ORIGIN ) && !VectorIsNull( touch->v.angles ))
				{
					matrix4x4	matrix;
					Matrix4x4_CreateFromEntity( matrix, touch->v.angles, offset, 1.0f );
					Matrix4x4_VectorITransform( matrix, ent->v.origin, test );
				}
				else
				{
					// offset the test point appropriately for this hull.
					VectorSubtract( ent->v.origin, offset, test );
				}

				// test hull for intersection with this model
				if( PM_HullPointContents( hull, hull->firstclipnode, test ) != CONTENTS_SOLID )
					continue;
			}
		}

		// never touch the triggers when "playersonly" is active
		if( !sv.playersonly )
		{
			svgame.globals->time = sv.time;
			svgame.dllFuncs.pfnTouch( touch, ent );
		}
	}

	// recurse down both sides
	if( node->axis == -1 ) return;

	if( ent->v.absmax[node->axis] > node->dist )
		SV_TouchLinks( ent, node->children[0] );
	if( ent->v.absmin[node->axis] < node->dist )
		SV_TouchLinks( ent, node->children[1] );
}

/*
===============
SV_FindTouchedLeafs

===============
*/
static void SV_FindTouchedLeafs( edict_t *ent, model_t *mod, mnode_t *node, int *headnode )
{
	int	sides;
	mleaf_t	*leaf;

	if( node->contents == CONTENTS_SOLID )
		return;

	// add an efrag if the node is a leaf
	if( node->contents < 0 )
	{
		if( ent->num_leafs >= MAX_ENT_LEAFS( FBitSet( mod->flags, MODEL_QBSP2 )))
		{
			// continue counting leafs,
			// so we know how many it's overrun
			ent->num_leafs = (MAX_ENT_LEAFS( FBitSet( mod->flags, MODEL_QBSP2 )) + 1);
		}
		else
		{
			leaf = (mleaf_t *)node;
			if( FBitSet( mod->flags, MODEL_QBSP2 ))
				ent->leafnums32[ent->num_leafs] = leaf->cluster;
			else
				ent->leafnums16[ent->num_leafs] = leaf->cluster;
			ent->num_leafs++;
		}
		return;
	}

	// NODE_MIXED
	sides = BOX_ON_PLANE_SIDE( ent->v.absmin, ent->v.absmax, node->plane );

	if(( sides == 3 ) && ( *headnode == -1 ))
		*headnode = node - mod->nodes;

	// recurse down the contacted sides
	if( sides & 1 )
		SV_FindTouchedLeafs( ent, mod, node_child( node, 0, mod ), headnode );
	if( sides & 2 )
		SV_FindTouchedLeafs( ent, mod, node_child( node, 1, mod ), headnode );
}

/*
===============
SV_LinkEdict
===============
*/
void GAME_EXPORT SV_LinkEdict( edict_t *ent, qboolean touch_triggers )
{
	areanode_t	*node;
	int		headnode;

	if( ent->area.prev ) SV_UnlinkEdict( ent );	// unlink from old position
	if( ent == svgame.edicts ) return;		// don't add the world
	if( !SV_IsValidEdict( ent )) return;		// never add freed ents

	// set the abs box
	svgame.dllFuncs.pfnSetAbsBox( ent );

	if( ent->v.movetype == MOVETYPE_FOLLOW && SV_IsValidEdict( ent->v.aiment ))
	{
		memcpy( ent->leafnums32, ent->v.aiment->leafnums32, sizeof( ent->leafnums32 ));
		ent->num_leafs = ent->v.aiment->num_leafs;
		ent->headnode = ent->v.aiment->headnode;
	}
	else
	{
		// link to PVS leafs
		ent->num_leafs = 0;
		ent->headnode = -1;
		headnode = -1;

		if( ent->v.modelindex )
			SV_FindTouchedLeafs( ent, sv.worldmodel, sv.worldmodel->nodes, &headnode );

		if( ent->num_leafs > MAX_ENT_LEAFS( FBitSet( sv.worldmodel->flags, MODEL_QBSP2 )))
		{
			memset( ent->leafnums32, -1, sizeof( ent->leafnums32 ));
			ent->num_leafs = 0;	// so we use headnode instead
			ent->headnode = headnode;
		}
	}

	// ignore non-solid bodies
	if( ent->v.solid == SOLID_NOT && ent->v.skin >= CONTENTS_EMPTY )
		return;

	// find the first node that the ent's box crosses
	node = sv_areanodes;

	while( 1 )
	{
		if( node->axis == -1 ) break;
		if( ent->v.absmin[node->axis] > node->dist )
			node = node->children[0];
		else if( ent->v.absmax[node->axis] < node->dist )
			node = node->children[1];
		else break; // crosses the node
	}

	// link it in
	if( ent->v.solid == SOLID_TRIGGER )
		InsertLinkBefore( &ent->area, &node->trigger_edicts );
	else if( ent->v.solid == SOLID_PORTAL )
		InsertLinkBefore( &ent->area, &node->portal_edicts );
	else InsertLinkBefore( &ent->area, &node->solid_edicts );

	if( touch_triggers && !iTouchLinkSemaphore )
	{
		iTouchLinkSemaphore = true;
		SV_TouchLinks( ent, sv_areanodes );
		iTouchLinkSemaphore = false;
	}
}

/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/
static void SV_WaterLinks( const vec3_t origin, int *pCont, areanode_t *node )
{
	link_t	*l, *next;
	edict_t	*touch;
	hull_t	*hull;
	vec3_t	test, offset;
	model_t	*mod;

	// get water edicts
	for( l = node->solid_edicts.next; l != &node->solid_edicts; l = next )
	{
		next = l->next;
		touch = EDICT_FROM_AREA( l );

		if( touch->v.solid != SOLID_NOT ) // disabled ?
			continue;

		if( touch->v.groupinfo )
		{
			if( svs.groupop == GROUP_OP_AND && !FBitSet( touch->v.groupinfo, svs.groupmask ))
				continue;

			if( svs.groupop == GROUP_OP_NAND && FBitSet( touch->v.groupinfo, svs.groupmask ))
				continue;
		}

		mod = SV_ModelHandle( touch->v.modelindex );

		// only brushes can have special contents
		if( !mod || mod->type != mod_brush )
			continue;

		if( !BoundsIntersect( origin, origin, touch->v.absmin, touch->v.absmax ))
			continue;

		// check water brushes accuracy
		hull = SV_HullForBsp( touch, vec3_origin, vec3_origin, offset );

		// support for rotational water
		if( FBitSet( mod->flags, MODEL_HAS_ORIGIN ) && !VectorIsNull( touch->v.angles ))
		{
			matrix4x4	matrix;
			Matrix4x4_CreateFromEntity( matrix, touch->v.angles, offset, 1.0f );
			Matrix4x4_VectorITransform( matrix, origin, test );
		}
		else
		{
			// offset the test point appropriately for this hull.
			VectorSubtract( origin, offset, test );
		}

		// test hull for intersection with this model
		if( PM_HullPointContents( hull, hull->firstclipnode, test ) == CONTENTS_EMPTY )
			continue;

		// compare contents ranking
		if( RankForContents( touch->v.skin ) > RankForContents( *pCont ))
			*pCont = touch->v.skin; // new content has more priority
	}

	// recurse down both sides
	if( node->axis == -1 ) return;

	if( origin[node->axis] > node->dist )
		SV_WaterLinks( origin, pCont, node->children[0] );
	if( origin[node->axis] < node->dist )
		SV_WaterLinks( origin, pCont, node->children[1] );
}

/*
=============
SV_TruePointContents

=============
*/
int SV_TruePointContents( const vec3_t p )
{
	int	cont;

	// sanity check
	if( !p ) return CONTENTS_NONE;

	// get base contents from world
	cont = PM_HullPointContents( &sv.worldmodel->hulls[0], 0, p );

	// check all water entities
	SV_WaterLinks( p, &cont, sv_areanodes );

	return cont;
}

/*
=============
SV_PointContents

=============
*/
int GAME_EXPORT SV_PointContents( const vec3_t p )
{
	int cont = SV_TruePointContents( p );

	if( cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN )
		cont = CONTENTS_WATER;
	return cont;
}

/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/
/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
void SV_ClipMoveToEntity( edict_t *ent, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, trace_t *trace )
{
	hull_t	*hull;
	model_t	*model;
	vec3_t	start_l, end_l;
	vec3_t	offset, temp;
	int	last_hitgroup;
	trace_t	trace_hitbox;
	int	i, j, hullcount;
	qboolean	rotated, transform_bbox;
	matrix4x4	matrix;

	PM_InitTrace( trace, end );

	model = SV_ModelHandle( ent->v.modelindex );

	if( model && model->type == mod_studio )
	{
		hull = SV_HullForStudioModel( ent, mins, maxs, offset, &hullcount );
	}
	else
	{
		hull = SV_HullForEntity( ent, mins, maxs, offset );
		hullcount = 1;
	}

	// rotate start and end into the models frame of reference
	if(( ent->v.solid == SOLID_BSP || ent->v.solid == SOLID_PORTAL ) && !VectorIsNull( ent->v.angles ))
		rotated = true;
	else rotated = false;

	if( FBitSet( host.features, ENGINE_PHYSICS_PUSHER_EXT ))
	{
		// keep untransformed bbox less than 45 degress or train on subtransit.bsp will stop working
		if(( check_angles( ent->v.angles[0] ) || check_angles( ent->v.angles[2] )) && !VectorIsNull( mins ))
			transform_bbox = true;
		else transform_bbox = false;
	}
	else transform_bbox = false;

	if( rotated )
	{
		vec3_t	out_mins, out_maxs;

		if( transform_bbox )
			Matrix4x4_CreateFromEntity( matrix, ent->v.angles, ent->v.origin, 1.0f );
		else Matrix4x4_CreateFromEntity( matrix, ent->v.angles, offset, 1.0f );

		Matrix4x4_VectorITransform( matrix, start, start_l );
		Matrix4x4_VectorITransform( matrix, end, end_l );

		if( transform_bbox )
		{
			World_TransformAABB( matrix, mins, maxs, out_mins, out_maxs );
			VectorSubtract( hull->clip_mins, out_mins, offset ); // calc new local offset

			for( j = 0; j < 3; j++ )
			{
				if( start_l[j] >= 0.0f )
					start_l[j] -= offset[j];
				else start_l[j] += offset[j];
				if( end_l[j] >= 0.0f )
					end_l[j] -= offset[j];
				else end_l[j] += offset[j];
			}
		}
	}
	else
	{
		VectorSubtract( start, offset, start_l );
		VectorSubtract( end, offset, end_l );
	}

	if( hullcount == 1 )
	{
		PM_RecursiveHullCheck( hull, hull->firstclipnode, 0.0f, 1.0f, start_l, end_l, (pmtrace_t *)trace );
	}
	else
	{
		last_hitgroup = 0;

		for( i = 0; i < hullcount; i++ )
		{
			PM_InitTrace( &trace_hitbox, end );

			PM_RecursiveHullCheck( &hull[i], hull[i].firstclipnode, 0.0f, 1.0f, start_l, end_l, (pmtrace_t *)&trace_hitbox );

			if( i == 0 || trace_hitbox.allsolid || trace_hitbox.startsolid || trace_hitbox.fraction < trace->fraction )
			{
				if( trace->startsolid )
				{
					*trace = trace_hitbox;
					trace->startsolid = true;
				}
				else *trace = trace_hitbox;

				last_hitgroup = i;
			}
		}

		trace->hitgroup = Mod_HitgroupForStudioHull( last_hitgroup );
	}

	if( trace->fraction != 1.0f )
	{
		// compute endpos (generic case)
		VectorLerp( start, trace->fraction, end, trace->endpos );

		if( rotated )
		{
			// transform plane
			VectorCopy( trace->plane.normal, temp );
			Matrix4x4_TransformPositivePlane( matrix, temp, trace->plane.dist, trace->plane.normal, &trace->plane.dist );
		}
		else
		{
			trace->plane.dist = DotProduct( trace->endpos, trace->plane.normal );
		}
	}

	if( trace->fraction < 1.0f || trace->startsolid )
		trace->ent = ent;
}

/*
==================
SV_PortalCSG

a portal is flush with a world surface behind it. this causes problems. namely that we can't pass through the portal plane
if the bsp behind it prevents out origin from getting through. so if the trace was clipped and ended infront of the portal,
continue the trace to the edges of the portal cutout instead.
==================
*/
static void SV_PortalCSG( edict_t *portal, const vec3_t trace_mins, const vec3_t trace_maxs, const vec3_t start, const vec3_t end, trace_t *trace )
{
	vec4_t	planes[6];	//far, near, right, left, up, down
	int	plane, k;
	vec3_t	worldpos;
	float	bestfrac;
	int	hitplane;
	model_t	*model;
	float	portalradius;

	// only run this code if we impacted on the portal's parent.
	if( trace->fraction == 1.0f && !trace->startsolid )
		return;

	// decide which clipping hull to use, based on the size
	model = SV_ModelHandle( portal->v.modelindex );

	if( !model || model->type != mod_brush )
		return;

	// make sure we use a sane valid position.
	if( trace->startsolid ) VectorCopy( start, worldpos );
	else VectorCopy( trace->endpos, worldpos );

	// determine the csg area. normals should be facing in
	AngleVectors( portal->v.angles, planes[1], planes[3], planes[5] );
	VectorNegate(planes[1], planes[0]);
	VectorNegate(planes[3], planes[2]);
	VectorNegate(planes[5], planes[4]);

	portalradius = model->radius * 0.5f;
	planes[0][3] = DotProduct( portal->v.origin, planes[0] ) - (4.0f / 32.0f);
	planes[1][3] = DotProduct( portal->v.origin, planes[1] ) - (4.0f / 32.0f);	//an epsilon beyond the portal
	planes[2][3] = DotProduct( portal->v.origin, planes[2] ) - portalradius;
	planes[3][3] = DotProduct( portal->v.origin, planes[3] ) - portalradius;
	planes[4][3] = DotProduct( portal->v.origin, planes[4] ) - portalradius;
	planes[5][3] = DotProduct( portal->v.origin, planes[5] ) - portalradius;

	// if we're actually inside the csg region
	for( plane = 0; plane < 6; plane++ )
	{
		float	d = DotProduct( worldpos, planes[plane] );
		vec3_t	nearest;

		for( k = 0; k < 3; k++ )
			nearest[k] = (planes[plane][k]>=0) ? trace_maxs[k] : trace_mins[k];

		// front plane gets further away with side
		if( !plane )
		{
			planes[plane][3] -= DotProduct( nearest, planes[plane] );
		}
		else if( plane > 1 )
		{
			// side planes get nearer with size
			planes[plane][3] += 24; // DotProduct( nearest, planes[plane] );
		}

		if( d - planes[plane][3] >= 0 )
			continue;	// endpos is inside
		else return; // end is already outside
	}

	// yup, we're inside, the trace shouldn't end where it actually did
	bestfrac = 1;
	hitplane = -1;

	for( plane = 0; plane < 6; plane++ )
	{
		float	ds = DotProduct( start, planes[plane] ) - planes[plane][3];
		float	de = DotProduct( end, planes[plane] ) - planes[plane][3];
		float	frac;

		if( ds >= 0 && de < 0 )
		{
			frac = (ds) / (ds - de);
			if( frac < bestfrac )
			{
				if( frac < 0 )
					frac = 0;
				bestfrac = frac;
				hitplane = plane;
			}
		}
	}

	trace->startsolid = trace->allsolid = false;

	// if we cross the front of the portal, don't shorten the trace,
	// that will artificially clip us
	if( hitplane == 0 && trace->fraction > bestfrac )
		return;

	// okay, elongate to clip to the portal hole properly.
	VectorLerp( start, bestfrac, end, trace->endpos );
	trace->fraction = bestfrac;

	if( hitplane >= 0 )
	{
		VectorCopy( planes[hitplane], trace->plane.normal );
		trace->plane.dist = planes[hitplane][3];
		if( hitplane == 1 ) trace->ent = portal;
	}
}

/*
==================
SV_CustomClipMoveToEntity

A part of physics engine implementation
or custom physics implementation
==================
*/
void SV_CustomClipMoveToEntity( edict_t *ent, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, trace_t *trace )
{
	// initialize custom trace
	PM_InitTrace( trace, end );

	if( svgame.physFuncs.ClipMoveToEntity != NULL )
	{
		// do custom sweep test
		svgame.physFuncs.ClipMoveToEntity( ent, start, mins, maxs, end, trace );
	}
	else
	{
		// function is missed, so we didn't hit anything
		trace->allsolid = false;
	}
}

/*
====================
SV_ClipToEntity

generic clip function
====================
*/
static qboolean SV_ClipToEntity( edict_t *touch, moveclip_t *clip )
{
	trace_t	trace;
	model_t	*mod;

	if( touch->v.groupinfo && SV_IsValidEdict( clip->passedict ) && clip->passedict->v.groupinfo != 0 )
	{
		if( svs.groupop == GROUP_OP_AND && !FBitSet( touch->v.groupinfo, clip->passedict->v.groupinfo ))
			return true;

		if( svs.groupop == GROUP_OP_NAND && FBitSet( touch->v.groupinfo, clip->passedict->v.groupinfo ))
			return true;
	}

	if( touch == clip->passedict || touch->v.solid == SOLID_NOT )
		return true;

	if( touch->v.solid == SOLID_TRIGGER )
		Host_Error( "trigger in clipping list\n" );

	// custom user filter
	if( svgame.dllFuncs2.pfnShouldCollide )
	{
		if( !svgame.dllFuncs2.pfnShouldCollide( touch, clip->passedict ))
			return true;
	}

	// monsterclip filter (solid custom is a static or dynamic bodies)
	if( touch->v.solid == SOLID_BSP || touch->v.solid == SOLID_CUSTOM )
	{
		// func_monsterclip works only with monsters that have same flag!
		if( FBitSet( touch->v.flags, FL_MONSTERCLIP ) && !clip->monsterclip )
			return true;
	}
	else
	{
		// ignore all monsters but pushables
		if( clip->type == MOVE_NOMONSTERS && touch->v.movetype != MOVETYPE_PUSHSTEP )
			return true;
	}

	mod = SV_ModelHandle( touch->v.modelindex );

	if( mod && mod->type == mod_brush && clip->ignoretrans )
	{
		// we ignore brushes with rendermode != kRenderNormal and without FL_WORLDBRUSH set
		if( touch->v.rendermode != kRenderNormal && !FBitSet( touch->v.flags, FL_WORLDBRUSH ))
			return true;
	}

	if( !BoundsIntersect( clip->boxmins, clip->boxmaxs, touch->v.absmin, touch->v.absmax ))
		return true;

	// aditional check to intersects clients with sphere
	if( touch->v.solid != SOLID_SLIDEBOX && !SV_CheckSphereIntersection( touch, clip->start, clip->end ))
		return true;

	// Xash3D extension
	if( SV_IsValidEdict( clip->passedict ) && clip->passedict->v.solid == SOLID_TRIGGER )
	{
		// never collide items and player (because call "give" always stuck item in player
		// and total trace returns fail (old half-life bug)
		// items touch should be done in SV_TouchLinks not here
		if( FBitSet( touch->v.flags, FL_CLIENT|FL_FAKECLIENT ))
			return true;
	}

	// g-cont. make sure what size is really zero - check all the components
	if( SV_IsValidEdict( clip->passedict ) && !VectorIsNull( clip->passedict->v.size ) && VectorIsNull( touch->v.size ))
		return true; // points never interact

	// might intersect, so do an exact clip
	if( clip->trace.allsolid ) return false;

	if( SV_IsValidEdict( clip->passedict ))
	{
	 	if( touch->v.owner == clip->passedict )
			return true; // don't clip against own missiles
		if( clip->passedict->v.owner == touch )
			return true; // don't clip against owner
	}

	// make sure we don't hit the world if we're inside the portal
	if( touch->v.solid == SOLID_PORTAL )
		SV_PortalCSG( touch, clip->mins, clip->maxs, clip->start, clip->end, &clip->trace );

	if( touch->v.solid == SOLID_CUSTOM )
		SV_CustomClipMoveToEntity( touch, clip->start, clip->mins, clip->maxs, clip->end, &trace );
	else if( FBitSet( touch->v.flags, FL_MONSTER ))
		SV_ClipMoveToEntity( touch, clip->start, clip->mins2, clip->maxs2, clip->end, &trace );
	else SV_ClipMoveToEntity( touch, clip->start, clip->mins, clip->maxs, clip->end, &trace );

	clip->trace = World_CombineTraces( &clip->trace, &trace, touch );

	return true;
}

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
static void SV_ClipToLinks( areanode_t *node, moveclip_t *clip )
{
	link_t	*l, *next;
	edict_t	*touch;

	// touch linked edicts
	for( l = node->solid_edicts.next; l != &node->solid_edicts; l = next )
	{
		next = l->next;

		touch = EDICT_FROM_AREA( l );

		if( !SV_ClipToEntity( touch, clip ))
			return; // trace.allsoild
	}

	// recurse down both sides
	if( node->axis == -1 ) return;

	if( clip->boxmaxs[node->axis] > node->dist )
		SV_ClipToLinks( node->children[0], clip );
	if( clip->boxmins[node->axis] < node->dist )
		SV_ClipToLinks( node->children[1], clip );
}

/*
====================
SV_ClipToPortals

Mins and maxs enclose the entire area swept by the move
====================
*/
static void SV_ClipToPortals( areanode_t *node, moveclip_t *clip )
{
	link_t	*l, *next;
	edict_t	*touch;

	// touch linked edicts
	for( l = node->portal_edicts.next; l != &node->portal_edicts; l = next )
	{
		next = l->next;

		touch = EDICT_FROM_AREA( l );

		if( !SV_ClipToEntity( touch, clip ))
			return; // trace.allsoild
	}

	// recurse down both sides
	if( node->axis == -1 ) return;

	if( clip->boxmaxs[node->axis] > node->dist )
		SV_ClipToPortals( node->children[0], clip );
	if( clip->boxmins[node->axis] < node->dist )
		SV_ClipToPortals( node->children[1], clip );
}

/*
====================
SV_ClipToWorldBrush

Mins and maxs enclose the entire area swept by the move
====================
*/
static void SV_ClipToWorldBrush( areanode_t *node, moveclip_t *clip )
{
	link_t	*l, *next;
	edict_t	*touch;
	trace_t	trace;

	for( l = node->solid_edicts.next; l != &node->solid_edicts; l = next )
	{
		next = l->next;

		touch = EDICT_FROM_AREA( l );

		if( touch->v.solid != SOLID_BSP || touch == clip->passedict || !( touch->v.flags & FL_WORLDBRUSH ))
			continue;

		if( !BoundsIntersect( clip->boxmins, clip->boxmaxs, touch->v.absmin, touch->v.absmax ))
			continue;

		if( clip->trace.allsolid ) return;

		SV_ClipMoveToEntity( touch, clip->start, clip->mins, clip->maxs, clip->end, &trace );

		clip->trace = World_CombineTraces( &clip->trace, &trace, touch );
	}

	// recurse down both sides
	if( node->axis == -1 ) return;

	if( clip->boxmaxs[node->axis] > node->dist )
		SV_ClipToWorldBrush( node->children[0], clip );

	if( clip->boxmins[node->axis] < node->dist )
		SV_ClipToWorldBrush( node->children[1], clip );
}

/*
==================
SV_Move
==================
*/
trace_t SV_Move( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e, qboolean monsterclip )
{
	moveclip_t clip = { 0 };

	SV_ClipMoveToEntity( EDICT_NUM( 0 ), start, mins, maxs, end, &clip.trace );

	if( clip.trace.fraction != 0.0f )
	{
		const float trace_fraction = clip.trace.fraction;
		vec3_t trace_endpos;
		VectorCopy( clip.trace.endpos, trace_endpos );

		clip.trace.fraction = 1.0f;
		clip.start = start;
		clip.end = trace_endpos;
		clip.type = (type & 0xFF);
		clip.ignoretrans = type >> 8;
		clip.monsterclip = false;
		clip.passedict = (e) ? e : EDICT_NUM( 0 );
		clip.mins = mins;
		clip.maxs = maxs;

		if( monsterclip && !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
			clip.monsterclip = true;

		if( clip.type == MOVE_MISSILE )
		{
			VectorSet( clip.mins2, -15.0f, -15.0f, -15.0f );
			VectorSet( clip.maxs2,  15.0f,  15.0f,  15.0f );
		}
		else
		{
			VectorCopy( mins, clip.mins2 );
			VectorCopy( maxs, clip.maxs2 );
		}

		World_MoveBounds( start, clip.mins2, clip.maxs2, trace_endpos, clip.boxmins, clip.boxmaxs );
		SV_ClipToLinks( sv_areanodes, &clip );
		SV_ClipToPortals( sv_areanodes, &clip );

		clip.trace.fraction *= trace_fraction;
		svgame.globals->trace_ent = clip.trace.ent;
	}

	SV_CopyTraceToGlobal( &clip.trace );

	return clip.trace;
}

/*
==================
SV_MoveNoEnts
==================
*/
trace_t GAME_EXPORT SV_MoveNoEnts( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e )
{
	moveclip_t	clip;
	vec3_t		trace_endpos;
	float		trace_fraction;

	memset( &clip, 0, sizeof( moveclip_t ));
	SV_ClipMoveToEntity( EDICT_NUM( 0 ), start, mins, maxs, end, &clip.trace );

	if( clip.trace.fraction != 0.0f )
	{
		VectorCopy( clip.trace.endpos, trace_endpos );
		trace_fraction = clip.trace.fraction;
		clip.trace.fraction = 1.0f;
		clip.start = start;
		clip.end = trace_endpos;
		clip.type = (type & 0xFF);
		clip.ignoretrans = type >> 8;
		clip.monsterclip = false;
		clip.passedict = (e) ? e : EDICT_NUM( 0 );
		clip.mins = mins;
		clip.maxs = maxs;

		VectorCopy( mins, clip.mins2 );
		VectorCopy( maxs, clip.maxs2 );

		World_MoveBounds( start, clip.mins2, clip.maxs2, trace_endpos, clip.boxmins, clip.boxmaxs );
		SV_ClipToWorldBrush( sv_areanodes, &clip );
		SV_ClipToPortals( sv_areanodes, &clip );

		clip.trace.fraction *= trace_fraction;
		svgame.globals->trace_ent = clip.trace.ent;
	}

	SV_CopyTraceToGlobal( &clip.trace );

	return clip.trace;
}

/*
==================
SV_TraceSurface

find the face where the traceline hit
assume pTextureEntity is valid
==================
*/
msurface_t *GAME_EXPORT SV_TraceSurface( edict_t *ent, const vec3_t start, const vec3_t end )
{
	matrix4x4		matrix;
	model_t		*bmodel;
	hull_t		*hull;
	vec3_t		start_l, end_l;
	vec3_t		offset;

	bmodel = SV_ModelHandle( ent->v.modelindex );
	if( !bmodel || bmodel->type != mod_brush )
		return NULL;

	hull = SV_HullForBsp( ent, vec3_origin, vec3_origin, offset );

	VectorSubtract( start, offset, start_l );
	VectorSubtract( end, offset, end_l );

	// rotate start and end into the models frame of reference
	if( !VectorIsNull( ent->v.angles ))
	{
		Matrix4x4_CreateFromEntity( matrix, ent->v.angles, offset, 1.0f );
		Matrix4x4_VectorITransform( matrix, start, start_l );
		Matrix4x4_VectorITransform( matrix, end, end_l );
	}

	return PM_RecursiveSurfCheck( bmodel, &bmodel->nodes[hull->firstclipnode], start_l, end_l );
}

/*
==================
SV_TraceTexture

find the face where the traceline hit
assume pTextureEntity is valid
==================
*/
const char *SV_TraceTexture( edict_t *ent, const vec3_t start, const vec3_t end )
{
	msurface_t	*surf = SV_TraceSurface( ent, start, end );

	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return NULL;

	return surf->texinfo->texture->name;
}

/*
==================
SV_MoveToss
==================
*/
trace_t SV_MoveToss( edict_t *tossent, edict_t *ignore )
{
	float 	gravity;
	vec3_t	move, end;
	vec3_t	original_origin;
	vec3_t	original_velocity;
	vec3_t	original_angles;
	vec3_t	original_avelocity;
	trace_t	trace;
	int	i;

	VectorCopy( tossent->v.origin, original_origin );
	VectorCopy( tossent->v.velocity, original_velocity );
	VectorCopy( tossent->v.angles, original_angles );
	VectorCopy( tossent->v.avelocity, original_avelocity );
	gravity = tossent->v.gravity * sv_gravity.value * 0.05f;

	for( i = 0; i < 200; i++ )
	{
		SV_CheckVelocity( tossent );
		tossent->v.velocity[2] -= gravity;
		VectorMA( tossent->v.angles, 0.05f, tossent->v.avelocity, tossent->v.angles );
		VectorScale( tossent->v.velocity, 0.05f, move );
		VectorAdd( tossent->v.origin, move, end );
		trace = SV_Move( tossent->v.origin, tossent->v.mins, tossent->v.maxs, end, MOVE_NORMAL, tossent, false );
		VectorCopy( trace.endpos, tossent->v.origin );
		if( trace.fraction < 1.0f ) break;
	}

	VectorCopy( original_origin, tossent->v.origin );
	VectorCopy( original_velocity, tossent->v.velocity );
	VectorCopy( original_angles, tossent->v.angles );
	VectorCopy( original_avelocity, tossent->v.avelocity );

	return trace;
}

/*
===============================================================================

	LIGHTING INFO

===============================================================================
*/

/*
=================
SV_RecursiveLightPoint
=================
*/
static qboolean SV_RecursiveLightPoint( model_t *model, mnode_t *node, const vec3_t start, const vec3_t end, vec3_t point_color )
{
	float front, back, frac;
	int i, side;
	vec3_t mid;
	mnode_t *children[2];
	int numsurfaces, firstsurface;

	// didn't hit anything
	if( !node || node->contents < 0 )
		return false;

	// calculate mid point
	front = PlaneDiff( start, node->plane );
	back = PlaneDiff( end, node->plane );

	node_children( children, node, model );

	side = front < 0.0f;
	if(( back < 0.0f ) == side )
		return SV_RecursiveLightPoint( model, children[side], start, end, point_color );

	frac = front / ( front - back );

	VectorLerp( start, frac, end, mid );

	// co down front side
	if( SV_RecursiveLightPoint( model, children[side], start, mid, point_color ))
		return true; // hit something

	if(( back < 0.0f ) == side )
		return false; // didn't hit anything

	// check for impact on this node
	numsurfaces = node_numsurfaces( node, model );
	firstsurface = node_firstsurface( node, model );
	for( i = 0; i < numsurfaces; i++ )
	{
		const msurface_t *surf = &model->surfaces[firstsurface + i];
		const mextrasurf_t *info = surf->info;
		int smax, tmax, map, size;
		int sample_size;
		float ds, dt, s, t;
		const color24 *lm;

		if( FBitSet( surf->flags, SURF_DRAWTILED ))
			continue;	// no lightmaps

		s = DotProduct( mid, info->lmvecs[0] ) + info->lmvecs[0][3];
		t = DotProduct( mid, info->lmvecs[1] ) + info->lmvecs[1][3];

		if( s < info->lightmapmins[0] || t < info->lightmapmins[1] )
			continue;

		ds = s - info->lightmapmins[0];
		dt = t - info->lightmapmins[1];

		if ( ds > info->lightextents[0] || dt > info->lightextents[1] )
			continue;

		if( !surf->samples )
			return true;

		sample_size = Mod_SampleSizeForFace( surf );
		smax = (info->lightextents[0] / sample_size) + 1;
		tmax = (info->lightextents[1] / sample_size) + 1;
		ds /= sample_size;
		dt /= sample_size;

		VectorClear( point_color );

		lm = surf->samples + Q_rint( dt ) * smax + Q_rint( ds );
		size = smax * tmax;

		for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
		{
			float scale = sv.lightstyles[surf->styles[map]].value;

			point_color[0] += lm->r * scale;
			point_color[1] += lm->g * scale;
			point_color[2] += lm->b * scale;

			lm += size; // skip to next lightmap
		}

		return true;
	}

	// go down back side
	return SV_RecursiveLightPoint( model, children[!side], mid, end, point_color );
}

/*
==================
SV_SetLightStyle

needs to get correct working SV_LightPoint
==================
*/
void SV_SetLightStyle( int style, const char* s, float f )
{
	int	j, k;

	j = Q_strncpy( sv.lightstyles[style].pattern, s, sizeof( sv.lightstyles[0].pattern ));
	sv.lightstyles[style].time = f;
	sv.lightstyles[style].length = j;

	for( k = 0; k < j; k++ )
		sv.lightstyles[style].map[k] = (float)(s[k] - 'a');

	if( sv.state != ss_active ) return;

	// tell the clients about changed lightstyle
	MSG_BeginServerCmd( &sv.reliable_datagram, svc_lightstyle );
	MSG_WriteByte( &sv.reliable_datagram, style );
	MSG_WriteString( &sv.reliable_datagram, sv.lightstyles[style].pattern );
	MSG_WriteFloat( &sv.reliable_datagram, sv.lightstyles[style].time );
}

/*
==================
SV_LightForEntity

grab the ambient lighting color for current point
==================
*/
int SV_LightForEntity( edict_t *pEdict )
{
	vec3_t point_color = { 1.0f, 1.0f, 1.0f };
	vec3_t start, end;

	if( !SV_IsValidEdict( pEdict ))
		return -1;

	if( FBitSet( pEdict->v.effects, EF_FULLBRIGHT ) || !sv.worldmodel->lightdata )
		return 255;

	// player has more precise light level that come from client-side
	if( FBitSet( pEdict->v.flags, FL_CLIENT ))
		return pEdict->v.light_level;

	VectorCopy( pEdict->v.origin, start );
	VectorCopy( pEdict->v.origin, end );

	if( FBitSet( pEdict->v.effects, EF_INVLIGHT ))
		end[2] = start[2] + world.size[2];
	else end[2] = start[2] - world.size[2];

	SV_RecursiveLightPoint( sv.worldmodel, sv.worldmodel->nodes, start, end, point_color );

	return VectorAvg( point_color );
}
