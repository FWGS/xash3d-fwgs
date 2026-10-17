/*
cl_democam.c - follow any player while a demo plays back
Copyright (C) 2026 Oscar Soler

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
#include "pm_defs.h"
#include "keydefs.h"
#include "studio.h"

/*
==============================================================================
DEMO CAMERA

A demo only stores what the recording client was sent, so it holds every
player when the server ran with sv_novis 1. While it plays back, the camera
can leave the recorded view and follow any of them, the way HLTV does it
but without help from client.dll (old ones like CS 1.3 have no spectator code).
Game specific: the eye height comes from democam_viewheight (28 in Half-Life,
17 in Counter-Strike):

  left click  - next player
  right click - previous player
  space       - recorded view -> first person -> third person
==============================================================================
*/
enum
{
	DEMOCAM_RECORDED = 0,
	DEMOCAM_FIRSTPERSON,
	DEMOCAM_CHASE,
	DEMOCAM_MODES
};

#define DEMOCAM_DUCK_VIEW_HEIGHT  12.0f // VEC_DUCK_VIEW, the same in HL and CS
#define DEMOCAM_CHASE_DISTANCE    96.0f
#define DEMOCAM_CHASE_HEIGHT      16.0f
#define DEMOCAM_FRAME_NOISE       4.0f

static CVAR_DEFINE_AUTO( democam_viewheight, "28", FCVAR_ARCHIVE, "demo camera: eye height of a standing player (VEC_VIEW of the game, 17 in Counter-Strike)" );

static int democam_mode;
static int democam_target; // entity index, 1..maxclients

/*
the demo can't show a silencer being put on or taken off, that animation
only plays for whoever does it, but every shot says whether it was there:
CS sends it as one of the boolean parameters of the weapon event
*/
static const struct
{
	const char *event;
	const char *viewmodel;
	int        bparam;
} democam_silencers[] =
{
	{ "events/m4a1.sc", "v_m4a1", 1 },
	{ "events/usp.sc",  "v_usp",  2 },
};

static qboolean democam_silenced[MAX_CLIENTS + 1][ARRAYSIZE( democam_silencers )];

static qboolean CL_DemoCamValidTarget( int index )
{
	cl_entity_t *ent;

	if( index < 1 || index > cl.maxclients )
		return false;

	ent = CL_GetEntityByIndex( index );

	// dead CS players turn into invisible observers
	if( !ent || !ent->model || ent->curstate.messagenum != cl.parsecount )
		return false;

	return !FBitSet( ent->curstate.effects, EF_NODRAW );
}

static void CL_DemoCamCycle( int dir )
{
	int i, index = democam_target;

	if( index < 1 || index > cl.maxclients )
		index = cl.playernum + 1;

	for( i = 0; i < cl.maxclients; i++ )
	{
		index += dir;

		if( index > cl.maxclients )
			index = 1;
		else if( index < 1 )
			index = cl.maxclients;

		if( CL_DemoCamValidTarget( index ))
		{
			democam_target = index;
			return;
		}
	}
}

/*
=================
CL_DemoCamActive

true while the camera shows someone other than the recorded view
=================
*/
qboolean CL_DemoCamActive( void )
{
	if( !cls.demoplayback || cl.background || democam_mode == DEMOCAM_RECORDED )
		return false;

	return CL_DemoCamValidTarget( democam_target );
}

/*
=================
CL_DemoCamHideEntity

the followed player is not drawn in first person, we're inside his head
=================
*/
qboolean CL_DemoCamFirstPerson( void )
{
	return democam_mode == DEMOCAM_FIRSTPERSON && CL_DemoCamActive();
}

qboolean CL_DemoCamHideEntity( const cl_entity_t *ent )
{
	return CL_DemoCamFirstPerson() && ent->index == democam_target;
}

static qboolean CL_DemoCamPlaying( void )
{
	return cls.demoplayback && !cl.background && cls.state == ca_active;
}

static void CL_DemoCamMode_f( void )
{
	if( !CL_DemoCamPlaying( ))
		return;

	democam_mode = ( democam_mode + 1 ) % DEMOCAM_MODES;

	if( democam_mode != DEMOCAM_RECORDED && !CL_DemoCamValidTarget( democam_target ))
		CL_DemoCamCycle( 1 );
}

static void CL_DemoCamFollow( int dir )
{
	if( !CL_DemoCamPlaying( ))
		return;

	if( democam_mode == DEMOCAM_RECORDED )
		democam_mode = DEMOCAM_FIRSTPERSON;

	CL_DemoCamCycle( dir );
}

static void CL_DemoCamNext_f( void )
{
	CL_DemoCamFollow( 1 );
}

static void CL_DemoCamPrev_f( void )
{
	CL_DemoCamFollow( -1 );
}

/*
=================
CL_DemoCamKey

takes the mouse buttons and space while a demo plays
=================
*/
qboolean CL_DemoCamKey( int key, qboolean down )
{
	if( !CL_DemoCamPlaying() || cls.key_dest != key_game )
		return false;

	if( key != K_MOUSE1 && key != K_MOUSE2 && key != K_SPACE )
		return false;

	if( !down )
		return true;

	if( key == K_SPACE )
		CL_DemoCamMode_f();
	else CL_DemoCamFollow( key == K_MOUSE1 ? 1 : -1 );

	return true;
}

void CL_DemoCamInit( void )
{
	Cvar_RegisterVariable( &democam_viewheight );
	Cmd_AddCommand( "democam_next", CL_DemoCamNext_f, "demo playback: follow the next player" );
	Cmd_AddCommand( "democam_prev", CL_DemoCamPrev_f, "demo playback: follow the previous player" );
	Cmd_AddCommand( "democam_mode", CL_DemoCamMode_f, "demo playback: recorded view, first person or third person" );
}

/*
=================
CL_DemoCamReset

every demo starts on the recorded view
=================
*/
void CL_DemoCamReset( void )
{
	democam_mode = DEMOCAM_RECORDED;
	democam_target = 0;
	memset( democam_silenced, 0, sizeof( democam_silenced ));
}

static void CL_DemoCamDrawInfo( void )
{
	con_nprint_t line = { .time_to_live = 0.1f, .color = { 1.0f, 0.7f, 0.1f }};
	const char *name;

	line.index = 1;

	if( !CL_DemoCamActive( ))
	{
		Con_NXPrintf( &line, "Recorded view  -  click: follow another player" );
		return;
	}

	name = cl.players[democam_target - 1].name;

	// the demo may have no userinfo for those who were playing before it started
	if( COM_StringEmpty( name ))
		name = va( "player %i", democam_target );

	Con_NXPrintf( &line, "Following %s (%s)  -  left/right click: switch  -  space: view",
		name,
		democam_mode == DEMOCAM_FIRSTPERSON ? "first person" : "third person" );
}

/*
=================
CL_DemoCamViewModel

the demo only knows the weapon the followed player carries on his model
(models/p_ak47.mdl), the one in his hands is its v_ version
=================
*/
static model_t *CL_DemoCamViewModel( const cl_entity_t *ent, int *index )
{
	model_t *pmodel = CL_ModelHandle( ent->curstate.weaponmodel );
	const char *base;
	char vname[MAX_QPATH];
	int i;

	if( !pmodel )
		return NULL;

	base = COM_FileWithoutPath( pmodel->name );

	if( Q_strnicmp( base, "p_", 2 ))
		return NULL;

	Q_snprintf( vname, sizeof( vname ), "%.*sv_%s", (int)( base - pmodel->name ), pmodel->name, base + 2 );

	for( i = 1; i < MAX_MODELS; i++ )
	{
		if( cl.models[i] && !Q_stricmp( cl.models[i]->name, vname ))
		{
			*index = i;
			return cl.models[i];
		}
	}

	return NULL;
}

enum
{
	DEMOCAM_ANIM_IDLE = 0,
	DEMOCAM_ANIM_DRAW,
	DEMOCAM_ANIM_SHOOT,
	DEMOCAM_ANIM_RELOAD,
};

static const char *democam_anim_names[][4] =
{
	{ "idle", NULL },
	{ "draw", "deploy", NULL },
	{ "shoot", "slash", "throw", "pressbutton" }, // knife, grenades and C4 have no "shoot"
	{ "reload", "insert", NULL }, // shotguns reload shell by shell
};

/*
=================
CL_DemoCamEvent

every event played back goes through here, the weapon ones tell
whether the player who fired had the silencer on
=================
*/
void CL_DemoCamEvent( const char *name, const event_args_t *args )
{
	int i;

	if( !cls.demoplayback || args->entindex < 1 || args->entindex > MAX_CLIENTS )
		return;

	for( i = 0; i < ARRAYSIZE( democam_silencers ); i++ )
	{
		if( !Q_stricmp( name, democam_silencers[i].event ))
			democam_silenced[args->entindex][i] = democam_silencers[i].bparam == 1 ? args->bparam1 : args->bparam2;
	}
}

static qboolean CL_DemoCamSilenced( const cl_entity_t *ent, const model_t *viewmodel )
{
	int i;

	if( ent->index < 1 || ent->index > MAX_CLIENTS )
		return false;

	for( i = 0; i < ARRAYSIZE( democam_silencers ); i++ )
	{
		if( Q_stristr( viewmodel->name, democam_silencers[i].viewmodel ))
			return democam_silenced[ent->index][i];
	}

	return false;
}

static mstudioseqdesc_t *CL_DemoCamSequences( model_t *model, int *numseq )
{
	studiohdr_t *hdr = (studiohdr_t *)Mod_StudioExtradata( model );

	if( !hdr || hdr->numseq <= 0 )
		return NULL;

	*numseq = hdr->numseq;
	return (mstudioseqdesc_t *)((byte *)hdr + hdr->seqindex );
}

/*
=================
CL_DemoCamFindSequence

viewmodels have no activities, but their sequences are named the same
way: idle1, shoot2, reload, draw... Weapons with a silencer have a second
set ending in _unsil, used when it's off
=================
*/
static int CL_DemoCamFindSequence( model_t *model, int anim, qboolean silenced )
{
	static int shot;
	mstudioseqdesc_t *seq;
	int numseq, i, j, found[16], numfound = 0;
	qboolean unsil = false;

	if( !( seq = CL_DemoCamSequences( model, &numseq )))
		return -1;

	for( i = 0; i < numseq; i++ )
	{
		if( Q_stristr( seq[i].label, "_unsil" ))
			unsil = true;
	}

	// with the silencer on, the plain names are the ones
	if( silenced )
		unsil = false;

	for( j = 0; j < 4 && democam_anim_names[anim][j] && !numfound; j++ )
	{
		const char *name = democam_anim_names[anim][j];

		for( i = 0; i < numseq && numfound < ARRAYSIZE( found ); i++ )
		{
			const char *label = seq[i].label;

			if( Q_strnicmp( label, name, Q_strlen( name )))
				continue;

			if( unsil != ( Q_stristr( label, "_unsil" ) != NULL ))
				continue;

			// shoot_empty and shootlast are for the last bullet
			if( anim == DEMOCAM_ANIM_SHOOT && ( Q_stristr( label, "empty" ) || Q_stristr( label, "last" )))
				continue;

			found[numfound++] = i;
		}
	}

	if( !numfound )
		return -1;

	// shots take turns between shoot1, shoot2...
	if( anim == DEMOCAM_ANIM_SHOOT )
		return found[shot++ % numfound];

	return found[0];
}

static float CL_DemoCamSequenceLength( model_t *model, int sequence )
{
	mstudioseqdesc_t *seq;
	int numseq;

	if( !( seq = CL_DemoCamSequences( model, &numseq )) || sequence < 0 || sequence >= numseq )
		return 0.0f;

	if( seq[sequence].fps <= 0.0f || seq[sequence].numframes <= 1 )
		return 0.0f;

	return ( seq[sequence].numframes - 1 ) / seq[sequence].fps;
}

/*
=================
CL_DemoCamPlayerAnim

nothing tells what the followed player does with his weapon, but his
own model shows it: CS plays ref_shoot_* / ref_reload_* on the upper
body, and every shot starts the sequence again from frame 0 (animtime
is no use, the server renews it every frame)
=================
*/
static int CL_DemoCamPlayerAnim( const cl_entity_t *ent )
{
	static int last_target, last_sequence;
	static float last_frame;
	mstudioseqdesc_t *seq;
	int numseq, anim = DEMOCAM_ANIM_IDLE;
	qboolean changed;

	// the frame arrives rounded and goes back a little now and then, a new shot drops it a lot
	changed = ent->curstate.sequence != last_sequence || ent->curstate.frame < last_frame - DEMOCAM_FRAME_NOISE;

	// a newly followed player starts with nothing going on
	if( ent->index != last_target )
		changed = false;

	last_target = ent->index;
	last_sequence = ent->curstate.sequence;
	last_frame = ent->curstate.frame;

	if( !changed || !( seq = CL_DemoCamSequences( ent->model, &numseq )) || ent->curstate.sequence >= numseq )
		return DEMOCAM_ANIM_IDLE;

	if( Q_stristr( seq[ent->curstate.sequence].label, "reload" ))
		anim = DEMOCAM_ANIM_RELOAD;
	else if( Q_stristr( seq[ent->curstate.sequence].label, "shoot" ))
		anim = DEMOCAM_ANIM_SHOOT;

	return anim;
}

static void CL_DemoCamSetupViewModel( const cl_entity_t *ent, const vec3_t eyes, const vec3_t angles )
{
	static int last_index, last_target, sequence;
	static qboolean last_silenced;
	static float anim_end;
	cl_entity_t *view = &clgame.viewent;
	int index = 0, anim, newseq = -1, numseq;
	qboolean silenced;

	view->model = CL_DemoCamViewModel( ent, &index );
	view->curstate.modelindex = index;
	anim = CL_DemoCamPlayerAnim( ent );

	if( !view->model )
		return;

	silenced = CL_DemoCamSilenced( ent, view->model );

	if( ent->index != last_target )
	{
		// just switched to him, he isn't drawing the weapon
		last_target = ent->index;
		last_index = index;
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_IDLE, silenced );
	}
	else if( index != last_index )
	{
		last_index = index;
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_DRAW, silenced );
	}
	else if( anim != DEMOCAM_ANIM_IDLE )
	{
		newseq = CL_DemoCamFindSequence( view->model, anim, silenced );
	}
	else if( silenced != last_silenced || ( anim_end && cl.time >= anim_end ))
	{
		// the shot or the reload is over, or a shot just told the silencer changed
		newseq = CL_DemoCamFindSequence( view->model, DEMOCAM_ANIM_IDLE, silenced );
		anim_end = 0.0f;
	}

	last_silenced = silenced;

	if( newseq >= 0 )
	{
		float length = CL_DemoCamSequenceLength( view->model, newseq );

		sequence = newseq;
		view->curstate.animtime = cl.time;
		anim_end = length > 0.0f ? cl.time + length : 0.0f;

		// idle loops by itself
		if( !Q_strnicmp( CL_DemoCamSequences( view->model, &numseq )[newseq].label, "idle", 4 ))
			anim_end = 0.0f;
	}

	// a weapon without the animation that was playing
	if( !CL_DemoCamSequences( view->model, &numseq ) || sequence >= numseq )
		sequence = 0;

	view->curstate.sequence = sequence;
	view->curstate.frame = 0.0f;
	view->curstate.framerate = 1.0f;
	view->curstate.body = 0;
	view->curstate.skin = 0;

	VectorCopy( eyes, view->origin );
	VectorCopy( eyes, view->curstate.origin );
	VectorCopy( eyes, view->latched.prevorigin );

	// studio models want the pitch upside down
	VectorSet( view->angles, -angles[PITCH], angles[YAW], 0.0f );
	VectorCopy( view->angles, view->curstate.angles );
	VectorCopy( view->angles, view->latched.prevangles );
}

/*
=================
CL_DemoCamApply

puts the view on the followed player, after client.dll has done its own
=================
*/
void CL_DemoCamApply( ref_params_t *fd )
{
	cl_entity_t *ent;
	vec3_t eyes, angles, forward, end;
	pmtrace_t tr;

	// menu background demos keep their own view
	if( !cls.demoplayback || cl.background )
		return;

	CL_DemoCamDrawInfo();

	if( !CL_DemoCamActive( ))
		return;

	ent = CL_GetEntityByIndex( democam_target );

	VectorCopy( ent->origin, eyes );
	eyes[2] += ent->curstate.usehull == 1 ? DEMOCAM_DUCK_VIEW_HEIGHT : democam_viewheight.value;

	// player models carry a third of the real pitch, upside down
	VectorCopy( ent->angles, angles );
	angles[PITCH] *= -3.0f;
	angles[ROLL] = 0.0f;

	if( democam_mode == DEMOCAM_CHASE )
	{
		AngleVectors( angles, forward, NULL, NULL );
		VectorMA( eyes, -DEMOCAM_CHASE_DISTANCE, forward, end );
		end[2] += DEMOCAM_CHASE_HEIGHT;

		// don't go through walls
		tr = CL_TraceLine( eyes, end, PM_STUDIO_IGNORE|PM_GLASS_IGNORE );
		VectorMA( tr.endpos, 4.0f, tr.plane.normal, eyes );
	}

	else CL_DemoCamSetupViewModel( ent, eyes, angles );

	VectorCopy( eyes, fd->vieworg );
	VectorCopy( angles, fd->viewangles );
}
