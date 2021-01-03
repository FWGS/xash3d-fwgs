/*
world.h - shared world routines
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

#ifndef WORLD_H
#define WORLD_H

#define MOVE_NORMAL		0	// normal trace
#define MOVE_NOMONSTERS	1	// ignore monsters (edicts with flags (FL_MONSTER|FL_FAKECLIENT|FL_CLIENT) set)
#define MOVE_MISSILE	2	// extra size for monsters

#define CONTENTS_NONE	0	// no custom contents specified

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/
#define MAX_TOTAL_ENT_LEAFS		128
#define AREA_NODES			32
#define AREA_DEPTH			4

#include "lightstyle.h"

extern const char		*et_name[];

// linked list
void InsertLinkBefore( link_t *l, link_t *before );
void RemoveLink( link_t *l );
void ClearLink( link_t *l );

// trace common
void World_MoveBounds( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, vec3_t boxmins, vec3_t boxmaxs );
void World_TransformAABB( matrix4x4 transform, const vec3_t mins, const vec3_t maxs, vec3_t outmins, vec3_t outmaxs );
trace_t World_CombineTraces( trace_t *cliptrace, trace_t *trace, edict_t *touch );
int RankForContents( int contents );

#define check_angles( x )	( (int)x == 90 || (int)x == 180 || (int)x == 270 || (int)x == -90 || (int)x == -180 || (int)x == -270 )

/*
===============================================================================

	EVENTS QUEUE (hl1 events code)

===============================================================================
*/
#include "event_api.h"
#include "event_args.h"

#define MAX_EVENT_QUEUE	64		// 16 simultaneous events, max

typedef struct event_info_s
{
	word		index;		// 0 implies not in use
	short		packet_index;	// Use data from state info for entity in delta_packet .
					// -1 implies separate info based on event
					// parameter signature
	short		entity_index;	// The edict this event is associated with
	float		fire_time;	// if non-zero, the time when the event should be fired
					// ( fixed up on the client )
	event_args_t	args;
	int		flags;		// reliable or not, etc. ( CLIENT ONLY )
} event_info_t;

typedef struct event_state_s
{
	event_info_t	ei[MAX_EVENT_QUEUE];
} event_state_t;

#endif//WORLD_H
