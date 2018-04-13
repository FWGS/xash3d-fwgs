/*
world.c - common worldtrace routines
Copyright (C) 2009 Uncle Mike

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
#include "world.h"
#include "pm_defs.h"
#include "mod_local.h"
#include "mathlib.h"
#include "studio.h"

// just for debug
const char *et_name[] =
{
	"normal",
	"player",
	"tempentity",
	"beam",
	"fragmented",
};

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
void ClearLink( link_t *l )
{
	l->prev = l->next = l;
}

/*
===============
RemoveLink

remove link from chain
===============
*/
void RemoveLink( link_t *l )
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
void InsertLinkBefore( link_t *l, link_t *before )
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

/*
==================
World_MoveBounds
==================
*/
void World_MoveBounds( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, vec3_t boxmins, vec3_t boxmaxs )
{
	int	i;
	
	for( i = 0; i < 3; i++ )
	{
		if( end[i] > start[i] )
		{
			boxmins[i] = start[i] + mins[i] - 1.0f;
			boxmaxs[i] = end[i] + maxs[i] + 1.0f;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1.0f;
			boxmaxs[i] = start[i] + maxs[i] + 1.0f;
		}
	}
}

trace_t World_CombineTraces( trace_t *cliptrace, trace_t *trace, edict_t *touch )
{
	if( trace->allsolid || trace->startsolid || trace->fraction < cliptrace->fraction )
	{
		trace->ent = touch;
		
		if( cliptrace->startsolid )
		{
			*cliptrace = *trace;
			cliptrace->startsolid = true;
		}
		else *cliptrace = *trace;
	}

	return *cliptrace;
}

/*
==================
World_TransformAABB
==================
*/
void World_TransformAABB( matrix4x4 transform, const vec3_t mins, const vec3_t maxs, vec3_t outmins, vec3_t outmaxs )
{
	vec3_t	p1, p2;
	matrix4x4	itransform;
	int	i;

	if( !outmins || !outmaxs ) return;

	Matrix4x4_Invert_Simple( itransform, transform );
	ClearBounds( outmins, outmaxs );

	// compute a full bounding box
	for( i = 0; i < 8; i++ )
	{
		p1[0] = ( i & 1 ) ? mins[0] : maxs[0];
		p1[1] = ( i & 2 ) ? mins[1] : maxs[1];
		p1[2] = ( i & 4 ) ? mins[2] : maxs[2];

		p2[0] = DotProduct( p1, itransform[0] );
		p2[1] = DotProduct( p1, itransform[1] );
		p2[2] = DotProduct( p1, itransform[2] );

		if( p2[0] < outmins[0] ) outmins[0] = p2[0];
		if( p2[0] > outmaxs[0] ) outmaxs[0] = p2[0];
		if( p2[1] < outmins[1] ) outmins[1] = p2[1];
		if( p2[1] > outmaxs[1] ) outmaxs[1] = p2[1];
		if( p2[2] < outmins[2] ) outmins[2] = p2[2];
		if( p2[2] > outmaxs[2] ) outmaxs[2] = p2[2];
	}

	// sanity check
	for( i = 0; i < 3; i++ )
	{
		if( outmins[i] > outmaxs[i] )
		{
			MsgDev( D_ERROR, "World_TransformAABB: backwards mins/maxs\n" );
			VectorClear( outmins );
			VectorClear( outmaxs );
			return;
		}
	}
}

/*
==================
RankForContents

Used for determine contents priority
==================
*/
int RankForContents( int contents )
{
	switch( contents )
	{
	case CONTENTS_EMPTY:	return 0;
	case CONTENTS_WATER:	return 1;
	case CONTENTS_TRANSLUCENT:	return 2;
	case CONTENTS_CURRENT_0:	return 3;
	case CONTENTS_CURRENT_90:	return 4;
	case CONTENTS_CURRENT_180:	return 5;
	case CONTENTS_CURRENT_270:	return 6;
	case CONTENTS_CURRENT_UP:	return 7;
	case CONTENTS_CURRENT_DOWN:	return 8;
	case CONTENTS_SLIME:	return 9;
	case CONTENTS_LAVA:		return 10;
	case CONTENTS_SKY:		return 11;
	case CONTENTS_SOLID:	return 12;
	default:			return 13; // any user contents has more priority than default
	}
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