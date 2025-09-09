/*
cl_pmove.c - client-side player physic
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
#include "client.h"
#include "const.h"
#include "cl_tent.h"
#include "pm_local.h"
#include "particledef.h"
#include "studio.h"

#define MAX_FORWARD			6	// forward probes for set idealpitch
#define MIN_CORRECTION_DISTANCE	0.25f	// use smoothing if error is > this
#define MIN_PREDICTION_EPSILON	0.5f	// complain if error is > this and we have cl_showerror set
#define MAX_PREDICTION_ERROR		64.0f	// above this is assumed to be a teleport, don't smooth, etc.

/*
=============
CL_PushPMStates

=============
*/
void GAME_EXPORT CL_PushPMStates( void )
{
	if( clgame.pushed ) return;
	clgame.oldphyscount = clgame.pmove->numphysent;
	clgame.oldviscount  = clgame.pmove->numvisent;
	clgame.pushed = true;
}

/*
=============
CL_PopPMStates

=============
*/
void GAME_EXPORT CL_PopPMStates( void )
{
	if( !clgame.pushed ) return;
	clgame.pmove->numphysent = clgame.oldphyscount;
	clgame.pmove->numvisent  = clgame.oldviscount;
	clgame.pushed = false;
}

/*
===============
CL_IsPredicted
===============
*/
static qboolean CL_IsPredicted( void )
{
	if( cl_nopred.value || cl.intermission )
		return false;

	// never predict the quake demos
	if( cls.demoplayback == DEMO_QUAKE1 )
		return false;
	return true;
}

/*
===============
CL_SetLastUpdate
===============
*/
void CL_SetLastUpdate( void )
{
	cls.lastupdate_sequence = cls.netchan.incoming_sequence;
}

/*
===============
CL_RedoPrediction
===============
*/
void CL_RedoPrediction( void )
{
	if ( cls.netchan.incoming_sequence != cls.lastupdate_sequence )
	{
		CL_PredictMovement( true );
		CL_CheckPredictionError();
	}
}

/*
===============
CL_SetIdealPitch
===============
*/
void CL_SetIdealPitch( void )
{
	float	angleval, sinval, cosval;
	int	i, j, step, dir, steps;
	float	z[MAX_FORWARD];
	vec3_t	top, bottom;
	pmtrace_t	tr;

	if( cl.local.onground == -1 )
		return;

	angleval = cl.viewangles[YAW] * M_PI2 / 360.0f;
	SinCos( angleval, &sinval, &cosval );

	// Now move forward by 36, 48, 60, etc. units from the eye position and drop lines straight down
	// 160 or so units to see what's below
	for( i = 0; i < MAX_FORWARD; i++ )
	{
		top[0] = cl.simorg[0] + cosval * (i + 3.0f) * 12.0f;
		top[1] = cl.simorg[1] + sinval * (i + 3.0f) * 12.0f;
		top[2] = cl.simorg[2] + cl.viewheight[2];

		bottom[0] = top[0];
		bottom[1] = top[1];
		bottom[2] = top[2] - 160.0f;

		// skip any monsters (only world and brush models)
		tr = CL_TraceLine( top, bottom, PM_STUDIO_BOX );
		if( tr.allsolid ) return; // looking at a wall, leave ideal the way is was

		if( tr.fraction == 1.0f )
			return;	// near a dropoff

		z[i] = top[2] + tr.fraction * (bottom[2] - top[2]);
	}

	dir = 0;
	steps = 0;

	for( j = 1; j < i; j++ )
	{
		step = z[j] - z[j-1];
		if( step > -ON_EPSILON && step < ON_EPSILON )
			continue;

		if( dir && ( step-dir > ON_EPSILON || step-dir < -ON_EPSILON ))
			return; // mixed changes

		steps++;
		dir = step;
	}

	if( !dir )
	{
		cl.local.idealpitch = 0.0f;
		return;
	}

	if( steps < 2 ) return;
	cl.local.idealpitch = -dir * cl_idealpitchscale.value;
}

/*
==================
CL_PlayerTeleported

check for instant movement in case
we don't want interpolate this
==================
*/
static qboolean CL_PlayerTeleported( local_state_t *from, local_state_t *to )
{
	int	len, maxlen;
	vec3_t	delta;

	VectorSubtract( to->playerstate.origin, from->playerstate.origin, delta );

	// compute potential max movement in units per frame and compare with entity movement
	maxlen = ( clgame.movevars.maxvelocity * ( 1.0f / GAME_FPS ));
	len = VectorLength( delta );

	return (len > maxlen);
}

/*
===================
CL_CheckPredictionError
===================
*/
void CL_CheckPredictionError( void )
{
	int		frame, cmd;
	static int	pos = 0;
	vec3_t		delta;
	float		dist;

	if( !CL_IsPredicted( ))
		return;

	// calculate the last usercmd_t we sent that the server has processed
	frame = ( cls.netchan.incoming_acknowledged ) & CL_UPDATE_MASK;
	cmd = cl.parsecountmod;

	// compare what the server returned with what we had predicted it to be
	VectorSubtract( cl.frames[cmd].playerstate[cl.playernum].origin, cl.local.predicted_origins[frame], delta );
	dist = VectorLength( delta );

	// save the prediction error for interpolation
	if( dist > MAX_PREDICTION_ERROR )
	{
		if( cl_showerror.value && host_developer.value )
			Con_NPrintf( 10 + ( ++pos & 3 ), "^3player teleported:^7 %.3f units\n", dist );

		// a teleport or something or gamepaused
		VectorClear( cl.local.prediction_error );
	}
	else
	{
		if( cl_showerror.value && dist > MIN_PREDICTION_EPSILON && host_developer.value )
			Con_NPrintf( 10 + ( ++pos & 3 ), "^1prediction error:^7 %.3f units\n", dist );

		VectorCopy( cl.frames[cmd].playerstate[cl.playernum].origin, cl.local.predicted_origins[frame] );

		// save for error interpolation
		VectorCopy( delta, cl.local.prediction_error );

		// GoldSrc checks for singleplayer
		// we would check for local server
		if( dist > MIN_CORRECTION_DISTANCE && !SV_Active() )
			cls.correction_time = cl_smoothtime.value;
	}
}

/*
=============
CL_SetUpPlayerPrediction

Calculate the new position of players, without other player clipping
We do this to set up real player prediction.
Players are predicted twice, first without clipping other players,
then with clipping against them.
This sets up the first phase.
=============
*/
void GAME_EXPORT CL_SetUpPlayerPrediction( int dopred, int bIncludeLocalClient )
{
	entity_state_t	*state;
	predicted_player_t	*player;
	cl_entity_t	*ent;
	int		i;

	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		state = &cl.frames[cl.parsecountmod].playerstate[i];
		player = &cls.predicted_players[i];

		player->active = false;

		if( state->messagenum != cl.parsecount )
			continue; // not present this frame

		if( !state->modelindex )
			continue;

		player->active = true;
		player->movetype = state->movetype;
		player->solid = state->solid;
		player->usehull = state->usehull;

		if( FBitSet( state->effects, EF_NODRAW ) && !bIncludeLocalClient && ( cl.playernum == i ))
			continue;

		// note that the local player is special, since he moves locally
		// we use his last predicted postition
		if( cl.playernum == i )
		{
			VectorCopy( state->origin, player->origin );
			VectorCopy( state->angles, player->angles );
		}
		else
		{
			ent = CL_GetEntityByIndex( i + 1 );

			CL_ComputePlayerOrigin( ent );

			VectorCopy( ent->origin, player->origin );
			VectorCopy( ent->angles, player->angles );
		}
	}
}

void CL_ClipPMoveToEntity( physent_t *pe, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, pmtrace_t *tr )
{
	Assert( tr != NULL );

	if( clgame.dllFuncs.pfnClipMoveToEntity != NULL )
	{
		// do custom sweep test
		clgame.dllFuncs.pfnClipMoveToEntity( pe, start, mins, maxs, end, tr );
	}
	else
	{
		// function is missed, so we didn't hit anything
		tr->allsolid = false;
	}
}

static void CL_CopyEntityToPhysEnt( physent_t *pe, entity_state_t *state, qboolean visent )
{
	model_t	*mod = CL_ModelHandle( state->modelindex );

	pe->player = 0;

	if( state->number >= 1 && state->number <= cl.maxclients )
		pe->player = state->number;

	if( pe->player )
	{
		// client or bot
		Q_snprintf( pe->name, sizeof( pe->name ), "player %i", pe->player - 1 );
	}
	else if( mod != NULL )
	{
		// otherwise copy the modelname
		Q_strncpy( pe->name, mod->name, sizeof( pe->name ));
	}
	else
	{
		Q_strncpy( pe->name, "entity %i", state->number );
	}

	pe->model = pe->studiomodel = NULL;

	VectorCopy( state->mins, pe->mins );
	VectorCopy( state->maxs, pe->maxs );

	if( state->solid == SOLID_BBOX )
	{
		if( FBitSet( mod->flags, STUDIO_TRACE_HITBOX ))
			pe->studiomodel = mod;
	}
	else
	{
		if( pe->solid != SOLID_BSP && ( mod != NULL ) && ( mod->type == mod_studio ))
			pe->studiomodel = mod;
		else pe->model = mod;
	}

	// rare case: not solid entities in vistrace
	if( visent && VectorIsNull( pe->mins ) && mod != NULL )
	{
		VectorCopy( mod->mins, pe->mins );
		VectorCopy( mod->maxs, pe->maxs );
	}

	pe->info = state->number;
	VectorCopy( state->origin, pe->origin );
	VectorCopy( state->angles, pe->angles );

	pe->solid = state->solid;
	pe->rendermode = state->rendermode;
	pe->skin = state->skin;
	pe->frame = state->frame;
	pe->sequence = state->sequence;

	memcpy( &pe->controller[0], &state->controller[0], sizeof( pe->controller ));
	memcpy( &pe->blending[0], &state->blending[0], sizeof( pe->blending ));

	pe->movetype = state->movetype;
	pe->takedamage = (pe->player) ? DAMAGE_YES : DAMAGE_NO;
	pe->team = state->team;
	pe->classnumber = state->playerclass;
	pe->blooddecal = 0;	// unused in GoldSrc

	// for mods
	pe->iuser1 = state->iuser1;
	pe->iuser2 = state->iuser2;
	pe->iuser3 = state->iuser3;
	pe->iuser4 = state->iuser4;
	pe->fuser1 = state->fuser1;
	pe->fuser2 = state->fuser2;
	pe->fuser3 = state->fuser3;
	pe->fuser4 = state->fuser4;

	VectorCopy( state->vuser1, pe->vuser1 );
	VectorCopy( state->vuser2, pe->vuser2 );
	VectorCopy( state->vuser3, pe->vuser3 );
	VectorCopy( state->vuser4, pe->vuser4 );
}

/*
====================
CL_AddLinksToPmove

collect solid entities
====================
*/
static void CL_AddLinksToPmove( frame_t *frame )
{
	entity_state_t	*state;
	model_t		*model;
	physent_t		*pe;
	int		i;

	if( !frame->valid ) return;

	for( i = 0; i < frame->num_entities; i++ )
	{
		state = &cls.packet_entities[(frame->first_entity + i) % cls.num_client_entities];

		if( state->number >= 1 && state->number <= cl.maxclients )
			continue;

		if( !state->modelindex )
			continue;

		model = CL_ModelHandle( state->modelindex );
		if( !model ) continue;

		if(( state->owner != 0 ) && ( state->owner == cl.playernum + 1 ))
			continue;

		if(( model->hulls[1].lastclipnode || model->type == mod_studio ) && clgame.pmove->numvisent < MAX_PHYSENTS )
		{
			pe = &clgame.pmove->visents[clgame.pmove->numvisent];
			CL_CopyEntityToPhysEnt( pe, state, true );
			clgame.pmove->numvisent++;
		}

		if( state->solid == SOLID_TRIGGER || ( state->solid == SOLID_NOT && state->skin >= CONTENTS_EMPTY ))
			continue;

		// dead body
		if( state->mins[2] == 0.0f && state->maxs[2] == 1.0f )
			continue;

		// can't collide with zeroed hull
		if( VectorIsNull( state->mins ) && VectorIsNull( state->maxs ))
			continue;

		if( state->solid == SOLID_NOT && state->skin == CONTENTS_LADDER )
		{
			if( clgame.pmove->nummoveent >= MAX_MOVEENTS )
				continue;

			pe = &clgame.pmove->moveents[clgame.pmove->nummoveent];
			CL_CopyEntityToPhysEnt( pe, state, false );
			clgame.pmove->nummoveent++;
		}
		else
		{
			if( !model->hulls[1].lastclipnode && model->type != mod_studio )
				continue;

			// reserve slots for all the clients
			if( clgame.pmove->numphysent >= ( MAX_PHYSENTS - cl.maxclients ))
				continue;

			pe = &clgame.pmove->physents[clgame.pmove->numphysent];
			CL_CopyEntityToPhysEnt( pe, state, false );
			clgame.pmove->numphysent++;
		}
	}
}

/*
===============
CL_SetSolidEntities

Builds all the pmove physents for the current frame
===============
*/
void CL_SetSolidEntities( void )
{
	physent_t	*pe = clgame.pmove->physents;

	// setup physents
	clgame.pmove->numvisent = 1;
	clgame.pmove->numphysent = 1;
	clgame.pmove->nummoveent = 0;

	memset( clgame.pmove->physents, 0, sizeof( physent_t ));
	memset( clgame.pmove->visents, 0, sizeof( physent_t ));

	pe->model = cl.worldmodel;
	if( pe->model ) Q_strncpy( pe->name, pe->model->name, sizeof( pe->name ));
	pe->takedamage = DAMAGE_YES;
	pe->solid = SOLID_BSP;

	// share to visents
	clgame.pmove->visents[0] = clgame.pmove->physents[0];

	// add all other entities exlucde players
	CL_AddLinksToPmove( &cl.frames[cl.parsecountmod] );
}

/*
===============
CL_SetSolidPlayers

Builds all the pmove physents for the current frame
Note that CL_SetUpPlayerPrediction() must be called first!
pmove must be setup with world and solid entity hulls before calling
(via CL_PredictMove)
===============
*/
void GAME_EXPORT CL_SetSolidPlayers( int playernum )
{
	entity_state_t	*state;
	predicted_player_t	*player;
	physent_t		*pe;
	int		i;

	if( !cl_solid_players.value )
		return;

	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		state = &cl.frames[cl.parsecountmod].playerstate[i];
		player = &cls.predicted_players[i];

		if( playernum == -1 )
		{
			if( i != cl.playernum && !player->active )
				continue;
		}
		else
		{
			if( !player->active )
				continue;	// not present this frame

			// the player object never gets added
			if( playernum == i )
				continue;
		}

		if( player->solid == SOLID_NOT )
			continue;	// dead body

		if( clgame.pmove->numphysent >= MAX_PHYSENTS )
			break;

		pe = &clgame.pmove->physents[clgame.pmove->numphysent];
		CL_CopyEntityToPhysEnt( pe, state, false );
		clgame.pmove->numphysent++;

		// some fields needs to be override from cls.predicted_players
		VectorCopy( player->origin, pe->origin );
		VectorCopy( player->angles, pe->angles );
		VectorCopy( host.player_mins[player->usehull], pe->mins );
		VectorCopy( host.player_maxs[player->usehull], pe->maxs );
		pe->movetype = player->movetype;
		pe->solid = player->solid;
	}
}

/*
=============
CL_WaterEntity

=============
*/
int GAME_EXPORT CL_WaterEntity( const float *rgflPos )
{
	physent_t		*pe;
	hull_t		*hull;
	vec3_t		test, offset;
	int		i, oldhull;

	if( !rgflPos ) return -1;

	oldhull = clgame.pmove->usehull;

	for( i = 0; i < clgame.pmove->numphysent; i++ )
	{
		pe = &clgame.pmove->physents[i];

		if( pe->solid != SOLID_NOT ) // disabled ?
			continue;

		// only brushes can have special contents
		if( !pe->model || pe->model->type != mod_brush )
			continue;

		// check water brushes accuracy
		clgame.pmove->usehull = 2;
		hull = PM_HullForBsp( pe, clgame.pmove, offset );
		clgame.pmove->usehull = oldhull;

		// offset the test point appropriately for this hull.
		VectorSubtract( rgflPos, offset, test );

		if( FBitSet( pe->model->flags, MODEL_HAS_ORIGIN ) && !VectorIsNull( pe->angles ))
		{
			matrix4x4	matrix;

			Matrix4x4_CreateFromEntity( matrix, pe->angles, offset, 1.0f );
			Matrix4x4_VectorITransform( matrix, rgflPos, test );
		}

		// test hull for intersection with this model
		if( PM_HullPointContents( hull, hull->firstclipnode, test ) == CONTENTS_EMPTY )
			continue;

		// found water entity
		return pe->info;
	}
	return -1;
}

/*
=============
CL_TraceLine

a simple engine traceline
=============
*/
pmtrace_t CL_TraceLine( vec3_t start, vec3_t end, int flags )
{
	int	old_usehull;
	pmtrace_t	tr;

	old_usehull = clgame.pmove->usehull;
	clgame.pmove->usehull = 2;
	tr = PM_PlayerTraceExt( clgame.pmove, start, end, flags, clgame.pmove->numphysent, clgame.pmove->physents, -1, NULL );
	clgame.pmove->usehull = old_usehull;

	return tr;
}

/*
=============
CL_VisTraceLine

trace by visible objects (thats can be non-solid)
=============
*/
pmtrace_t *CL_VisTraceLine( vec3_t start, vec3_t end, int flags )
{
	int		old_usehull;
	static pmtrace_t	tr;

	old_usehull = clgame.pmove->usehull;
	clgame.pmove->usehull = 2;
	tr = PM_PlayerTraceExt( clgame.pmove, start, end, flags, clgame.pmove->numvisent, clgame.pmove->visents, -1, NULL );
	clgame.pmove->usehull = old_usehull;

	return &tr;
}

/*
=============
CL_GetWaterEntity

returns water brush where inside pos
=============
*/
cl_entity_t *CL_GetWaterEntity( const float *rgflPos )
{
	int	entnum;

	entnum = CL_WaterEntity( rgflPos );
	if( entnum <= 0 ) return NULL; // world or not water

	return CL_GetEntityByIndex( entnum );
}

static int GAME_EXPORT pfnTestPlayerPosition( float *pos, pmtrace_t *ptrace )
{
	return PM_TestPlayerPosition( clgame.pmove, pos, ptrace, NULL );
}

static void GAME_EXPORT pfnStuckTouch( int hitent, pmtrace_t *tr )
{
	PM_StuckTouch( clgame.pmove, hitent, tr );
}

static int GAME_EXPORT pfnTruePointContents( float *p )
{
	return PM_TruePointContents( clgame.pmove, p );
}

static pmtrace_t GAME_EXPORT pfnPlayerTrace( float *start, float *end, int traceFlags, int ignore_pe )
{
	return PM_PlayerTraceExt( clgame.pmove, start, end, traceFlags, clgame.pmove->numphysent, clgame.pmove->physents, ignore_pe, NULL );
}

static void *pfnHullForBsp( physent_t *pe, float *offset )
{
	return PM_HullForBsp( pe, clgame.pmove, offset );
}

static float GAME_EXPORT pfnTraceModel( physent_t *pe, float *start, float *end, trace_t *trace )
{
	return PM_TraceModel( clgame.pmove, pe, start, end, trace );
}

static void GAME_EXPORT pfnPlaySound( int channel, const char *sample, float volume, float attenuation, int fFlags, int pitch )
{
	if( !clgame.pmove->runfuncs )
		return;

	S_StartSound( NULL, clgame.pmove->player_index + 1, channel, S_RegisterSound( sample ), volume, attenuation, pitch, fFlags );
}

static void GAME_EXPORT pfnPlaybackEventFull( int flags, int clientindex, word eventindex, float delay, float *origin,
	float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	CL_PlaybackEvent( flags, NULL, eventindex, delay, origin, angles, fparam1, fparam2, iparam1, iparam2, bparam1, bparam2 );
}

static pmtrace_t GAME_EXPORT pfnPlayerTraceEx( float *start, float *end, int traceFlags, pfnIgnore pmFilter )
{
	return PM_PlayerTraceExt( clgame.pmove, start, end, traceFlags, clgame.pmove->numphysent, clgame.pmove->physents, -1, pmFilter );
}

static int GAME_EXPORT pfnTestPlayerPositionEx( float *pos, pmtrace_t *ptrace, pfnIgnore pmFilter )
{
	return PM_TestPlayerPosition( clgame.pmove, pos, ptrace, pmFilter );
}

static pmtrace_t *pfnTraceLineEx( float *start, float *end, int flags, int usehull, pfnIgnore pmFilter )
{
	return PM_TraceLineEx( clgame.pmove, start, end, flags, usehull, pmFilter );
}

/*
===============
CL_InitClientMove

===============
*/
void CL_InitClientMove( void )
{
	int	i;

	Pmove_Init ();

	clgame.pmove->server = false;	// running at client
	clgame.pmove->movevars = &clgame.movevars;
	clgame.pmove->runfuncs = false;

	// enumerate client hulls
	for( i = 0; i < MAX_MAP_HULLS; i++ )
	{
		if( clgame.dllFuncs.pfnGetHullBounds( i, host.player_mins[i], host.player_maxs[i] ))
			Con_Reportf( "CL: hull%i, player_mins: %g %g %g, player_maxs: %g %g %g\n", i,
			host.player_mins[i][0], host.player_mins[i][1], host.player_mins[i][2],
			host.player_maxs[i][0], host.player_maxs[i][1], host.player_maxs[i][2] );
	}

	memcpy( clgame.pmove->player_mins, host.player_mins, sizeof( host.player_mins ));
	memcpy( clgame.pmove->player_maxs, host.player_maxs, sizeof( host.player_maxs ));

	// common utilities
	clgame.pmove->PM_Info_ValueForKey = Info_ValueForKey;
	clgame.pmove->PM_Particle = CL_Particle; // ref should be initialized here already
	clgame.pmove->PM_TestPlayerPosition = pfnTestPlayerPosition;
	clgame.pmove->Con_NPrintf = Con_NPrintf;
	clgame.pmove->Con_DPrintf = Con_DPrintf;
	clgame.pmove->Con_Printf = Con_Printf;
	clgame.pmove->Sys_FloatTime = Sys_DoubleTime;
	clgame.pmove->PM_StuckTouch = pfnStuckTouch;
	clgame.pmove->PM_PointContents = (void*)PM_CL_PointContents;
	clgame.pmove->PM_TruePointContents = pfnTruePointContents;
	clgame.pmove->PM_HullPointContents = (void*)PM_HullPointContents;
	clgame.pmove->PM_PlayerTrace = pfnPlayerTrace;
	clgame.pmove->PM_TraceLine = PM_CL_TraceLine;
	clgame.pmove->RandomLong = COM_RandomLong;
	clgame.pmove->RandomFloat = COM_RandomFloat;
	clgame.pmove->PM_GetModelType = pfnGetModelType;
	clgame.pmove->PM_GetModelBounds = pfnGetModelBounds;
	clgame.pmove->PM_HullForBsp = pfnHullForBsp;
	clgame.pmove->PM_TraceModel = pfnTraceModel;
	clgame.pmove->COM_FileSize = COM_FileSize;
	clgame.pmove->COM_LoadFile = COM_LoadFile;
	clgame.pmove->COM_FreeFile = COM_FreeFile;
	clgame.pmove->memfgets = COM_MemFgets;
	clgame.pmove->PM_PlaySound = pfnPlaySound;
	clgame.pmove->PM_TraceTexture = PM_CL_TraceTexture;
	clgame.pmove->PM_PlaybackEventFull = pfnPlaybackEventFull;
	clgame.pmove->PM_PlayerTraceEx = pfnPlayerTraceEx;
	clgame.pmove->PM_TestPlayerPositionEx = pfnTestPlayerPositionEx;
	clgame.pmove->PM_TraceLineEx = pfnTraceLineEx;
	clgame.pmove->PM_TraceSurface = pfnTraceSurface;

	// initalize pmove
	clgame.dllFuncs.pfnPlayerMoveInit( clgame.pmove );
}

static void CL_SetupPMove( playermove_t *pmove, const local_state_t *from, const usercmd_t *ucmd, qboolean runfuncs, double time )
{
	const entity_state_t	*ps;
	const clientdata_t	*cd;

	ps = &from->playerstate;
	cd = &from->client;

	pmove->player_index = ps->number - 1;

	// a1ba: workaround bug where the server refuse to send our local player in delta
	// cl.playernum, in theory, must be equal to our local player index anyway
	//
	// this might not be a real solution, since everything else will be bogus
	// but we need to properly run prediction and avoid potential memory
	// corruption
	if( pmove->player_index < 0 )
		pmove->player_index = bound( 0, cl.playernum, cl.maxclients - 1 );

	pmove->multiplayer = (cl.maxclients > 1);
	pmove->runfuncs = runfuncs;
	pmove->time = time * 1000.0f;
	pmove->frametime = ucmd->msec / 1000.0f;
	VectorCopy( ps->origin, pmove->origin );
	VectorCopy( ps->angles, pmove->angles );
	VectorCopy( pmove->angles, pmove->oldangles );
	VectorCopy( cd->velocity, pmove->velocity );
	VectorCopy( ps->basevelocity, pmove->basevelocity );
	VectorCopy( cd->view_ofs, pmove->view_ofs );
	VectorClear( pmove->movedir );
	pmove->flDuckTime = (float)cd->flDuckTime;
	pmove->bInDuck = cd->bInDuck;
	pmove->usehull = ps->usehull;
	pmove->flTimeStepSound = cd->flTimeStepSound;
	pmove->iStepLeft = ps->iStepLeft;
	pmove->flFallVelocity = ps->flFallVelocity;
	pmove->flSwimTime = (float)cd->flSwimTime;
	VectorCopy( cd->punchangle, pmove->punchangle );
	pmove->flNextPrimaryAttack = 0.0f; // not used by PM_ code
	pmove->effects = ps->effects;
	pmove->flags = cd->flags;
	pmove->gravity = ps->gravity;
	pmove->friction = ps->friction;
	pmove->oldbuttons = ps->oldbuttons;
	pmove->waterjumptime = (float)cd->waterjumptime;
	pmove->dead = (cl.local.health <= 0);
	pmove->deadflag = cd->deadflag;
	pmove->spectator = (cls.spectator != 0);
	pmove->movetype = ps->movetype;
	pmove->onground = ps->onground;
	pmove->waterlevel = cd->waterlevel;
	pmove->watertype = cd->watertype;
	pmove->maxspeed = clgame.movevars.maxspeed;
	pmove->clientmaxspeed = cd->maxspeed;
	pmove->iuser1 = cd->iuser1;
	pmove->iuser2 = cd->iuser2;
	pmove->iuser3 = cd->iuser3;
	pmove->iuser4 = cd->iuser4;
	pmove->fuser1 = cd->fuser1;
	pmove->fuser2 = cd->fuser2;
	pmove->fuser3 = cd->fuser3;
	pmove->fuser4 = cd->fuser4;
	VectorCopy( cd->vuser1, pmove->vuser1 );
	VectorCopy( cd->vuser2, pmove->vuser2 );
	VectorCopy( cd->vuser3, pmove->vuser3 );
	VectorCopy( cd->vuser4, pmove->vuser4 );
	pmove->cmd = *ucmd;	// copy current cmds

	Q_strncpy( pmove->physinfo, cls.physinfo, sizeof( pmove->physinfo ));
}

static const void CL_FinishPMove( const playermove_t *pmove, local_state_t *to )
{
	entity_state_t	*ps;
	clientdata_t	*cd;

	ps = &to->playerstate;
	cd = &to->client;

	cd->flags = pmove->flags;
	cd->bInDuck = pmove->bInDuck;
	cd->flTimeStepSound = pmove->flTimeStepSound;
	cd->flDuckTime = (int)pmove->flDuckTime;
	cd->flSwimTime = (int)pmove->flSwimTime;
	cd->waterjumptime = (int)pmove->waterjumptime;
	cd->watertype = pmove->watertype;
	cd->waterlevel = pmove->waterlevel;
	cd->maxspeed = pmove->clientmaxspeed;
	cd->deadflag = pmove->deadflag;
	VectorCopy( pmove->velocity, cd->velocity );
	VectorCopy( pmove->view_ofs, cd->view_ofs );
	VectorCopy( pmove->origin, ps->origin );
	VectorCopy( pmove->angles, ps->angles );
	VectorCopy( pmove->basevelocity, ps->basevelocity );
	VectorCopy( pmove->punchangle, cd->punchangle );
	ps->oldbuttons = (uint)pmove->cmd.buttons;
	ps->friction = pmove->friction;
	ps->movetype = pmove->movetype;
	ps->onground = pmove->onground;
	ps->effects = pmove->effects;
	ps->usehull = pmove->usehull;
	ps->iStepLeft = pmove->iStepLeft;
	ps->flFallVelocity = pmove->flFallVelocity;
	cd->iuser1 = pmove->iuser1;
	cd->iuser2 = pmove->iuser2;
	cd->iuser3 = pmove->iuser3;
	cd->iuser4 = pmove->iuser4;
	cd->fuser1 = pmove->fuser1;
	cd->fuser2 = pmove->fuser2;
	cd->fuser3 = pmove->fuser3;
	cd->fuser4 = pmove->fuser4;
	VectorCopy( pmove->vuser1, cd->vuser1 );
	VectorCopy( pmove->vuser2, cd->vuser2 );
	VectorCopy( pmove->vuser3, cd->vuser3 );
	VectorCopy( pmove->vuser4, cd->vuser4 );
}

/*
=================
CL_RunUsercmd

Runs prediction code for user cmd
=================
*/
static void CL_RunUsercmd( local_state_t *from, local_state_t *to, usercmd_t *u, qboolean runfuncs, double *time, unsigned int random_seed )
{
	usercmd_t		cmd;

	if( u->msec > 50 )
	{
		local_state_t	temp;
		usercmd_t		split;

		memset( &temp, 0, sizeof( temp ));

		split = *u;
		split.msec /= 2;
		CL_RunUsercmd( from, &temp, &split, runfuncs, time, random_seed );
		split.impulse = split.weaponselect = 0;
		CL_RunUsercmd( &temp, to, &split, runfuncs, time, random_seed );
		return;
	}

	cmd = *u;	// deal with local copy
	*to = *from;

	if( CL_IsPredicted( ))
	{
		// setup playermove state
		CL_SetupPMove( clgame.pmove, from, &cmd, runfuncs, *time );

		// motor!
		clgame.dllFuncs.pfnPlayerMove( clgame.pmove, false );

		// copy results back to client
		CL_FinishPMove( clgame.pmove, to );

		if( clgame.pmove->onground > 0 && clgame.pmove->onground < clgame.pmove->numphysent )
			cl.local.lastground = clgame.pmove->physents[clgame.pmove->onground].info;
		else cl.local.lastground = clgame.pmove->onground; // world(0) or in air(-1)
	}

	clgame.dllFuncs.pfnPostRunCmd( from, to, &cmd, runfuncs, *time, random_seed );

	*time += (double)cmd.msec / 1000.0;
}


/*
=================
CL_MoveSpectatorCamera

spectator movement code
=================
*/
void CL_MoveSpectatorCamera( void )
{
	double	time = cl.time;

	if( !cls.spectator )
		return;

	CL_SetUpPlayerPrediction( false, true );
	CL_SetSolidPlayers( cl.playernum );
	CL_RunUsercmd( &cls.spectator_state, &cls.spectator_state, &cl.cmd, true, &time, (uint)( time * 100.0 ));

	VectorCopy( cls.spectator_state.client.velocity, cl.simvel );
	VectorCopy( cls.spectator_state.client.origin, cl.simorg );
	VectorCopy( cls.spectator_state.client.punchangle, cl.punchangle );
	VectorCopy( cls.spectator_state.client.view_ofs, cl.viewheight );
}

/*
=================
CL_PredictMovement

Sets cl.predicted.origin and cl.predicted.angles
=================
*/
void CL_PredictMovement( qboolean repredicting )
{
	runcmd_t		*to_cmd = NULL, *from_cmd;
	local_state_t	*from = NULL, *to = NULL;
	frame_t *frame = NULL;
	uint		i, stoppoint;
	double		f = 1.0;
	double		time;

	if( cls.state != ca_active || cls.spectator )
		return;

	if( cls.demoplayback && !repredicting )
		CL_DemoInterpolateAngles();

	CL_SetUpPlayerPrediction( false, false );

	if( !cl.validsequence )
		return;

	if(( cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged ) >= CL_UPDATE_MASK )
		return;

	// this is the last frame received from the server
	frame = &cl.frames[cl.parsecountmod];

	if( !CL_IsPredicted( ))
	{
		VectorCopy( frame->clientdata.velocity, cl.simvel );
		VectorCopy( frame->clientdata.origin, cl.simorg );
		VectorCopy( frame->clientdata.punchangle, cl.punchangle );
		VectorCopy( frame->clientdata.view_ofs, cl.viewheight );
		cl.local.usehull = frame->playerstate[cl.playernum].usehull;
		cl.local.waterlevel = frame->clientdata.waterlevel;

		if( FBitSet( frame->clientdata.flags, FL_ONGROUND ))
			cl.local.onground = frame->playerstate[cl.playernum].onground;
		else cl.local.onground = -1;
	}

	from = &cl.predicted_frames[cl.parsecountmod];
	from_cmd = &cl.commands[cls.netchan.incoming_acknowledged & CL_UPDATE_MASK];
	memcpy( from->weapondata, frame->weapondata, sizeof( from->weapondata ));
	from->playerstate = frame->playerstate[cl.playernum];
	from->client = frame->clientdata;
	if( !frame->valid ) return;

	time = frame->time;
	stoppoint = ( repredicting ) ? 0 : 1;
	cl.local.repredicting = repredicting;
	cl.local.onground = -1;

	// predict forward until cl.time <= to->senttime
	CL_PushPMStates();
	CL_SetSolidPlayers( cl.playernum );

	for( i = 1; i < CL_UPDATE_MASK && cls.netchan.incoming_acknowledged + i < cls.netchan.outgoing_sequence + stoppoint; i++ )
	{
		uint		current_command;
		uint		current_command_mod;
		qboolean		runfuncs;

		current_command = cls.netchan.incoming_acknowledged + i;
		current_command_mod = current_command & CL_UPDATE_MASK;

		to = &cl.predicted_frames[(cl.parsecountmod + i) & CL_UPDATE_MASK];
		to_cmd = &cl.commands[current_command_mod];
		runfuncs = ( !repredicting && !to_cmd->processedfuncs );

		CL_RunUsercmd( from, to, &to_cmd->cmd, runfuncs, &time, current_command );
		VectorCopy( to->playerstate.origin, cl.local.predicted_origins[current_command_mod] );
		to_cmd->processedfuncs = true;

		if( to_cmd->senttime >= host.realtime )
			break;

		from = to;
		from_cmd = to_cmd;
	}

	CL_PopPMStates();

	if(( i == CL_UPDATE_MASK ) || ( !to && !repredicting ))
	{
		cl.local.repredicting = false;
		return; // net hasn't deliver packets in a long time...
	}

	if( !to )
	{
		to = from;
		to_cmd = from_cmd;
	}

	if( !CL_IsPredicted( ))
	{
		// keep onground actual
		if( FBitSet( frame->clientdata.flags, FL_ONGROUND ))
			cl.local.onground = frame->playerstate[cl.playernum].onground;
		else cl.local.onground = -1;

		if( !repredicting || !cl_lw.value )
			cl.local.viewmodel = to->client.viewmodel;
		cl.local.repredicting = false;
		cl.local.moving = false;
		return;
	}

	// now interpolate some fraction of the final frame
	if( to_cmd->senttime != from_cmd->senttime )
		f = bound( 0.0, (host.realtime - from_cmd->senttime) / (to_cmd->senttime - from_cmd->senttime) * 0.1, 1.0 );
	else f = 0.0;

	if( CL_PlayerTeleported( from, to ))
	{
		VectorCopy( to->client.velocity, cl.simvel );
		VectorCopy( to->playerstate.origin, cl.simorg );
		VectorCopy( to->client.punchangle, cl.punchangle );
		VectorCopy( to->client.view_ofs, cl.viewheight );
	}
	else
	{
		VectorLerp( from->playerstate.origin, f, to->playerstate.origin, cl.simorg );
		VectorLerp( from->client.velocity, f, to->client.velocity, cl.simvel );
		VectorLerp( from->client.punchangle, f, to->client.punchangle, cl.punchangle );

		if( from->playerstate.usehull == to->playerstate.usehull )
			VectorLerp( from->client.view_ofs, f, to->client.view_ofs, cl.viewheight );
		else VectorCopy( to->client.view_ofs, cl.viewheight );
	}

	cl.local.waterlevel = to->client.waterlevel;
	cl.local.usehull = to->playerstate.usehull;
	if( !repredicting || !cl_lw.value )
		cl.local.viewmodel = to->client.viewmodel;

	if( FBitSet( to->client.flags, FL_ONGROUND ))
	{
		cl_entity_t	*ent = CL_GetEntityByIndex( cl.local.lastground );
		cl.local.onground = cl.local.lastground;
		cl.local.moving = false;

		if( ent )
		{
			vec3_t delta;

			delta[0] = ent->curstate.origin[0] - ent->prevstate.origin[0];
			delta[1] = ent->curstate.origin[1] - ent->prevstate.origin[1];
			delta[2] = 0.0f;

			if( VectorLength( delta ) > 0.0f )
			{
				cls.correction_time = 0;
				cl.local.moving = true;
			}
		}
	}
	else
	{
		cl.local.onground = -1;
		cl.local.moving = false;
	}

	if( cls.correction_time > 0 && !cl_nosmooth.value && cl_smoothtime.value )
	{
		vec3_t delta;
		float frac;

		// only decay timer once per frame
		if( !repredicting )
			cls.correction_time -= host.frametime;

		// Make sure smoothtime is postive
		if( cl_smoothtime.value <= 0.0f )
			Cvar_DirectSet( &cl_smoothtime, "0.1" );

		// Clamp from 0 to cl_smoothtime.value
		cls.correction_time = bound( 0.0, cls.correction_time, cl_smoothtime.value );

		// Compute backward interpolation fraction along full correction
		frac = 1.0f - cls.correction_time / cl_smoothtime.value;

		// Determine how much error we still have to make up for
		VectorSubtract( cl.simorg, cl.local.lastorigin, delta );

		// Scale the error by the backlerp fraction
		VectorScale( delta, frac, delta );

		// Go some fraction of the way
		// FIXME, Probably can't do this any more
		VectorAdd( cl.local.lastorigin, delta, cl.simorg );
	}

	VectorCopy( cl.simorg, cl.local.lastorigin );
	cl.local.repredicting = false;
}
