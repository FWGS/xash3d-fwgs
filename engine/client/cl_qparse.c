/*
cl_qparse.c - parse a message received from the Quake demo
Copyright (C) 2018 Uncle Mike

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
#include "particledef.h"
#include "cl_tent.h"
#include "shake.h"
#include "hltv.h"
#include "input.h"
#include "base_cmd.h"

enum {
	STAT_HEALTH = 0,
	STAT_FRAGS,
	STAT_WEAPON,
	STAT_AMMO,
	STAT_ARMOR,
	STAT_WEAPONFRAME,
	STAT_SHELLS,
	STAT_NAILS,
	STAT_ROCKETS,
	STAT_CELLS,
	STAT_ACTIVEWEAPON,
	STAT_TOTALSECRETS,
	STAT_TOTALMONSTERS,
	STAT_SECRETS,  // bumped on client side by svc_foundsecret
	STAT_MONSTERS, // bumped by svc_killedmonster
	MAX_STATS =	32,
};

static char	cmd_buf[8192];
static char	msg_buf[8192];
static sizebuf_t	msg_demo;

/*
==================
CL_DispatchQuakeMessage

==================
*/
static void CL_DispatchQuakeMessage( const char *name )
{
	CL_DispatchUserMessage( name, msg_demo.iCurBit >> 3, msg_demo.pData );
	MSG_Clear( &msg_demo ); // don't forget to clear buffer
}

/*
==================
CL_ParseQuakeStats

redirect to qwrap->client
==================
*/
static void CL_ParseQuakeStats( sizebuf_t *msg )
{
	MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));	// stat num
	MSG_WriteLong( &msg_demo, MSG_ReadLong( msg ));	// stat value
	CL_DispatchQuakeMessage( "Stats" );
}

/*
==================
CL_EntityTeleported

check for instant movement in case
we don't want interpolate this
==================
*/
static qboolean CL_QuakeEntityTeleported( cl_entity_t *ent, entity_state_t *newstate )
{
	float	len, maxlen;
	vec3_t	delta;

	VectorSubtract( newstate->origin, ent->prevstate.origin, delta );

	// compute potential max movement in units per frame and compare with entity movement
	maxlen = ( clgame.movevars.maxvelocity * ( 1.0f / GAME_FPS ));
	len = VectorLength( delta );

	return (len > maxlen);
}

/*
==================
CL_ParseQuakeStats

redirect to qwrap->client
==================
*/
static int CL_UpdateQuakeStats( sizebuf_t *msg, int statnum, qboolean has_update )
{
	int 	value = 0;

	MSG_WriteByte( &msg_demo, statnum );	// stat num

	if( has_update )
	{
		if( statnum == STAT_HEALTH )
			value = MSG_ReadShort( msg );
		else value = MSG_ReadByte( msg );
	}

	MSG_WriteLong( &msg_demo, value );
	CL_DispatchQuakeMessage( "Stats" );

	return value;
}

/*
==================
CL_UpdateQuakeGameMode

redirect to qwrap->client
==================
*/
static void CL_UpdateQuakeGameMode( int gamemode )
{
	MSG_WriteByte( &msg_demo, gamemode );
	CL_DispatchQuakeMessage( "GameMode" );
}

/*
==================
CL_ParseQuakeSound

==================
*/
static void CL_ParseQuakeSound( sizebuf_t *msg )
{
	int 	channel, sound;
	int	flags, entnum;
	float 	volume, attn;
	sound_t	handle;
	vec3_t	pos;

	flags = MSG_ReadByte( msg );

	if( FBitSet( flags, SND_VOLUME ))
		volume = (float)MSG_ReadByte( msg ) / 255.0f;
	else volume = VOL_NORM;

	if( FBitSet( flags, SND_ATTENUATION ))
		attn = (float)MSG_ReadByte( msg ) / 64.0f;
	else attn = ATTN_NONE;

	channel = MSG_ReadWord( msg );
	sound = MSG_ReadByte( msg );	// Quake1 have max 255 precached sounds. erm

	// positioned in space
	MSG_ReadVec3Coord( msg, pos );

	entnum = channel >> 3;	// entity reletive
	channel &= 7;

	// see precached sound
	handle = cl.sound_index[sound];

	if( !cl.audio_prepped )
		return; // too early

	S_StartSound( pos, entnum, channel, handle, volume, attn, PITCH_NORM, flags );
}

/*
==================
CL_ParseQuakeServerInfo

==================
*/
static void CL_ParseQuakeServerInfo( sizebuf_t *msg )
{
	resource_t	*pResource;
	const char	*pResName;
	int		gametype;
	int		i;

	Con_Reportf( "Serverdata packet received.\n" );
	cls.timestart = Platform_DoubleTime();

	cls.demowaiting = false;	// server is changed

	// wipe the client_t struct
	if( !cls.changelevel && !cls.changedemo )
		CL_ClearState ();
	cl.background = (cls.demonum != -1) ? true : false;
	cls.state = ca_connected;

	// parse protocol version number
	i = MSG_ReadLong( msg );

	if( i != PROTOCOL_VERSION_QUAKE )
	{
		Con_Printf( "\n" S_ERROR "Server use invalid protocol (%i should be %i)\n", i, PROTOCOL_VERSION_QUAKE );
		CL_StopPlayback();
		Host_AbortCurrentFrame();
	}

	cl.maxclients = MSG_ReadByte( msg );
	gametype = MSG_ReadByte( msg );
	clgame.maxEntities = GI->max_edicts;
	clgame.maxEntities = bound( 600, clgame.maxEntities, MAX_EDICTS );
	clgame.maxModels = MAX_MODELS;
	Q_strncpy( clgame.maptitle, MSG_ReadString( msg ), sizeof( clgame.maptitle ));

	// Re-init hud video, especially if we changed game directories
	clgame.dllFuncs.pfnVidInit();

	if( Con_FixedFont( ))
	{
		// seperate the printfs so the server message can have a color
		Con_Print( "\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n" );
		Con_Print( va( "%c%s\n\n", 2, clgame.maptitle ));
	}

	// multiplayer game?
	if( cl.maxclients > 1 )
	{
		// allow console in multiplayer games
		host.allow_console = true;

		// loading user settings
		CSCR_LoadDefaultCVars( "user.scr" );

		if( r_decals.value > mp_decals.value )
			Cvar_DirectSet( &r_decals, mp_decals.string );
	}
	else Cvar_DirectSet( &r_decals, NULL );

	// tell the game parts about background state
	Cvar_DirectFullSet( &cl_background, cl.background ? "1" : "0", FCVAR_READ_ONLY );

	S_StopBackgroundTrack ();

	if( !cls.changedemo )
		UI_SetActiveMenu( cl.background );
	else if( !cls.demoplayback )
		Key_SetKeyDest( key_menu );

	// don't reset cursor in background mode
	if( cl.background )
		IN_MouseRestorePos();

	// will be changed later
	cl.viewentity = cl.playernum + 1;
	gameui.globals->maxClients = cl.maxclients;
	Q_strncpy( gameui.globals->maptitle, clgame.maptitle, sizeof( gameui.globals->maptitle ));

	if( !cls.changelevel && !cls.changedemo )
		CL_InitEdicts( cl.maxclients ); // re-arrange edicts

	// Quake just have a large packet of initialization data
	for( i = 1; i < MAX_MODELS; i++ )
	{
		pResName = MSG_ReadString( msg );

		if( COM_StringEmptyOrNULL( pResName ))
			break; // end of list

		pResource = Mem_Calloc( cls.mempool, sizeof( resource_t ));
		pResource->type = t_model;

		Q_strncpy( pResource->szFileName, pResName, sizeof( pResource->szFileName ));
		if( i == 1 ) Q_strncpy( clgame.mapname, pResName, sizeof( clgame.mapname ));
		pResource->nDownloadSize = -1;
		pResource->nIndex = i;

		CL_AddToResourceList( pResource, &cl.resourcesneeded );
	}

	for( i = 1; i < MAX_SOUNDS; i++ )
	{
		pResName = MSG_ReadString( msg );

		if( COM_StringEmptyOrNULL( pResName ))
			break; // end of list

		pResource = Mem_Calloc( cls.mempool, sizeof( resource_t ));
		pResource->type = t_sound;

		Q_strncpy( pResource->szFileName, pResName, sizeof( pResource->szFileName ));
		pResource->nDownloadSize = -1;
		pResource->nIndex = i;

		CL_AddToResourceList( pResource, &cl.resourcesneeded );
	}

	// get splash name
	if( cls.demoplayback && ( cls.demonum != -1 ))
		Cvar_Set( "cl_levelshot_name", va( "levelshots/%s_%s", cls.demoname, refState.wideScreen ? "16x9" : "4x3" ));
	else Cvar_Set( "cl_levelshot_name", va( "levelshots/%s_%s", clgame.mapname, refState.wideScreen ? "16x9" : "4x3" ));
	Cvar_SetValue( "scr_loading", 0.0f ); // reset progress bar

	if(( cl_allow_levelshots.value && !cls.changelevel ) || cl.background )
	{
		if( !FS_FileExists( va( "%s.bmp", cl_levelshot_name.string ), true ))
			Cvar_Set( "cl_levelshot_name", "*black" ); // render a black screen
		cls.scrshot_request = scrshot_plaque; // request levelshot even if exist (check filetime)
	}

	memset( &clgame.movevars, 0, sizeof( clgame.movevars ));
	memset( &clgame.oldmovevars, 0, sizeof( clgame.oldmovevars ));
	memset( &clgame.centerPrint, 0, sizeof( clgame.centerPrint ));
	cl.video_prepped = false;
	cl.audio_prepped = false;

	// GAME_COOP or GAME_DEATHMATCH
	CL_UpdateQuakeGameMode( gametype );

	// now we can start to precache
	CL_BatchResourceRequest( true );

	clgame.movevars.wateralpha = 1.0f;
	clgame.entities->curstate.scale = 0.0f;
	clgame.movevars.waveHeight = 0.0f;
	clgame.movevars.zmax = 14172.0f;	// 8192 * 1.74
	clgame.movevars.gravity = 800.0f;	// quake doesn't write gravity in demos
	clgame.movevars.maxvelocity = 2000.0f;

	clgame.oldmovevars = clgame.movevars;
}

/*
==================
CL_ParseQuakeClientData

==================
*/
static void CL_ParseQuakeClientData( sizebuf_t *msg )
{
	int	i, bits = MSG_ReadWord( msg );
	frame_t	*frame;

	// this is the frame update that this message corresponds to
	i = cls.netchan.incoming_sequence;

	cl.parsecount = i;					// ack'd incoming messages.
	cl.parsecountmod = cl.parsecount & CL_UPDATE_MASK;	// index into window.
	frame = &cl.frames[cl.parsecountmod];			// frame at index.
	frame->time = cl.mtime[0];				// mark network received time
	frame->receivedtime = host.realtime;			// time now that we are parsing.
	memset( &frame->graphdata, 0, sizeof( netbandwidthgraph_t ));
	memset( frame->flags, 0, sizeof( frame->flags ));
	frame->first_entity = cls.next_client_entities;
	frame->num_entities = 0;
	frame->valid = true; // assume valid

	if( FBitSet( bits, SU_VIEWHEIGHT ))
		frame->clientdata.view_ofs[2] = MSG_ReadChar( msg );
	else frame->clientdata.view_ofs[2] = 22.0f;

	if( FBitSet( bits, SU_IDEALPITCH ))
		cl.local.idealpitch = MSG_ReadChar( msg );
	else cl.local.idealpitch = 0;

	for( i = 0; i < 3; i++ )
	{
		if( FBitSet( bits, SU_PUNCH1 << i ))
			frame->clientdata.punchangle[i] = (float)MSG_ReadChar( msg );
		else frame->clientdata.punchangle[i] = 0.0f;

		if( FBitSet( bits, ( SU_VELOCITY1 << i )))
			frame->clientdata.velocity[i] = MSG_ReadChar( msg ) * 16.0f;
		else frame->clientdata.velocity[i] = 0;
	}

	if( FBitSet( bits, SU_ONGROUND ))
		SetBits( frame->clientdata.flags, FL_ONGROUND );
	if( FBitSet( bits, SU_INWATER ))
		SetBits( frame->clientdata.flags, FL_INWATER );

	// [always sent]
	MSG_WriteLong( &msg_demo, MSG_ReadLong( msg ));
	CL_DispatchQuakeMessage( "Items" );

	if( FBitSet( bits, SU_WEAPONFRAME ))
		CL_UpdateQuakeStats( msg, STAT_WEAPONFRAME, true );
	else CL_UpdateQuakeStats( msg, STAT_WEAPONFRAME, false );

	if( FBitSet( bits, SU_ARMOR ))
		CL_UpdateQuakeStats( msg, STAT_ARMOR, true );
	else CL_UpdateQuakeStats( msg, STAT_ARMOR, false );

	if( FBitSet( bits, SU_WEAPON ))
		frame->clientdata.viewmodel = CL_UpdateQuakeStats( msg, STAT_WEAPON, true );
	else frame->clientdata.viewmodel = CL_UpdateQuakeStats( msg, STAT_WEAPON, false );

	cl.local.health = CL_UpdateQuakeStats( msg, STAT_HEALTH, true );
	CL_UpdateQuakeStats( msg, STAT_AMMO, true );
	CL_UpdateQuakeStats( msg, STAT_SHELLS, true );
	CL_UpdateQuakeStats( msg, STAT_NAILS, true );
	CL_UpdateQuakeStats( msg, STAT_ROCKETS, true );
	CL_UpdateQuakeStats( msg, STAT_CELLS, true );
	CL_UpdateQuakeStats( msg, STAT_ACTIVEWEAPON, true );
}

/*
==================
CL_ParseQuakeEntityData

Parse an entity update message from the server
If an entities model or origin changes from frame to frame, it must be
relinked.  Other attributes can change without relinking.
==================
*/
static void CL_ParseQuakeEntityData( sizebuf_t *msg, int bits )
{
	int		i, newnum, pack;
	qboolean		forcelink;
	entity_state_t	*state;
	frame_t		*frame;
	cl_entity_t	*ent;

	// first update is the final signon stage where we actually receive an entity (i.e., the world at least)
	if( cls.signon == ( SIGNONS - 1 ))
	{
		// we are done with signon sequence.
		cls.signon = SIGNONS;

		// Clear loading plaque.
		CL_SignonReply( PROTO_QUAKE );
	}

	// alloc next slot to store update
	state = &cls.packet_entities[cls.next_client_entities % cls.num_client_entities];
	cl.validsequence = cls.netchan.incoming_sequence;
	frame = &cl.frames[cl.parsecountmod];
	pack = frame->num_entities;

	if( FBitSet( bits, U_MOREBITS ))
	{
		i = MSG_ReadByte( msg );
		SetBits( bits, i << 8 );
	}

	if( FBitSet( bits, U_LONGENTITY ))
		newnum = MSG_ReadWord( msg );
	else newnum = MSG_ReadByte( msg );

	memset( state, 0, sizeof( *state ));
	SetBits( state->entityType, ENTITY_NORMAL );
	state->number = newnum;

	// mark all the players
	ent = CL_EDICT_NUM( newnum );
	ent->index = newnum; // enumerate entity index
	ent->player = CL_IsPlayerIndex( newnum );
	state->animtime = cl.mtime[0];

	if( ent->curstate.msg_time != cl.mtime[1] )
		forcelink = true;	// no previous frame to lerp from
	else forcelink = false;

	if( FBitSet( bits, U_MODEL ))
		state->modelindex = MSG_ReadByte( msg );
	else state->modelindex = ent->baseline.modelindex;

	if( FBitSet( bits, U_FRAME ))
		state->frame = MSG_ReadByte( msg );
	else state->frame = ent->baseline.frame;

	if( FBitSet( bits, U_COLORMAP ))
		state->colormap = MSG_ReadByte( msg );
	else state->colormap = ent->baseline.colormap;

	if( FBitSet( bits, U_SKIN ))
		state->skin = MSG_ReadByte( msg );
	else state->skin = ent->baseline.skin;

	if( FBitSet( bits, U_EFFECTS ))
		state->effects = MSG_ReadByte( msg );
	else state->effects = ent->baseline.effects;

	if( FBitSet( bits, U_ORIGIN1 ))
		state->origin[0] = MSG_ReadCoord( msg );
	else state->origin[0] = ent->baseline.origin[0];

	if( FBitSet( bits, U_ANGLE1 ))
		state->angles[0] = MSG_ReadAngle( msg );
	else state->angles[0] = ent->baseline.angles[0];

	if( FBitSet( bits, U_ORIGIN2 ))
		state->origin[1] = MSG_ReadCoord( msg );
	else state->origin[1] = ent->baseline.origin[1];

	if( FBitSet( bits, U_ANGLE2 ))
		state->angles[1] = MSG_ReadAngle( msg );
	else state->angles[1] = ent->baseline.angles[1];

	if( FBitSet( bits, U_ORIGIN3 ))
		state->origin[2] = MSG_ReadCoord( msg );
	else state->origin[2] = ent->baseline.origin[2];

	if( FBitSet( bits, U_ANGLE3 ))
		state->angles[2] = MSG_ReadAngle( msg );
	else state->angles[2] = ent->baseline.angles[2];

	if( FBitSet( bits, U_TRANS ))
	{
		int	temp = MSG_ReadFloat( msg );
		float	alpha = MSG_ReadFloat( msg );

		if( alpha == 0.0f ) alpha = 1.0f;

		if( alpha < 1.0f )
		{
			state->rendermode = kRenderTransTexture;
			state->renderamt = (int)(alpha * 255.0f);
		}

		if( temp == 2 && MSG_ReadFloat( msg ))
			SetBits( state->effects, EF_FULLBRIGHT );
	}

	if( FBitSet( bits, U_NOLERP ))
		state->movetype = MOVETYPE_STEP;
	else state->movetype = MOVETYPE_NOCLIP;

	if( CL_QuakeEntityTeleported( ent, state ))
	{
		// remove smooth stepping
		if( cl.viewentity == ent->index )
			cl.skip_interp = true;
		forcelink = true;
	}

	if( FBitSet( state->effects, 16 ))
		SetBits( state->effects, EF_NODRAW );

	if(( newnum - 1 ) == cl.playernum )
		VectorCopy( state->origin, frame->clientdata.origin );

	if( forcelink )
	{
		VectorCopy( state->origin, ent->baseline.vuser1 );

		SetBits( state->effects, EF_NOINTERP );

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
CL_ParseQuakeParticles

==================
*/
static void CL_ParseQuakeParticle( sizebuf_t *msg )
{
	int	count, color;
	vec3_t	org, dir;

	MSG_ReadVec3Coord( msg, org );
	dir[0] = MSG_ReadChar( msg ) * 0.0625f;
	dir[1] = MSG_ReadChar( msg ) * 0.0625f;
	dir[2] = MSG_ReadChar( msg ) * 0.0625f;
	count = MSG_ReadByte( msg );
	color = MSG_ReadByte( msg );
	if( count == 255 ) count = 1024;

	R_RunParticleEffect( org, dir, color, count );
}

/*
===================
CL_ParseQuakeStaticSound

===================
*/
static void CL_ParseQuakeStaticSound( sizebuf_t *msg )
{
	int	sound_num;
	float 	vol, attn;
	vec3_t	org;

	MSG_ReadVec3Coord( msg, org );
	sound_num = MSG_ReadByte( msg );
	vol = (float)MSG_ReadByte( msg ) / 255.0f;
	attn = (float)MSG_ReadByte( msg ) / 64.0f;

	S_StartSound( org, 0, CHAN_STATIC, cl.sound_index[sound_num], vol, attn, PITCH_NORM, 0 );
}

/*
==================
CL_ParseQuakeDamage

redirect to qwrap->client
==================
*/
static void CL_ParseQuakeDamage( sizebuf_t *msg )
{
	MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));	// armor
	MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));	// blood
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));	// direction
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));	// direction
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));	// direction
	CL_DispatchQuakeMessage( "Damage" );
}

/*
===================
CL_ParseStaticEntity

===================
*/
static void CL_ParseQuakeStaticEntity( sizebuf_t *msg )
{
	entity_state_t state = { 0 };
	cl_entity_t	*ent;
	int		i;

	if( !clgame.static_entities )
		clgame.static_entities = Mem_Calloc( clgame.mempool, sizeof( cl_entity_t ) * MAX_STATIC_ENTITIES );

	state.modelindex = MSG_ReadByte( msg );
	state.frame = MSG_ReadByte( msg );
	state.colormap = MSG_ReadByte( msg );
	state.skin = MSG_ReadByte( msg );
	state.origin[0] = MSG_ReadCoord( msg );
	state.angles[0] = MSG_ReadAngle( msg );
	state.origin[1] = MSG_ReadCoord( msg );
	state.angles[1] = MSG_ReadAngle( msg );
	state.origin[2] = MSG_ReadCoord( msg );
	state.angles[2] = MSG_ReadAngle( msg );

	i = clgame.numStatics;
	if( i >= MAX_STATIC_ENTITIES )
	{
		Con_Printf( S_ERROR "%s: static entities limit exceeded!\n", __func__ );
		return;
	}

	ent = &clgame.static_entities[i];
	clgame.numStatics++;

	ent->index = 0; // ???
	ent->baseline = state;
	ent->curstate = state;
	ent->prevstate = state;

	// statics may be respawned in game e.g. for demo recording
	if( cls.state == ca_connected || cls.state == ca_validate )
		ent->trivial_accept = INVALID_HANDLE;

	// setup the new static entity
	VectorCopy( ent->curstate.origin, ent->origin );
	VectorCopy( ent->curstate.angles, ent->angles );
	ent->model = CL_ModelHandle( state.modelindex );
	ent->curstate.framerate = 1.0f;
	CL_ResetLatchedVars( ent, true );

	if( ent->model != NULL )
	{
		// auto 'solid' faces
		if( FBitSet( ent->model->flags, MODEL_TRANSPARENT ) && Host_IsQuakeCompatible())
		{
			ent->curstate.rendermode = kRenderTransAlpha;
			ent->curstate.renderamt = 255;
		}
	}

	R_AddEfrags( ent );	// add link
}

/*
===================
CL_ParseQuakeBaseline

===================
*/
static void CL_ParseQuakeBaseline( sizebuf_t *msg )
{
	entity_state_t	state;
	cl_entity_t	*ent;
	int		newnum;

	newnum = MSG_ReadWord( msg ); // entnum

	if( newnum >= clgame.maxEntities )
		Host_Error( "%s: no free edicts\n", __func__ );

	// parse baseline
	memset( &state, 0, sizeof( state ));
	state.modelindex = MSG_ReadByte( msg );
	state.frame = MSG_ReadByte( msg );
	state.colormap = MSG_ReadByte( msg );
	state.skin = MSG_ReadByte( msg );
	state.origin[0] = MSG_ReadCoord( msg );
	state.angles[0] = MSG_ReadAngle( msg );
	state.origin[1] = MSG_ReadCoord( msg );
	state.angles[1] = MSG_ReadAngle( msg );
	state.origin[2] = MSG_ReadCoord( msg );
	state.angles[2] = MSG_ReadAngle( msg );

	ent = CL_EDICT_NUM( newnum );
	ent->index = newnum;
	ent->player = CL_IsPlayerIndex( newnum );
	ent->prevstate = ent->baseline = state;
}

/*
===================
CL_ParseQuakeTempEntity

===================
*/
static void CL_ParseQuakeTempEntity( sizebuf_t *msg )
{
	int	type = MSG_ReadByte( msg );

	MSG_WriteByte( &msg_demo, type );

	if( type == 17 )
		MSG_WriteString( &msg_demo, MSG_ReadString( msg ));

	// TE_LIGHTNING1, TE_LIGHTNING2, TE_LIGHTNING3, TE_BEAM, TE_LIGHTNING4
	if( type == 5 || type == 6 || type == 9 || type == 13 || type == 17 )
		MSG_WriteWord( &msg_demo, MSG_ReadWord( msg ));

	// all temp ents have position at beginning
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));
	MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));

	// TE_LIGHTNING1, TE_LIGHTNING2, TE_LIGHTNING3, TE_BEAM, TE_EXPLOSION3, TE_LIGHTNING4
	if( type == 5 || type == 6 || type == 9 || type == 13 || type == 16 || type == 17 )
	{
		// write endpos for beams
		MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));
		MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));
		MSG_WriteCoord( &msg_demo, MSG_ReadCoord( msg ));
	}

	// TE_EXPLOSION2
	if( type == 12 )
	{
		MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));
		MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));
	}

	// TE_SMOKE (nehahra)
	if( type == 18 )
		MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));

	CL_DispatchQuakeMessage( "TempEntity" );
}

/*
===================
CL_ParseQuakeSignon

very important message
===================
*/
static void CL_ParseQuakeSignon( sizebuf_t *msg )
{
	int	i = MSG_ReadByte( msg );

	if( i == 3 ) cls.signon = SIGNONS - 1;
	Con_Reportf( "%s: %d\n", __func__, i );
}

/*
==================
CL_ParseNehahraShowLMP

redirect to qwrap->client
==================
*/
static void CL_ParseNehahraShowLMP( sizebuf_t *msg )
{
	MSG_WriteString( &msg_demo, MSG_ReadString( msg ));
	MSG_WriteString( &msg_demo, MSG_ReadString( msg ));
	MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));
	MSG_WriteByte( &msg_demo, MSG_ReadByte( msg ));
	CL_DispatchQuakeMessage( "Stats" );
}

/*
==================
CL_ParseNehahraHideLMP

redirect to qwrap->client
==================
*/
static void CL_ParseNehahraHideLMP( sizebuf_t *msg )
{
	MSG_WriteString( &msg_demo, MSG_ReadString( msg ));
	CL_DispatchQuakeMessage( "Stats" );
}

/*
==================
CL_QuakeStuffText

==================
*/
static void CL_QuakeStuffText( const char *text )
{
	Q_strncat( cmd_buf, text, sizeof( cmd_buf ));

	// a1ba: didn't filtered, anyway quake protocol
	// only supported for demos, not network games
	Cbuf_AddText( text );
}

/*
==================
CL_QuakeExecStuff

==================
*/
static void CL_QuakeExecStuff( void )
{
	char	*text = cmd_buf;
	char	token[256];
	int	argc = 0;

	// check if no commands this frame
	if( COM_StringEmptyOrNULL( text ))
		return;

	while( 1 )
	{
		// skip whitespace up to a /n
		while( *text && ((byte)*text ) <= ' ' && *text != '\r' && *text != '\n' )
			text++;

		if( *text == '\n' || *text == '\r' )
		{
			// a newline seperates commands in the buffer
			if( *text == '\r' && text[1] == '\n' )
				text++;
			argc = 0;
			text++;
		}

		if( !*text ) break;

		text = COM_ParseFileSafe( text, token, sizeof( token ), PFILE_IGNOREBRACKET, NULL, NULL );

		if( !text ) break;

		if( argc == 0 )
		{
			// debug: find all missed commands and cvars to add them into QWrap
			cmdalias_t *alias;
			cmd_t *cmd;
			convar_t *cvar;

			BaseCmd_FindAll( token, &cmd, &alias, &cvar );

			if( !cvar && !cmd )
				Con_Printf( S_WARN "'%s' is not exist\n", token );
//			else Msg( "cmd: %s\n", token );

			// process some special commands
			if( !Q_stricmp( token, "playdemo" ))
				cls.changedemo = true;
			argc++;
		}
	}

	// reset the buffer
	cmd_buf[0] = '\0';
}

/*
==================
CL_ParseQuakeMessage

==================
*/
void CL_ParseQuakeMessage( sizebuf_t *msg )
{
	int		cmd, param1, param2;
	size_t		bufStart;
	const char	*str;

	// init excise buffer
	MSG_Init( &msg_demo, "UserMsg", msg_buf, sizeof( msg_buf ));

	// parse the message
	while( 1 )
	{
		if( MSG_CheckOverflow( msg ))
		{
			Host_Error( "%s: overflow!\n", __func__ );
			return;
		}

		// mark start position
		bufStart = MSG_GetNumBytesRead( msg );

		// end of message (align bits)
		if( MSG_GetNumBitsLeft( msg ) < 8 )
			break;

		cmd = MSG_ReadServerCmd( msg );

		// if the high bit of the command byte is set, it is a fast update
		if( FBitSet( cmd, 128 ))
		{
			CL_ParseQuakeEntityData( msg, cmd & 127 );
			continue;
		}

		// record command for debugging spew on parse problem
		CL_Parse_RecordCommand( cmd, bufStart );

		// other commands
		switch( cmd )
		{
		case svc_nop:
			// this does nothing
			break;
		case svc_disconnect:
			CL_DemoCompleted ();
			break;
		case svc_updatestat:
			CL_ParseQuakeStats( msg );
			break;
		case svc_version:
			param1 = MSG_ReadLong( msg );
			if( param1 != PROTOCOL_VERSION_QUAKE )
				Host_Error( "Server is protocol %i instead of %i\n", param1, PROTOCOL_VERSION_QUAKE );
			break;
		case svc_setview:
			CL_ParseViewEntity( msg );
			break;
		case svc_sound:
			CL_ParseQuakeSound( msg );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_time:
			Cbuf_AddText( "\n" ); // new frame was started
			CL_ParseServerTime( msg, PROTO_QUAKE );
			break;
		case svc_print:
			str = MSG_ReadString( msg );
			Con_Printf( "%s%s", str, *str == 2 ? "\n" : "" );
			break;
		case svc_stufftext:
			CL_QuakeStuffText( MSG_ReadString( msg ));
			break;
		case svc_setangle:
			cl.viewangles[0] = MSG_ReadAngle( msg );
			cl.viewangles[1] = MSG_ReadAngle( msg );
			cl.viewangles[2] = MSG_ReadAngle( msg );
			break;
		case svc_serverdata:
			Cbuf_Execute(); // make sure any stuffed commands are done
			CL_ParseQuakeServerInfo( msg );
			break;
		case svc_lightstyle:
			CL_ParseLightStyle( msg, PROTO_QUAKE );
			break;
		case svc_updatename:
			param1 = MSG_ReadByte( msg );
			Q_strncpy( cl.players[param1].name, MSG_ReadString( msg ), sizeof( cl.players[0].name ));
			Q_strncpy( cl.players[param1].model, "player", sizeof( cl.players[0].name ));
			break;
		case svc_updatefrags:
			param1 = MSG_ReadByte( msg );
			param2 = MSG_ReadShort( msg );
			// HACKHACK: store frags into spectator
			cl.players[param1].spectator = param2;
			break;
		case svc_clientdata:
			CL_ParseQuakeClientData( msg );
			cl.frames[cl.parsecountmod].graphdata.client += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_stopsound:
			param1 = MSG_ReadWord( msg );
			S_StopSound( param1 >> 3, param1 & 7, NULL );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_updatecolors:
			param1 = MSG_ReadByte( msg );
			param2 = MSG_ReadByte( msg );
			cl.players[param1].topcolor = param2 & 0xF;
			cl.players[param1].bottomcolor = (param2 & 0xF0) >> 4;
			break;
		case svc_particle:
			CL_ParseQuakeParticle( msg );
			break;
		case svc_damage:
			CL_ParseQuakeDamage( msg );
			break;
		case svc_spawnstatic:
			CL_ParseQuakeStaticEntity( msg );
			break;
		case svc_spawnbinary:
			// never used in Quake
			break;
		case svc_spawnbaseline:
			CL_ParseQuakeBaseline( msg );
			break;
		case svc_temp_entity:
			CL_ParseQuakeTempEntity( msg );
			cl.frames[cl.parsecountmod].graphdata.tentities += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_setpause:
			cl.paused = MSG_ReadByte( msg );
			break;
		case svc_signonnum:
			CL_ParseQuakeSignon( msg );
			break;
		case svc_centerprint:
			str = MSG_ReadString( msg );
			CL_DispatchUserMessage( "HudText", Q_strlen( str ) + 1, (void *)str );
			break;
		case svc_killedmonster:
			CL_DispatchQuakeMessage( "KillMonster" ); // just an event
			break;
		case svc_foundsecret:
			CL_DispatchQuakeMessage( "FoundSecret" ); // just an event
			break;
		case svc_spawnstaticsound:
			CL_ParseQuakeStaticSound( msg );
			break;
		case svc_intermission:
			cl.intermission = 1;
			break;
		case svc_finale:
			CL_ParseFinaleCutscene( msg, 2 );
			break;
		case svc_cdtrack:
			param1 = MSG_ReadByte( msg );
			param1 = bound( 0, param1, MAX_CDTRACKS - 1 ); // tracknum
			param2 = MSG_ReadByte( msg );
			param2 = bound( 0, param2, MAX_CDTRACKS - 1 ); // loopnum
			if(( cls.demoplayback || cls.demorecording ) && ( cls.forcetrack != -1 ))
				S_StartBackgroundTrack( clgame.cdtracks[cls.forcetrack], clgame.cdtracks[cls.forcetrack], 0, false );
			else S_StartBackgroundTrack( clgame.cdtracks[param1], clgame.cdtracks[param2], 0, false );
			break;
		case svc_sellscreen:
			Cmd_ExecuteString( "help" );	// open quake menu
			break;
		case svc_cutscene:
			CL_ParseFinaleCutscene( msg, 3 );
			break;
		case svc_hidelmp:
			CL_ParseNehahraHideLMP( msg );
			break;
		case svc_showlmp:
			CL_ParseNehahraShowLMP( msg );
			break;
		case svc_skybox:
			Q_strncpy( clgame.movevars.skyName, MSG_ReadString( msg ), sizeof( clgame.movevars.skyName ));
			break;
		case svc_skyboxsize:
			MSG_ReadCoord( msg ); // obsolete
			break;
		case svc_fog:
			if( MSG_ReadByte( msg ))
			{
				float	fog_settings[4];
				int	packed_fog[4];

				fog_settings[3] = MSG_ReadFloat( msg );	// density
				fog_settings[0] = MSG_ReadByte( msg );	// red
				fog_settings[1] = MSG_ReadByte( msg );	// green
				fog_settings[2] = MSG_ReadByte( msg );	// blue
				packed_fog[0] = fog_settings[0] * 255;
				packed_fog[1] = fog_settings[1] * 255;
				packed_fog[2] = fog_settings[2] * 255;
				packed_fog[3] = fog_settings[3] * 255;
				clgame.movevars.fog_settings = (packed_fog[1]<<24)|(packed_fog[2]<<16)|(packed_fog[3]<<8)|packed_fog[0];
			}
			else
			{
				clgame.movevars.fog_settings = 0;
			}
			break;
		default:
			Host_Error( "%s: Illegible server message\n", __func__ );
			break;
		}
	}

	// now process packet.
	CL_ProcessPacket( &cl.frames[cl.parsecountmod] );

	// add new entities into physic lists
	CL_SetSolidEntities();

	// check deferred cmds
	CL_QuakeExecStuff();
}
