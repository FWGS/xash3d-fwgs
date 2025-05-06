/*
sv_init.c - server initialize operations
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
#include "server.h"
#include "net_encode.h"
#include "library.h"
#include "voice.h"
#include "pm_local.h"

#if XASH_LOW_MEMORY != 2
int SV_UPDATE_BACKUP = SINGLEPLAYER_BACKUP;
#endif
server_t		sv;	// local server
server_static_t	svs;	// persistant server info
svgame_static_t	svgame;	// persistant game info

/*
==================
Host_SetServerState
==================
*/
static void Host_SetServerState( int state )
{
	Cvar_FullSet( "host_serverstate", va( "%i", state ), FCVAR_READ_ONLY );
	sv.state = state;
}

/*
================
SV_AddResource

generic method to put the resources into array
================
*/
static void SV_AddResource( resourcetype_t type, const char *name, int size, byte flags, int index )
{
	resource_t	*pResource = &sv.resources[sv.num_resources];

	if( sv.num_resources >= MAX_RESOURCES )
		Host_Error( "MAX_RESOURCES limit exceeded (%d)\n", MAX_RESOURCES );
	sv.num_resources++;

	Q_strncpy( pResource->szFileName, name, sizeof( pResource->szFileName ));
	pResource->nDownloadSize = size;
	pResource->ucFlags = flags;
	pResource->nIndex = index;
	pResource->type = type;
}

/*
================
SV_SendSingleResource

hot precache on a flying
================
*/
static void SV_SendSingleResource( const char *name, resourcetype_t type, int index, byte flags )
{
	resource_t	*pResource = &sv.resources[sv.num_resources];
	int		nSize = 0;

	if( !COM_CheckString( name ))
		return;

	switch( type )
	{
	case t_model:
		nSize = ( name[0] != '*' ) ? FS_FileSize( name, false ) : 0;
		break;
	case t_sound:
		nSize = FS_FileSize( va( DEFAULT_SOUNDPATH "%s", name ), false );
		break;
	default:
		nSize = FS_FileSize( name, false );
		break;
	}

	SV_AddResource( type, name, nSize, flags, index );
	MSG_BeginServerCmd( &sv.reliable_datagram, svc_resource );
	SV_SendResource( pResource, &sv.reliable_datagram );
}

/*
================
SV_ModelIndex

register unique model for a server and client
================
*/
int SV_ModelIndex( const char *filename )
{
	char	name[MAX_QPATH];
	int	i;

	if( !COM_CheckString( filename ))
		return 0;

	if( *filename == '\\' || *filename == '/' )
		filename++;
	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.model_precache[i], name ))
			return i;
	}

	if( i == MAX_MODELS )
	{
		Host_Error( "MAX_MODELS limit exceeded (%d)\n", MAX_MODELS );
		return 0;
	}

	// register new model
	Q_strncpy( sv.model_precache[i], name, sizeof( sv.model_precache[i] ));

	if( sv.state != ss_loading )
	{
		// send the update to everyone
		SV_SendSingleResource( name, t_model, i, sv.model_precache_flags[i] );
		Con_Printf( S_WARN "late precache of %s\n", name );
	}

	return i;
}

/*
================
SV_SoundIndex

register unique sound for client
================
*/
int GAME_EXPORT SV_SoundIndex( const char *filename )
{
	char	name[MAX_QPATH];
	int	i;

	if( !COM_CheckString( filename ))
		return 0;

	if( filename[0] == '!' )
	{
		Con_Printf( S_WARN "'%s' do not precache sentence names!\n", filename );
		return 0;
	}

	if( *filename == '\\' || *filename == '/' )
		filename++;
	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_SOUNDS && sv.sound_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.sound_precache[i], name ))
			return i;
	}

	if( i == MAX_SOUNDS )
	{
		Host_Error( "MAX_SOUNDS limit exceeded (%d)\n", MAX_SOUNDS );
		return 0;
	}

	// register new sound
	Q_strncpy( sv.sound_precache[i], name, sizeof( sv.sound_precache[i] ));

	if( sv.state != ss_loading )
	{
		// send the update to everyone
		SV_SendSingleResource( name, t_sound, i, 0 );
		Con_Printf( S_WARN "late precache of %s\n", name );
	}

	return i;
}

/*
================
SV_EventIndex

register network event for a server and client
================
*/
int SV_EventIndex( const char *filename )
{
	char	name[MAX_QPATH];
	int	i;

	if( !COM_CheckString( filename ))
		return 0;

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_EVENTS && sv.event_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.event_precache[i], name ))
			return i;
	}

	if( i == MAX_EVENTS )
	{
		Host_Error( "MAX_EVENTS limit exceeded (%d)\n", MAX_EVENTS );
		return 0;
	}

	// register new event
	Q_strncpy( sv.event_precache[i], name, sizeof( sv.event_precache[i] ));

	if( sv.state != ss_loading )
	{
		// send the update to everyone
		SV_SendSingleResource( name, t_eventscript, i, RES_FATALIFMISSING );
	}

	return i;
}

/*
================
SV_GenericIndex

register generic resourse for a server and client
================
*/
int GAME_EXPORT SV_GenericIndex( const char *filename )
{
	char	name[MAX_QPATH];
	int	i;

	if( !COM_CheckString( filename ))
		return 0;

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	for( i = 1; i < MAX_CUSTOM && sv.files_precache[i][0]; i++ )
	{
		if( !Q_stricmp( sv.files_precache[i], name ))
			return i;
	}

	if( i == MAX_CUSTOM )
	{
		Host_Error( "MAX_CUSTOM limit exceeded (%d)\n", MAX_CUSTOM );
		return 0;
	}

	// register new generic resource
	Q_strncpy( sv.files_precache[i], name, sizeof( sv.files_precache[i] ));

	if( sv.state != ss_loading )
	{
		// send the update to everyone
		SV_SendSingleResource( name, t_generic, i, RES_FATALIFMISSING );
	}

	return i;
}

static resourcetype_t SV_DetermineResourceType( const char *filename )
{
	if( !Q_strncmp( filename, DEFAULT_SOUNDPATH, sizeof( DEFAULT_SOUNDPATH ) - 1 ) && Sound_SupportedFileFormat( COM_FileExtension( filename )))
		return t_sound;
	else
		return t_generic;
}

static const char *SV_GetResourceTypeName( resourcetype_t restype )
{
	switch( restype )
	{
		case t_decal: return "decal";
		case t_eventscript: return "eventscript";
		case t_generic: return "generic";
		case t_model: return "model";
		case t_skin: return "skin";
		case t_sound: return "sound";
		case t_world: return "world";
		default: return "unknown";
	}
}

static void SV_ReadResourceList( const char *filename )
{
	string token;
	byte *afile;
	char *pfile;
	resourcetype_t restype;

	afile = FS_LoadFile( filename, NULL, false );
	if( !afile ) return;

	pfile = (char *)afile;

	Con_DPrintf( "Precaching from %s\n", filename );
	Con_DPrintf( "----------------------------------\n" );

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		if( !COM_IsSafeFileToDownload( token ))
			continue;

		COM_FixSlashes( token );
		restype = SV_DetermineResourceType( token );
		Con_DPrintf( "  %s (%s)\n", token, SV_GetResourceTypeName( restype ));
		switch( restype )
		{
			// TODO do we need to handle other resource types specifically too?
			case t_sound:
			{
				const char *filepath = token;
				filepath += sizeof( DEFAULT_SOUNDPATH ) - 1; // skip "sound/" part
				SV_SoundIndex( filepath );
				break;
			}
			default:
				SV_GenericIndex( token );
				break;
		}
	}

	Con_DPrintf( "----------------------------------\n" );
	Mem_Free( afile );
}

/*
================
SV_CreateGenericResources

loads external resource list
================
*/
static void SV_CreateGenericResources( void )
{
	string	filename;
	int i;

	Q_strncpy( filename, sv.model_precache[1], sizeof( filename ));
	COM_ReplaceExtension( filename, ".res", sizeof( filename ));
	COM_FixSlashes( filename );

	SV_ReadResourceList( filename );
	SV_ReadResourceList( "reslist.txt" );

	for( i = 0; i < world.wadlist.count; i++ )
	{
		if( world.wadlist.wadusage[i] > 0 )
		{
			SV_GenericIndex( world.wadlist.wadnames[i] );
		}
	}
}

/*
================
SV_CreateResourceList

add resources to common list
================
*/
static void SV_CreateResourceList( void )
{
	qboolean	ffirstsent = false;
	int	i, nSize;
	char	*s;

	sv.num_resources = 0;

	for( i = 1; i < MAX_CUSTOM; i++ )
	{
		s = sv.files_precache[i];
		if( !COM_CheckString( s )) break; // end of list
		nSize = FS_FileSize( s, false );
		SV_AddResource( t_generic, s, nSize, RES_FATALIFMISSING, i );
	}

	for( i = 1; i < MAX_SOUNDS; i++ )
	{
		s = sv.sound_precache[i];
		if( !COM_CheckString( s ))
			break; // end of list

		if( s[0] == '!' )
		{
			if( !ffirstsent )
			{
				SV_AddResource( t_sound, "!", 0, RES_FATALIFMISSING, i );
				ffirstsent = true;
			}
		}
		else
		{
			nSize = FS_FileSize( va( DEFAULT_SOUNDPATH "%s", s ), false );
			SV_AddResource( t_sound, s, nSize, 0, i );
		}
	}

	for( i = 1; i < MAX_MODELS; i++ )
	{
		s = sv.model_precache[i];
		if( !COM_CheckString( s )) break; // end of list
		nSize = ( s[0] != '*' ) ? FS_FileSize( s, false ) : 0;
		SV_AddResource( t_model, s, nSize, sv.model_precache_flags[i], i );
	}

	// just send names
	for( i = 0; i < MAX_DECALS && host.draw_decals[i][0]; i++ )
	{
		SV_AddResource( t_decal, host.draw_decals[i], 0, 0, i );
	}

	for( i = 1; i < MAX_EVENTS; i++ )
	{
		s = sv.event_precache[i];
		if( !COM_CheckString( s )) break; // end of list
		nSize = FS_FileSize( s, false );
		SV_AddResource( t_eventscript, s, nSize, RES_FATALIFMISSING, i );
	}
}

/*
================
SV_WriteVoiceCodec
================
*/
static void SV_WriteVoiceCodec( sizebuf_t *msg )
{
	MSG_BeginServerCmd( msg, svc_voiceinit );
	MSG_WriteString( msg, VOICE_DEFAULT_CODEC );
	MSG_WriteByte( msg, (int)sv_voicequality.value );
}

/*
================
SV_CreateBaseline

Entity baselines are used to compress the update messages
to the clients -- only the fields that differ from the
baseline will be transmitted

INTERNAL RESOURCE
================
*/
static void SV_CreateBaseline( void )
{
	entity_state_t	nullstate, *base;
	int		playermodel;
	int		delta_type;
	int		entnum;

	if( svs.maxclients > 1 )
		SV_WriteVoiceCodec( &sv.signon );

	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		playermodel = SV_ModelIndex( DEFAULT_PLAYER_PATH_QUAKE );
	else playermodel = SV_ModelIndex( DEFAULT_PLAYER_PATH_HALFLIFE );

	memset( &nullstate, 0, sizeof( nullstate ));

	for( entnum = 0; entnum < svgame.numEntities; entnum++ )
	{
		edict_t	*pEdict = EDICT_NUM( entnum );

		if( !SV_IsValidEdict( pEdict ))
			continue;

		if( entnum != 0 && entnum <= svs.maxclients )
		{
			delta_type = DELTA_PLAYER;
		}
		else
		{
			if( !pEdict->v.modelindex )
				continue; // invisible
			delta_type = DELTA_ENTITY;
		}

		// take current state as baseline
		base = &svs.baselines[entnum];

		base->number = entnum;

		// set entity type
		if( FBitSet( pEdict->v.flags, FL_CUSTOMENTITY ))
			base->entityType = ENTITY_BEAM;
		else base->entityType = ENTITY_NORMAL;

		svgame.dllFuncs.pfnCreateBaseline( delta_type, entnum, base, pEdict, playermodel, host.player_mins[0], host.player_maxs[0] );
		sv.last_valid_baseline = entnum;
	}

	// create the instanced baselines
	svgame.dllFuncs.pfnCreateInstancedBaselines();

	// now put the baseline into the signon message.
	MSG_BeginServerCmd( &sv.signon, svc_spawnbaseline );

	for( entnum = 0; entnum < svgame.numEntities; entnum++ )
	{
		edict_t	*pEdict = EDICT_NUM( entnum );

		if( !SV_IsValidEdict( pEdict ))
			continue;

		if( entnum != 0 && entnum <= svs.maxclients )
		{
			delta_type = DELTA_PLAYER;
		}
		else
		{
			if( !pEdict->v.modelindex )
				continue; // invisible
			delta_type = DELTA_ENTITY;
		}

		// take current state as baseline
		base = &svs.baselines[entnum];

		MSG_WriteDeltaEntity( &nullstate, base, &sv.signon, true, delta_type, 1.0f, 0 );
	}

	MSG_WriteUBitLong( &sv.signon, LAST_EDICT, MAX_ENTITY_BITS ); // end of baselines
	MSG_WriteUBitLong( &sv.signon, sv.num_instanced, 6 );

	for( entnum = 0; entnum < sv.num_instanced; entnum++ )
	{
		base = &sv.instanced[entnum].baseline;
		MSG_WriteDeltaEntity( &nullstate, base, &sv.signon, true, DELTA_ENTITY, 1.0f, 0 );
	}
}

/*
================
SV_FreeOldEntities

remove immediate entities
================
*/
void SV_FreeOldEntities( void )
{
	edict_t	*ent;
	int	i;

	// at end of frame kill all entities which supposed to it
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		ent = EDICT_NUM( i );

		if( !ent->free && FBitSet( ent->v.flags, FL_KILLME ))
			SV_FreeEdict( ent );
	}

	// decrement svgame.numEntities if the highest number entities died
	for( ; ( ent = EDICT_NUM( svgame.numEntities - 1 )) && ent->free; svgame.numEntities-- );
}

/*
================
SV_ActivateServer

activate server on changed map, run physics
================
*/
void SV_ActivateServer( int runPhysics )
{
	int		i, numFrames;
	byte		msg_buf[MAX_INIT_MSG];
	sizebuf_t		msg;
	sv_client_t	*cl;

	if( !svs.initialized )
		return;

	MSG_Init( &msg, "ActivateServer", msg_buf, sizeof( msg_buf ));

	// always clearing newunit variable
	Cvar_SetValue( "sv_newunit", 0 );

	// relese all intermediate entities
	SV_FreeOldEntities ();

	// Activate the DLL server code
	svgame.globals->time = sv.time;
	svgame.dllFuncs.pfnServerActivate( svgame.edicts, svgame.numEntities, svs.maxclients );

	SV_SetStringArrayMode( true );

	// parse user-specified resources
	SV_CreateGenericResources();

	if( runPhysics )
	{
		numFrames = (svs.maxclients <= 1) ? 2 : 8;
		sv.frametime = SV_SPAWN_TIME;
	}
	else
	{
		sv.frametime = 0.001;
		numFrames = 1;
	}

	// run some frames to allow everything to settle
	for( i = 0; i < numFrames; i++ )
		SV_Physics();

	// create a baseline for more efficient communications
	SV_CreateBaseline();

	// collect all info from precached resources
	SV_CreateResourceList();

	// check and count all files that marked by user as unmodified (typically is a player models etc)
	SV_TransferConsistencyInfo();

	// send serverinfo to all connected clients
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( cl->state < cs_connected )
			continue;

		Netchan_Clear( &cl->netchan );
		cl->delta_sequence = -1;

		// bump connect timeout
		cl->connection_started = host.realtime;
	}

	// invoke to refresh all movevars
	memset( &svgame.oldmovevars, 0, sizeof( movevars_t ));
	svgame.globals->changelevel = false;

	// setup hostflags
	sv.hostflags = 0;

	HPAK_FlushHostQueue();

	// tell what kind of server has been started.
	if( svs.maxclients > 1 )
		Con_Printf( "%i player server started\n", svs.maxclients );
	else Con_Printf( "Game started\n" );

	Log_Printf( "Started map \"%s\" (CRC \"%u\")\n", sv.name, sv.worldmapCRC );

	// dedicated server purge unused resources here
	if( Host_IsDedicated() )
		Mod_FreeUnused ();

	host.movevars_changed = true;
	Host_SetServerState( ss_active );

	Con_DPrintf( "level loaded at %.2f sec\n", Sys_DoubleTime() - svs.timestart );

	if( sv.ignored_static_ents )
		Con_Printf( S_WARN "%i static entities was rejected due buffer overflow\n", sv.ignored_static_ents );

	if( sv.ignored_world_decals )
		Con_Printf( S_WARN "%i static decals was rejected due buffer overflow\n", sv.ignored_world_decals );
}

/*
================
SV_DeactivateServer

deactivate server, free edicts, strings etc
================
*/
void SV_DeactivateServer( void )
{
	int	i;
	const char	*cycle = Cvar_VariableString( "disconcfgfile" );

	if( COM_CheckString( cycle ))
		Cbuf_AddTextf( "exec %s\n", cycle );

	if( COM_CheckStringEmpty( sv.name ))
		Cbuf_AddTextf( "exec maps/%s_unload.cfg\n", sv.name );

	if( !svs.initialized || sv.state == ss_dead )
		return;

	svgame.globals->time = sv.time;
	svgame.dllFuncs.pfnServerDeactivate();
	Host_SetServerState( ss_dead );

	SV_FreeEdicts ();

	PM_ClearPhysEnts( svgame.pmove );

	SV_EmptyStringPool( true );
	Mem_EmptyPool( svgame.stringspool );

	for( i = 0; i < svs.maxclients; i++ )
	{
		// release client frames
		if( svs.clients[i].frames )
			Mem_Free( svs.clients[i].frames );
		svs.clients[i].frames = NULL;
	}

	svgame.globals->maxEntities = GI->max_edicts;
	svgame.globals->maxClients = svs.maxclients;
	svgame.numEntities = svs.maxclients + 1; // clients + world
	svgame.globals->startspot = 0;
	svgame.globals->mapname = 0;
}

/*
==============
SV_InitGame

A brand new game has been started
==============
*/
qboolean SV_InitGame( void )
{
	string dllpath;

	if( svgame.hInstance )
		return true;

	// first initialize?
	COM_ResetLibraryError();

	COM_GetCommonLibraryPath( LIBRARY_SERVER, dllpath, sizeof( dllpath ));

	if( !SV_LoadProgs( dllpath ))
	{
		Con_Printf( S_ERROR "can't initialize %s: %s\n", dllpath, COM_GetLibraryError() );
		return false; // failed to loading server.dll
	}

	// client frames will be allocated in SV_ClientConnect
	return true;
}

/*
==============
SV_ShutdownGame

prepare to close server
==============
*/
void SV_ShutdownGame( void )
{
	if( !GameState->loadGame )
		SV_ClearGameState();

	SV_FinalMessage( "", true );
	S_StopBackgroundTrack();
	CL_StopPlayback(); // stop demo too

	if( GameState->newGame )
	{
		Host_EndGame( false, DEFAULT_ENDGAME_MESSAGE );
	}
	else
	{
		S_StopAllSounds( true );
		SV_DeactivateServer();
	}
}

/*
================
SV_SetupClients

determine the game type and prepare clients
================
*/
static void SV_SetupClients( void )
{
	qboolean	changed_maxclients = false;

	// check if clients count was really changed
	if( svs.maxclients != (int)sv_maxclients.value )
		changed_maxclients = true;

	if( !changed_maxclients ) return; // nothing to change

	// if clients count was changed we need to run full shutdown procedure
	if( svs.maxclients )
		SV_Shutdown( "Server was killed due to maxclients change\n" );

	// copy the actual value from cvar
	svs.maxclients = (int)sv_maxclients.value;

	// dedicated servers are can't be single player and are usually DM
	if( Host_IsDedicated() )
		svs.maxclients = bound( 4, svs.maxclients, MAX_CLIENTS );
	else svs.maxclients = bound( 1, svs.maxclients, MAX_CLIENTS );

	if( svs.maxclients == 1 )
		Cvar_SetValue( "deathmatch", 0.0f );
	else Cvar_SetValue( "deathmatch", 1.0f );

	// make cvars consistant
	if( coop.value ) Cvar_SetValue( "deathmatch", 0.0f );

	// feedback for cvar
	Cvar_FullSet( "maxplayers", va( "%d", svs.maxclients ), FCVAR_LATCH );
#if XASH_LOW_MEMORY != 2
	SV_UPDATE_BACKUP = ( svs.maxclients == 1 ) ? SINGLEPLAYER_BACKUP : MULTIPLAYER_BACKUP;
#endif

	svs.clients = Z_Realloc( svs.clients, sizeof( sv_client_t ) * svs.maxclients );
	svs.num_client_entities = svs.maxclients * SV_UPDATE_BACKUP * NUM_PACKET_ENTITIES;
	svs.packet_entities = Z_Realloc( svs.packet_entities, sizeof( entity_state_t ) * svs.num_client_entities );
	Con_Reportf( "%s alloced by server packet entities\n", Q_memprint( sizeof( entity_state_t ) * svs.num_client_entities ));

	// init network stuff
	NET_Config(( svs.maxclients > 1 ), true );
	svgame.numEntities = svs.maxclients + 1; // clients + world
	ClearBits( sv_maxclients.flags, FCVAR_CHANGED );
}

qboolean CRC32_MapFile( dword *crcvalue, const char *filename, qboolean multiplayer )
{
	char	headbuf[1024], buffer[1024];
	int	i, num_bytes, lumplen;
	int	version, hdr_size;
	dheader_t	*header;
	file_t	*f;

	if( !crcvalue ) return false;

	// always calc same checksum for singleplayer
	if( multiplayer == false )
	{
		*crcvalue = (('H'<<24)+('S'<<16)+('A'<<8)+'X');
		return true;
	}

	f = FS_Open( filename, "rb", false );
	if( !f ) return false;

	// read version number
	FS_Read( f, &version, sizeof( int ));
	FS_Seek( f, 0, SEEK_SET );

	hdr_size = sizeof( int ) + sizeof( dlump_t ) * HEADER_LUMPS;
	num_bytes = FS_Read( f, headbuf, hdr_size );

	// corrupted map ?
	if( num_bytes != hdr_size )
	{
		FS_Close( f );
		return false;
	}

	header = (dheader_t *)headbuf;

	// invalid version ?
	switch( header->version )
	{
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		break;
	default:
		FS_Close( f );
		return false;
	}

	CRC32_Init( crcvalue );

	for( i = LUMP_PLANES; i < HEADER_LUMPS; i++ )
	{
		lumplen = header->lumps[i].filelen;
		FS_Seek( f, header->lumps[i].fileofs, SEEK_SET );

		while( lumplen > 0 )
		{
			if( lumplen >= sizeof( buffer ))
				num_bytes = FS_Read( f, buffer, sizeof( buffer ));
			else num_bytes = FS_Read( f, buffer, lumplen );

			if( num_bytes > 0 )
			{
				lumplen -= num_bytes;
				CRC32_ProcessBuffer( crcvalue, buffer, num_bytes );
			}

			// file unexpected end ?
			if( FS_Eof( f )) break;
		}
	}

	FS_Close( f );

	return 1;
}

void SV_FreeTestPacket( void )
{
	if( svs.testpacket_buf )
	{
		Mem_Free( svs.testpacket_buf );
		svs.testpacket_buf = NULL;
	}

	if( svs.testpacket_crcs )
	{
		Mem_Free( svs.testpacket_crcs );
		svs.testpacket_crcs = NULL;
	}
}

/*
================
SV_GenerateTestPacket
================
*/
static void SV_GenerateTestPacket( void )
{
	const int maxsize = FRAGMENT_MAX_SIZE;
	uint32_t crc;
	file_t *file;
	byte *filepos;
	int i;

	if( !sv_allow_testpacket.value )
	{
		SV_FreeTestPacket();
		return;
	}

	// testpacket already generated once, exit
	// testpacket and lookup table takes ~300k of memory
	// disable for low memory mode
	if( svs.testpacket_buf || XASH_LOW_MEMORY > 0 )
		return;

	// don't need in singleplayer with full client
	if( svs.maxclients <= 1 )
		return;

	file = FS_Open( "gfx.wad", "rb", false );
	if( FS_FileLength( file ) < maxsize )
	{
		FS_Close( file );
		return;
	}

	svs.testpacket_buf = Mem_Malloc( host.mempool, sizeof( *svs.testpacket_buf ) * maxsize );

	// write packet base data
	MSG_Init( &svs.testpacket, "BandWidthTest", svs.testpacket_buf, maxsize );
	MSG_WriteLong( &svs.testpacket, -1 );
	MSG_WriteString( &svs.testpacket, S2C_BANDWIDTHTEST );
	svs.testpacket_crcpos = svs.testpacket.pData + MSG_GetNumBytesWritten( &svs.testpacket );
	MSG_WriteDword( &svs.testpacket, 0 ); // to be changed by crc

	// time to read our file
	svs.testpacket_filepos = MSG_GetNumBytesWritten( &svs.testpacket );
	svs.testpacket_filelen = maxsize - svs.testpacket_filepos;

	filepos = svs.testpacket.pData + svs.testpacket_filepos;
	FS_Read( file, filepos, svs.testpacket_filelen );
	FS_Close( file );

	// now generate checksums lookup table
	svs.testpacket_crcs = Mem_Malloc( host.mempool, sizeof( *svs.testpacket_crcs ) * svs.testpacket_filelen );
	crc = 0; // intentional omit of CRC32_Init because of the client

	// TODO: shrink to minimum!
	for( i = 0; i < svs.testpacket_filelen; i++ )
	{
		uint32_t crc2;

		CRC32_ProcessByte( &crc, filepos[i] );
		svs.testpacket_crcs[i] = crc;
#if 0
		// test
		crc2 = 0;
		CRC32_ProcessBuffer( &crc2, filepos, i + 1 );
		if( svs.testpacket_crcs[i] != crc2 )
		{
			Con_Printf( "%d: 0x%x != 0x%x\n", i, svs.testpacket_crcs[i], crc2 );
			svs.testpacket_crcs[i] = crc = crc2;
		}
#endif
	}
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
qboolean SV_SpawnServer( const char *mapname, const char *startspot, qboolean background )
{
	int		i, current_skill;
	edict_t		*ent;
	const char	*cycle;

	SV_SetupClients();

	if( !SV_InitGame( ))
		return false;

	Delta_Init(); // re-initialize delta

	// unlock sv_cheats in local game
	ClearBits( sv_cheats.flags, FCVAR_READ_ONLY );

	svs.initialized = true;
	Log_Open();
	Log_Printf( "Loading map \"%s\"\n", mapname );
	Log_PrintServerVars();

	svs.timestart = Sys_DoubleTime();
	svs.spawncount++; // any partially connected client will be restarted

	for( i = 0; i < ARRAYSIZE( svs.challenge_salt ); i++ )
		svs.challenge_salt[i] = COM_RandomLong( 0, 0x7FFFFFFE );

	cycle = Cvar_VariableString( "mapchangecfgfile" );

	if( COM_CheckString( cycle ))
		Cbuf_AddTextf( "exec %s\n", cycle );

	Cbuf_AddTextf( "exec maps/%s_load.cfg\n", mapname );

	// let's not have any servers with no name
	if( !COM_CheckString( hostname.string ))
		Cvar_Set( "hostname", svgame.dllFuncs.pfnGetGameDescription ? svgame.dllFuncs.pfnGetGameDescription() : FS_Title( ));

	if( startspot )
	{
		Con_Printf( "Spawn Server: %s [%s]\n", mapname, startspot );
	}
	else
	{
		Con_DPrintf( "Spawn Server: %s\n", mapname );
	}

	memset( &sv, 0, sizeof( sv ));	// wipe the entire per-level structure
	sv.time = svgame.globals->time = 1.0f;	// server spawn time it's always 1.0 second
	sv.background = background;

	// initialize buffers
	MSG_Init( &sv.signon, "Signon", sv.signon_buf, sizeof( sv.signon_buf ));
	MSG_Init( &sv.multicast, "Multicast", sv.multicast_buf, sizeof( sv.multicast_buf ));
	MSG_Init( &sv.datagram, "Datagram", sv.datagram_buf, sizeof( sv.datagram_buf ));
	MSG_Init( &sv.reliable_datagram, "Reliable Datagram", sv.reliable_datagram_buf, sizeof( sv.reliable_datagram_buf ));
	MSG_Init( &sv.spec_datagram, "Spectator Datagram", sv.spectator_buf, sizeof( sv.spectator_buf ));

	// clearing all the baselines
	memset( svs.static_entities, 0, sizeof( entity_state_t ) * MAX_STATIC_ENTITIES );
	memset( svs.baselines, 0, sizeof( entity_state_t ) * GI->max_edicts );

	// make cvars consistant
	if( coop.value ) Cvar_SetValue( "deathmatch", 0 );
	current_skill = Q_rint( skill.value );
	current_skill = bound( 0, current_skill, 3 );
	Cvar_SetValue( "skill", (float)current_skill );

	// enforce hpk_max_size
	HPAK_CheckSize( hpk_custom_file.string );

	// force normal player collisions for single player
	if( svs.maxclients == 1 )
		Cvar_SetValue( "sv_clienttrace", 1 );

	// copy gamemode into svgame.globals
	svgame.globals->deathmatch = deathmatch.value;
	svgame.globals->coop = coop.value;
	svgame.globals->maxClients = svs.maxclients;

	if( sv.background )
	{
		// tell the game parts about background state
		Cvar_FullSet( "sv_background", "1", FCVAR_READ_ONLY );
		Cvar_FullSet( "cl_background", "1", FCVAR_READ_ONLY );
	}
	else
	{
		Cvar_FullSet( "sv_background", "0", FCVAR_READ_ONLY );
		Cvar_FullSet( "cl_background", "0", FCVAR_READ_ONLY );
	}

	// force normal player collisions for single player
	if( svs.maxclients == 1 ) Cvar_SetValue( "sv_clienttrace", 1 );

	// allow loading maps from subdirectories, strip extension anyway
	Q_strncpy( sv.name, mapname, sizeof( sv.name ));
	COM_StripExtension( sv.name );

	// precache and static commands can be issued during map initialization
	Host_SetServerState( ss_loading );

	if( startspot )
		Q_strncpy( sv.startspot, startspot, sizeof( sv.startspot ));
	else sv.startspot[0] = '\0';

	Q_snprintf( sv.model_precache[WORLD_INDEX], sizeof( sv.model_precache[0] ), "maps/%s.bsp", sv.name );
	SetBits( sv.model_precache_flags[WORLD_INDEX], RES_FATALIFMISSING );
	sv.worldmodel = sv.models[WORLD_INDEX] = Mod_LoadWorld( sv.model_precache[WORLD_INDEX], true );
	CRC32_MapFile( &sv.worldmapCRC, sv.model_precache[WORLD_INDEX], svs.maxclients > 1 );

	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ) && FS_FileExists( "progs.dat", false ))
	{
		file_t *f = FS_Open( "progs.dat", "rb", false );
		FS_Seek( f, sizeof( int ), SEEK_SET );
		FS_Read( f, &sv.progsCRC, sizeof( int ));
		FS_Close( f );
	}

	for( i = WORLD_INDEX; i < sv.worldmodel->numsubmodels; i++ )
	{
		Q_snprintf( sv.model_precache[i+1], sizeof( sv.model_precache[i+1] ), "*%i", i );
		sv.models[i+1] = Mod_ForName( sv.model_precache[i+1], false, false );
		SetBits( sv.model_precache_flags[i+1], RES_FATALIFMISSING );
	}

	// leave slots at start for clients only
	for( i = 0; i < svs.maxclients; i++ )
	{
		// needs to reconnect
		if( svs.clients[i].state > cs_connected )
			svs.clients[i].state = cs_connected;

		ent = EDICT_NUM( i + 1 );
		svs.clients[i].pViewEntity = NULL;
		svs.clients[i].edict = ent;
		SV_InitEdict( ent );
	}

	// heartbeats will always be sent to the id master
	NET_MasterClear();

	// get actual movevars
	SV_UpdateMovevars( true );

	// clear physics interaction links
	SV_ClearWorld();

	// pregenerate test packet
	SV_GenerateTestPacket();

	return true;
}

qboolean SV_Active( void )
{
	return (sv.state != ss_dead);
}

qboolean SV_Initialized( void )
{
	return svs.initialized;
}

int SV_GetMaxClients( void )
{
	return svs.maxclients;
}

/*
================
SV_ExecLoadLevel

State machine exec new map
================
*/
void SV_ExecLoadLevel( void )
{
	SV_SetStringArrayMode( false );
	if( SV_SpawnServer( GameState->levelName, NULL, GameState->backgroundMap ))
	{
		SV_SpawnEntities( GameState->levelName );
		SV_ActivateServer( true );
	}
}

/*
================
SV_ExecLoadGame

State machine exec load saved game
================
*/
void SV_ExecLoadGame( void )
{
	if( SV_SpawnServer( GameState->levelName, NULL, false ))
	{
		if( !SV_LoadGameState( GameState->levelName ))
			SV_SpawnEntities( GameState->levelName );
		SV_ActivateServer( false );
	}
}

/*
================
SV_ExecChangeLevel

State machine exec changelevel path
================
*/
void SV_ExecChangeLevel( void )
{
	SV_ChangeLevel( GameState->loadGame, GameState->levelName, GameState->landmarkName, GameState->backgroundMap );
}
