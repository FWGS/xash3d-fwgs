/*
sv_frame.c - server world snapshot
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
#include "net_encode.h"

typedef struct
{
	int		num_entities;
	entity_state_t	entities[MAX_VISIBLE_PACKET];
	byte		sended[MAX_EDICTS_BYTES];
} sv_ents_t;

static int	c_fullsend;	// just a debug counter
static int	c_notsend;

/*
=======================
SV_EntityNumbers
=======================
*/
static int SV_EntityNumbers( const void *a, const void *b )
{
	int	ent1, ent2;

	ent1 = ((entity_state_t *)a)->number;
	ent2 = ((entity_state_t *)b)->number;

	// watcom libc compares ents with itself
	if( ent1 == ent2 )
		return 0;

	if( ent1 < ent2 )
		return -1;
	return 1;
}

/*
=============
SV_AddEntitiesToPacket

=============
*/
static void SV_AddEntitiesToPacket( edict_t *pViewEnt, edict_t *pClient, client_frame_t *frame, sv_ents_t *ents, qboolean from_client )
{
	edict_t		*ent;
	byte		*clientpvs;
	byte		*clientphs;
	qboolean		fullvis = false;
	sv_client_t	*cl = NULL;
	qboolean		player;
	entity_state_t	*state;
	int		e;

	// during an error shutdown message we may need to transmit
	// the shutdown message after the server has shutdown, so
	// specifically check for it
	if( sv.state == ss_dead )
		return;

	cl = SV_ClientFromEdict( pClient, true );

	ASSERT( cl != NULL );

	// portals can't change hostflags
	if( from_client )
	{
		// setup hostflags
		if( FBitSet( cl->flags, FCL_LOCAL_WEAPONS ))
			SetBits( sv.hostflags, SVF_SKIPLOCALHOST );
		else ClearBits( sv.hostflags, SVF_SKIPLOCALHOST );

		// reset viewents each frame
		cl->num_viewents = 0;
	}

	svgame.dllFuncs.pfnSetupVisibility( pViewEnt, pClient, &clientpvs, &clientphs );
	if( !clientpvs ) fullvis = true;

	// g-cont: of course we can send world but not want to do it :-)
	for( e = 1; e < svgame.numEntities; e++ )
	{
		byte	*pset;

		ent = EDICT_NUM( e );

		// don't double add an entity through portals (in case this already added)
		if( CHECKVISBIT( ents->sended, e ))
			continue;

		if( e >= 1 && e <= svs.maxclients )
			player = 1;
		else player = 0;

		if( player )
		{
			sv_client_t *cl = &svs.clients[e - 1];

			if( cl->state != cs_spawned )
				continue;

			if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
				continue;
		}

		if( FBitSet( ent->v.effects, EF_REQUEST_PHS ))
			pset = clientphs;
		else pset = clientpvs;

		state = &ents->entities[ents->num_entities];

		// add entity to the net packet
		if( svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient, sv.hostflags, player, pset ))
		{
			// to prevent adds it twice through portals
			SETVISBIT( ents->sended, e );

			if( SV_IsValidEdict( ent->v.aiment ) && FBitSet( ent->v.aiment->v.effects, EF_MERGE_VISIBILITY ))
			{
				if( cl->num_viewents < MAX_VIEWENTS )
				{
					cl->viewentity[cl->num_viewents] = ent->v.aiment;
					cl->num_viewents++;
				}
			}

			// if we are full, silently discard entities
			if( ents->num_entities < ( MAX_VISIBLE_PACKET - 1 ))
			{
				ents->num_entities++;	// entity accepted
				c_fullsend++;		// debug counter

			}
			else
			{
				// visibility list is full
				// continue counting entities,
				// so we know how many it's ovreflowed
				c_notsend++;
			}
		}

		if( fullvis ) continue; // portal ents will be added anyway, ignore recursion

		// if it's a portal entity, add everything visible from its camera position
		if( from_client && FBitSet( ent->v.effects, EF_MERGE_VISIBILITY ))
		{
			SetBits( sv.hostflags, SVF_MERGE_VISIBILITY );
			SV_AddEntitiesToPacket( ent, pClient, frame, ents, false );
			ClearBits( sv.hostflags, SVF_MERGE_VISIBILITY );
		}
	}
}

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/
/*
=============
SV_FindBestBaseline

trying to deltas with previous entities
set frame to NULL to check for static entities
=============
*/
int SV_FindBestBaseline( int index, entity_state_t **baseline, entity_state_t *to, client_frame_t *frame, qboolean player )
{
	int	bestBitCount;
	int	i, bitCount;
	int	bestfound, j;

	bestBitCount = j = Delta_TestBaseline( *baseline, to, player, sv.time );
	bestfound = index;

	// lookup backward for previous 64 states and try to interpret current delta as baseline
	for( i = index - 1; bestBitCount > 0 && i >= 0 && ( index - i ) < ( MAX_CUSTOM_BASELINES - 1 ); i-- )
	{
		// don't worry about underflow in circular buffer
		entity_state_t *test;

		// if set, then it's normal entity
		if( frame != NULL )
			test = &svs.packet_entities[(frame->first_entity+i) % svs.num_client_entities];
		else
			test = &svs.static_entities[i];

		if( to->entityType == test->entityType )
		{
			bitCount = Delta_TestBaseline( test, to, player, sv.time );

			if( bitCount < bestBitCount )
			{
				bestBitCount = bitCount;
				bestfound = i;
			}
		}
	}

	// using delta from previous entity as baseline for current
	if( index != bestfound )
	{
		if( frame != NULL )
			*baseline = &svs.packet_entities[(frame->first_entity+bestfound) % svs.num_client_entities];
		else
			*baseline = &svs.static_entities[bestfound];
	}
	return index - bestfound;
}

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message->
=============
*/
static void SV_EmitPacketEntities( sv_client_t *cl, client_frame_t *to, sizebuf_t *msg )
{
	entity_state_t	*oldent, *newent;
	int		oldindex, newindex;
	int		i, oldnum, newnum;
	qboolean		player;
	int		oldmax;
	client_frame_t	*from;

	// this is the frame that we are going to delta update from
	if( cl->delta_sequence != -1 )
	{
		from = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK];
		oldmax = from->num_entities;

		// the snapshot's entities may still have rolled off the buffer, though
		if( from->first_entity <= ( svs.next_client_entities - svs.num_client_entities ))
		{
			Con_DPrintf( S_WARN "%s: delta request from out of date entities.\n", cl->name );
			MSG_BeginServerCmd( msg, svc_packetentities );
			MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );

			from = NULL;
			oldmax = 0;
		}
		else
		{
			MSG_BeginServerCmd( msg, svc_deltapacketentities );
			MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );
			MSG_WriteByte( msg, cl->delta_sequence );
		}
	}
	else
	{
		from = NULL;
		oldmax = 0;

		MSG_BeginServerCmd( msg, svc_packetentities );
		MSG_WriteUBitLong( msg, to->num_entities - 1, MAX_VISIBLE_PACKET_BITS );
	}

	newent = NULL;
	oldent = NULL;
	newindex = 0;
	oldindex = 0;

	while( newindex < to->num_entities || oldindex < oldmax )
	{
		if( newindex >= to->num_entities )
		{
			newnum = MAX_ENTNUMBER;
			player = false;
		}
		else
		{
			newent = &svs.packet_entities[(to->first_entity+newindex) % svs.num_client_entities];
			player = SV_IsPlayerIndex( newent->number );
			newnum = newent->number;
		}

		if( oldindex >= oldmax )
		{
			oldnum = MAX_ENTNUMBER;
		}
		else
		{
			oldent = &svs.packet_entities[(from->first_entity+oldindex) % svs.num_client_entities];
			oldnum = oldent->number;
		}

		if( newnum == oldnum )
		{
			// delta update from old position
			// because the force parm is false, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteDeltaEntity( oldent, newent, msg, false, player, sv.time, 0 );
			oldindex++;
			newindex++;
			continue;
		}

		if( newnum < oldnum )
		{
			entity_state_t	*baseline = &svs.baselines[newnum];
			const char	*classname = SV_ClassName( EDICT_NUM( newnum ));
			int		offset = 0;

			// trying to reduce message by select optimal baseline
			if( !sv_instancedbaseline.value || !sv.num_instanced || sv.last_valid_baseline > newnum )
			{
				offset = SV_FindBestBaseline( newindex, &baseline, newent, to, player );
			}
			else
			{
				for( i = 0; i < sv.num_instanced; i++ )
				{
					if( !Q_strcmp( classname, sv.instanced[i].classname ))
					{
						baseline = &sv.instanced[i].baseline;
						offset = -i - 1; // to avoid zero offset
						break;
					}
				}
			}

			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity( baseline, newent, msg, true, player, sv.time, offset );
			newindex++;
			continue;
		}

		if( newnum > oldnum )
		{
			edict_t	*ed = EDICT_NUM( oldent->number );
			qboolean	force = false;

			// check if entity completely removed from server
			if( ed->free || FBitSet( ed->v.flags, FL_KILLME ))
				force = true;

			// remove from message
			MSG_WriteDeltaEntity( oldent, NULL, msg, force, false, sv.time, 0 );
			oldindex++;
			continue;
		}
	}

	MSG_WriteUBitLong( msg, LAST_EDICT, MAX_ENTITY_BITS ); // end of packetentities
}

/*
=============
SV_EmitEvents

=============
*/
static void SV_EmitEvents( sv_client_t *cl, client_frame_t *to, sizebuf_t *msg )
{
	event_state_t	*es;
	event_info_t	*info;
	entity_state_t	*state;
	event_args_t	nullargs;
	int		ev_count = 0;
	int		count, ent_index;
	int		i, j, ev;

	memset( &nullargs, 0, sizeof( nullargs ));
	es = &cl->events;

	// count events
	for( ev = 0; ev < MAX_EVENT_QUEUE; ev++ )
	{
		if( es->ei[ev].index )
			ev_count++;
	}

	// nothing to send
	if( !ev_count ) return; // nothing to send

	if ( ev_count >= MAX_EVENT_QUEUE / 2 )
		ev_count = ( MAX_EVENT_QUEUE / 2 ) - 1;

	for( i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		info = &es->ei[i];
		if( info->index == 0 )
			continue;

		ent_index = info->entity_index;

		for( j = 0; j < to->num_entities; j++ )
		{
			state = &svs.packet_entities[(to->first_entity+j) % svs.num_client_entities];
			if( state->number == ent_index )
				break;
		}

		if( j < to->num_entities )
		{
			info->packet_index = j;
			info->args.ducking = 0;

			if( !FBitSet( info->args.flags, FEVENT_ORIGIN ))
				VectorClear( info->args.origin );

			if( !FBitSet( info->args.flags, FEVENT_ANGLES ))
				VectorClear( info->args.angles );

			VectorClear( info->args.velocity );
		}
		else
		{
			// couldn't find
			info->packet_index = to->num_entities;
			info->args.entindex = ent_index;
		}
	}

	MSG_BeginServerCmd( msg, svc_event );	// create message
	MSG_WriteUBitLong( msg, ev_count, 5 );	// up to MAX_EVENT_QUEUE events

	for( count = i = 0; i < MAX_EVENT_QUEUE; i++ )
	{
		info = &es->ei[i];

		if( info->index == 0 )
		{
			info->packet_index = -1;
			info->entity_index = -1;
			continue;
		}

		// only send if there's room
		if( count < ev_count )
		{
			MSG_WriteUBitLong( msg, info->index, MAX_EVENT_BITS ); // 1024 events

			if( info->packet_index == -1 )
			{
				MSG_WriteOneBit( msg, 0 );
			}
			else
			{
				MSG_WriteOneBit( msg, 1 );
				MSG_WriteUBitLong( msg, info->packet_index, MAX_ENTITY_BITS );

				if( !memcmp( &nullargs, &info->args, sizeof( event_args_t )))
				{
					MSG_WriteOneBit( msg, 0 );
				}
				else
				{
					MSG_WriteOneBit( msg, 1 );
					MSG_WriteDeltaEvent( msg, &nullargs, &info->args );
				}
			}

			if( info->fire_time )
			{
				MSG_WriteOneBit( msg, 1 );
				MSG_WriteWord( msg, ( info->fire_time * 100.0f ));
			}
			else MSG_WriteOneBit( msg, 0 );
		}

		info->index = 0;
		info->packet_index = -1;
		info->entity_index = -1;
		count++;
	}
}

/*
=============
SV_EmitPings

=============
*/
static void SV_EmitPings( sizebuf_t *msg )
{
	sv_client_t *cl;
	int i;

	MSG_BeginServerCmd( msg, svc_pings );

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		int packet_loss, ping;

		if( cl->state != cs_spawned )
			continue;

		SV_GetPlayerStats( cl, &ping, &packet_loss );

		// there are 25 bits for each client
		MSG_WriteOneBit( msg, 1 );
		MSG_WriteUBitLong( msg, i, MAX_CLIENT_BITS );
		MSG_WriteUBitLong( msg, ping, 12 );
		MSG_WriteUBitLong( msg, packet_loss, 7 );
	}

	// end marker
	MSG_WriteOneBit( msg, 0 );
}

/*
==================
SV_WriteClientdataToMessage

==================
*/
static void SV_WriteClientdataToMessage( sv_client_t *cl, sizebuf_t *msg )
{
	clientdata_t	nullcd;
	clientdata_t	*from_cd, *to_cd;
	weapon_data_t	nullwd;
	weapon_data_t	*from_wd, *to_wd;
	client_frame_t	*frame;
	edict_t		*clent;
	int		i;

	memset( &nullcd, 0, sizeof( nullcd ));
	frame = &cl->frames[cl->netchan.outgoing_sequence & SV_UPDATE_MASK];
	frame->senttime = host.realtime;
	frame->ping_time = -1.0f;
	clent = cl->edict;

	if( cl->chokecount != 0 )
	{
		MSG_BeginServerCmd( msg, svc_choke );
		cl->chokecount = 0;
	}

	// update client fixangle
	switch( clent->v.fixangle )
	{
	case 1:
		MSG_BeginServerCmd( msg, svc_setangle );
		MSG_WriteVec3Angles( msg, clent->v.angles );
		break;
	case 2:
		MSG_BeginServerCmd( msg, svc_addangle );
		MSG_WriteBitAngle( msg, clent->v.avelocity[YAW], 16 );
		clent->v.avelocity[YAW] = 0.0f;
		break;
	}

	clent->v.fixangle = 0; // reset fixangle

	memset( &frame->clientdata, 0, sizeof( frame->clientdata ));

	// update clientdata_t
	svgame.dllFuncs.pfnUpdateClientData( clent, FBitSet( cl->flags, FCL_LOCAL_WEAPONS ), &frame->clientdata );

	MSG_BeginServerCmd( msg, svc_clientdata );
	if( FBitSet( cl->flags, FCL_HLTV_PROXY )) return;	// don't send more nothing

	if( cl->delta_sequence == -1 ) from_cd = &nullcd;
	else from_cd = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK].clientdata;
	to_cd = &frame->clientdata;

	if( cl->delta_sequence == -1 )
	{
		MSG_WriteOneBit( msg, 0 );	// no delta-compression
	}
	else
	{
		MSG_WriteOneBit( msg, 1 );	// we are delta-ing from
		MSG_WriteByte( msg, cl->delta_sequence );
	}

	// write clientdata_t
	MSG_WriteClientData( msg, from_cd, to_cd, sv.time );

	if( FBitSet( cl->flags, FCL_LOCAL_WEAPONS ) && svgame.dllFuncs.pfnGetWeaponData( clent, frame->weapondata ))
	{
		memset( &nullwd, 0, sizeof( nullwd ));

		for( i = 0; i < MAX_LOCAL_WEAPONS; i++ )
		{
			if( cl->delta_sequence == -1 ) from_wd = &nullwd;
			else from_wd = &cl->frames[cl->delta_sequence & SV_UPDATE_MASK].weapondata[i];
			to_wd = &frame->weapondata[i];

			MSG_WriteWeaponData( msg, from_wd, to_wd, sv.time, i );
		}
	}

	// end marker
	MSG_WriteOneBit( msg, 0 );
}

/*
==================
SV_WriteEntitiesToClient

==================
*/
static void SV_WriteEntitiesToClient( sv_client_t *cl, sizebuf_t *msg )
{
	client_frame_t	*frame;
	entity_state_t	*state;
	static sv_ents_t	frame_ents;
	int		i, send_pings;

	frame = &cl->frames[cl->netchan.outgoing_sequence & SV_UPDATE_MASK];
	send_pings = SV_ShouldUpdatePing( cl );

	memset( frame_ents.sended, 0, sizeof( frame_ents.sended ));
	ClearBits( sv.hostflags, SVF_MERGE_VISIBILITY );

	// clear everything in this snapshot
	frame_ents.num_entities = c_fullsend = c_notsend = 0;

	// add all the entities directly visible to the eye, which
	// may include portal entities that merge other viewpoints
	SV_AddEntitiesToPacket( cl->pViewEntity, cl->edict, frame, &frame_ents, true );

	if( c_notsend != cl->ignored_ents )
	{
		if( c_notsend > 0 )
			Con_Printf( S_ERROR "Too many entities in visible packet list. Ignored %d entities\n", c_notsend );
		cl->ignored_ents = c_notsend;
	}

	// if there were portals visible, there may be out of order entities
	// in the list which will need to be resorted for the delta compression
	// to work correctly.  This also catches the error condition
	// of an entity being included twice.
	qsort( frame_ents.entities, frame_ents.num_entities, sizeof( frame_ents.entities[0] ), SV_EntityNumbers );

	// it will break all connected clients, but it takes more than one week to overflow it
	if(( (uint)svs.next_client_entities ) + frame_ents.num_entities >= 0x7FFFFFFE )
	{
		svs.next_client_entities = 0;

		// delta is broken for now, cannot keep connected clients
		SV_FinalMessage( "Server will restart due delta is outdated\n", true );
	}

	// copy the entity states out
	frame->first_entity = svs.next_client_entities;
	frame->num_entities = 0;

	for( i = 0; i < frame_ents.num_entities; i++ )
	{
		// add it to the circular packet_entities array
		state = &svs.packet_entities[svs.next_client_entities % svs.num_client_entities];
		*state = frame_ents.entities[i];
		svs.next_client_entities++;
		frame->num_entities++;
	}

	SV_EmitPacketEntities( cl, frame, msg );
	SV_EmitEvents( cl, frame, msg );
	if( send_pings ) SV_EmitPings( msg );
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/
/*
=======================
SV_SendClientDatagram
=======================
*/
static void SV_SendClientDatagram( sv_client_t *cl )
{
	byte	msg_buf[MAX_DATAGRAM];
	sizebuf_t	msg;

	memset( msg_buf, 0, sizeof( msg_buf ));
	MSG_Init( &msg, "Datagram", msg_buf, sizeof( msg_buf ));

	// always send servertime at new frame
	MSG_BeginServerCmd( &msg, svc_time );
	MSG_WriteFloat( &msg, sv.time );

	SV_WriteClientdataToMessage( cl, &msg );
	SV_WriteEntitiesToClient( cl, &msg );

	// copy the accumulated multicast datagram
	// for this client out to the message
	if( MSG_CheckOverflow( &cl->datagram ))
	{
		Con_Printf( S_WARN "%s overflowed for %s\n", MSG_GetName( &cl->datagram ), cl->name );
	}
	else
	{
		if( MSG_GetNumBytesWritten( &cl->datagram ) < MSG_GetNumBytesLeft( &msg ))
			MSG_WriteBits( &msg, MSG_GetData( &cl->datagram ), MSG_GetNumBitsWritten( &cl->datagram ));
		else if( host.realtime > cl->overflow_warn_time )
		{
			Con_DPrintf( S_WARN "Ignoring unreliable datagram for %s, would overflow on msg\n", cl->name );
			cl->overflow_warn_time = host.realtime + 5.0f;
		}
	}

	MSG_Clear( &cl->datagram );

	if( MSG_CheckOverflow( &msg ))
	{
		// must have room left for the packet header
		Con_Printf( S_ERROR "%s overflowed for %s\n", MSG_GetName( &msg ), cl->name );
		MSG_Clear( &msg );
	}

	// send the datagram
	Netchan_TransmitBits( &cl->netchan, MSG_GetNumBitsWritten( &msg ), MSG_GetData( &msg ));
}

/*
=======================
SV_UpdateUserInfo
=======================
*/
static void SV_UpdateUserInfo( sv_client_t *cl )
{
	SV_FullClientUpdate( cl, &sv.reliable_datagram );
	ClearBits( cl->flags, FCL_RESEND_USERINFO );
	cl->next_sendinfotime = host.realtime + 1.0;
}

/*
=======================
SV_UpdateToReliableMessages
=======================
*/
static void SV_UpdateToReliableMessages( void )
{
	sv_client_t	*cl;
	int		i;

	// check for changes to be sent over the reliable streams to all clients
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( !cl->edict ) continue;	// not in game yet

		if( cl->state != cs_spawned )
			continue;

		if( FBitSet( cl->flags, FCL_RESEND_USERINFO ) && cl->next_sendinfotime <= host.realtime )
		{
			if( MSG_GetNumBytesLeft( &sv.reliable_datagram ) >= ( Q_strlen( cl->userinfo ) + 6 ))
				SV_UpdateUserInfo( cl );
		}

		if( FBitSet( cl->flags, FCL_RESEND_MOVEVARS ))
		{
			SV_FullUpdateMovevars( cl, &cl->netchan.message );
			ClearBits( cl->flags, FCL_RESEND_MOVEVARS );
		}
	}

	// clear the server datagram if it overflowed.
	if( MSG_CheckOverflow( &sv.datagram ))
	{
		Con_DPrintf( S_ERROR "sv.datagram overflowed!\n" );
		MSG_Clear( &sv.datagram );
	}

	// clear the server datagram if it overflowed.
	if( MSG_CheckOverflow( &sv.spec_datagram ))
	{
		Con_DPrintf( S_ERROR "sv.spec_datagram overflowed!\n" );
		MSG_Clear( &sv.spec_datagram );
	}

	// now send the reliable and server datagrams to all clients.
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;	// reliables go to all connected or spawned

		if( MSG_GetNumBytesWritten( &sv.reliable_datagram ) < MSG_GetNumBytesLeft( &cl->netchan.message ))
			MSG_WriteBits( &cl->netchan.message, MSG_GetData( &sv.reliable_datagram ), MSG_GetNumBitsWritten( &sv.reliable_datagram ));
		else Netchan_CreateFragments( &cl->netchan, &sv.reliable_datagram );

		if( MSG_GetNumBytesWritten( &sv.datagram ) < MSG_GetNumBytesLeft( &cl->datagram ))
			MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.datagram ), MSG_GetNumBitsWritten( &sv.datagram ));
		else Con_DPrintf( S_WARN "Ignoring unreliable datagram for %s, would overflow\n", cl->name );

		if( FBitSet( cl->flags, FCL_HLTV_PROXY ))
		{
			if( MSG_GetNumBytesWritten( &sv.spec_datagram ) < MSG_GetNumBytesLeft( &cl->datagram ))
				MSG_WriteBits( &cl->datagram, MSG_GetData( &sv.spec_datagram ), MSG_GetNumBitsWritten( &sv.spec_datagram ));
			else Con_DPrintf( S_WARN "Ignoring spectator datagram for %s, would overflow\n", cl->name );
		}
	}

	// now clear the reliable and datagram buffers.
	MSG_Clear( &sv.reliable_datagram );
	MSG_Clear( &sv.spec_datagram );
	MSG_Clear( &sv.datagram );
}

/*
=======================
SV_SendClientMessages
=======================
*/
void SV_SendClientMessages( void )
{
	sv_client_t *cl;
	int          i;
	double       updaterate_time;
	double       time_until_next_message;

	if( sv.state == ss_dead )
		return;

	SV_UpdateToReliableMessages ();

	// send a message to each connected client
	for( i = 0, sv.current_client = svs.clients; i < svs.maxclients; i++, sv.current_client++ )
	{
		cl = sv.current_client;

		if( cl->state <= cs_zombie || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		if( FBitSet( cl->flags, FCL_SKIP_NET_MESSAGE ))
		{
			ClearBits( cl->flags, FCL_SKIP_NET_MESSAGE );
			continue;
		}

		if( !host_limitlocal.value && NET_IsLocalAddress( cl->netchan.remote_address ))
			SetBits( cl->flags, FCL_SEND_NET_MESSAGE );

		if( cl->state == cs_spawned )
		{
			// Try to send a message as soon as we can.
			// If the target time for sending is within the next frame interval ( based on last frame ),
			// trigger the send now. Note that in single player,
			// FCL_SEND_NET_MESSAGE flag is also set any time a packet arrives from the client.
			time_until_next_message = cl->next_messagetime - ( host.realtime + sv.frametime );
			if( time_until_next_message <= 0.0 )
				SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
			else if( time_until_next_message > 2.0 ) // something got hosed
				SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
		}

		// if the reliable message overflowed, drop the client
		if( MSG_CheckOverflow( &cl->netchan.message ))
		{
			MSG_Clear( &cl->netchan.message );
			MSG_Clear( &cl->datagram );
			SV_BroadcastPrintf( NULL, "%s overflowed\n", cl->name );
			Con_DPrintf( S_ERROR "reliable overflow for %s\n", cl->name );
			SV_DropClient( cl, false );
			SetBits( cl->flags, FCL_SEND_NET_MESSAGE );
			cl->netchan.cleartime = 0.0;	// don't choke this message
		}
		else if( FBitSet( cl->flags, FCL_SEND_NET_MESSAGE ))
		{
			// If we haven't gotten a message in sv_failuretime seconds, then stop sending messages to this client
			// until we get another packet in from the client. This prevents crash/drop and reconnect where they are
			// being hosed with "sequenced packet without connection" packets.
			if( sv_failuretime.value < ( host.realtime - cl->netchan.last_received ))
				ClearBits( cl->flags, FCL_SEND_NET_MESSAGE );
		}

		// only send messages if the client has sent one
		// and the bandwidth is not choked
		if( FBitSet( cl->flags, FCL_SEND_NET_MESSAGE ))
		{
			// bandwidth choke active?
			if( !Netchan_CanPacket( &cl->netchan, cl->state == cs_spawned ))
			{
				cl->chokecount++;
				continue;
			}

			// now that we were able to send, reset timer to point to next possible send time.
			// check here also because sv_max/minupdaterate could been changed in runtime
			updaterate_time = bound( 1.0 / sv_maxupdaterate.value, cl->cl_updaterate, 1.0 / sv_minupdaterate.value );
			cl->next_messagetime = host.realtime + sv.frametime + updaterate_time;
			ClearBits( cl->flags, FCL_SEND_NET_MESSAGE );

			// NOTE: we should send frame even if server is not simulated to prevent overflow
			if( cl->state == cs_spawned )
				SV_SendClientDatagram( cl );
			else Netchan_TransmitBits( &cl->netchan, 0, NULL ); // just update reliable
		}
	}

	// reset current client
	sv.current_client = NULL;
}

/*
=======================
SV_SkipUpdates

used before changing level
=======================
*/
void SV_SkipUpdates( void )
{
	sv_client_t	*cl;
	int		i;

	if( sv.state == ss_dead )
		return;

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ))
			continue;

		SetBits( cl->flags, FCL_SKIP_NET_MESSAGE );
	}
}

/*
=======================
SV_InactivateClients

Purpose: Prepare for level transition, etc.
=======================
*/
void SV_InactivateClients( void )
{
	int		i;
	sv_client_t	*cl;

	if( sv.state == ss_dead )
		return;

	// send a message to each connected client
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( !cl->state || !cl->edict )
			continue;

		if( !cl->edict  )
			continue;

		if( FBitSet( cl->edict->v.flags, FL_FAKECLIENT ))
		{
			SV_DropClient( cl, false );
			continue;
		}

		if( cl->state > cs_connected )
		{
			cl->state = cs_connected;

			// bump connect timeout
			cl->connection_started = host.realtime;
		}

		COM_ClearCustomizationList( &cl->customdata, false );
		memset( cl->physinfo, 0, MAX_PHYSINFO_STRING );

		// NOTE: many mods sending messages that must be applied on a next level
		// e.g. CryOfFear sending HideHud and PlayMp3 that affected after map change
		if( svgame.globals->changelevel )
			continue;

		MSG_Clear( &cl->netchan.message );
		MSG_Clear( &cl->datagram );
	}
}
