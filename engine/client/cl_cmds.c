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
		Q_snprintf( path, sizeof( path ), "media/%s", Cmd_Argv( 1 ));
		COM_DefaultExtension( path, ".avi", sizeof( path ));
		SCR_PlayCinematic( path );
		break;
	case 3:	// sequenced cinematics used this
		SCR_PlayCinematic( Cmd_Argv( 1 ));
		break;
	}
}

/*
==============================================================================

			CD AUDIO EMULATION

==============================================================================
*/
static struct
{
	int      track;   // current track number, zero if not playing or playing a file
	qboolean paused;
	qboolean looped;
	qboolean enabled;
} cdaudio = { .enabled = true };

static void CL_CDAudio_PlayTrack( int track, qboolean loop )
{
	track = bound( 1, track, MAX_CDTRACKS );

	if( loop )
	{
		// a1ba: some maps contain cd track set in worldspawn
		// forcibly stopping music on changelevel
		//
		// server notifies us about this in sv_client.c SV_PutClientInServer
		// through "cd loop" command
		//
		// however, it could be set to a track that doesn't contain music data (in CD terms)
		// so we catch it here and music will carry over changelevels
		if( !COM_StringEmpty( clgame.cdtracks[track-1] ))
			S_StartBackgroundTrack( clgame.cdtracks[track-1], clgame.cdtracks[track-1], 0, false );
	}
	else S_StartBackgroundTrack( clgame.cdtracks[track-1], NULL, 0, false );

	cdaudio.track = track;
	cdaudio.paused = false;
	cdaudio.looped = loop;
}

static void CL_CDAudio_PlayFile( const char *name, qboolean loop )
{
	S_StartBackgroundTrack( name, loop ? name : NULL, 0, true );

	cdaudio.track = 0;
	cdaudio.paused = false;
	cdaudio.looped = loop;
}

// original Xash3D accepts file names in place of track numbers
static void CL_CDAudio_Play( const char *pszTrack, qboolean loop )
{
	if( Q_isdigit( pszTrack ))
		CL_CDAudio_PlayTrack( Q_atoi( pszTrack ), loop );
	else CL_CDAudio_PlayFile( pszTrack, loop );
}

static void CL_CDAudio_Stop( void )
{
	S_StopBackgroundTrack();

	cdaudio.track = 0;
	cdaudio.paused = false;
	cdaudio.looped = false;
}

/*
===============
CL_CD_f

Emulate audio-cd system
===============
*/
void CL_CD_f( void )
{
	if( Cmd_Argc() < 2 ) return;
	const char *command = Cmd_Argv( 1 );
	const char *pszTrack = Cmd_Argv( 2 );

	if( !Q_stricmp( command, "on" ))
	{
		cdaudio.enabled = true;
		return;
	}

	if( !Q_stricmp( command, "reset" ))
	{
		cdaudio.enabled = true;
		CL_CDAudio_Stop();
		return;
	}

	if( !cdaudio.enabled )
		return; // CD-player is disabled

	if( !Q_stricmp( command, "off" ))
	{
		CL_CDAudio_Stop();
		cdaudio.enabled = false;
	}
	else if( !Q_stricmp( command, "remap" ) || !Q_stricmp( command, "close" ))
	{
		// no physical CD drive to remap tracks or close the tray
	}
	else if( !Q_stricmp( command, "eject" ))
	{
		CL_CDAudio_Stop();
	}
	else if( !Q_stricmp( command, "play" ))
	{
		CL_CDAudio_Play( pszTrack, false );
	}
	else if( !Q_stricmp( command, "loop" ))
	{
		CL_CDAudio_Play( pszTrack, true );
	}
	else if( !Q_stricmp( command, "playfile" ))
	{
		CL_CDAudio_PlayFile( pszTrack, false );
	}
	else if( !Q_stricmp( command, "loopfile" ))
	{
		CL_CDAudio_PlayFile( pszTrack, true );
	}
	else if( !Q_stricmp( command, "mp3track" ))
	{
		// zero-based track number, unlike "play" and "loop"
		if( Cmd_Argc() > 2 )
			CL_CDAudio_PlayTrack( Q_atoi( pszTrack ) + 1, false );
	}
	else if( !Q_stricmp( command, "stop" ))
	{
		CL_CDAudio_Stop();
	}
	else if( !Q_stricmp( command, "fadeout" ))
	{
		S_FadeOutBackgroundTrack( s_musicfadetime.value );
	}
	else if( !Q_stricmp( command, "pause" ))
	{
		S_StreamSetPause( true );
		cdaudio.paused = true;
	}
	else if( !Q_stricmp( command, "resume" ))
	{
		S_StreamSetPause( false );
		cdaudio.paused = false;
	}
	else if( !Q_stricmp( command, "mp3info" ))
	{
		S_PrintBackgroundTrackState();
		Con_Printf( "Current MP3 Track: %i\n", cdaudio.track );
		Con_Printf( "Current MP3 Volume: %i\n", (int)( s_musicvolume.value * 127 ));
	}
	else if( !Q_stricmp( command, "info" ))
	{
		int	maxTrack = 0;

		for( int i = 0; i < MAX_CDTRACKS; i++ )
		{
			if( !COM_StringEmpty( clgame.cdtracks[i] ) )
				maxTrack++;
		}

		Con_Printf( "%u tracks\n", maxTrack );
		if( cdaudio.track )
		{
			if( cdaudio.paused ) Con_Printf( "Paused %s track %u\n", cdaudio.looped ? "looping" : "playing", cdaudio.track );
			else Con_Printf( "Currently %s track %u\n", cdaudio.looped ? "looping" : "playing", cdaudio.track );
		}
		Con_Printf( "Volume is %f\n", s_musicvolume.value );
	}
	else Con_Printf( "%s: unknown command %s\n", Cmd_Argv( 0 ), command );
}

/*
===============
CL_MP3_f

Play music files, shares the state with the audio-cd emulation
===============
*/
void CL_MP3_f( void )
{
	if( Cmd_Argc() < 2 ) return;
	const char *command = Cmd_Argv( 1 );
	const char *pszTrack = Cmd_Argv( 2 );

	if( !cdaudio.enabled )
		return; // CD-player is disabled

	if( !Q_stricmp( command, "play" ))
	{
		CL_CDAudio_Play( pszTrack, false );
	}
	else if( !Q_stricmp( command, "loop" ))
	{
		CL_CDAudio_Play( pszTrack, true );
	}
	else if( !Q_stricmp( command, "playfile" ))
	{
		CL_CDAudio_PlayFile( pszTrack, false );
	}
	else if( !Q_stricmp( command, "loopfile" ))
	{
		CL_CDAudio_PlayFile( pszTrack, true );
	}
	else if( !Q_stricmp( command, "stop" ))
	{
		CL_CDAudio_Stop();
	}
	else Con_Printf( "%s: unknown command %s\n", Cmd_Argv( 0 ), command );
}

/*
==============================================================================

			SCREEN SHOTS

==============================================================================
*/
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
		Q_snprintf( cls.shotname, sizeof( cls.shotname ),
			"levelshots/%s_%s.bmp", cls.demoname, refState.wideScreen ? "16x9" : "4x3" );
		Q_snprintf( filename, sizeof( filename ), "%s.dem", cls.demoname );

		// make sure what levelshot is newer than demo
		ft1 = FS_FileTime( filename, false );
		ft2 = FS_FileTime( cls.shotname, true );
	}
	else
	{
		Q_snprintf( cls.shotname, sizeof( cls.shotname ),
			"levelshots/%s_%s.bmp", clgame.mapname, refState.wideScreen ? "16x9" : "4x3" );

		// make sure what levelshot is newer than bsp
		ft1 = FS_FileTime( cl.worldmodel->name, false );
		ft2 = FS_FileTime( cls.shotname, true );
	}

	// missing levelshot or level never than levelshot
	if( ft2 == -1 || ft1 > ft2 )
		cls.scrshot_action = scrshot_plaque;	// build new frame for levelshot
	else cls.scrshot_action = scrshot_inactive;	// disable - not needs
}

static scrshot_t CL_GetScreenshotTypeFromString( const char *string )
{
	if( !Q_stricmp( string, "snapshot" ))
		return scrshot_snapshot;

	if( !Q_stricmp( string, "screenshot" ))
		return scrshot_normal;

	if( !Q_stricmp( string, "saveshot" ))
		return scrshot_savegame;

	if( !Q_stricmp( string, "envshot" ))
		return scrshot_envshot;

	if( !Q_stricmp( string, "skyshot" ))
		return scrshot_skyshot;

	return scrshot_inactive;
}

void CL_GenericShot_f( void )
{
	const char *argv0 = Cmd_Argv( 0 );
	scrshot_t type = CL_GetScreenshotTypeFromString( argv0 );

	if( type == scrshot_normal || type == scrshot_snapshot )
	{
		if( CL_IsDevOverviewMode() == 1 )
			type = scrshot_mapshot;
	}
	else
	{
		if( Cmd_Argc() < 2 )
		{
			Con_Printf( S_USAGE "%s <shotname>\n", argv0 );
			return;
		}
	}

	switch( type )
	{
	case scrshot_envshot:
	case scrshot_skyshot:
		Q_snprintf( cls.shotname, sizeof( cls.shotname ), "gfx/env/%s", Cmd_Argv( 1 ));
		break;
	case scrshot_savegame:
		Q_snprintf( cls.shotname, sizeof( cls.shotname ), DEFAULT_SAVE_DIRECTORY "%s.bmp", Cmd_Argv( 1 ));
		break;
	case scrshot_mapshot:
		Q_snprintf( cls.shotname, sizeof( cls.shotname ), "overviews/%s.bmp", clgame.mapname );
		break;
	case scrshot_normal:
	case scrshot_snapshot:
	{
		string checkname;

		// allow overriding screenshot by users request
		if( Cmd_Argc() > 1 )
		{
			Q_strncpy( cls.shotname, Cmd_Argv( 1 ), sizeof( cls.shotname ));
			break;
		}

		if( type == scrshot_snapshot )
			FS_AllowDirectPaths( true );

		for( int i = 0; i < 9999; i++ )
		{
			int ret;

			if( type == scrshot_snapshot )
				ret = Q_snprintf( checkname, sizeof( checkname ), "../%s_%04d.png", clgame.mapname, i );
			else
				ret = Q_snprintf( checkname, sizeof( checkname ), "scrshots/%s_shot%04d.png", clgame.mapname, i );

			if( ret <= 0 )
			{
				Con_Printf( S_ERROR "unable to write %s\n", argv0 );
				FS_AllowDirectPaths( false );
				return;
			}

			if( !FS_FileExists( checkname, true ))
				break;
		}

		FS_AllowDirectPaths( false );

		Q_strncpy( cls.shotname, checkname, sizeof( cls.shotname ));
		break;
	}
	case scrshot_inactive:
	case scrshot_plaque:
	default:
		return; // shouldn't happen
	}

	cls.scrshot_action = type; // build new frame for saveshot
	cls.envshot_vieworg = NULL;
	cls.envshot_viewsize = 0;
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

	R_SetupSky( Cmd_Argv( 1 ));
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

/*
=============
CL_WavePlayLen_f

=============
*/
void CL_WavePlayLen_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( "waveplaylen <wave file name>: returns approximate number of milliseconds a wave file will take to play.\n" );
		return;
	}

	const char *name = Cmd_Argv( 1 );
	uint msecs = Sound_GetApproxWavePlayLen( name );

	if( msecs == 0 )
	{
		Con_Printf( "Unable to read %s, file may be missing or incorrectly formatted.\n", name );
		return;
	}

	Con_Printf( "Play time is approximately %dms\n", msecs );
}
