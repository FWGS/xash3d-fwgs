/*
cl_frame.c - client world snapshot
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
#include "client.h"
#include "net_encode.h"
#include "entity_types.h"
#include "pm_local.h"
#include "cl_tent.h"
#include "studio.h"
#include "dlight.h"
#include "sound.h"
#include "input.h"

#define STUDIO_INTERPOLATION_FIX

/*
==================
CL_IsPlayerIndex

detect player entity
==================
*/
qboolean CL_IsPlayerIndex( int idx )
{
	return ( idx >= 1 && idx <= cl.maxclients );
}

/*
=========================================================================

FRAME INTERPOLATION

=========================================================================
*/
/*
==================
CL_UpdatePositions

Store another position into interpolation circular buffer
==================
*/
void CL_UpdatePositions( cl_entity_t *ent )
{
	position_history_t	*ph;

	ent->current_position = (ent->current_position + 1) & HISTORY_MASK;
	ph = &ent->ph[ent->current_position];

	VectorCopy( ent->curstate.origin, ph->origin );
	VectorCopy( ent->curstate.angles, ph->angles );
	ph->animtime = ent->curstate.animtime;	// !!!
}

/*
==================
CL_ResetPositions

Interpolation init or reset after teleporting
==================
*/
void CL_ResetPositions( cl_entity_t *ent )
{
	position_history_t	store;

	if( !ent ) return;

	store = ent->ph[ent->current_position];
	ent->current_position = 1;

	memset( ent->ph, 0, sizeof( position_history_t ) * HISTORY_MAX );
	memcpy( &ent->ph[1], &store, sizeof( position_history_t ));
	memcpy( &ent->ph[0], &store, sizeof( position_history_t ));
}

/*
==================
CL_EntityTeleported

check for instant movement in case
we don't want interpolate this
==================
*/
qboolean CL_EntityTeleported( cl_entity_t *ent )
{
	float	len, maxlen;
	vec3_t	delta;

	VectorSubtract( ent->curstate.origin, ent->prevstate.origin, delta );

	// compute potential max movement in units per frame and compare with entity movement
	maxlen = ( clgame.movevars.maxvelocity * ( 1.0f / GAME_FPS ));
	len = VectorLength( delta );

	return (len > maxlen);
}

/*
==================
CL_CompareTimestamps

round-off floating errors
==================
*/
qboolean CL_CompareTimestamps( float t1, float t2 )
{
	int	iTime1 = t1 * 1000;
	int	iTime2 = t2 * 1000;

	return (( iTime1 - iTime2 ) <= 1 );
}

/*
==================
CL_EntityIgnoreLerp

some ents will be ignore lerping
==================
*/
qboolean CL_EntityIgnoreLerp( cl_entity_t *e )
{
	if( e->model && e->model->type == mod_alias )
		return false;

	return (e->curstate.movetype == MOVETYPE_NONE) ? true : false;
}

/*
==================
CL_EntityCustomLerp

==================
*/
qboolean CL_EntityCustomLerp( cl_entity_t *e )
{
	switch( e->curstate.movetype )
	{
	case MOVETYPE_NONE:
	case MOVETYPE_STEP:
	case MOVETYPE_WALK:
	case MOVETYPE_FLY:
	case MOVETYPE_COMPOUND:
		return false;
	}

	return true;
}

/*
==================
CL_ParametricMove

check for parametrical moved entities
==================
*/
qboolean CL_ParametricMove( cl_entity_t *ent )
{
	float	frac, dt, t;
	vec3_t	delta;

	if( ent->curstate.starttime == 0.0f || ent->curstate.impacttime == 0.0f )
		return false;

	VectorSubtract( ent->curstate.endpos, ent->curstate.startpos, delta );
	dt = ent->curstate.impacttime - ent->curstate.starttime;

	if( dt != 0.0f )
	{
		if( ent->lastmove > cl.time )
			t = ent->lastmove;
		else t = cl.time;

		frac = ( t - ent->curstate.starttime ) / dt;
		frac = bound( 0.0f, frac, 1.0f );
		VectorMA( ent->curstate.startpos, frac, delta, ent->curstate.origin );

		ent->lastmove = t;
	}

	VectorNormalize( delta );
	if( VectorLength( delta ) > 0.0f )
		VectorAngles( delta, ent->curstate.angles ); // re-aim projectile

	return true;
}

/*
====================
CL_UpdateLatchedVars

====================
*/
void CL_UpdateLatchedVars( cl_entity_t *ent )
{
	if( !ent->model || ( ent->model->type != mod_alias && ent->model->type != mod_studio ))
		return; // below fields used only for alias and studio interpolation

	VectorCopy( ent->prevstate.origin, ent->latched.prevorigin );
	VectorCopy( ent->prevstate.angles, ent->latched.prevangles );

	if( ent->model->type == mod_alias )
		ent->latched.prevframe = ent->prevstate.frame;
	ent->latched.prevanimtime = ent->prevstate.animtime;

	if( ent->curstate.sequence != ent->prevstate.sequence )
	{
		memcpy( ent->prevstate.blending, ent->latched.prevseqblending, sizeof( ent->prevstate.blending ));
		ent->latched.prevsequence = ent->prevstate.sequence;
		ent->latched.sequencetime = ent->curstate.animtime;
	}

	memcpy( ent->latched.prevcontroller, ent->prevstate.controller, sizeof( ent->latched.prevcontroller ));
	memcpy( ent->latched.prevblending, ent->prevstate.blending, sizeof( ent->latched.prevblending ));

	// update custom latched vars
	if( clgame.drawFuncs.CL_UpdateLatchedVars != NULL )
		clgame.drawFuncs.CL_UpdateLatchedVars( ent, false );
}

/*
====================
CL_GetStudioEstimatedFrame

====================
*/
float CL_GetStudioEstimatedFrame( cl_entity_t *ent )
{
	studiohdr_t	*pstudiohdr;
	mstudioseqdesc_t	*pseqdesc;
	int		sequence;

	if( ent->model != NULL && ent->model->type == mod_studio )
	{
		pstudiohdr = (studiohdr_t *)Mod_StudioExtradata( ent->model );

		if( pstudiohdr )
		{
			sequence = bound( 0, ent->curstate.sequence, pstudiohdr->numseq - 1 );
			pseqdesc = (mstudioseqdesc_t *)((byte *)pstudiohdr + pstudiohdr->seqindex) + sequence;
			return ref.dllFuncs.R_StudioEstimateFrame( ent, pseqdesc );
		}
	}

	return 0;
}

/*
====================
CL_ResetLatchedVars

====================
*/
void CL_ResetLatchedVars( cl_entity_t *ent, qboolean full_reset )
{
	if( !ent->model || ( ent->model->type != mod_alias && ent->model->type != mod_studio ))
		return; // below fields used only for alias and studio interpolation

	if( full_reset )
	{
		// don't modify for sprites to avoid broke sprite interp
		memcpy( ent->latched.prevblending, ent->curstate.blending, sizeof( ent->latched.prevblending ));
		ent->latched.sequencetime = ent->curstate.animtime;
		memcpy( ent->latched.prevcontroller, ent->curstate.controller, sizeof( ent->latched.prevcontroller ));
		if( ent->model->type == mod_studio )
			ent->latched.prevframe = CL_GetStudioEstimatedFrame( ent );
		else if( ent->model->type == mod_alias )
			ent->latched.prevframe = ent->curstate.frame;
		ent->prevstate = ent->curstate;
	}

	ent->latched.prevanimtime = ent->curstate.animtime = cl.mtime[0];
	VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
	VectorCopy( ent->curstate.angles, ent->latched.prevangles );
	ent->latched.prevsequence = ent->curstate.sequence;

	// update custom latched vars
	if( clgame.drawFuncs.CL_UpdateLatchedVars != NULL )
		clgame.drawFuncs.CL_UpdateLatchedVars( ent, true );
}

/*
==================
CL_ProcessEntityUpdate

apply changes since new frame received
==================
*/
void CL_ProcessEntityUpdate( cl_entity_t *ent )
{
	qboolean	parametric;

	ent->model = CL_ModelHandle( ent->curstate.modelindex );
	ent->index = ent->curstate.number;

	if( FBitSet( ent->curstate.entityType, ENTITY_NORMAL ))
		COM_NormalizeAngles( ent->curstate.angles );

	parametric = CL_ParametricMove( ent );

	// allow interpolation on bmodels too
	if( ent->model && ent->model->type == mod_brush )
		ent->curstate.animtime = ent->curstate.msg_time;

	if( CL_EntityCustomLerp( ent ) && !parametric )
		ent->curstate.animtime = ent->curstate.msg_time;

	if( !CL_CompareTimestamps( ent->curstate.animtime, ent->prevstate.animtime ) || CL_EntityIgnoreLerp( ent ))
	{
		CL_UpdateLatchedVars( ent );
		CL_UpdatePositions( ent );
	}

	// g-cont. it should be done for all the players?
	if( ent->player && !FBitSet( host.features, ENGINE_COMPUTE_STUDIO_LERP ))
		ent->curstate.angles[PITCH] /= -3.0f;

	VectorCopy( ent->curstate.origin, ent->origin );
	VectorCopy( ent->curstate.angles, ent->angles );

	// initialize attachments for now
	VectorCopy( ent->origin, ent->attachment[0] );
	VectorCopy( ent->origin, ent->attachment[1] );
	VectorCopy( ent->origin, ent->attachment[2] );
	VectorCopy( ent->origin, ent->attachment[3] );
}

/*
==================
CL_FindInterpolationUpdates

find two timestamps
==================
*/
qboolean CL_FindInterpolationUpdates( cl_entity_t *ent, float targettime, position_history_t **ph0, position_history_t **ph1 )
{
	qboolean	extrapolate = true;
	int	i, i0, i1, imod;
	float	at;

	imod = ent->current_position;
	i0 = (imod - 0) & HISTORY_MASK;	// curpos (lerp end)
	i1 = (imod - 1) & HISTORY_MASK;	// oldpos (lerp start)

	for( i = 1; i < HISTORY_MAX - 1; i++ )
	{
		at = ent->ph[( imod - i ) & HISTORY_MASK].animtime;
		if( at == 0.0f ) break;

		if( targettime > at )
		{
			// found it
			i0 = (( imod - i ) + 1 ) & HISTORY_MASK;
			i1 = (( imod - i ) + 0 ) & HISTORY_MASK;
			extrapolate = false;
			break;
		}
	}

	if( ph0 != NULL ) *ph0 = &ent->ph[i0];
	if( ph1 != NULL ) *ph1 = &ent->ph[i1];

	return extrapolate;
}

/*
==================
CL_PureOrigin

non-local players interpolation
==================
*/
void CL_PureOrigin( cl_entity_t *ent, float t, vec3_t outorigin, vec3_t outangles )
{
	qboolean		extrapolate;
	float		t1, t0, frac;
	position_history_t	*ph0, *ph1;
	vec3_t		delta;

	// NOTE: ph0 is next, ph1 is a prev
	extrapolate = CL_FindInterpolationUpdates( ent, t, &ph0, &ph1 );

	if ( !ph0 || !ph1 )
		return;

	t0 = ph0->animtime;
	t1 = ph1->animtime;

	if( t0 != 0.0f )
	{
		vec4_t	q, q1, q2;

		VectorSubtract( ph0->origin, ph1->origin, delta );

		if( t0 != t1 )
			frac = ( t - t1 ) / ( t0 - t1 );
		else frac = 1.0f;

		frac = bound( 0.0f, frac, 1.2f );

		VectorMA( ph1->origin, frac, delta, outorigin );

		AngleQuaternion( ph0->angles, q1, false );
		AngleQuaternion( ph1->angles, q2, false );
		QuaternionSlerp( q2, q1, frac, q );
		QuaternionAngle( q, outangles );
	}
	else
	{
		// no backup found
		VectorCopy( ph1->origin, outorigin );
		VectorCopy( ph1->angles, outangles );
	}
}

/*
==================
CL_InterpolateModel

non-players interpolation
==================
*/
int CL_InterpolateModel( cl_entity_t *e )
{
	position_history_t  *ph0 = NULL, *ph1 = NULL;
	vec3_t		origin, angles, delta;
	float		t, t1, t2, frac;
	vec4_t		q, q1, q2;

	VectorCopy( e->curstate.origin, e->origin );
	VectorCopy( e->curstate.angles, e->angles );

	if( cls.timedemo || !e->model )
		return 1;

	if( cls.demoplayback == DEMO_QUAKE1 )
	{
		// quake lerping is easy
		VectorLerp( e->prevstate.origin, cl.lerpFrac, e->curstate.origin, e->origin );
		AngleQuaternion( e->prevstate.angles, q1, false );
		AngleQuaternion( e->curstate.angles, q2, false );
		QuaternionSlerp( q1, q2, cl.lerpFrac, q );
		QuaternionAngle( q, e->angles );
		return 1;
	}

	if( cl.maxclients <= 1 )
		return 1;

	if( e->model->type == mod_brush && !cl_bmodelinterp->value )
		return 1;

	if( cl.local.moving && cl.local.onground == e->index )
		return 1;

	t = cl.time - cl_interp->value;
	CL_FindInterpolationUpdates( e, t, &ph0, &ph1 );

	if( ph0 == NULL || ph1 == NULL )
		return 0;

	t1 = ph1->animtime;
	t2 = ph0->animtime;

	if( t - t1 < 0.0f )
		return 0;

	if( t1 == 0.0f )
	{
		VectorCopy( ph0->origin, e->origin );
		VectorCopy( ph0->angles, e->angles );
		return 0;
	}

	if( t2 == t1 )
	{
		VectorCopy( ph0->origin, e->origin );
		VectorCopy( ph0->angles, e->angles );
		return 1;
	}

	VectorSubtract( ph0->origin, ph1->origin, delta );
	frac = (t - t1) / (t2 - t1);

	if( frac < 0.0f )
		return 0;

	if( frac > 1.0f )
		frac = 1.0f;

	VectorMA( ph1->origin, frac, delta, origin );

	AngleQuaternion( ph0->angles, q1, false );
	AngleQuaternion( ph1->angles, q2, false );
	QuaternionSlerp( q2, q1, frac, q );
	QuaternionAngle( q, angles );

	VectorCopy( origin, e->origin );
	VectorCopy( angles, e->angles );

	return 1;
}

/*
=============
CL_ComputePlayerOrigin

interpolate non-local clients
=============
*/
void CL_ComputePlayerOrigin( cl_entity_t *ent )
{
	float	targettime;
	vec4_t	q, q1, q2;
	vec3_t	origin;
	vec3_t	angles;

	if( !ent->player || ent->index == ( cl.playernum + 1 ))
		return;

	if( cls.demoplayback == DEMO_QUAKE1 )
	{
		// quake lerping is easy
		VectorLerp( ent->prevstate.origin, cl.lerpFrac, ent->curstate.origin, ent->origin );
		AngleQuaternion( ent->prevstate.angles, q1, false );
		AngleQuaternion( ent->curstate.angles, q2, false );
		QuaternionSlerp( q1, q2, cl.lerpFrac, q );
		QuaternionAngle( q, ent->angles );
		return;
	}

	targettime = cl.time - cl_interp->value;
	CL_PureOrigin( ent, targettime, origin, angles );

	VectorCopy( angles, ent->angles );
	VectorCopy( origin, ent->origin );
}

/*
=================
CL_ProcessPlayerState

process player states after the new packet has received
=================
*/
void CL_ProcessPlayerState( int playerindex, entity_state_t *state )
{
	entity_state_t	*ps;

	ps = &cl.frames[cl.parsecountmod].playerstate[playerindex];
	ps->number = state->number;
	ps->messagenum = cl.parsecount;
	ps->msg_time = cl.mtime[0];

	clgame.dllFuncs.pfnProcessPlayerState( ps, state );
}

/*
=================
CL_ResetLatchedState

reset latched state if this frame entity was teleported
or just EF_NOINTERP was set
=================
*/
void CL_ResetLatchedState( int pnum, frame_t *frame, cl_entity_t *ent )
{
	if( CHECKVISBIT( frame->flags, pnum ))
	{
		VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
		VectorCopy( ent->curstate.angles, ent->latched.prevangles );

		CL_ResetLatchedVars( ent, true );
		CL_ResetPositions( ent );

		// parametric interpolation will starts at this point
		if( ent->curstate.starttime != 0.0f && ent->curstate.impacttime != 0.0f )
			ent->lastmove = cl.time;
	}
}

/*
=================
CL_ProcessPacket

process player states after the new packet has received
=================
*/
void CL_ProcessPacket( frame_t *frame )
{
	entity_state_t	*state;
	cl_entity_t	*ent;
	int		pnum;

	for( pnum = 0; pnum < frame->num_entities; pnum++ )
	{
		// request the entity state from circular buffer
		state = &cls.packet_entities[(frame->first_entity+pnum) % cls.num_client_entities];
		state->messagenum = cl.parsecount;
		state->msg_time = cl.mtime[0];

		// mark all the players
		ent = &clgame.entities[state->number];
		ent->player = CL_IsPlayerIndex( state->number );

		if( state->number == ( cl.playernum + 1 ))
			clgame.dllFuncs.pfnTxferLocalOverrides( state, &frame->clientdata );

		// shuffle states
		ent->prevstate = ent->curstate;
		ent->curstate = *state;

		CL_ProcessEntityUpdate( ent );
		CL_ResetLatchedState( pnum, frame, ent );
		if( !ent->player ) continue;

		CL_ProcessPlayerState(( state->number - 1 ), state );

		if( state->number == ( cl.playernum + 1 ))
			CL_CheckPredictionError();
	}
}

/*
=========================================================================

FRAME PARSING

=========================================================================
*/
/*
=================
CL_FlushEntityPacket

Read and ignore whole entity packet.
=================
*/
void CL_FlushEntityPacket( sizebuf_t *msg )
{
	int		newnum;
	entity_state_t	from, to;

	memset( &from, 0, sizeof( from ));

	cl.frames[cl.parsecountmod].valid = false;
	cl.validsequence = 0; // can't render a frame

	// read it all, but ignore it
	while( 1 )
	{
		newnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );
		if( newnum == LAST_EDICT ) break; // done

		if( MSG_CheckOverflow( msg ))
			Host_Error( "CL_FlushEntityPacket: overflow\n" );

		MSG_ReadDeltaEntity( msg, &from, &to, newnum, CL_IsPlayerIndex( newnum ) ? DELTA_PLAYER : DELTA_ENTITY, cl.mtime[0] );
	}
}

/*
=================
CL_DeltaEntity

processing delta update
=================
*/
void CL_DeltaEntity( sizebuf_t *msg, frame_t *frame, int newnum, entity_state_t *old, qboolean has_update )
{
	cl_entity_t	*ent;
	entity_state_t	*state;
	qboolean		newent = (old) ? false : true;
	int		pack = frame->num_entities;
	int		delta_type = DELTA_ENTITY;
	qboolean		alive = true;

	// alloc next slot to store update
	state = &cls.packet_entities[cls.next_client_entities % cls.num_client_entities];
	if( CL_IsPlayerIndex( newnum )) delta_type = DELTA_PLAYER;

	if(( newnum < 0 ) || ( newnum >= clgame.maxEntities ))
	{
		Con_DPrintf( S_ERROR "CL_DeltaEntity: invalid newnum: %d\n", newnum );
		if( has_update )
			MSG_ReadDeltaEntity( msg, old, state, newnum, delta_type, cl.mtime[0] );
		return;
	}

	ent = CL_EDICT_NUM( newnum );
	ent->index = newnum; // enumerate entity index
	if( newent ) old = &ent->baseline;

	if( has_update )
		alive = MSG_ReadDeltaEntity( msg, old, state, newnum, delta_type, cl.mtime[0] );
	else memcpy( state, old, sizeof( entity_state_t ));

	if( !alive )
	{
		CL_KillDeadBeams( ent ); // release dead beams
#if 0
		// this is for reference
		if( state->number == -1 )
			Con_DPrintf( "Entity %i was removed from server\n", newnum );
		else Con_Dprintf( "Entity %i was removed from delta-message\n", newnum );
#endif
		return;
	}

	if( newent )
	{
		// interpolation must be reset
		SETVISBIT( frame->flags, pack );

		// release beams from previous entity
		CL_KillDeadBeams( ent );
	}

	// add entity to packet
	cls.next_client_entities++;
	frame->num_entities++;
}

/*
==================
CL_ParsePacketEntities

An svc_packetentities has just been parsed, deal with the
rest of the data stream.
==================
*/
int CL_ParsePacketEntities( sizebuf_t *msg, qboolean delta )
{
	frame_t		*newframe, *oldframe;
	int		oldindex, newnum, oldnum;
	int		playerbytes = 0;
	int		oldpacket;
	int		bufStart;
	entity_state_t	*oldent;
	qboolean		player;
	int		count;

	// save first uncompressed packet as timestamp
	if( cls.changelevel && !delta && cls.demorecording )
		CL_WriteDemoJumpTime();

	// sentinel count. save it for debug checking
	if( cls.legacymode )
		count = MSG_ReadWord( msg );
	else count = MSG_ReadUBitLong( msg, MAX_VISIBLE_PACKET_BITS ) + 1;

	newframe = &cl.frames[cl.parsecountmod];

	// allocate parse entities
	memset( newframe->flags, 0, sizeof( newframe->flags ));
	newframe->first_entity = cls.next_client_entities;
	newframe->num_entities = 0;
	newframe->valid = true; // assume valid

	if( delta )
	{
		int	subtracted;

		oldpacket = MSG_ReadByte( msg );
		subtracted = ( cls.netchan.incoming_sequence - oldpacket ) & 0xFF;

		if( subtracted == 0 )
		{
			Con_NPrintf( 2, "^3Warning:^1 update too old\n^7\n" );
			CL_FlushEntityPacket( msg );
			return playerbytes;
		}

		if( subtracted >= CL_UPDATE_MASK )
		{
			// we can't use this, it is too old
			Con_NPrintf( 2, "^3Warning:^1 delta frame is too old^7\n" );
			CL_FlushEntityPacket( msg );
			return playerbytes;
		}

		oldframe = &cl.frames[oldpacket & CL_UPDATE_MASK];

		if(( cls.next_client_entities - oldframe->first_entity ) > ( cls.num_client_entities - NUM_PACKET_ENTITIES ))
		{
			Con_NPrintf( 2, "^3Warning:^1 delta frame is too old^7\n" );
			CL_FlushEntityPacket( msg );
			return playerbytes;
		}
	}
	else
	{
		// this is a full update that we can start delta compressing from now
		oldframe = NULL;
		oldpacket = -1;		// delta too old or is initial message
		cl.send_reply = true;	// send reply
		cls.demowaiting = false;	// we can start recording now
	}

	// mark current delta state
	cl.validsequence = cls.netchan.incoming_sequence;

	oldent = NULL;
	oldindex = 0;

	if( !oldframe )
	{
		oldnum = MAX_ENTNUMBER;
	}
	else
	{
		if( oldindex >= oldframe->num_entities )
		{
			oldnum = MAX_ENTNUMBER;
		}
		else
		{
			oldent = &cls.packet_entities[(oldframe->first_entity+oldindex) % cls.num_client_entities];
			oldnum = oldent->number;
		}
	}

	while( 1 )
	{
		int lastedict;
		if( cls.legacymode )
		{
			newnum = MSG_ReadWord( msg );
			lastedict = 0;
		}
		else
		{
			newnum = MSG_ReadUBitLong( msg, MAX_ENTITY_BITS );
			lastedict = LAST_EDICT;
		}

		if( newnum == lastedict ) break; // end of packet entities
		if( MSG_CheckOverflow( msg ))
			Host_Error( "CL_ParsePacketEntities: overflow\n" );
		player = CL_IsPlayerIndex( newnum );

		while( oldnum < newnum )
		{
			// one or more entities from the old packet are unchanged
			CL_DeltaEntity( msg, newframe, oldnum, oldent, false );
			oldindex++;

			if( oldindex >= oldframe->num_entities )
			{
				oldnum = MAX_ENTNUMBER;
			}
			else
			{
				oldent = &cls.packet_entities[(oldframe->first_entity+oldindex) % cls.num_client_entities];
				oldnum = oldent->number;
			}
		}

		if( oldnum == newnum )
		{
			// delta from previous state
			bufStart = MSG_GetNumBytesRead( msg );
			CL_DeltaEntity( msg, newframe, newnum, oldent, true );
			if( player ) playerbytes += MSG_GetNumBytesRead( msg ) - bufStart;
			oldindex++;

			if( oldindex >= oldframe->num_entities )
			{
				oldnum = MAX_ENTNUMBER;
			}
			else
			{
				oldent = &cls.packet_entities[(oldframe->first_entity+oldindex) % cls.num_client_entities];
				oldnum = oldent->number;
			}
			continue;
		}

		if( oldnum > newnum )
		{
			// delta from baseline ?
			bufStart = MSG_GetNumBytesRead( msg );
			CL_DeltaEntity( msg, newframe, newnum, NULL, true );
			if( player ) playerbytes += MSG_GetNumBytesRead( msg ) - bufStart;
			continue;
		}
	}

	// any remaining entities in the old frame are copied over
	while( oldnum != MAX_ENTNUMBER )
	{
		// one or more entities from the old packet are unchanged
		CL_DeltaEntity( msg, newframe, oldnum, oldent, false );
		oldindex++;

		if( oldindex >= oldframe->num_entities )
		{
			oldnum = MAX_ENTNUMBER;
		}
		else
		{
			oldent = &cls.packet_entities[(oldframe->first_entity+oldindex) % cls.num_client_entities];
			oldnum = oldent->number;
		}
	}

	if( newframe->num_entities != count && newframe->num_entities != 0 )
		Con_Reportf( S_WARN "CL_Parse%sPacketEntities: (%i should be %i)\n", delta ? "Delta" : "", newframe->num_entities, count );

	if( !newframe->valid )
		return playerbytes; // frame is not valid but message was parsed

	// now process packet.
	CL_ProcessPacket( newframe );

	// add new entities into physic lists
	CL_SetSolidEntities();

	// first update is the final signon stage where we actually receive an entity (i.e., the world at least)
	if( cls.signon == ( SIGNONS - 1 ))
	{
		// we are done with signon sequence.
		cls.signon = SIGNONS;

		// Clear loading plaque.
		CL_SignonReply ();
	}

	return playerbytes;
}

/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARMS

==========================================================================
*/
/*
=============
CL_AddVisibleEntity

all the visible entities should pass this filter
=============
*/
qboolean CL_AddVisibleEntity( cl_entity_t *ent, int entityType )
{
	if( !ent || !ent->model )
		return false;

	// check for adding this entity
	if( !clgame.dllFuncs.pfnAddEntity( entityType, ent, ent->model->name ))
	{
		// local player was reject by game code, so ignore any effects
		if( RP_LOCALCLIENT( ent ))
			cl.local.apply_effects = false;
		return false;
	}

	// don't add the player in firstperson mode
	if( RP_LOCALCLIENT( ent ) && !CL_IsThirdPerson( ) && ( ent->index == cl.viewentity ))
		return false;

	if( entityType == ET_BEAM )
	{
		ref.dllFuncs.CL_AddCustomBeam( ent );
		return true;
	}
	else if( !ref.dllFuncs.R_AddEntity( ent, entityType ))
	{
		return false;
	}

	// because pTemp->entity.curstate.effects
	// is already occupied by FTENT_FLICKER
	if( entityType != ET_TEMPENTITY )
	{
		// no reason to do it twice
		if( RP_LOCALCLIENT( ent ))
			cl.local.apply_effects = false;

		// apply client-side effects
		CL_AddEntityEffects( ent );

		// alias & studiomodel efefcts
		CL_AddModelEffects( ent );
	}

	return true;
}

/*
=============
CL_LinkCustomEntity

Add server beam to draw list
=============
*/
void CL_LinkCustomEntity( cl_entity_t *ent, entity_state_t *state )
{
	ent->curstate.movetype = state->modelindex; // !!!

	if( ent->model->type != mod_sprite )
		Con_Reportf( S_WARN "bad model on beam ( %s )\n", ent->model->name );

	ent->latched.prevsequence = ent->curstate.sequence;
	VectorCopy( ent->origin, ent->latched.prevorigin );
	VectorCopy( ent->angles, ent->latched.prevangles );
	ent->prevstate = ent->curstate;

	CL_AddVisibleEntity( ent, ET_BEAM );
}

/*
=============
CL_LinkPlayers

Create visible entities in the correct position
for all current players
=============
*/
void CL_LinkPlayers( frame_t *frame )
{
	entity_state_t	*state;
	cl_entity_t	*ent;
	int		i;

	ent = CL_GetLocalPlayer();

	// apply muzzleflash to weaponmodel
	if( ent && FBitSet( ent->curstate.effects, EF_MUZZLEFLASH ))
		SetBits( clgame.viewent.curstate.effects, EF_MUZZLEFLASH );
	cl.local.apply_effects = true;

	// check all the clients but add only visible
	for( i = 0, state = frame->playerstate; i < MAX_CLIENTS; i++, state++ )
	{
		if( state->messagenum != cl.parsecount )
			continue;	// not present this frame

		if( !state->modelindex || FBitSet( state->effects, EF_NODRAW ))
			continue;

		ent = &clgame.entities[i + 1];

		// fixup the player indexes...
		if( ent->index != ( i + 1 )) ent->index = (i + 1);

		if( i == cl.playernum )
		{
			if( cls.demoplayback != DEMO_QUAKE1 )
			{
				VectorCopy( state->origin, ent->origin );
				VectorCopy( state->origin, ent->prevstate.origin );
				VectorCopy( state->origin, ent->curstate.origin );
			}
			VectorCopy( ent->curstate.angles, ent->angles );
		}

		if( FBitSet( ent->curstate.effects, EF_NOINTERP ))
			CL_ResetLatchedVars( ent, false );

		if( CL_EntityTeleported( ent ))
		{
			VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
			VectorCopy( ent->curstate.angles, ent->latched.prevangles );
			CL_ResetPositions( ent );
		}

		if ( i == cl.playernum )
		{
			if( cls.demoplayback == DEMO_QUAKE1 )
				VectorLerp( ent->prevstate.origin, cl.lerpFrac, ent->curstate.origin, cl.simorg );
			VectorCopy( cl.simorg, ent->origin );
		}
		else
		{
			VectorCopy( ent->curstate.origin, ent->origin );
			VectorCopy( ent->curstate.angles, ent->angles );

			// interpolate non-local clients
			CL_ComputePlayerOrigin( ent );
		}

		VectorCopy( ent->origin, ent->attachment[0] );
		VectorCopy( ent->origin, ent->attachment[1] );
		VectorCopy( ent->origin, ent->attachment[2] );
		VectorCopy( ent->origin, ent->attachment[3] );

		CL_AddVisibleEntity( ent, ET_PLAYER );
	}

	// apply local player effects if entity is not added
	if( cl.local.apply_effects ) CL_AddEntityEffects( CL_GetLocalPlayer( ));
}

/*
===============
CL_LinkPacketEntities

===============
*/
void CL_LinkPacketEntities( frame_t *frame )
{
	cl_entity_t	*ent;
	entity_state_t	*state;
	qboolean		parametric;
	qboolean		interpolate;
	int		i;

	for( i = 0; i < frame->num_entities; i++ )
	{
		state = &cls.packet_entities[(frame->first_entity + i) % cls.num_client_entities];

		// clients are should be done in CL_LinkPlayers
		if( state->number >= 1 && state->number <= cl.maxclients )
			continue;

		// if set to invisible, skip
		if( !state->modelindex || FBitSet( state->effects, EF_NODRAW ))
			continue;

		ent = CL_GetEntityByIndex( state->number );

		if( !ent )
		{
			Con_Reportf( S_ERROR "CL_LinkPacketEntity: bad entity %i\n", state->number );
			continue;
		}

		// animtime must keep an actual
		ent->curstate.animtime = state->animtime;
		ent->curstate.frame = state->frame;
		interpolate = false;

		if( !ent->model ) continue;

		if( ent->curstate.rendermode == kRenderNormal )
		{
			// auto 'solid' faces
			if( FBitSet( ent->model->flags, MODEL_TRANSPARENT ) && Host_IsQuakeCompatible( ))
			{
				ent->curstate.rendermode = kRenderTransAlpha;
				ent->curstate.renderamt = 255;
			}
		}

		parametric = ( ent->curstate.impacttime != 0.0f && ent->curstate.starttime != 0.0f );

		if( !parametric && ent->curstate.movetype != MOVETYPE_COMPOUND )
		{
			if( ent->curstate.animtime == ent->prevstate.animtime && !VectorCompare( ent->curstate.origin, ent->prevstate.origin ))
				ent->lastmove = cl.time + 0.2;

			if( FBitSet( ent->curstate.eflags, EFLAG_SLERP ))
			{
				if( ent->curstate.animtime != 0.0f && ( ent->model->type == mod_alias || ent->model->type == mod_studio ))
				{
#ifdef STUDIO_INTERPOLATION_FIX
					if( ent->lastmove >= cl.time )
						VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
					if( FBitSet( host.features, ENGINE_COMPUTE_STUDIO_LERP ))
						interpolate = true;
					else ent->curstate.movetype = MOVETYPE_STEP;
#else
					if( ent->lastmove >= cl.time )
					{
						CL_ResetLatchedVars( ent, true );
						VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
						VectorCopy( ent->curstate.angles, ent->latched.prevangles );

						// disable step interpolation in client.dll
						ent->curstate.movetype = MOVETYPE_NONE;
					}
					else
					{
						// restore step interpolation in client.dll
						ent->curstate.movetype = MOVETYPE_STEP;
					}
#endif
				}
			}
		}

		if( ent->model->type == mod_brush )
		{
			CL_InterpolateModel( ent );
		}
		else
		{
			if( parametric )
			{
				CL_ParametricMove( ent );

				VectorCopy( ent->curstate.origin, ent->origin );
				VectorCopy( ent->curstate.angles, ent->angles );
			}
			else if( CL_EntityCustomLerp( ent ))
			{
				if ( !CL_InterpolateModel( ent ))
					continue;
			}
			else if( ent->curstate.movetype == MOVETYPE_STEP && !NET_IsLocalAddress( cls.netchan.remote_address ))
			{
				if( !CL_InterpolateModel( ent ))
					continue;
			}
			else
			{
				// no interpolation right now
				VectorCopy( ent->curstate.origin, ent->origin );
				VectorCopy( ent->curstate.angles, ent->angles );
			}

			if( ent->model->type == mod_studio )
			{
				if( interpolate && FBitSet( host.features, ENGINE_COMPUTE_STUDIO_LERP ))
					ref.dllFuncs.R_StudioLerpMovement( ent, cl.time, ent->origin, ent->angles );
			}
		}

		if( !FBitSet( state->entityType, ENTITY_NORMAL ))
		{
			CL_LinkCustomEntity( ent, state );
			continue;
		}

		if( ent->model->type != mod_brush )
		{
			// NOTE: never pass sprites with rendercolor '0 0 0' it's a stupid Valve Hammer Editor bug
			if( !ent->curstate.rendercolor.r && !ent->curstate.rendercolor.g && !ent->curstate.rendercolor.b )
				ent->curstate.rendercolor.r = ent->curstate.rendercolor.g = ent->curstate.rendercolor.b = 255;
		}

		// XASH SPECIFIC
		if( ent->curstate.rendermode == kRenderNormal && ent->curstate.renderfx == kRenderFxNone )
			ent->curstate.renderamt = 255.0f;

		if( ent->curstate.aiment != 0 && ent->curstate.movetype != MOVETYPE_COMPOUND )
			ent->curstate.movetype = MOVETYPE_FOLLOW;

		if( FBitSet( ent->curstate.effects, EF_NOINTERP ))
			CL_ResetLatchedVars( ent, false );

		if( CL_EntityTeleported( ent ))
		{
			VectorCopy( ent->curstate.origin, ent->latched.prevorigin );
			VectorCopy( ent->curstate.angles, ent->latched.prevangles );
			CL_ResetPositions( ent );
		}

		VectorCopy( ent->origin, ent->attachment[0] );
		VectorCopy( ent->origin, ent->attachment[1] );
		VectorCopy( ent->origin, ent->attachment[2] );
		VectorCopy( ent->origin, ent->attachment[3] );

		CL_AddVisibleEntity( ent, ET_NORMAL );
	}
}

/*
===============
CL_MoveThirdpersonCamera

think thirdperson
===============
*/
void CL_MoveThirdpersonCamera( void )
{
	if( cls.state == ca_disconnected || cls.state == ca_cinematic )
		return;

	// think thirdperson camera
	clgame.dllFuncs.CAM_Think ();
}

/*
===============
CL_EmitEntities

add visible entities to refresh list
process frame interpolation etc
===============
*/
void CL_EmitEntities( void )
{
	if( cl.paused ) return; // don't waste time

	// not in server yet, no entities to redraw
	if( cls.state != ca_active || !cl.validsequence )
		return;

	// make sure we have at least one valid update
	if( !cl.frames[cl.parsecountmod].valid )
		return;

	// animate lightestyles
	ref.dllFuncs.CL_RunLightStyles ();

	// decay dynamic lights
	CL_DecayLights ();

	// compute last interpolation amount
	CL_UpdateFrameLerp ();

	// set client ideal pitch when mlook is disabled
	CL_SetIdealPitch ();

	ref.dllFuncs.R_ClearScene ();

	// link all the visible clients first
	CL_LinkPlayers ( &cl.frames[cl.parsecountmod] );

	// link all the entities that actually have update
	CL_LinkPacketEntities ( &cl.frames[cl.parsecountmod] );

	// link custom user temp entities
	clgame.dllFuncs.pfnCreateEntities();

	// evaluate temp entities
	CL_TempEntUpdate ();

	// fire events (client and server)
	CL_FireEvents ();

	// handle spectator camera movement
	CL_MoveSpectatorCamera();

	// perfomance test
	CL_TestLights();
}

/*
==========================================================================

SOUND ENGINE IMPLEMENTATION

==========================================================================
*/
qboolean CL_GetEntitySpatialization( channel_t *ch )
{
	cl_entity_t	*ent;
	qboolean		valid_origin;

	if( ch->entnum == 0 )
	{
		ch->staticsound = true;
		return true; // static sound
	}

	if(( ch->entnum - 1 ) == cl.playernum )
	{
		VectorCopy( refState.vieworg, ch->origin );
		return true;
	}

	valid_origin = VectorIsNull( ch->origin ) ? false : true;
	ent = CL_GetEntityByIndex( ch->entnum );

	// entity is not present on the client but has valid origin
	if( !ent || !ent->index || ent->curstate.messagenum == 0 )
		return valid_origin;

#if 0
	// uncomment this if you want enable additional check by PVS
	if( ent->curstate.messagenum != cl.parsecount )
		return valid_origin;
#endif
	// setup origin
	VectorAverage( ent->curstate.mins, ent->curstate.maxs, ch->origin );
	VectorAdd( ch->origin, ent->curstate.origin, ch->origin );

	return true;
}

qboolean CL_GetMovieSpatialization( rawchan_t *ch )
{
	cl_entity_t	*ent;
	qboolean		valid_origin;

	valid_origin = VectorIsNull( ch->origin ) ? false : true;
	ent = CL_GetEntityByIndex( ch->entnum );

	// entity is not present on the client but has valid origin
	if( !ent || !ent->index || ent->curstate.messagenum == 0 )
		return valid_origin;

	// setup origin
	VectorAverage( ent->curstate.mins, ent->curstate.maxs, ch->origin );
	VectorAdd( ch->origin, ent->curstate.origin, ch->origin );

	// setup radius
	if( ent->model != NULL && ent->model->radius ) ch->radius = ent->model->radius;
	else ch->radius = RadiusFromBounds( ent->curstate.mins, ent->curstate.maxs );

	return true;
}

void CL_ExtraUpdate( void )
{
	clgame.dllFuncs.IN_Accumulate();
	S_ExtraUpdate();
}
