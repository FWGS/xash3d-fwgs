/*
cl_cmds.c - client console commnds
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
#include "client.h"

/*
====================
CL_PlayVideo_f

movie <moviename>
====================
*/
void CL_PlayVideo_f( void )
{
	string	path;

	if( Cmd_Argc() != 2 && Cmd_Argc() != 3 )
	{
		Con_Printf( S_USAGE "movie <moviename> [full]\n" );
		return;
	}

	if( cls.state == ca_active )
	{
		Con_Printf( "Can't play movie while connected to a server.\nPlease disconnect first.\n" );
		return;
	}

	switch( Cmd_Argc( ))
	{
	case 2:	// simple user version
		Q_snprintf( path, sizeof( path ), "media/%s.avi", Cmd_Argv( 1 ));
		SCR_PlayCinematic( path );
		break;
	case 3:	// sequenced cinematics used this
		SCR_PlayCinematic( Cmd_Argv( 1 ));
		break;
	}
}

/*
===============
CL_PlayCDTrack_f

Emulate audio-cd system
===============
*/
void CL_PlayCDTrack_f( void )
{
	const char	*command;
	const char	*pszTrack;
	static int	track = 0;
	static qboolean	paused = false;
	static qboolean	looped = false;
	static qboolean	enabled = true;

	if( Cmd_Argc() < 2 ) return;
	command = Cmd_Argv( 1 );
	pszTrack = Cmd_Argv( 2 );

	if( !enabled && Q_stricmp( command, "on" ))
		return; // CD-player is disabled

	if( !Q_stricmp( command, "play" ))
	{
		if( Q_isdigit( pszTrack ))
		{
			track = bound( 1, Q_atoi( Cmd_Argv( 2 )), MAX_CDTRACKS );
			S_StartBackgroundTrack( clgame.cdtracks[track-1], NULL, 0, false );
		}
		else S_StartBackgroundTrack( pszTrack, NULL, 0, true );
		paused = false;
		looped = false;
	}
	else if( !Q_stricmp( command, "loop" ))
	{
		if( Q_isdigit( pszTrack ))
		{
			track = bound( 1, Q_atoi( Cmd_Argv( 2 )), MAX_CDTRACKS );
			S_StartBackgroundTrack( clgame.cdtracks[track-1], clgame.cdtracks[track-1], 0, false );
		}
		else S_StartBackgroundTrack( pszTrack, pszTrack, 0, true );
		paused = false;
		looped = true;
	}
	else if( !Q_stricmp( command, "pause" ))
	{
		S_StreamSetPause( true );
		paused = true;
	}
	else if( !Q_stricmp( command, "resume" ))
	{
		S_StreamSetPause( false );
		paused = false;
	}
	else if( !Q_stricmp( command, "stop" ))
	{
		S_StopBackgroundTrack();
		paused = false;
		looped = false;
		track = 0;
	}
	else if( !Q_stricmp( command, "on" ))
	{
		enabled = true;
	}
	else if( !Q_stricmp( command, "off" ))
	{
		enabled = false;
	}
	else if( !Q_stricmp( command, "info" ))
	{
		int	i, maxTrack;

		for( maxTrack = i = 0; i < MAX_CDTRACKS; i++ )
			if( Q_strlen( clgame.cdtracks[i] )) maxTrack++;
			
		Con_Printf( "%u tracks\n", maxTrack );
		if( track )
		{
			if( paused ) Con_Printf( "Paused %s track %u\n", looped ? "looping" : "playing", track );
			else Con_Printf( "Currently %s track %u\n", looped ? "looping" : "playing", track );
		}
		Con_Printf( "Volume is %f\n", Cvar_VariableValue( "MP3Volume" ));
		return;
	}
	else Con_Printf( "%s: unknown command %s\n", Cmd_Argv( 0 ), command );
}

/* 
================== 
CL_ScreenshotGetName
================== 
*/  
qboolean CL_ScreenshotGetName( int lastnum, char *filename )
{
	if( lastnum < 0 || lastnum > 9999 )
	{
		Con_Printf( S_ERROR "unable to write screenshot\n" );
		return false;
	}

	Q_sprintf( filename, "scrshots/%s_shot%04d.png", clgame.mapname, lastnum );

	return true;
}

/* 
================== 
CL_SnapshotGetName
================== 
*/  
qboolean CL_SnapshotGetName( int lastnum, char *filename )
{
	if( lastnum < 0 || lastnum > 9999 )
	{
		Con_Printf( S_ERROR "unable to write snapshot\n" );
		FS_AllowDirectPaths( false );
		return false;
	}

	Q_sprintf( filename, "../%s_%04d.png", clgame.mapname, lastnum );

	return true;
}

/* 
============================================================================== 
 
			SCREEN SHOTS 
 
============================================================================== 
*/
/* 
================== 
CL_ScreenShot_f

normal screenshot
================== 
*/
void CL_ScreenShot_f( void ) 
{
	int	i;
	string	checkname;

	if( CL_IsDevOverviewMode() == 1 )
	{
		// special case for write overview image and script file
		Q_snprintf( cls.shotname, sizeof( cls.shotname ), "overviews/%s.bmp", clgame.mapname );
		cls.scrshot_action = scrshot_mapshot; // build new frame for mapshot
	}
	else
	{
		// scan for a free filename
		for( i = 0; i < 9999; i++ )
		{
			if( !CL_ScreenshotGetName( i, checkname ))
				return;	// no namespace

			if( !FS_FileExists( checkname, false ))
				break;
		}

		Q_strncpy( cls.shotname, checkname, sizeof( cls.shotname ));
		cls.scrshot_action = scrshot_normal; // build new frame for screenshot
	}

	cls.envshot_vieworg = NULL; // no custom view
	cls.envshot_viewsize = 0;
}

/* 
================== 
CL_SnapShot_f

save screenshots into root dir
================== 
*/
void CL_SnapShot_f( void ) 
{
	int	i;
	string	checkname;

	if( CL_IsDevOverviewMode() == 1 )
	{
		// special case for write overview image and script file
		Q_snprintf( cls.shotname, sizeof( cls.shotname ), "overviews/%s.bmp", clgame.mapname );
		cls.scrshot_action = scrshot_mapshot; // build new frame for mapshot
	}
	else
	{
		FS_AllowDirectPaths( true );

		// scan for a free filename
		for( i = 0; i < 9999; i++ )
		{
			if( !CL_SnapshotGetName( i, checkname ))
				return;	// no namespace

			if( !FS_FileExists( checkname, false ))
				break;
		}

		FS_AllowDirectPaths( false );
		Q_strncpy( cls.shotname, checkname, sizeof( cls.shotname ));
		cls.scrshot_action = scrshot_snapshot; // build new frame for screenshot
	}

	cls.envshot_vieworg = NULL; // no custom view
	cls.envshot_viewsize = 0;
}

/* 
================== 
CL_EnvShot_f

cubemap view
================== 
*/
void CL_EnvShot_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "envshot <shotname>\n" );
		return;
	}

	Q_sprintf( cls.shotname, "gfx/env/%s", Cmd_Argv( 1 ));
	cls.scrshot_action = scrshot_envshot;	// build new frame for envshot
	cls.envshot_vieworg = NULL; // no custom view
	cls.envshot_viewsize = 0;
}

/* 
================== 
CL_SkyShot_f

skybox view
================== 
*/
void CL_SkyShot_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "skyshot <shotname>\n" );
		return;
	}

	Q_sprintf( cls.shotname, "gfx/env/%s", Cmd_Argv( 1 ));
	cls.scrshot_action = scrshot_skyshot;	// build new frame for skyshot
	cls.envshot_vieworg = NULL; // no custom view
	cls.envshot_viewsize = 0;
}

/* 
================== 
CL_LevelShot_f

splash logo while map is loading
================== 
*/ 
void CL_LevelShot_f( void )
{
	size_t	ft1, ft2;
	string	filename;

	if( cls.scrshot_request != scrshot_plaque ) return;
	cls.scrshot_request = scrshot_inactive;

	// check for exist
	if( cls.demoplayback && ( cls.demonum != -1 ))
	{
		Q_sprintf( cls.shotname, "levelshots/%s_%s.bmp", cls.demoname, refState.wideScreen ? "16x9" : "4x3" );
		Q_snprintf( filename, sizeof( filename ), "%s.dem", cls.demoname );

		// make sure what levelshot is newer than demo
		ft1 = FS_FileTime( filename, false );
		ft2 = FS_FileTime( cls.shotname, true );
	}
	else
	{
		Q_sprintf( cls.shotname, "levelshots/%s_%s.bmp", clgame.mapname, refState.wideScreen ? "16x9" : "4x3" );

		// make sure what levelshot is newer than bsp
		ft1 = FS_FileTime( cl.worldmodel->name, false );
		ft2 = FS_FileTime( cls.shotname, true );
	}

	// missing levelshot or level never than levelshot
	if( ft2 == -1 || ft1 > ft2 )
		cls.scrshot_action = scrshot_plaque;	// build new frame for levelshot
	else cls.scrshot_action = scrshot_inactive;	// disable - not needs
}

/* 
================== 
CL_SaveShot_f

mini-pic in loadgame menu
================== 
*/ 
void CL_SaveShot_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "saveshot <savename>\n" );
		return;
	}

	Q_sprintf( cls.shotname, "%s%s.bmp", DEFAULT_SAVE_DIRECTORY, Cmd_Argv( 1 ));
	cls.scrshot_action = scrshot_savegame;	// build new frame for saveshot
}

/*
==============
CL_DeleteDemo_f

==============
*/
void CL_DeleteDemo_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "killdemo <name>\n" );
		return;
	}

	if( cls.demorecording && !Q_stricmp( cls.demoname, Cmd_Argv( 1 )))
	{
		Con_Printf( "Can't delete %s - recording\n", Cmd_Argv( 1 ));
		return;
	}

	// delete demo
	FS_Delete( va( "%s.dem", Cmd_Argv( 1 )));
}

/*
=================
CL_SetSky_f

Set a specified skybox (only for local clients)
=================
*/
void CL_SetSky_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "skyname <skybox>\n" );
		return;
	}

	ref.dllFuncs.R_SetupSky( Cmd_Argv( 1 ));
}

/*
=============
SCR_Viewpos_f

viewpos (level-designer helper)
=============
*/
void SCR_Viewpos_f( void )
{
	Con_Printf( "org ( %g %g %g )\n", refState.vieworg[0], refState.vieworg[1], refState.vieworg[2] );
	Con_Printf( "ang ( %g %g %g )\n", refState.viewangles[0], refState.viewangles[1], refState.viewangles[2] );
}
