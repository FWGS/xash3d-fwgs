/*
cl_events.c - client-side event system implementation
Copyright (C) 2011 Uncle Mike

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
#include "event_flags.h"
#include "net_encode.h"
#include "con_nprint.h"

/*
===============
CL_ResetEvent

===============
*/
void CL_ResetEvent( event_info_t *ei )
{
	ei->index = 0;
	memset( &ei->args, 0, sizeof( ei->args ));
	ei->fire_time = 0.0;
	ei->flags = 0;
}

/*
=============
CL_CalcPlayerVelocity

compute velocity for a given client
=============
*/
void CL_CalcPlayerVelocity( int idx, vec3_t velocity )
{
	clientdata_t	*pcd;
	vec3_t		delta;
	double		dt;

	VectorClear( velocity );

	if( idx <= 0 || idx > cl.maxclients )
		return;

	if( idx == cl.playernum + 1 )
	{
		pcd = &cl.frames[cl.parsecountmod].clientdata;
		VectorCopy( pcd->velocity, velocity );
	}
	else
	{
		dt = clgame.entities[idx].curstate.animtime - clgame.entities[idx].prevstate.animtime;

		if( dt != 0.0 )
		{
			VectorSubtract( clgame.entities[idx].curstate.velocity, clgame.entities[idx].prevstate.velocity, delta );
			VectorScale( delta, 1.0f / dt, velocity );
		}
		else
		{
			VectorCopy( clgame.entities[idx].curstate.velocity, velocity );
		}
	}
}

/*
=============
CL_DescribeEvent

=============
*/
void CL_DescribeEvent( event_info_t *ei, int slot )
{
	int		idx = (slot & 63) * 2;
	con_nprint_t	info;
	string origin_str = { 0 }; //, angles_str = { 0 };

	if( !cl_showevents->value )
		return;

	info.time_to_live = 1.0f;
	info.index = idx;

	// mark reliable as green and unreliable as red
	if( FBitSet( ei->flags, FEV_RELIABLE ))
		VectorSet( info.color, 0.5f, 1.0f, 0.5f );
	else VectorSet( info.color, 1.0f, 0.5f, 0.5f );

	if( !VectorIsNull( ei->args.origin ))
	{
		Q_snprintf( origin_str, sizeof( origin_str ), "(%.2f,%.2f,%.2f)",
			ei->args.origin[0], ei->args.origin[1], ei->args.origin[2]);
	}

	/*if( !VectorIsNull( ei->args.angles ))
	{
		Q_snprintf( angles_str, sizeof( angles_str ), "ang %.2f %.2f %.2f",
			ei->args.angles[0], ei->args.angles[1], ei->args.angles[2]);
	}*/

	Con_NXPrintf( &info, "%i %.2f %c %s %s",
		slot, cl.time,
		(FBitSet( ei->flags, FEV_CLIENT ) ? 'c' :
		FBitSet( ei->flags, FEV_SERVER ) ? 's' : '?'),
		cl.event_precache[ei->index],
		origin_str);

	info.index++;

	Con_NXPrintf( &info, "b(%i,%i) i(%i,%i) f(%.2f,%.2f)",
		ei->args.bparam1, ei->args.bparam2,
		ei->args.iparam1, ei->args.iparam2,
		ei->args.fparam1, ei->args.fparam2);
}

/*
=============
CL_SetEventIndex

=============
*/
void CL_SetEventIndex( const char *szEvName, int ev_index )
{
	cl_user_event_t	*ev;
	int		i;

	if( !szEvName || !*szEvName )
		return; // ignore blank names

	// search event by name to link with
	for( i = 0; i < MAX_EVENTS; i++ )
	{
		ev = clgame.events[i];
		if( !ev ) break;

		if( !Q_stricmp( ev->name, szEvName ))
		{
			ev->index = ev_index;
			return;
		}
	}
}

/*
=============
CL_EventIndex

=============
*/
word CL_EventIndex( const char *name )
{
	word	i;

	if( !COM_CheckString( name ))
		return 0;

	for( i = 1; i < MAX_EVENTS && cl.event_precache[i][0]; i++ )
	{
		if( !Q_stricmp( cl.event_precache[i], name ))
			return i;
	}
	return 0;
}

/*
=============
CL_RegisterEvent

=============
*/
void CL_RegisterEvent( int lastnum, const char *szEvName, pfnEventHook func )
{
	cl_user_event_t	*ev;

	if( lastnum == MAX_EVENTS )
		return;

	// clear existing or allocate new one
	if( !clgame.events[lastnum] )
		clgame.events[lastnum] = Mem_Calloc( cls.mempool, sizeof( cl_user_event_t ));
	else memset( clgame.events[lastnum], 0, sizeof( cl_user_event_t ));

	ev = clgame.events[lastnum];

	// NOTE: ev->index will be set later
	Q_strncpy( ev->name, szEvName, MAX_QPATH );
	ev->func = func;
}

/*
=============
CL_FireEvent

=============
*/
qboolean CL_FireEvent( event_info_t *ei, int slot )
{
	cl_user_event_t	*ev;
	const char	*name;
	int		i, idx;

	if( !ei || !ei->index )
		return false;

	// get the func pointer
	for( i = 0; i < MAX_EVENTS; i++ )
	{
		ev = clgame.events[i];

		if( !ev )
		{
			idx = bound( 1, ei->index, ( MAX_EVENTS - 1 ));
			Con_Reportf( S_ERROR "CL_FireEvent: %s not precached\n", cl.event_precache[idx] );
			break;
		}

		if( ev->index == ei->index )
		{
			if( ev->func )
			{
				CL_DescribeEvent( ei, slot );
				ev->func( &ei->args );
				return true;
			}

			name = cl.event_precache[ei->index];
			Con_Reportf( S_ERROR "CL_FireEvent: %s not hooked\n", name );
			break;
		}
	}

	return false;
}

/*
=============
CL_FireEvents

called right before draw frame
=============
*/
void CL_FireEvents( void )
{
	event_state_t	*es;
	event_info_t	*ei;
	int		i;

	es = &cl.events;

	for( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		ei = &es->ei[i];

		if( ei->index == 0 )
			continue;

		// delayed event!
		if( ei->fire_time && ( ei->fire_time > cl.time ))
			continue;

		CL_FireEvent( ei, i );

		// zero out the remaining fields
		CL_ResetEvent( ei );
	}
}

/*
=============
CL_FindEvent

find first empty event
=============
*/
event_info_t *CL_FindEmptyEvent( void )
{
	int		i;
	event_state_t	*es;
	event_info_t	*ei;

	es = &cl.events;

	// look for first slot where index is != 0
	for( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		ei = &es->ei[i];
		if( ei->index != 0 )
			continue;
		return ei;
	}

	// no slots available
	return NULL;
}

/*
=============
CL_FindEvent

replace only unreliable events
=============
*/
event_info_t *CL_FindUnreliableEvent( void )
{
	event_state_t	*es;
	event_info_t	*ei;
	int		i;

	es = &cl.events;

	for ( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		ei = &es->ei[i];
		if( ei->index != 0 )
		{
			// it's reliable, so skip it
			if( FBitSet( ei->flags, FEV_RELIABLE ))
				continue;
		}
		return ei;
	}

	// this should never happen
	return NULL;
}

/*
=============
CL_QueueEvent

=============
*/
void CL_QueueEvent( int flags, int index, float delay, event_args_t *args )
{
	event_info_t	*ei;

	// find a normal slot
	ei = CL_FindEmptyEvent();

	if( !ei )
	{
		if( FBitSet( flags, FEV_RELIABLE ))
		{
			ei = CL_FindUnreliableEvent();
		}

		if( !ei ) return;
	}

	ei->index	= index;
	ei->packet_index = 0;
	ei->fire_time = delay ? (cl.time + delay) : 0.0f;
	ei->flags	= flags;
	ei->args = *args;
}

/*
=============
CL_ParseReliableEvent

=============
*/
void CL_ParseReliableEvent( sizebuf_t *msg )
{
	int		event_index;
	event_args_t	nullargs, args;
	float		delay = 0.0f;

	memset( &nullargs, 0, sizeof( nullargs ));

	event_index = MSG_ReadUBitLong( msg, MAX_EVENT_BITS );

	if( MSG_ReadOneBit( msg ))
		delay = (float)MSG_ReadWord( msg ) * (1.0f / 100.0f);

	// reliable events not use delta-compression just null-compression
	MSG_ReadDeltaEvent( msg, &nullargs, &args );

	if( args.entindex > 0 && args.entindex <= cl.maxclients )
		args.angles[PITCH] *= -3.0f;

	CL_QueueEvent( FEV_RELIABLE|FEV_SERVER, event_index, delay, &args );
}


/*
=============
CL_ParseEvent

=============
*/
void CL_ParseEvent( sizebuf_t *msg )
{
	int		event_index;
	int		i, num_events;
	int		packet_index;
	event_args_t	nullargs, args;
	entity_state_t	*state;
	float		delay;

	memset( &nullargs, 0, sizeof( nullargs ));
	memset( &args, 0, sizeof( args ));

	num_events = MSG_ReadUBitLong( msg, 5 );

	// parse events queue
	for( i = 0 ; i < num_events; i++ )
	{
		event_index = MSG_ReadUBitLong( msg, MAX_EVENT_BITS );

		if( MSG_ReadOneBit( msg ))
			packet_index = MSG_ReadUBitLong( msg, cls.legacymode ? MAX_LEGACY_ENTITY_BITS : MAX_ENTITY_BITS );
		else packet_index = -1;

		if( MSG_ReadOneBit( msg ))
		{
			MSG_ReadDeltaEvent( msg, &nullargs, &args );
		}

		if( MSG_ReadOneBit( msg ))
			delay = (float)MSG_ReadWord( msg ) * (1.0f / 100.0f);
		else delay = 0.0f;

		if( packet_index != -1 )
		{
			frame_t	*frame = &cl.frames[cl.parsecountmod];

			if( packet_index < frame->num_entities )
			{
				state = &cls.packet_entities[(frame->first_entity+packet_index)%cls.num_client_entities];
				args.entindex = state->number;

				if( VectorIsNull( args.origin ))
					VectorCopy( state->origin, args.origin );

				if( VectorIsNull( args.angles ))
					VectorCopy( state->angles, args.angles );

				COM_NormalizeAngles( args.angles );

				if( state->number > 0 && state->number <= cl.maxclients )
				{
					args.angles[PITCH] *= -3.0f;
					CL_CalcPlayerVelocity( state->number, args.velocity );
					args.ducking = ( state->usehull == 1 );
				}
			}
			else
			{
				if( args.entindex != 0 )
				{
					if( args.entindex > 0 && args.entindex <= cl.maxclients )
						args.angles[PITCH] /= -3.0f;
				}
			}

			// Place event on queue
			CL_QueueEvent( FEV_SERVER, event_index, delay, &args );
		}
	}
}

/*
=============
CL_PlaybackEvent

=============
*/
void GAME_EXPORT CL_PlaybackEvent( int flags, const edict_t *pInvoker, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	event_args_t	args;

	if( FBitSet( flags, FEV_SERVER ))
		return;

	// first check event for out of bounds
	if( eventindex < 1 || eventindex >= MAX_EVENTS )
	{
		Con_DPrintf( S_ERROR "CL_PlaybackEvent: invalid eventindex %i\n", eventindex );
		return;
	}

	// check event for precached
	if( !CL_EventIndex( cl.event_precache[eventindex] ))
	{
		Con_DPrintf( S_ERROR "CL_PlaybackEvent: event %i was not precached\n", eventindex );
		return;
	}

	SetBits( flags, FEV_CLIENT ); // it's a client event
	ClearBits( flags, FEV_NOTHOST|FEV_HOSTONLY|FEV_GLOBAL );
	if( delay < 0.0f ) delay = 0.0f; // fixup negative delays

	memset( &args, 0, sizeof( args ));

	VectorCopy( origin, args.origin );
	VectorCopy( angles, args.angles );
	VectorCopy( cl.simvel, args.velocity );
	args.entindex = cl.playernum + 1;
	args.ducking = ( cl.local.usehull == 1 );

	args.fparam1 = fparam1;
	args.fparam2 = fparam2;
	args.iparam1 = iparam1;
	args.iparam2 = iparam2;
	args.bparam1 = bparam1;
	args.bparam2 = bparam2;

	CL_QueueEvent( flags, eventindex, delay, &args );
}
