/*
cl_parse.c - parse a message received from the server
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
#include "particledef.h"
#include "cl_tent.h"
#include "shake.h"
#include "hltv.h"
#include "input.h"


/*
==================
CL_ParseStaticEntity

static client entity
==================
*/
static void CL_LegacyParseStaticEntity( sizebuf_t *msg )
{
	int		i;
	entity_state_t state = { 0 };
	cl_entity_t	*ent;

	if( !clgame.static_entities )
		clgame.static_entities = Mem_Calloc( clgame.mempool, sizeof( cl_entity_t ) * MAX_STATIC_ENTITIES );

	state.modelindex = MSG_ReadShort( msg );
	state.sequence = MSG_ReadByte( msg );
	state.frame = MSG_ReadByte( msg );
	state.colormap = MSG_ReadWord( msg );
	state.skin = MSG_ReadByte( msg );

	for( i = 0; i < 3; i++ )
	{
		state.origin[i] = MSG_ReadCoord( msg );
		state.angles[i] = MSG_ReadBitAngle( msg, 16 );
	}

	state.rendermode = MSG_ReadByte( msg );

	if( state.rendermode != kRenderNormal )
	{
		state.renderamt = MSG_ReadByte( msg );
		state.rendercolor.r = MSG_ReadByte( msg );
		state.rendercolor.g = MSG_ReadByte( msg );
		state.rendercolor.b = MSG_ReadByte( msg );
		state.renderfx = MSG_ReadByte( msg );
	}

	i = clgame.numStatics;
	if( i >= MAX_STATIC_ENTITIES )
	{
		Con_Printf( S_ERROR "MAX_STATIC_ENTITIES limit exceeded!\n" );
		return;
	}

	ent = &clgame.static_entities[i];
	clgame.numStatics++;

	// all states are same
	ent->baseline = ent->curstate = ent->prevstate = state;
	ent->index = 0; // static entities doesn't has the numbers

	// statics may be respawned in game e.g. for demo recording
	if( cls.state == ca_connected || cls.state == ca_validate )
		ent->trivial_accept = INVALID_HANDLE;

	// setup the new static entity
	VectorCopy( ent->curstate.origin, ent->origin );
	VectorCopy( ent->curstate.angles, ent->angles );
	ent->model = CL_ModelHandle( state.modelindex );
	ent->curstate.framerate = 1.0f;
	CL_ResetLatchedVars( ent, true );

	if( ent->curstate.rendermode == kRenderNormal && ent->model != NULL )
	{
		// auto 'solid' faces
		if( FBitSet( ent->model->flags, MODEL_TRANSPARENT ) && Host_IsQuakeCompatible( ))
		{
			ent->curstate.rendermode = kRenderTransAlpha;
			ent->curstate.renderamt = 255;
		}
	}

	R_AddEfrags( ent );	// add link
}

static void CL_LegacyParseSoundPacket( sizebuf_t *msg, qboolean is_ambient )
{
	vec3_t	pos;
	int 	chan, sound;
	float 	volume, attn;
	int	flags, pitch, entnum;
	sound_t	handle = 0;

	flags = MSG_ReadWord( msg );
	if( flags & SND_LEGACY_LARGE_INDEX )
	{
		sound = MSG_ReadWord( msg );
		flags &= ~SND_LEGACY_LARGE_INDEX;
	}
	else
		sound = MSG_ReadByte( msg );
	chan = MSG_ReadByte( msg );

	if( FBitSet( flags, SND_VOLUME ))
		volume = (float)MSG_ReadByte( msg ) / 255.0f;
	else volume = VOL_NORM;

	if( FBitSet( flags, SND_ATTENUATION ))
		attn = (float)MSG_ReadByte( msg ) / 64.0f;
	else attn = ATTN_NONE;

	if( FBitSet( flags, SND_PITCH ))
		pitch = MSG_ReadByte( msg );
	else pitch = PITCH_NORM;

	// entity reletive
	entnum = MSG_ReadWord( msg );

	// positioned in space
	MSG_ReadVec3Coord( msg, pos );

	if( FBitSet( flags, SND_SENTENCE ))
	{
		char	sentenceName[32];
		Q_snprintf( sentenceName, sizeof( sentenceName ), "!%i", sound );
		handle = S_RegisterSound( sentenceName );
	}
	else handle = cl.sound_index[sound];	// see precached sound

	if( !cl.audio_prepped )
		return; // too early

	// g-cont. sound and ambient sound have only difference with channel
	if( is_ambient )
	{
		S_AmbientSound( pos, entnum, handle, volume, attn, pitch, flags );
	}
	else
	{
		S_StartSound( pos, entnum, chan, handle, volume, attn, pitch, flags );
	}
}
/*
================
CL_PrecacheSound

prceache sound from server
================
*/
static void CL_LegacyPrecacheSound( sizebuf_t *msg )
{
	int	soundIndex;

	soundIndex = MSG_ReadUBitLong( msg, MAX_SOUND_BITS );

	if( soundIndex < 0 || soundIndex >= MAX_SOUNDS )
		Host_Error( "%s: bad soundindex %i\n", __func__, soundIndex );

	Q_strncpy( cl.sound_precache[soundIndex], MSG_ReadString( msg ), sizeof( cl.sound_precache[0] ));

	// when we loading map all resources is precached sequentially
	//if( !cl.audio_prepped ) return;

	cl.sound_index[soundIndex] = S_RegisterSound( cl.sound_precache[soundIndex] );
}

static void CL_LegacyPrecacheModel( sizebuf_t *msg )
{
	int	modelIndex;
	string model;

	modelIndex = MSG_ReadUBitLong( msg, MAX_LEGACY_MODEL_BITS );

	if( modelIndex < 0 || modelIndex >= MAX_MODELS )
		Host_Error( "%s: bad modelindex %i\n", __func__, modelIndex );

	Q_strncpy( model, MSG_ReadString( msg ), sizeof( model ));
	//Q_strncpy( cl.model_precache[modelIndex], BF_ReadString( msg ), sizeof( cl.model_precache[0] ));

	// when we loading map all resources is precached sequentially
	//if( !cl.video_prepped ) return;
	if( modelIndex == 1 && !cl.worldmodel )
	{
		CL_ClearWorld ();

		cl.models[modelIndex] = cl.worldmodel = Mod_LoadWorld( model, true );
		return;

	}

	//Mod_RegisterModel( cl.model_precache[modelIndex], modelIndex );

	cl.models[modelIndex] = Mod_ForName( model, false, false );
	cl.nummodels = Q_max( cl.nummodels, modelIndex  );
}

static void CL_LegacyPrecacheEvent( sizebuf_t *msg )
{
	int	eventIndex;

	eventIndex = MSG_ReadUBitLong( msg, MAX_EVENT_BITS );

	if( eventIndex < 0 || eventIndex >= MAX_EVENTS )
		Host_Error( "%s: bad eventindex %i\n", __func__, eventIndex );

	Q_strncpy( cl.event_precache[eventIndex], MSG_ReadString( msg ), sizeof( cl.event_precache[0] ));

	// can be set now
	CL_SetEventIndex( cl.event_precache[eventIndex], eventIndex );
}

#if XASH_LOW_MEMORY == 0
#define MAX_LEGACY_RESOURCES 2048
#elif XASH_LOW_MEMORY == 2
#define MAX_LEGACY_RESOURCES 1
#elif XASH_LOW_MEMORY == 1
#define MAX_LEGACY_RESOURCES 512
#endif
/*
==============
CL_ParseResourceList

==============
*/
static void CL_LegacyParseResourceList( sizebuf_t *msg )
{
	int	i = 0;
	static struct
	{
		int  rescount;
		int  restype[MAX_LEGACY_RESOURCES];
		char resnames[MAX_LEGACY_RESOURCES][MAX_QPATH];
	} reslist;

	memset( &reslist, 0, sizeof( reslist ));
	reslist.rescount = MSG_ReadWord( msg ) - 1;

	if( reslist.rescount > MAX_LEGACY_RESOURCES )
		Host_Error( "MAX_RESOURCES reached\n" );

	for( i = 0; i < reslist.rescount; i++ )
	{
		reslist.restype[i] = MSG_ReadWord( msg );
		Q_strncpy( reslist.resnames[i], MSG_ReadString( msg ), sizeof( reslist.resnames[i] ));
	}

	if( cls.demoplayback )
		return;

	if( !cl_allow_download.value )
	{
		Con_DPrintf( "Refusing new resource, cl_allowdownload set to 0\n" );
		reslist.rescount = 0;
	}

	if( cls.state == ca_active && !cl_download_ingame.value )
	{
		Con_DPrintf( "Refusing new resource, cl_download_ingame set to 0\n" );
		reslist.rescount = 0;
	}

	HTTP_ResetProcessState();

	host.downloadcount = 0;

	for( i = 0; i < reslist.rescount; i++ )
	{
		char soundpath[MAX_VA_STRING];
		const char *path;

		if( reslist.restype[i] == t_sound )
		{
			Q_snprintf( soundpath, sizeof( soundpath ), DEFAULT_SOUNDPATH "%s", reslist.resnames[i] );

			path = soundpath;
		}
		else path = reslist.resnames[i];

		if( FS_FileExists( path, false ))
			continue;	// already exists

		host.downloadcount++;
		HTTP_AddDownload( path, -1, true, NULL );
	}

	if( !host.downloadcount )
	{
		MSG_WriteByte( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, "continueloading" );
	}
}

/*
=====================
CL_ParseLegacyServerMessage

dispatch messages
=====================
*/
void CL_ParseLegacyServerMessage( sizebuf_t *msg )
{
	size_t		bufStart, playerbytes;
	int		cmd;
	int		old_background;
	const char	*s;

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

		// record command for debugging spew on parse problem
		CL_Parse_RecordCommand( cmd, bufStart );

		if( CL_ParseCommonDLLMessage( msg, PROTO_LEGACY, cmd, bufStart ))
			continue;

		// other commands
		switch( cmd )
		{
		case svc_bad:
			Host_Error( "svc_bad\n" );
			break;
		case svc_nop:
			// this does nothing
			break;
		case svc_disconnect:
			CL_Drop ();
			Host_AbortCurrentFrame ();
			break;
		case svc_legacy_event:
			CL_ParseEvent( msg, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_legacy_changing:
			old_background = cl.background;
			if( MSG_ReadOneBit( msg ))
			{
				int maxclients = cl.maxclients;

				// we can only changelevel in singleplayer
				// and singleplayer always runs in current protocol
				// cls.changelevel = true;
				S_StopAllSounds( true );

				Con_Printf( "Server changing, reconnecting\n" );

				if( cls.demoplayback )
				{
					SCR_BeginLoadingPlaque( cl.background );
					cls.changedemo = true;
				}

				CL_ClearState( );

				// a1ba: need to restore cl.maxclients because engine chooses
				// frame backups count depending on this value
				// In general, it's incorrect to call CL_InitEdicts right after
				// CL_ClearState because of this bug. Some time later this logic
				// should be re-done.
				CL_InitEdicts( maxclients ); // re-arrange edicts
			}
			else Con_Printf( "Server disconnected, reconnecting\n" );

			if( cls.demoplayback )
			{
				cl.background = (cls.demonum != -1) ? true : false;
				cls.state = ca_connected;
			}
			else
			{
				// g-cont. local client skip the challenge
				if( SV_Active( ))
					cls.state = ca_disconnected;
				else cls.state = ca_connecting;
				cl.background = old_background;
				cls.connect_time = MAX_HEARTBEAT;
				cls.connect_retry = 0;
			}
			break;
		case svc_setview:
			CL_ParseViewEntity( msg );
			break;
		case svc_sound:
			CL_LegacyParseSoundPacket( msg, false );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_legacy_ambientsound:
			CL_LegacyParseSoundPacket( msg, true );
			cl.frames[cl.parsecountmod].graphdata.sound += MSG_GetNumBytesRead( msg ) - bufStart;

			break;
		case svc_time:
			CL_ParseServerTime( msg, PROTO_LEGACY );
			break;
		case svc_print:
			Con_Printf( "%s", MSG_ReadString( msg ));
			break;
		case svc_stufftext:
			s = MSG_ReadString( msg );
			if( cl_trace_stufftext.value )
			{
				size_t len = Q_strlen( s );
				Con_Printf( "Stufftext: %s%c", s, len && s[len-1] == '\n' ? '\0' : '\n' );
			}

#ifdef HACKS_RELATED_HLMODS
			// disable Cry Of Fear antisave protection
			if( !Q_strnicmp( s, "disconnect", 10 ) && cls.signon != SIGNONS )
				break; // too early
#endif
			Cbuf_AddFilteredText( s );
			break;
		case svc_setangle:
			CL_ParseSetAngle( msg );
			break;
		case svc_serverdata:
			Cbuf_Execute(); // make sure any stuffed commands are done
			CL_ParseServerData( msg, PROTO_LEGACY );
			break;
		case svc_lightstyle:
			CL_ParseLightStyle( msg, PROTO_LEGACY );
			break;
		case svc_updateuserinfo:
			CL_UpdateUserinfo( msg, PROTO_LEGACY );
			break;
		case svc_deltatable:
			Delta_ParseTableField( msg );
			break;
		case svc_clientdata:
			CL_ParseClientData( msg, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.client += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_resource:
			CL_ParseResource( msg );
			break;
		case svc_pings:
			CL_UpdateUserPings( msg );
			break;
		case svc_particle:
			CL_ParseParticles( msg, PROTO_LEGACY );
			break;
		case svc_restoresound:
			Con_Printf( S_ERROR "%s: svc_restoresound: implement me!\n", __func__ );
			break;
		case svc_spawnstatic:
			CL_LegacyParseStaticEntity( msg );
			break;
		case svc_event_reliable:
			CL_ParseReliableEvent( msg, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.event += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		case svc_spawnbaseline:
			CL_ParseBaseline( msg, PROTO_LEGACY );
			break;
		case svc_setpause:
			cl.paused = ( MSG_ReadOneBit( msg ) != 0 );
			break;
		case svc_signonnum:
			CL_ParseSignon( msg, PROTO_LEGACY );
			break;
		case svc_centerprint:
			CL_CenterPrint( MSG_ReadString( msg ), 0.25f );
			break;
		case svc_legacy_modelindex:
			CL_LegacyPrecacheModel( msg );
			break;
		case svc_legacy_soundindex:
			CL_LegacyPrecacheSound( msg );
			break;
		case svc_restore:
			CL_ParseRestore( msg );
			break;
		case svc_legacy_eventindex:
			CL_LegacyPrecacheEvent(msg);
			break;
		case svc_bspdecal:
			CL_ParseStaticDecal( msg );
			break;
		case svc_addangle:
			CL_ParseAddAngle( msg );
			break;
		case svc_usermessage:
			CL_RegisterUserMessage( msg, PROTO_LEGACY );
			break;
		case svc_packetentities:
			playerbytes = CL_ParsePacketEntities( msg, false, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_deltapacketentities:
			playerbytes = CL_ParsePacketEntities( msg, true, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.players += playerbytes;
			cl.frames[cl.parsecountmod].graphdata.entities += MSG_GetNumBytesRead( msg ) - bufStart - playerbytes;
			break;
		case svc_legacy_chokecount:
		{
			int i, j;
			i = MSG_ReadByte( msg );
			j = cls.netchan.incoming_acknowledged - 1;
			for( ; i > 0 && j > cls.netchan.outgoing_sequence - CL_UPDATE_BACKUP; j-- )
			{
				if( cl.frames[j & CL_UPDATE_MASK].receivedtime != -3.0 )
				{
					cl.frames[j & CL_UPDATE_MASK].choked = true;
					cl.frames[j & CL_UPDATE_MASK].receivedtime = -2.0;
					i--;
				}
			}
			break;
		}
			//cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].choked = true;
			//cl.frames[cls.netchan.incoming_sequence & CL_UPDATE_MASK].receivedtime = -2.0;
			break;
		case svc_resourcelist:
			CL_LegacyParseResourceList( msg );
			break;
		case svc_deltamovevars:
			CL_ParseMovevars( msg );
			break;
		case svc_resourcerequest:
			CL_ParseResourceRequest( msg );
			break;
		case svc_customization:
			CL_ParseCustomization( msg );
			break;
		case svc_crosshairangle:
			CL_ParseCrosshairAngle( msg );
			break;
		case svc_soundfade:
			CL_ParseSoundFade( msg );
			break;
		case svc_filetxferfailed:
			CL_ParseFileTransferFailed( msg );
			break;
		case svc_hltv:
			CL_ParseHLTV( msg );
			break;
		case svc_resourcelocation:
			CL_ParseResLocation( msg );
			break;
		case svc_querycvarvalue:
			CL_ParseCvarValue( msg, false, PROTO_LEGACY );
			break;
		case svc_querycvarvalue2:
			CL_ParseCvarValue( msg, true, PROTO_LEGACY );
			break;
		default:
			CL_ParseUserMessage( msg, cmd, PROTO_LEGACY );
			cl.frames[cl.parsecountmod].graphdata.usr += MSG_GetNumBytesRead( msg ) - bufStart;
			break;
		}
	}
}

void CL_LegacyPrecache_f( void )
{
	int	spawncount, i;
	model_t *mod;

	if( cls.legacymode != PROTO_LEGACY )
		return;

	spawncount = Q_atoi( Cmd_Argv( 1 ));

	Con_Printf( "Setting up renderer...\n" );

	// load tempent sprites (glowshell, muzzleflashes etc)
	CL_LoadClientSprites ();

	// invalidate all decal indexes
	memset( cl.decal_index, 0, sizeof( cl.decal_index ));
	cl.video_prepped = true;
	cl.audio_prepped = true;
	if( clgame.entities )
		clgame.entities->model = cl.worldmodel;

	// load skybox
	R_SetupSky( clgame.movevars.skyName );

	// tell rendering system we have a new set of models.
	ref.dllFuncs.R_NewMap ();

	CL_SetupOverviewParams();

	// release unused SpriteTextures
	for( i = 1, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
	{
		if( mod->needload == NL_UNREFERENCED && COM_CheckString( mod->name ))
			Mod_FreeModel( mod );
	}

//	Mod_FreeUnused ();

	if( host_developer.value <= DEV_NONE )
		Con_ClearNotify(); // clear any lines of console text

	// done with all resources, issue prespawn command.
	// Include server count in case server disconnects and changes level during d/l
	MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
	MSG_WriteStringf( &cls.netchan.message, "begin %i", spawncount );
	cls.signon = SIGNONS - 1;
}
