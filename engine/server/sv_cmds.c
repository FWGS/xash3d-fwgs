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
void SV_ClientPrintf( sv_client_t *cl, char *fmt, ... )
{
	char	string[MAX_SYSPATH];
	va_list	argptr;

	if( FBitSet( cl->flags, FCL_FAKECLIENT ))
		return;
	
	va_start( argptr, fmt );
	Q_vsprintf( string, fmt, argptr );
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
void SV_BroadcastPrintf( sv_client_t *ignore, char *fmt, ... )
{
	char		string[MAX_SYSPATH];
	va_list		argptr;
	sv_client_t	*cl;
	int		i;

	va_start( argptr, fmt );
	Q_vsprintf( string, fmt, argptr );
	va_end( argptr );

	if( sv.state == ss_active )
	{
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

	if( host.type == HOST_DEDICATED )
	{
		// echo to console
		Con_DPrintf( string );
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
	Q_vsprintf( string, fmt, argptr );
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
	char		*s;
	sv_client_t	*cl;
	int		i, idnum;

	if( !svs.clients || sv.background )
		return NULL;

	if( svs.maxclients == 1 || Cmd_Argc() < 2 )
	{
		// special case for local client
		return svs.clients;
	}

	s = Cmd_Argv( 1 );

	// numeric values are just slot numbers
	if( Q_isdigit( s ) || (s[0] == '-' && Q_isdigit( s + 1 )))
	{
		idnum = Q_atoi( s );

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
qboolean SV_ValidateMap( const char *pMapName, qboolean check_spawn )
{
	char	*spawn_entity;
	int	flags;

	// determine spawn entity classname
	if( !check_spawn || (int)sv_maxclients->value <= 1 )
		spawn_entity = GI->sp_entity;
	else spawn_entity = GI->mp_entity;

	flags = SV_MapIsValid( pMapName, spawn_entity, NULL );

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

	if( check_spawn && !FBitSet( flags, MAP_HAS_SPAWNPOINT ))
	{
		Con_Printf( S_ERROR "map %s doesn't have a valid spawnpoint\n", pMapName );
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
void SV_Map_f( void )
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

	if( !SV_ValidateMap( mapname, true ))
		return;

	Cvar_DirectSet( sv_hostmap, mapname );
	COM_LoadLevel( mapname, false );
}

/*
==================
SV_MapBackground_f

Set background map (enable physics in menu)
==================
*/
void SV_MapBackground_f( void )
{
	char	mapname[MAX_QPATH];

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

	if( !SV_ValidateMap( mapname, false ))
		return;

	// background map is always run as singleplayer
	Cvar_FullSet( "maxplayers", "1", FCVAR_LATCH );
	Cvar_FullSet( "deathmatch", "0", FCVAR_LATCH );
	Cvar_FullSet( "coop", "0", FCVAR_LATCH );

	COM_LoadLevel( mapname, true );
}

/*
==================
SV_NextMap_f

Change map for next in alpha-bethical ordering
For development work
==================
*/
void SV_NextMap_f( void )
{
	char	nextmap[MAX_QPATH];
	int	i, next;
	search_t	*t;

	t = FS_Search( "maps/*.bsp", true, true ); // only in gamedir
	if( !t )
	{
		Con_Printf( "next map can't be found\n" );
		return;
	}

	for( i = 0; i < t->numfilenames; i++ )
	{
		const char *ext = COM_FileExtension( t->filenames[i] );

		if( Q_stricmp( ext, "bsp" ))
			continue;

		COM_FileBase( t->filenames[i], nextmap );
		if( Q_stricmp( sv_hostmap->string, nextmap ))
			continue; 

		next = ( i + 1 ) % t->numfilenames;
		COM_FileBase( t->filenames[next], nextmap );
		Cvar_DirectSet( sv_hostmap, nextmap );

		// found current point, check for valid
		if( SV_ValidateMap( nextmap, true ))
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
void SV_NewGame_f( void )
{
	if( Cmd_Argc() != 1 )
	{
		Con_Printf( S_USAGE "newgame\n" );
		return;
	}

	COM_NewGame( GI->startmap );
}

/*
==============
SV_HazardCourse_f

==============
*/
void SV_HazardCourse_f( void )
{
	if( Cmd_Argc() != 1 )
	{
		Con_Printf( S_USAGE "hazardcourse\n" );
		return;
	}

	// special case for Gunman Chronicles: playing avi-file
	if( FS_FileExists( va( "media/%s.avi", GI->trainmap ), false ))
	{
		Cbuf_AddText( va( "wait; movie %s\n", GI->trainmap ));
		Host_EndGame( true, DEFAULT_ENDGAME_MESSAGE );
	}
	else COM_NewGame( GI->trainmap );
}

/*
==============
SV_Load_f

==============
*/
void SV_Load_f( void )
{
	char	path[MAX_QPATH];

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "load <savename>\n" );
		return;
	}

	Q_snprintf( path, sizeof( path ), "%s%s.sav", DEFAULT_SAVE_DIRECTORY, Cmd_Argv( 1 ));
	SV_LoadGame( path );
}

/*
==============
SV_QuickLoad_f

==============
*/
void SV_QuickLoad_f( void )
{
	Cbuf_AddText( "echo Quick Loading...; wait; load quick" );
}

/*
==============
SV_Save_f

==============
*/
void SV_Save_f( void )
{
	switch( Cmd_Argc( ))
	{
	case 1:
		SV_SaveGame( "new" );
		break;
	case 2:
		SV_SaveGame( Cmd_Argv( 1 ));
		break;
	default:
		Con_Printf( S_USAGE "save <savename>\n" );
		break;
	}
}

/*
==============
SV_QuickSave_f

==============
*/
void SV_QuickSave_f( void )
{
	Cbuf_AddText( "echo Quick Saving...; wait; save quick" );
}

/*
==============
SV_DeleteSave_f

==============
*/
void SV_DeleteSave_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "killsave <name>\n" );
		return;
	}

	// delete save and saveshot
	FS_Delete( va( "%s%s.sav", DEFAULT_SAVE_DIRECTORY, Cmd_Argv( 1 )));
	FS_Delete( va( "%s%s.bmp", DEFAULT_SAVE_DIRECTORY, Cmd_Argv( 1 )));
}

/*
==============
SV_AutoSave_f

==============
*/
void SV_AutoSave_f( void )
{
	if( Cmd_Argc() != 1 )
	{
		Con_Printf( S_USAGE "autosave\n" );
		return;
	}

	SV_SaveGame( "autosave" );
}

/*
==================
SV_Restart_f

restarts current level
==================
*/
void SV_Restart_f( void )
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
void SV_Reload_f( void )
{
	// because reload can be multiple issued
	if( GameState->nextstate != STATE_RUNFRAME )
		return;

	if( !SV_LoadGame( SV_GetLatestSave( )))
		COM_LoadLevel( sv_hostmap->string, false );
}

/*
==================
SV_ChangeLevel_f

classic change level
==================
*/
void SV_ChangeLevel_f( void )
{
	if( Cmd_Argc() != 2 )
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
void SV_ChangeLevel2_f( void )
{
	if( Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "changelevel2 <mapname> <landmark>\n" );
		return;
	}

	SV_QueueChangeLevel( Cmd_Argv( 1 ), Cmd_Argv( 2 ));
}

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
void SV_Kick_f( void )
{
	sv_client_t	*cl;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "kick <userid> | <name>\n" );
		return;
	}

	if(( cl = SV_SetPlayer( )) == NULL )
		return;

	if( NET_IsLocalAddress( cl->netchan.remote_address ))
	{
		Con_Printf( "The local player cannot be kicked!\n" );
		return;
	}

	Log_Printf( "Kick: \"%s<%i>\" was kicked\n", cl->name, cl->userid );
	SV_BroadcastPrintf( cl, "%s was kicked\n", cl->name );
	SV_ClientPrintf( cl, "You were kicked from the game\n" );
	SV_DropClient( cl, false );
}

/*
==================
SV_EntPatch_f
==================
*/
void SV_EntPatch_f( void )
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
SV_Status_f
================
*/
void SV_Status_f( void )
{
	sv_client_t	*cl;
	int		i;

	if( !svs.clients || sv.background )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	Con_Printf( "map: %s\n", sv.name );
	Con_Printf( "num score ping    name            lastmsg address               port \n" );
	Con_Printf( "--- ----- ------- --------------- ------- --------------------- ------\n" );

	for( i = 0, cl = svs.clients; i < svs.maxclients; i++, cl++ )
	{
		int	j, l;
		char	*s;

		if( !cl->state ) continue;

		Con_Printf( "%3i ", i );
		Con_Printf( "%5i ", (int)cl->edict->v.frags );

		if( cl->state == cs_connected ) Con_Printf( "Connect" );
		else if( cl->state == cs_zombie ) Con_Printf( "Zombie " );
		else if( FBitSet( cl->flags, FCL_FAKECLIENT )) Con_Printf( "Bot   " );
		else Con_Printf( "%7i ", SV_CalcPing( cl ));

		Con_Printf( "%s", cl->name );
		l = 24 - Q_strlen( cl->name );
		for( j = 0; j < l; j++ ) Con_Printf( " " );
		Con_Printf( "%g ", ( host.realtime - cl->netchan.last_received ));
		s = NET_BaseAdrToString( cl->netchan.remote_address );
		Con_Printf( "%s", s );
		l = 22 - Q_strlen( s );
		for( j = 0; j < l; j++ ) Con_Printf( " " );
		Con_Printf( "%5i", cl->netchan.qport );
		Con_Printf( "\n" );
	}
	Con_Printf( "\n" );
}

/*
==================
SV_ConSay_f
==================
*/
void SV_ConSay_f( void )
{
	char	*p, text[MAX_SYSPATH];

	if( Cmd_Argc() < 2 ) return;

	if( !svs.clients || sv.background )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	Q_snprintf( text, sizeof( text ), "%s: ", Cvar_VariableString( "hostname" ));
	p = Cmd_Args();

	if( *p == '"' )
	{
		p++;
		p[Q_strlen(p) - 1] = 0;
	}

	Q_strncat( text, p, MAX_SYSPATH );
	SV_BroadcastPrintf( NULL, "%s\n", text );
	Log_Printf( "Server say: \"%s\"\n", p );
}

/*
==================
SV_Heartbeat_f
==================
*/
void SV_Heartbeat_f( void )
{
	svs.last_heartbeat = MAX_HEARTBEAT;
}

/*
===========
SV_ServerInfo_f

Examine or change the serverinfo string
===========
*/
void SV_ServerInfo_f( void )
{
	convar_t	*var;

	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "Server info settings:\n" );
		Info_Print( svs.serverinfo );
		Con_Printf( "Total %i symbols\n", Q_strlen( svs.serverinfo ));
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
	var = Cvar_FindVar( Cmd_Argv( 1 ));
	if( var )
	{
		freestring( var->string ); // free the old value string	
		var->string = copystring( Cmd_Argv( 2 ));
		var->value = Q_atof( var->string );
	}

	Info_SetValueForStarKey( svs.serverinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), MAX_SERVERINFO_STRING );
	SV_BroadcastCommand( "fullserverinfo \"%s\"\n", SV_Serverinfo( ));
}

/*
===========
SV_LocalInfo_f

Examine or change the localinfo string
===========
*/
void SV_LocalInfo_f( void )
{
	if( Cmd_Argc() == 1 )
	{
		Con_Printf( "Local info settings:\n" );
		Info_Print( svs.localinfo );
		Con_Printf( "Total %i symbols\n", Q_strlen( svs.localinfo ));
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
void SV_ClientInfo_f( void )
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
===============
SV_KillServer_f

Kick everyone off, possibly in preparation for a new game
===============
*/
void SV_KillServer_f( void )
{
	Host_ShutdownServer();
}

/*
===============
SV_PlayersOnly_f

disable plhysics but players
===============
*/
void SV_PlayersOnly_f( void )
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
void SV_EdictUsage_f( void )
{
	int	active;

	if( sv.state != ss_active )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	active = pfnNumberOfEntities(); 
	Con_Printf( "%5i edicts is used\n", active );
	Con_Printf( "%5i edicts is free\n", GI->max_edicts - active );
	Con_Printf( "%5i total\n", GI->max_edicts );
}

/*
===============
SV_EntityInfo_f

===============
*/
void SV_EntityInfo_f( void )
{
	edict_t	*ent;
	int	i;

	if( sv.state != ss_active )
	{
		Con_Printf( "^3no server running.\n" );
		return;
	}

	for( i = 0; i < svgame.numEntities; i++ )
	{
		ent = EDICT_NUM( i );
		if( !SV_IsValidEdict( ent )) continue;

		Con_Printf( "%5i origin: %.f %.f %.f", i, ent->v.origin[0], ent->v.origin[1], ent->v.origin[2] );

		if( ent->v.classname )
			Con_Printf( ", class: %s", STRING( ent->v.classname ));

		if( ent->v.globalname )
			Con_Printf( ", global: %s", STRING( ent->v.globalname ));

		if( ent->v.targetname )
			Con_Printf( ", name: %s", STRING( ent->v.targetname ));

		if( ent->v.target )
			Con_Printf( ", target: %s", STRING( ent->v.target ));

		if( ent->v.model )
			Con_Printf( ", model: %s", STRING( ent->v.model ));

		Con_Printf( "\n" );
	}
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
	Cmd_AddCommand( "map", SV_Map_f, "start new level" );

	if( host.type == HOST_NORMAL )
	{
		Cmd_AddCommand( "newgame", SV_NewGame_f, "begin new game" );
		Cmd_AddCommand( "hazardcourse", SV_HazardCourse_f, "starting a Hazard Course" );
		Cmd_AddCommand( "map_background", SV_MapBackground_f, "set background map" );
		Cmd_AddCommand( "load", SV_Load_f, "load a saved game file" );
		Cmd_AddCommand( "loadquick", SV_QuickLoad_f, "load a quick-saved game file" );
		Cmd_AddCommand( "reload", SV_Reload_f, "continue from latest save or restart level" );
		Cmd_AddCommand( "killsave", SV_DeleteSave_f, "delete a saved game file and saveshot" );
		Cmd_AddCommand( "nextmap", SV_NextMap_f, "load next level" );
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
	Cmd_AddCommand( "localinfo", SV_LocalInfo_f, "examine or change the localinfo string" );
	Cmd_AddCommand( "serverinfo", SV_ServerInfo_f, "examine or change the serverinfo string" );
	Cmd_AddCommand( "clientinfo", SV_ClientInfo_f, "print user infostring (player num required)" );
	Cmd_AddCommand( "playersonly", SV_PlayersOnly_f, "freezes time, except for players" );
	Cmd_AddCommand( "restart", SV_Restart_f, "restarting current level" );
	Cmd_AddCommand( "entpatch", SV_EntPatch_f, "write entity patch to allow external editing" );
	Cmd_AddCommand( "edict_usage", SV_EdictUsage_f, "show info about edicts usage" );
	Cmd_AddCommand( "entity_info", SV_EntityInfo_f, "show more info about edicts" );
	Cmd_AddCommand( "shutdownserver", SV_KillServer_f, "shutdown current server" );
	Cmd_AddCommand( "changelevel", SV_ChangeLevel_f, "change level" );
	Cmd_AddCommand( "changelevel2", SV_ChangeLevel2_f, "smooth change level" );

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
	Cmd_RemoveCommand( "localinfo" );
	Cmd_RemoveCommand( "serverinfo" );
	Cmd_RemoveCommand( "clientinfo" );
	Cmd_RemoveCommand( "playersonly" );
	Cmd_RemoveCommand( "restart" );
	Cmd_RemoveCommand( "entpatch" );
	Cmd_RemoveCommand( "edict_usage" );
	Cmd_RemoveCommand( "entity_info" );
	Cmd_RemoveCommand( "shutdownserver" );
	Cmd_RemoveCommand( "changelevel" );
	Cmd_RemoveCommand( "changelevel2" );

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