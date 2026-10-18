/*
sv_cmds.c - server console commands
Copyright (C) 2007 Uncle Mike

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

/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes
=================
*/
void SV_ClientPrintf( sv_client_t *cl, const char *fmt, ... )
{
	char	string[MAX_SYSPATH];
	va_list	argptr;

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;

	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_BeginServerCmd( &cl->netchan.message, svc_print );
	MSG_WriteString( &cl->netchan.message, string );
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf( sv_client_t *ignore, const char *fmt, ... )
{
	char		string[MAX_SYSPATH];
	va_list		argptr;

	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	if( sv.state == ss_active )
	{
		sv_client_t	*cl;
		int		i;

		for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
		{
			if( FBitSet( cl->flags, FCL_FAKECLIENT ))
				continue;

			if( cl == ignore || cl->state != cs_spawned )
				continue;

			MSG_BeginServerCmd( &cl->netchan.message, svc_print );
			MSG_WriteString( &cl->netchan.message, string );
		}
	}

	if( Host_IsDedicated() )
	{
		// echo to console
		Con_DPrintf( "%s", string );
	}
}

/*
=================
SV_BroadcastCommand

Sends text to all active clients
=================
*/
void SV_BroadcastCommand( const char *fmt, ... )
{
	char	string[MAX_SYSPATH];
	va_list	argptr;

	if( sv.state == ss_dead )
		return;

	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_BeginServerCmd( &sv.reliable_datagram, svc_stufftext );
	MSG_WriteString( &sv.reliable_datagram, string );
}

/*
==================
SV_SetPlayer

Sets sv_client and sv_player to the player with idnum Cmd_Argv(1)
==================
*/
static sv_client_t *SV_SetPlayer( void )
{
	sv_client_t	*cl;
	int		i;

	if( !svs.clients || sv.background )
		return NULL;

	if( svs.maxclients == 1 || Cmd_Argc() < 2 )
	{
		// special case for local client
		return svs.clients;
	}

	const char	*s = Cmd_Argv( 1 );

	// numeric values are just slot numbers
	if( Q_isdigit( s ) || (s[0] == '-' && Q_isdigit( s + 1 )))
	{
		int	idnum = Q_atoi( s );

		if( idnum < 0 || idnum >= svs.maxclients )
		{
			Con_Printf( "Bad client slot: %i\n", idnum );
			return NULL;
		}

		cl = &svs.clients[idnum];

		if( !cl->state )
		{
			Con_Printf( "Client %i is not active\n", idnum );
			return NULL;
		}
		return cl;
	}

	// check for a name match
	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		if( !cl->state ) continue;

		if( !Q_strcmp( cl->name, s ))
			return cl;
	}

	Con_Printf( "Userid %s is not on the server\n", s );
	return NULL;
}

/*
==================
SV_ValidateMap

check map for typically errors
==================
*/
static qboolean SV_ValidateMap( const char *pMapName )
{
	int	flags = SV_MapIsValid( pMapName, NULL );

	if( FBitSet( flags, MAP_INVALID_VERSION ))
	{
		Con_Printf( S_ERROR "map %s is invalid or not supported\n", pMapName );
		return false;
	}

	if( !FBitSet( flags, MAP_IS_EXIST ))
	{
		Con_Printf( S_ERROR "map %s doesn't exist\n", pMapName );
		return false;
	}

	return true;
}

/*
==================
SV_Map_f

Goes directly to a given map without any savegame archiving.
For development work
==================
*/
static void SV_Map_f( void )
{
	char	mapname[MAX_QPATH];

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "map <mapname>\n" );
		return;
	}

	// hold mapname to other place
	Q_strncpy( mapname, Cmd_Argv( 1 ), sizeof( mapname ));
	COM_StripExtension( mapname );

	if( !SV_ValidateMap( mapname ))
		return;

	Cvar_DirectSet( &sv_hostmap, mapname );
	COM_LoadLevel( mapname, false );
}

/*
==================
SV_Maps_f

Lists maps according to given substring.

TODO: Make it more convenient. (Timestamp check, temporary file, ...)
==================
*/
static void SV_Maps_f( void )
{
	const char *separator = "-------------------";
	const char *argStr = Cmd_Argv( 1 ); // Substr

	if( Cmd_Argc() != 2 )
	{
		Msg( S_USAGE "maps <substring>\nmaps * for full listing\n" );
		return;
	}

	search_t *mapList = FS_Search( va( "maps/*%s*.bsp", argStr ), true, true );

	if( !mapList )
	{
		Msg( "No related map found in \"%s/maps\"\n", GI->gamefolder );
		return;
	}

	int nummaps = Cmd_ListMaps( mapList, NULL, 0, false );

	Mem_Free( mapList );

	Msg( "%s\nDirectory: \"%s/maps\" - Maps listed: %d\n", separator, GI->gamefolder, nummaps );
}

/*
==================
SV_MapBackground_f

Set background map (enable physics in menu)
==================
*/
static void SV_MapBackground_f( void )
{
	char	mapname[MAX_QPATH];

	if( Host_IsDedicated( ))
	{
		Con_Printf( S_ERROR "no background maps are allowed in dedicated mode" );
		return;
	}

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "map_background <mapname>\n" );
		return;
	}

	if( SV_Active() && !sv.background )
	{
		if( GameState->nextstate == STATE_RUNFRAME )
			Con_Printf( S_ERROR "can't set background map while game is active\n" );
		return;
	}

	// hold mapname to other place
	Q_strncpy( mapname, Cmd_Argv( 1 ), sizeof( mapname ));
	COM_StripExtension( mapname );

	if( !SV_ValidateMap( mapname ))
		return;

	// background map is always run as singleplayer
	Cvar_FullSet( "maxplayers", "1", FCVAR_LATCH );
	Cvar_FullSet( "deathmatch", "0", FCVAR_LATCH|FCVAR_SERVER );
	Cvar_FullSet( "coop", "0", FCVAR_LATCH|FCVAR_SERVER );

	COM_LoadLevel( mapname, true );
}

/*
==================
SV_NextMap_f

Change map for next in alpha-bethical ordering
For development work
==================
*/
static void SV_NextMap_f( void )
{
	char	nextmap[MAX_QPATH];
	search_t	*t = FS_Search( "maps\\*.bsp", true, con_gamemaps.value ); // only in gamedir

	if( !t ) t = FS_Search( "maps/*.bsp", true, con_gamemaps.value ); // only in gamedir

	if( !t )
	{
		Con_Printf( "next map can't be found\n" );
		return;
	}

	for( int i = 0; i < t->numfilenames; i++ )
	{
		const char *ext = COM_FileExtension( t->filenames[i] );

		if( Q_stricmp( ext, "bsp" ))
			continue;

		COM_FileBase( t->filenames[i], nextmap, sizeof( nextmap ));
		if( Q_stricmp( sv_hostmap.string, nextmap ))
			continue;

		int	next = ( i + 1 ) % t->numfilenames;
		COM_FileBase( t->filenames[next], nextmap, sizeof( nextmap ));
		Cvar_DirectSet( &sv_hostmap, nextmap );

		// found current point, check for valid
		if( SV_ValidateMap( nextmap ))
		{
			// found and valid
			COM_LoadLevel( nextmap, false );
			Mem_Free( t );
			return;
		}
		// jump to next map
	}

	Con_Printf( "failed to load next map\n" );
	Mem_Free( t );
}

/*
==============
SV_NewGame_f

==============
*/
static void SV_NewGame_f( void )
{
	if( Cmd_Argc() == 1 )
		COM_NewGame( GI->startmap );
	else if( Cmd_Argc() == 2 )
		COM_NewGame( Cmd_Argv( 1 ));
	else
		Con_Printf( S_USAGE "newgame\n" );
}

/*
==============
SV_HazardCourse_f

==============
*/
static void SV_HazardCourse_f( void )
{
	if( Cmd_Argc() != 1 )
	{
		Con_Printf( S_USAGE "hazardcourse\n" );
		return;
	}

	// special case for Gunman Chronicles: playing avi-file
	if( FS_FileExists( va( "media/%s.avi", GI->trainmap ), false ))
	{
		Cbuf_AddTextf( "wait; movie %s\n", GI->trainmap );
		Host_EndGame( true, DEFAULT_ENDGAME_MESSAGE );
	}
	else COM_NewGame( GI->trainmap );
}

/*
==============
SV_Load_f

==============
*/
static void SV_Load_f( void )
{
	char	path[MAX_QPATH];

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "load <savename>\n" );
		return;
	}

	Q_snprintf( path, sizeof( path ), DEFAULT_SAVE_DIRECTORY "%s.sav", Cmd_Argv( 1 ));
	SV_LoadGame( path );
}

/*
==============
SV_QuickLoad_f

==============
*/
static void SV_QuickLoad_f( void )
{
	Cbuf_AddText( "echo Quick Loading...; wait; load quick\n" );
}

/*
==============
SV_Save_f

==============
*/
static void SV_Save_f( void )
{
	qboolean ret = false;

	switch( Cmd_Argc( ))
	{
	case 1:
		ret = SV_SaveGame( "new" );
		break;
	case 2:
		ret = SV_SaveGame( Cmd_Argv( 1 ));
		break;
	default:
		Con_Printf( S_USAGE "save <savename>\n" );
		break;
	}

	if( ret && CL_Active() && !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		CL_HudMessage( "GAMESAVED" ); // defined in titles.txt
}

/*
==============
SV_QuickSave_f

==============
*/
static void SV_QuickSave_f( void )
{
	Cbuf_AddText( "echo Quick Saving...; wait; save quick\n" );
}

/*
==============
SV_DeleteSave_f

==============
*/
static void SV_DeleteSave_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "killsave <name>\n" );
		return;
	}

	// delete save and saveshot
	FS_Delete( va( DEFAULT_SAVE_DIRECTORY "%s.sav", Cmd_Argv( 1 )));
	FS_Delete( va( DEFAULT_SAVE_DIRECTORY "%s.bmp", Cmd_Argv( 1 )));
}

/*
==============
SV_AutoSave_f

==============
*/
static void SV_AutoSave_f( void )
{
	if( Cmd_Argc() != 1 )
	{
		Con_Printf( S_USAGE "autosave\n" );
		return;
	}

	if( sv_autosave.value )
		SV_SaveGame( "autosave" );
}

/*
==================
SV_Restart_f

restarts current level
==================
*/
static void SV_Restart_f( void )
{
	// because restart can be multiple issued
	if( sv.state != ss_active )
		return;
	COM_LoadLevel( sv.name, sv.background );
}

/*
==================
SV_Reload_f

continue from latest savedgame
==================
*/
static void SV_Reload_f( void )
{
	// because reload can be multiple issued
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( !SV_LoadGame( SV_GetLatestSave( )))
		COM_LoadLevel( sv_hostmap.string, false );
}

/*
==================
SV_ChangeLevel_f

classic change level
==================
*/
static void SV_ChangeLevel_f( void )
{
	if( Cmd_Argc() < 2 ) // allow extra arguments, for compatibility
	{
		Con_Printf( S_USAGE "changelevel <mapname>\n" );
		return;
	}

	SV_QueueChangeLevel( Cmd_Argv( 1 ), NULL );
}

/*
==================
SV_ChangeLevel2_f

smooth change level
==================
*/
static void SV_ChangeLevel2_f( void )
{
	if( Cmd_Argc() < 2 ) // allow extra arguments, for compatibility
	{
		Con_Printf( S_USAGE "changelevel2 <mapname> [landmark]\n" );
		return;
	}

	if( Cmd_Argc() == 2 ) // with single argument, behaves like usual changelevel
		SV_QueueChangeLevel( Cmd_Argv( 1 ), NULL );
	else SV_QueueChangeLevel( Cmd_Argv( 1 ), Cmd_Argv( 2 ));
}

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
static void SV_Kick_f( void )
{
	sv_client_t	*cl;

	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "kick <#id|name> [reason]\n" );
		return;
	}

	const char *param = Cmd_Argv( 1 );

	if( *param == '#' && Q_isdigit( param + 1 ) )
		cl = SV_ClientById( Q_atoi( param + 1 ) );
	else cl = SV_ClientByName( param );

	if( !cl )
	{
		Con_Printf( "Client is not on the server\n" );
		return;
	}

	SV_KickPlayer( cl, "%s", Cmd_Argv( 2 ));
}

/*
==================
SV_EntPatch_f
==================
*/
static void SV_EntPatch_f( void )
{
	const char	*mapname;

	if( Cmd_Argc() < 2 )
	{
		if( sv.state != ss_dead )
		{
			mapname = sv.name;
		}
		else
		{
			Con_Printf( S_USAGE "entpatch <mapname>\n" );
			return;
		}
	}
	else mapname = Cmd_Argv( 1 );

	SV_WriteEntityPatch( mapname );
}

/*
================
SV_GetGameAppID

maps the current game directory to an AppID,
used by the status/stat version line
================
*/
static int SV_GetGameAppID( void )
{
	static const struct
	{
		int appid;
		const char *gamedir;
	} map[] =
	{
		{ 10,  "cstrike" },
		{ 20,  "tfc" },
		{ 30,  "dod" },
		{ 40,  "dmc" },
		{ 50,  "gearbox" },
		{ 60,  "ricochet" },
		{ 70,  "valve" },
		{ 80,  "czero" },
		{ 100, "czeror" },
		{ 130, "bshift" },
		{ 150, "cstrike_beta" },
	};
	const char *gamedir = GI ? GI->gamefolder : "valve";

	for( int i = 0; i < ARRAYSIZE( map ); i++ )
	{
		if( !Q_stricmp( map[i].gamedir, gamedir ))
			return map[i].appid;
	}

	return 70;
}

/*
================
SV_StatusPrintServerInfo

prints the status header
================
*/
static void SV_StatusPrintServerInfo( qboolean withAppID )
{
	netadr_t ip4, ip6;
	NET_GetLocalAddress( &ip4, &ip6 );

	Con_Printf( "hostname:  %s\n", Cvar_VariableString( "hostname" ));

	if( withAppID )
		Con_Printf( "version :  %i/%s %d insecure (%i)\n",
			PROTOCOL_VERSION, XASH_VERSION, Q_buildnum(), SV_GetGameAppID( ));
	else
		Con_Printf( "version :  %i/%s %d insecure\n",
			PROTOCOL_VERSION, XASH_VERSION, Q_buildnum( ));

	if( ip4.type == NA_IP )
		Con_Printf( "tcp/ip  :  %s\n", NET_AdrToString( ip4 ));
	if( ip6.type == NA_IP6 )
		Con_Printf( "tcp/ipv6:  %s\n", NET_AdrToString( ip6 ));

	Con_Printf( "map     :  %s at: %d x, %d y, %d z\n", sv.name, 0, 0, 0 );
}

/*
================
SV_StatusGetUniqueID

returns the uniqueid column value, parsers expect a single token
================
*/
static const char *SV_StatusGetUniqueID( const sv_client_t *cl )
{
	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return "BOT";

	const char *uuid = Info_ValueForKey( cl->useragent, "uuid" );

	return COM_StringEmpty( uuid ) ? "UNKNOWN" : uuid;
}

/*
================
SV_StatusGetPlaytime

splits connection time into hours, minutes and seconds
================
*/
static void SV_StatusGetPlaytime( const sv_client_t *cl, int *hours, int *minutes, int *seconds )
{
	*seconds = host.realtime - cl->netchan.connect_time;
	*minutes = *seconds / 60;
	*hours = 0;

	if( *minutes )
	{
		*seconds %= 60;
		*hours = *minutes / 60;
		if( *hours )
			*minutes %= 60;
	}
}

/*
================
SV_StatusCanPrint

common checks for status/stat/stats
================
*/
static qboolean SV_StatusCanPrint( void )
{
#if !XASH_DEDICATED
	if( !svs.clients && CL_Active( ))
	{
		Cmd_ForwardToServer();
		return false;
	}
#endif // XASH_DEDICATED

	if( !svs.clients || sv.background )
	{
		Con_Printf( "^3no server running.\n" );
		return false;
	}

	return true;
}

/*
================
SV_Status_f
================
*/
static void SV_Status_f( void )
{
	if( !SV_StatusCanPrint( ))
		return;

	int players, bots;
	SV_GetPlayerCount( &players, &bots );
	SV_StatusPrintServerInfo( true );
	Con_Printf( "players :  %i active (%i max)\n\n", players, svs.maxclients );
	Con_Printf( "#      name userid uniqueid frag time ping loss adr\n" );

	for( int i = 0; i < svs.maxclients; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];

		if( cl->state != cs_spawned || !cl->edict )
			continue;

		int hours, minutes, seconds;
		SV_StatusGetPlaytime( cl, &hours, &minutes, &seconds );

		string playtime;
		if( hours )
			Q_snprintf( playtime, sizeof( playtime ), "%2i:%02i:%02i", hours, minutes, seconds );
		else
			Q_snprintf( playtime, sizeof( playtime ), "%02i:%02i", minutes, seconds );

		char line[512];
		Q_snprintf( line, sizeof( line ), "#%2i %8s %i %s %3i %s %4i  %3i",
			i + 1, va( "\"%s\"", cl->name ), cl->userid, SV_StatusGetUniqueID( cl ),
			(int)cl->edict->v.frags, playtime, SV_CalcPing( cl ), (int)cl->packet_loss );

		size_t len = Q_strlen( line );

		if( cl->netchan.remote_address.type == NA_IP )
		{
			Q_snprintf( line + len, sizeof( line ) - len, " %s", NET_AdrToString( cl->netchan.remote_address ));
			len = Q_strlen( line );
		}

		Q_snprintf( line + len, sizeof( line ) - len, "\n" );
		Con_Printf( "%s", line );
	}

	Con_Printf( "%i users\n", players );
}

/*
================
SV_StatusFormatted_f

tab-separated status variant, same data as status
================
*/
static void SV_StatusFormatted_f( void )
{
	if( !SV_StatusCanPrint( ))
		return;

	int players, bots;
	SV_GetPlayerCount( &players, &bots );
	SV_StatusPrintServerInfo( false );
	Con_Printf( "players :  %i active (%i max)\n\n", players, svs.maxclients );
	Con_Printf( "%-2.2s\t%-9.9s\t%-7.7s\t%-20.20s\t%-4.4s\t%-8.8s\t%-4.4s\t%-4.4s\t%-21.21s\n",
		"# ", "name", "userid   ", "uniqueid ", "frag", "time    ", "ping", "loss", "adr" );

	for( int i = 0; i < svs.maxclients; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];

		if( cl->state != cs_spawned || !cl->edict )
			continue;

		int seconds = host.realtime - cl->netchan.connect_time;
		int minutes = seconds / 60;
		int hours = minutes / 60;

		string playtime;
		if( minutes && hours )
			Q_snprintf( playtime, sizeof( playtime ), "%-2i:%02i:%02i", hours, minutes % 60, seconds % 60 );
		else
			Q_snprintf( playtime, sizeof( playtime ), "%02i:%02i", minutes, seconds % 60 );

		Con_Printf( "%-2.2s\t%-9.9s\t%-7.7s\t%-20.20s\t%-4.4s\t%-8.8s\t%-4.4s\t%-4.4s\t%-21.21s\n",
			va( "%-2i", i + 1 ), va( "\"%s\"", cl->name ), va( "%-7i", cl->userid ), SV_StatusGetUniqueID( cl ),
			va( "%-4i", (int)cl->edict->v.frags ), playtime, va( "%-4i", SV_CalcPing( cl )),
			va( "%-4i", (int)cl->packet_loss ), NET_AdrToString( cl->netchan.remote_address ));
	}

	Con_Printf( "%i users\n", players );
}

/*
================
SV_Stats_f

print server statistics
================
*/
static void SV_Stats_f( void )
{
	double avgIn = 0.0;
	double avgOut = 0.0;
	int players = 0;

	if( svs.clients )
	{
		for( int i = 0; i < svs.maxclients; i++ )
		{
			const sv_client_t *cl = &svs.clients[i];

			if( FBitSet( cl->flags, FCL_FAKECLIENT ) || cl->state < cs_connected )
				continue;

			players++;
			avgIn += cl->netchan.flow[FLOW_INCOMING].avgkbytespersec;
			avgOut += cl->netchan.flow[FLOW_OUTGOING].avgkbytespersec;
		}
	}

	Con_Printf( "CPU   In    Out   Uptime  Users   FPS    Players\n" );
	Con_Printf( "%5.2f %5.2f %5.2f %7i %5i %7.2f %7i\n",
		100.0 * Sys_GetCPUUsage(),
		avgIn, avgOut,
		(int)host.realtime / 60,
		SV_GetUserIDCount(),
		host.frametime > 0.0 ? 1.0 / host.frametime : 0.0,
		players );
}

/*
================
SV_Status2_f

verbose server status, the Xash3D FWGS flavored one:
client useragent, input devices and other per-player details
================
*/
static void SV_Status2_f( void )
{
#if !XASH_DEDICATED
	if( !svs.clients && CL_Active( ))
	{
		Cmd_ForwardToServer();
		return;
	}
#endif // XASH_DEDICATED

	if( !svs.clients || sv.background )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	Con_Printf( "map: %s\n", sv.name );
	Con_Printf( "# score ping dev  lastmsg qport useragent\t\tname\t\taddress\n" );

	for( int i = 0; i < svs.maxclients; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];

		if( !cl->state || !cl->edict )
			continue;

		const char *s;
		if( cl->state == cs_connected )
			s = "Connect ";
		else if( cl->state == cs_spawning )
			s = "Spawning";
		else if( cl->state == cs_zombie )
			s = "Zombie  ";
		else if( FBitSet( cl->flags, FCL_FAKECLIENT ))
			s = "Bot     ";
		else
			s = va( "%8i", SV_CalcPing( cl ));

		int input_devices = Q_atoi( Info_ValueForKey( cl->useragent, "d" ));
		int j = 0;
		char devices[8];

		if( FBitSet( input_devices, INPUT_DEVICE_MOUSE ))
			devices[j++] = 'm';

		if( FBitSet( input_devices, INPUT_DEVICE_TOUCH ))
			devices[j++] = 't';

		if( FBitSet( input_devices, INPUT_DEVICE_JOYSTICK ))
			devices[j++] = 'j';

		if( FBitSet( input_devices, INPUT_DEVICE_VR ))
			devices[j++] = 'v';

		if( j == 0 )
			Q_strncpy( devices, "n/a", sizeof( devices ));
		else
			devices[j++] = 0;

		string version;
		string os;
		string arch;
		Q_strncpy( version, Info_ValueForKey( cl->useragent, "v" ), sizeof( version ));
		Q_strncpy( os, Info_ValueForKey( cl->useragent, "o" ), sizeof( os ));
		Q_strncpy( arch, Info_ValueForKey( cl->useragent, "a" ), sizeof( arch ));
		int buildnum = Q_atoi( Info_ValueForKey( cl->useragent, "b" ));

		if( COM_StringEmpty( version ))
			Q_strncpy( version, "n/a", sizeof( version ));
		if( COM_StringEmpty( os ))
			Q_strncpy( os, "n/a", sizeof( os ));
		if( COM_StringEmpty( arch ))
			Q_strncpy( arch, "n/a", sizeof( arch ));

		Con_Printf( "%2i %5i %4s %4s %.5f %5i %s (%s-%s %i)\t%8s\t%8s\n",
			i, (int)cl->edict->v.frags, s, devices, host.realtime - cl->netchan.last_received, cl->netchan.qport,
			version, os, arch, buildnum,
			cl->name, NET_BaseAdrToString( cl->netchan.remote_address ) );

	}
	Con_Printf( "\n" );
}

/*
==================
SV_ConSay_f
==================
*/
static void SV_ConSay_f( void )
{
	char		text[MAX_SYSPATH];

	if( Cmd_Argc() < 2 ) return;

	if( !svs.clients || sv.background )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	const char	*p = Cmd_Args();
	Q_strncpy( text, *p == '"' ? p + 1 : p, sizeof( text ));

	if( *p == '"' )
	{
		text[Q_strlen(text) - 1] = 0;
	}

	Log_Printf( "Server say: \"%s\"\n", text );
	Q_snprintf( text, sizeof( text ), "%s: %s", Cvar_VariableString( "hostname" ), p );
	SV_BroadcastPrintf( NULL, "%s\n", text );
}

/*
==================
SV_Heartbeat_f
==================
*/
static void SV_Heartbeat_f( void )
{
	NET_MasterClear();
}

/*
===========
SV_ServerInfo_f

Examine or change the serverinfo string
===========
*/
static void SV_ServerInfo_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "Server info settings:\n" );
		Info_Print( svs.serverinfo );
		Con_Printf( "Total %zu symbols\n", Q_strlen( svs.serverinfo ));
		return;
	}

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "serverinfo [ <key> <value> ]\n");
		return;
	}

	if( Cmd_Argv(1)[0] == '*' )
	{
		Con_Printf( "Star variables cannot be changed.\n" );
		return;
	}

	// if this is a cvar, change it too
	convar_t	*var = Cvar_FindVar( Cmd_Argv( 1 ));
	if( var )
	{
		freestring( var->string ); // free the old value string
		var->string = copystring( Cmd_Argv( 2 ));
		var->value = Q_atof( var->string );
	}

	Info_SetValueForStarKey( svs.serverinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), MAX_SERVERINFO_STRING );
	SV_BroadcastCommand( "fullserverinfo \"%s\"\n", svs.serverinfo );
}

/*
===========
SV_LocalInfo_f

Examine or change the localinfo string
===========
*/
static void SV_LocalInfo_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "Local info settings:\n" );
		Info_Print( svs.localinfo );
		Con_Printf( "Total %zu symbols\n", Q_strlen( svs.localinfo ));
		return;
	}

	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "localinfo [ <key> <value> ]\n");
		return;
	}

	if( Cmd_Argv(1)[0] == '*' )
	{
		Con_Printf( "Star variables cannot be changed.\n" );
		return;
	}

	Info_SetValueForStarKey( svs.localinfo, Cmd_Argv(1), Cmd_Argv(2), MAX_LOCALINFO_STRING );
}

/*
===========
SV_ClientInfo_f

Examine all a users info strings
===========
*/
static void SV_ClientInfo_f( void )
{
	sv_client_t	*cl;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "clientinfo <userid>\n" );
		return;
	}

	if(( cl = SV_SetPlayer( )) == NULL )
		return;

	Con_Printf( "userinfo\n" );
	Con_Printf( "--------\n" );
	Info_Print( cl->userinfo );

}

/*
===========
SV_ClientUserAgent_f

Examine useragent strings
===========
*/
static void SV_ClientUserAgent_f( void )
{
	sv_client_t	*cl;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "clientuseragent <userid>\n" );
		return;
	}

	if(( cl = SV_SetPlayer( )) == NULL )
		return;

	Con_Printf( "useragent\n" );
	Con_Printf( "---------\n" );
	Info_Print( cl->useragent );
}

/*
===============
SV_KillServer_f

Kick everyone off, possibly in preparation for a new game
===============
*/
static void SV_KillServer_f( void )
{
	SV_Shutdown( "Server was killed due to shutdownserver command\n" );
}

/*
===============
SV_PlayersOnly_f

disable plhysics but players
===============
*/
static void SV_PlayersOnly_f( void )
{
	if( !Cvar_VariableInteger( "sv_cheats" )) return;

	sv.playersonly ^= 1;

	SV_BroadcastPrintf( NULL, "%s game physic\n", sv.playersonly ? "Freeze" : "Resume" );
}

/*
===============
SV_EdictUsage_f

===============
*/
static void SV_EdictUsage_f( void )
{
	if( sv.state != ss_active )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	int	active = pfnNumberOfEntities();
	Con_Printf( "%5i edicts is used\n", active );
	Con_Printf( "%5i edicts is free\n", GI->max_edicts - active );
	Con_Printf( "%5i total\n", GI->max_edicts );
}

/*
===============
SV_EntityInfo_f

===============
*/
static void SV_EntityInfo_f( void )
{
	if( sv.state != ss_active )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	for( int i = 0; i < svgame.numEntities; i++ )
	{
		edict_t	*ent = SV_EdictNum( i );
		if( !SV_IsValidEdict( ent )) continue;

		Con_Printf( "%5i origin: %.f %.f %.f", i, ent->v.origin[0], ent->v.origin[1], ent->v.origin[2] );

		if( ent->v.classname )
			Con_Printf( ", class: %s", SV_GetString( ent->v.classname ));

		if( ent->v.globalname )
			Con_Printf( ", global: %s", SV_GetString( ent->v.globalname ));

		if( ent->v.targetname )
			Con_Printf( ", name: %s", SV_GetString( ent->v.targetname ));

		if( ent->v.target )
			Con_Printf( ", target: %s", SV_GetString( ent->v.target ));

		if( ent->v.model )
			Con_Printf( ", model: %s", SV_GetString( ent->v.model ));

		Con_Printf( "\n" );
	}
}

/*
================
Rcon_Redirect_f

Force redirect N lines of console output to client
================
*/
static void Rcon_Redirect_f( void )
{
	int lines = 2000;

	if( !host.rd.target )
	{
		Msg( "redirect is only valid from rcon\n" );
		return;
	}

	if( Cmd_Argc() == 2 )
		lines = Q_atoi( Cmd_Argv( 1 ) );

	host.rd.lines = lines;
	Msg( "Redirection enabled for next %d lines\n", lines );
}

static void SV_ListMessages_f( void )
{
	int i;

	Con_Printf( "num size name\n" );
	for( i = 1; i < MAX_USER_MESSAGES; i++ )
	{
		if( COM_StringEmpty( svgame.msg[i].name ))
			break;

		Con_Printf( "%3d\t%3d\t%s\n", svgame.msg[i].number, svgame.msg[i].size, svgame.msg[i].name );
	}

	Con_Printf( "Total %i messages\n", i - 1 );
}

/*
==================
SV_InitHostCommands

commands that create server
is available always
==================
*/
void SV_InitHostCommands( void )
{
	Cmd_AddRestrictedCommand( "map", SV_Map_f, "start new level" );
	Cmd_AddCommand( "maps", SV_Maps_f, "list maps" );

	if( host.type == HOST_NORMAL )
	{
		Cmd_AddRestrictedCommand( "newgame", SV_NewGame_f, "begin new game" );
		Cmd_AddRestrictedCommand( "hazardcourse", SV_HazardCourse_f, "starting a Hazard Course" );
		Cmd_AddRestrictedCommand( "map_background", SV_MapBackground_f, "set background map" );
		Cmd_AddRestrictedCommand( "load", SV_Load_f, "load a saved game file" );
		Cmd_AddRestrictedCommand( "loadquick", SV_QuickLoad_f, "load a quick-saved game file" );
		Cmd_AddRestrictedCommand( "reload", SV_Reload_f, "continue from latest save or restart level" );
		Cmd_AddRestrictedCommand( "killsave", SV_DeleteSave_f, "delete a saved game file and saveshot" );
		Cmd_AddRestrictedCommand( "nextmap", SV_NextMap_f, "load next level" );
	}
}

/*
==================
SV_InitOperatorCommands
==================
*/
void SV_InitOperatorCommands( void )
{
	Cmd_AddCommand( "heartbeat", SV_Heartbeat_f, "send a heartbeat to the master server" );
	Cmd_AddCommand( "kick", SV_Kick_f, "kick a player off the server by number or name" );
	Cmd_AddCommand( "status", SV_Status_f, "print server status information" );
	Cmd_AddCommand( "status2", SV_Status2_f, "print verbose server status information" );
	Cmd_AddCommand( "stat", SV_StatusFormatted_f, "print server status information, tab-separated" );
	Cmd_AddCommand( "stats", SV_Stats_f, "print server statistics" );
	Cmd_AddCommand( "localinfo", SV_LocalInfo_f, "examine or change the localinfo string" );
	Cmd_AddCommand( "serverinfo", SV_ServerInfo_f, "examine or change the serverinfo string" );
	Cmd_AddCommand( "clientinfo", SV_ClientInfo_f, "print user infostring (player num required)" );
	Cmd_AddCommand( "clientuseragent", SV_ClientUserAgent_f, "print user agent (player num required)" );
	Cmd_AddCommand( "playersonly", SV_PlayersOnly_f, "freezes time, except for players" );
	Cmd_AddCommand( "restart", SV_Restart_f, "restarting current level" );
	Cmd_AddCommand( "entpatch", SV_EntPatch_f, "write entity patch to allow external editing" );
	Cmd_AddCommand( "edict_usage", SV_EdictUsage_f, "show info about edicts usage" );
	Cmd_AddCommand( "entity_info", SV_EntityInfo_f, "show more info about edicts" );
	Cmd_AddCommand( "shutdownserver", SV_KillServer_f, "shutdown current server" );
	Cmd_AddCommand( "changelevel", SV_ChangeLevel_f, "change level" );
	Cmd_AddCommand( "changelevel2", SV_ChangeLevel2_f, "smooth change level" );
	Cmd_AddCommand( "redirect", Rcon_Redirect_f, "force enable rcon redirection" );
	Cmd_AddCommand( "logaddress", SV_SetLogAddress_f, "sets address and port for remote logging host" );
	Cmd_AddCommand( "log", SV_ServerLog_f, "enables logging to file" );
	Cmd_AddCommand( "str64stats", SV_PrintStr64Stats_f, "print engine pool string statistics" );
	Cmd_AddCommand( "sv_list_messages", SV_ListMessages_f, "list registered user messages" );

	if( host.type == HOST_NORMAL )
	{
		Cmd_AddCommand( "save", SV_Save_f, "save the game to a file" );
		Cmd_AddCommand( "savequick", SV_QuickSave_f, "save the game to the quicksave" );
		Cmd_AddCommand( "autosave", SV_AutoSave_f, "save the game to 'autosave' file" );
	}
	else if( host.type == HOST_DEDICATED )
	{
		Cmd_AddCommand( "say", SV_ConSay_f, "send a chat message to everyone on the server" );
	}
}

/*
==================
SV_KillOperatorCommands
==================
*/
void SV_KillOperatorCommands( void )
{
	Cmd_RemoveCommand( "heartbeat" );
	Cmd_RemoveCommand( "kick" );
	Cmd_RemoveCommand( "status" );
	Cmd_RemoveCommand( "status2" );
	Cmd_RemoveCommand( "stat" );
	Cmd_RemoveCommand( "stats" );
	Cmd_RemoveCommand( "localinfo" );
	Cmd_RemoveCommand( "serverinfo" );
	Cmd_RemoveCommand( "clientinfo" );
	Cmd_RemoveCommand( "clientuseragent" );
	Cmd_RemoveCommand( "playersonly" );
	Cmd_RemoveCommand( "restart" );
	Cmd_RemoveCommand( "entpatch" );
	Cmd_RemoveCommand( "edict_usage" );
	Cmd_RemoveCommand( "entity_info" );
	Cmd_RemoveCommand( "shutdownserver" );
	Cmd_RemoveCommand( "changelevel" );
	Cmd_RemoveCommand( "changelevel2" );
	Cmd_RemoveCommand( "redirect" );
	Cmd_RemoveCommand( "logaddress" );
	Cmd_RemoveCommand( "log" );
	Cmd_RemoveCommand( "str64stats" );

	if( host.type == HOST_NORMAL )
	{
		Cmd_RemoveCommand( "save" );
		Cmd_RemoveCommand( "savequick" );
		Cmd_RemoveCommand( "autosave" );
	}
	else if( host.type == HOST_DEDICATED )
	{
		Cmd_RemoveCommand( "say" );
	}
}
