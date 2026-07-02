/*
cl_video.c - avi video player
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
#include "client.h"

/*
=================================================================

AVI PLAYING

=================================================================
*/

static movie_state_t	*cin_state;
static int		cin_texture;

/*
==================
SCR_NextMovie

Called when a demo or cinematic finishes
If the "nextmovie" cvar is set, that command will be issued
==================
*/
qboolean SCR_NextMovie( void )
{
	string	str;

	if( cls.movienum == -1 )
	{
		S_StopAllSounds( true );
		SCR_StopCinematic();
		CL_CheckStartupDemos();
		return false; // don't play movies
	}

	if( !cls.movies[cls.movienum][0] || cls.movienum == MAX_MOVIES )
	{
		S_StopAllSounds( true );
		SCR_StopCinematic();
		cls.movienum = -1;
		CL_CheckStartupDemos();
		return false;
	}

	Q_snprintf( str, MAX_STRING, "movie %s full\n", cls.movies[cls.movienum] );

	Cbuf_InsertText( str );
	cls.movienum++;

	return true;
}

static void SCR_CreateStartupVids( void )
{
	file_t	*f;

	f = FS_Open( DEFAULT_VIDEOLIST_PATH, "w", false );
	if( !f )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, DEFAULT_VIDEOLIST_PATH );
		return;
	}

	// make standard video playlist: sierra, valve
	FS_Print( f, "media/sierra.avi\n" );
	FS_Print( f, "media/valve.avi\n" );
	FS_Close( f );
}

#if XASH_GAMECUBE
static int SCR_AddStartupVid( int c, const char *movie )
{
	if( c >= MAX_MOVIES )
		return c;

	Q_strncpy( cls.movies[c], movie, sizeof( cls.movies[0] ));
	return c + 1;
}

static int SCR_AddDefaultStartupVids( void )
{
	int c = 0;

	if( FS_FileExists( "media/sierra.avi", false ))
		c = SCR_AddStartupVid( c, "media/sierra.avi" );
	if( FS_FileExists( "media/valve.avi", false ))
		c = SCR_AddStartupVid( c, "media/valve.avi" );
	if( c > 0 )
		Con_Reportf( "Xash3D GameCube: using read-only startup AVI playlist (%d)\n", c );
	return c;
}

static int SCR_LoadStartupVidList( void )
{
	byte *afile;
	char *pfile;
	string token;
	int c = 0;
	const qboolean have_playlist = FS_FileExists( DEFAULT_VIDEOLIST_PATH, false );

	afile = FS_LoadFile( DEFAULT_VIDEOLIST_PATH, NULL, false );
	if( afile )
	{
		pfile = (char *)afile;

		while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			if( Q_stricmp( COM_FileExtension( token ), "avi" ))
				continue;
			if( !FS_FileExists( token, false ))
				continue;

			c = SCR_AddStartupVid( c, token );
			if( c >= MAX_MOVIES )
				break;
		}

		Mem_Free( afile );
	}

	if( c <= 0 && !have_playlist )
		c = SCR_AddDefaultStartupVids();
	else if( c <= 0 && have_playlist )
		Con_Reportf( "Xash3D GameCube: startup intro playlist explicitly disabled\n" );

	return c;
}
#endif

void SCR_CheckStartupVids( void )
{
	int	c = 0;
	byte *afile;
	char *pfile;
	string	token;

#if 0
	if( host_developer.value )
	{
		// don't run movies where we in developer-mode
		cls.movienum = -1;
		CL_CheckStartupDemos();
		return;
	}
#endif

	if( Sys_CheckParm( "-nointro" ) || cls.demonum != -1 || GameState->nextstate != STATE_RUNFRAME )
	{
		// don't run movies where we in developer-mode
		cls.movienum = -1;
		CL_CheckStartupDemos();
		return;
	}

#if XASH_GAMECUBE
	c = SCR_LoadStartupVidList();
	if( c > 0 )
		goto run_cinematic;
	if( FS_FileExists( DEFAULT_VIDEOLIST_PATH, false ) )
	{
		cls.movienum = -1;
		Con_Reportf( "Xash3D GameCube: activating menu without startup cinematic\n" );
		CL_CheckStartupDemos();
		if( cls.movienum == -1 && cls.demonum == -1 && cls.state == ca_disconnected )
			UI_SetActiveMenu( true );
		return;
	}
#endif

	if( !FS_FileExists( DEFAULT_VIDEOLIST_PATH, false ))
	{
		SCR_CreateStartupVids();
	}

	afile = FS_LoadFile( DEFAULT_VIDEOLIST_PATH, NULL, false );
	if( !afile )
	{
		return; // something bad happens
	}

	pfile = (char *)afile;

	while(( pfile = COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		Q_strncpy( cls.movies[c], token, sizeof( cls.movies[0] ));

		if( ++c > MAX_MOVIES - 1 )
		{
			Con_Printf( S_WARN "too many movies (%d) specified in %s\n", MAX_MOVIES, DEFAULT_VIDEOLIST_PATH );
			break;
		}
	}

	Mem_Free( afile );

run_cinematic:
	// run cinematic
	cls.movienum = 0;
#if XASH_GAMECUBE
	if( c > 0 )
	{
		cls.movienum = 1;
		Con_Reportf( "Xash3D GameCube: startup intro playlist (%d)\n", c );
		if( !SCR_PlayCinematic( cls.movies[0] ))
		{
			cls.movienum = -1;
			CL_CheckStartupDemos();
		}
		return;
	}
#endif
	SCR_NextMovie ();
	Cbuf_Execute();
}

/*
==================
SCR_RunCinematic
==================
*/
void SCR_RunCinematic( void )
{
	if( cls.state != ca_cinematic )
		return;

	if( !AVI_IsActive( cin_state ))
	{
		SCR_NextMovie( );
		return;
	}

	if( UI_IsVisible( ))
	{
		// these can happens when user set +menu_ option to cmdline
		AVI_CloseVideo( cin_state );
		cls.state = ca_disconnected;
		Key_SetKeyDest( key_menu );
		S_StopStreaming();
		cls.movienum = -1;
		cls.signon = 0;
		return;
	}
}

/*
==================
SCR_DrawCinematic

Returns true if a cinematic is active, meaning the view rendering
should be skipped
==================
*/
qboolean SCR_DrawCinematic( void )
{
	if( !ref.initialized )
		return false;

	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.R_DrawStretchPic( 0, 0, refState.width, refState.height, 0, 0, 1, 1, R_GetBuiltinTexture( REF_BLACK_TEXTURE ));

	if( !AVI_Think( cin_state ))
		return SCR_NextMovie();

	return true;
}

/*
==================
SCR_PlayCinematic
==================
*/
qboolean SCR_PlayCinematic( const char *arg )
{
	int x, y, w, h;
	double video_ratio, screen_ratio, scale;
#if !XASH_GAMECUBE
	const char	*fullpath = FS_GetDiskPath( arg, false );
#endif
#if XASH_GAMECUBE
	string movie_path;
	char *option;
#endif

#if !XASH_GAMECUBE
	if( FS_FileExists( arg, false ) && !fullpath )
	{
		Con_Printf( S_ERROR "Couldn't load %s from packfile. Please extract it\n", arg );
		return false;
	}
#endif

#if XASH_GAMECUBE
	Q_strncpy( movie_path, arg, sizeof( movie_path ));
	option = Q_strchr( movie_path, ' ' );
	if( option )
		*option = '\0';
	Con_Reportf( "Xash3D GameCube: intro AVI play %s\n", movie_path );
	AVI_OpenVideo( cin_state, movie_path, true, false );
#else
	AVI_OpenVideo( cin_state, fullpath, true, false );
#endif
	if( !AVI_IsActive( cin_state ) || !AVI_GetVideoInfo( cin_state, &w, &h, NULL ))
	{
		AVI_CloseVideo( cin_state );
		return false;
	}

	video_ratio = (double)w / (double)h;
	screen_ratio = (double)refState.width / (double)refState.height;

	if( video_ratio < screen_ratio )
		scale = (double)refState.height / (double)h;
	else
		scale = (double)refState.width / (double)w;

	w = Q_rint( w * scale );
	h = Q_rint( h * scale );

	if( video_ratio < screen_ratio )
	{
		x = (refState.width - w) / 2.0;
		y = 0;
	}
	else
	{
		x = 0;
		y = (refState.height - h) / 2.0;
	}

	if( AVI_HaveAudioTrack( cin_state ))
	{
		// Reset any prior soundtrack state; GameCube starts audible playback
		// after the first intro frame has been uploaded.
		S_StopAllSounds( true );
	}

	AVI_SetParm( cin_state,
		AVI_RENDER_X, x,
		AVI_RENDER_Y, y,
		AVI_RENDER_W, w,
		AVI_RENDER_H, h,
		AVI_PARM_LAST );

	UI_SetActiveMenu( false );
	cls.state = ca_cinematic;
	Con_FastClose();
	cls.signon = 0;

	return true;
}

/*
==================
SCR_StopCinematic
==================
*/
void SCR_StopCinematic( void )
{
	if( cls.state != ca_cinematic )
		return;

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: stop cinematic movienum=%d state=%d\n", cls.movienum, cls.state );
#endif
	AVI_CloseVideo( cin_state );
	S_StopStreaming();

	cls.state = ca_disconnected;
	cls.signon = 0;

#if XASH_GAMECUBE
	Con_Reportf( "Xash3D GameCube: activating menu after cinematic\n" );
#endif
	UI_SetActiveMenu( true );
}

/*
==================
SCR_InitCinematic
==================
*/
void SCR_InitCinematic( void )
{
	AVI_Initailize ();
	cin_state = AVI_GetState( CIN_MAIN );
#if XASH_GAMECUBE
	/* Keep the cinematic texture at the source intro resolution so the
	 * renderer can scale the full frame instead of baking in a 320x240 copy. */
	cin_texture = ref.dllFuncs.GL_CreateTexture( "*cintexture", 640, 480, NULL, TF_NOMIPMAP|TF_CLAMP );
#else
	cin_texture = ref.dllFuncs.GL_CreateTexture( "*cintexture", 64, 64, NULL, TF_NOMIPMAP|TF_CLAMP );
#endif
}

int SCR_GetCinematicTexture( void )
{
	return cin_texture;
}

/*
==================
SCR_FreeCinematic
==================
*/
void SCR_FreeCinematic( void )
{
	movie_state_t	*cin_state;

	// release videos
	cin_state = AVI_GetState( CIN_LOGO );
	AVI_CloseVideo( cin_state );

	cin_state = AVI_GetState( CIN_MAIN );
	AVI_CloseVideo( cin_state );

	AVI_Shutdown();
}
