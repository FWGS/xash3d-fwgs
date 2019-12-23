/*
cl_scrn.c - refresh screen
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
#include "vgui_draw.h"
#include "qfont.h"
#include "input.h"
#include "library.h"

convar_t *scr_centertime;
convar_t *scr_loading;
convar_t *scr_download;
convar_t *scr_viewsize;
convar_t *cl_testlights;
convar_t *cl_allow_levelshots;
convar_t *cl_levelshot_name;
convar_t *cl_envshot_size;
convar_t *v_dark;

typedef struct
{
	int	x1, y1, x2, y2;
} dirty_t;

static dirty_t	scr_dirty, scr_old_dirty[2];
static qboolean	scr_init = false;

/*
==============
SCR_DrawFPS
==============
*/
void SCR_DrawFPS( int height )
{
	float		calc;
	rgba_t		color;
	double		newtime;
	static double	nexttime = 0, lasttime = 0;
	static double	framerate = 0;
	static int	framecount = 0;
	static int	minfps = 9999;
	static int	maxfps = 0;
	char		fpsstring[64];
	int		offset;

	if( cls.state != ca_active || !cl_showfps->value || cl.background )
		return; 

	switch( cls.scrshot_action )
	{
	case scrshot_normal:
	case scrshot_snapshot:
	case scrshot_inactive:
		break;
	default: return;
	}

	newtime = Sys_DoubleTime();
	if( newtime >= nexttime )
	{
		framerate = framecount / (newtime - lasttime);
		lasttime = newtime;
		nexttime = Q_max( nexttime + 1.0, lasttime - 1.0 );
		framecount = 0;
	}

	calc = framerate;
	framecount++;

	if( calc < 1.0f )
	{
		Q_snprintf( fpsstring, sizeof( fpsstring ), "%4i spf", (int)(1.0f / calc + 0.5f));
		MakeRGBA( color, 255, 0, 0, 255 );
	}
	else
	{
		int	curfps = (int)(calc + 0.5f);

		if( curfps < minfps ) minfps = curfps;
		if( curfps > maxfps ) maxfps = curfps;

		if( cl_showfps->value == 2 )
			Q_snprintf( fpsstring, sizeof( fpsstring ), "fps: ^1%4i min, ^3%4i cur, ^2%4i max", minfps, curfps, maxfps );
		else Q_snprintf( fpsstring, sizeof( fpsstring ), "%4i fps", curfps );
		MakeRGBA( color, 255, 255, 255, 255 );
	}

	Con_DrawStringLen( fpsstring, &offset, NULL );
	Con_DrawString( refState.width - offset - 4, height, fpsstring, color );
}

/*
==============
SCR_NetSpeeds

same as r_speeds but for network channel
==============
*/
void SCR_NetSpeeds( void )
{
	static char	msg[MAX_SYSPATH];
	int		x, y, height;
	char		*p, *start, *end;
	float		time = cl.mtime[0];
	static int	min_svfps = 100;
	static int	max_svfps = 0;
	int		cur_svfps = 0;
	static int	min_clfps = 100;
	static int	max_clfps = 0;
	int		cur_clfps = 0;
	rgba_t		color;

	if( !host.allow_console )
		return;

	if( !net_speeds->value || cls.state != ca_active )
		return;

	// prevent to get too big values at max
	if( cl_serverframetime() > 0.0001f )
	{
		cur_svfps = Q_rint( 1.0f / cl_serverframetime( ));
		if( cur_svfps < min_svfps ) min_svfps = cur_svfps;
		if( cur_svfps > max_svfps ) max_svfps = cur_svfps;
	}

	// prevent to get too big values at max
	if( cl_clientframetime() > 0.0001f )
	{
		cur_clfps = Q_rint( 1.0f / cl_clientframetime( ));
		if( cur_clfps < min_clfps ) min_clfps = cur_clfps;
		if( cur_clfps > max_clfps ) max_clfps = cur_clfps;
	}

	Q_snprintf( msg, sizeof( msg ), "sv fps: ^1%4i min, ^3%4i cur, ^2%4i max\ncl fps: ^1%4i min, ^3%4i cur, ^2%4i max\nGame Time: %02d:%02d\nTotal received from server: %s\nTotal sent to server: %s\n",
	min_svfps, cur_svfps, max_svfps, min_clfps, cur_clfps, max_clfps, (int)(time / 60.0f ), (int)fmod( time, 60.0f ), Q_memprint( cls.netchan.total_received ), Q_memprint( cls.netchan.total_sended ));

	x = refState.width - 320;
	y = 384;

	Con_DrawStringLen( NULL, NULL, &height );
	MakeRGBA( color, 255, 255, 255, 255 );

	p = start = msg;

	do
	{
		end = Q_strchr( p, '\n' );
		if( end ) msg[end-start] = '\0';

		Con_DrawString( x, y, p, color );
		y += height;

		if( end ) p = end + 1;
		else break;
	} while( 1 );
}

/*
================
SCR_RSpeeds
================
*/
void SCR_RSpeeds( void )
{
	char	msg[2048];

	if( !host.allow_console )
		return;

	if( ref.dllFuncs.R_SpeedsMessage( msg, sizeof( msg )))
	{
		int	x, y, height;
		char	*p, *start, *end;
		rgba_t	color;

		x = refState.width - 340;
		y = 64;

		Con_DrawStringLen( NULL, NULL, &height );
		MakeRGBA( color, 255, 255, 255, 255 );

		p = start = msg;
		do
		{
			end = Q_strchr( p, '\n' );
			if( end ) msg[end-start] = '\0';

			Con_DrawString( x, y, p, color );
			y += height;

			// handle '\n\n'
			if( *p == '\n' ) 
				y += height;
			if( end ) p = end + 1;
			else break;
		} while( 1 );
	}
}

/*
================
SCR_MakeLevelShot

creates levelshot at next frame
================
*/
void SCR_MakeLevelShot( void )
{
	if( cls.scrshot_request != scrshot_plaque )
		return;

	// make levelshot at nextframe()
	Cbuf_AddText( "levelshot\n" );
}

/*
===============
VID_WriteOverviewScript

Create overview script file
===============
*/
void VID_WriteOverviewScript( void )
{
	ref_overview_t	*ov = &clgame.overView;
	string		filename;
	file_t		*f;

	Q_snprintf( filename, sizeof( filename ), "overviews/%s.txt", clgame.mapname );

	f = FS_Open( filename, "w", false );
	if( !f ) return;

	FS_Printf( f, "// overview description file for %s.bsp\n\n", clgame.mapname );
	FS_Print( f, "global\n{\n" );
	FS_Printf( f, "\tZOOM\t%.2f\n", ov->flZoom );
	FS_Printf( f, "\tORIGIN\t%.2f\t%.2f\t%.2f\n", ov->origin[0], ov->origin[1], ov->origin[2] );
	FS_Printf( f, "\tROTATED\t%i\n", ov->rotated ? 1 : 0 );
	FS_Print( f, "}\n\nlayer\n{\n" );
	FS_Printf( f, "\tIMAGE\t\"overviews/%s.bmp\"\n", clgame.mapname );
	FS_Printf( f, "\tHEIGHT\t%.2f\n", ov->zFar );	// ???
	FS_Print( f, "}\n" );

	FS_Close( f );
}

/*
================
SCR_MakeScreenShot

create a requested screenshot type
================
*/
void SCR_MakeScreenShot( void )
{
	qboolean	iRet = false;
	int	viewsize;

	if( cls.envshot_viewsize > 0 )
		viewsize = cls.envshot_viewsize;
	else viewsize = cl_envshot_size->value;

	switch( cls.scrshot_action )
	{
	case scrshot_normal:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_SCREENSHOT );
		break;
	case scrshot_snapshot:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_SNAPSHOT );
		break;
	case scrshot_plaque:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_LEVELSHOT );
		break;
	case scrshot_savegame:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_MINISHOT );
		break;
	case scrshot_envshot:
		iRet = ref.dllFuncs.VID_CubemapShot( cls.shotname, viewsize, cls.envshot_vieworg, false );
		break;
	case scrshot_skyshot:
		iRet = ref.dllFuncs.VID_CubemapShot( cls.shotname, viewsize, cls.envshot_vieworg, true );
		break;
	case scrshot_mapshot:
		iRet = ref.dllFuncs.VID_ScreenShot( cls.shotname, VID_MAPSHOT );
		if( iRet )
			VID_WriteOverviewScript(); // store overview script too
		break;
	case scrshot_inactive:
		return;
	}

	// report
	if( iRet )
	{
		// snapshots don't writes message about image		
		if( cls.scrshot_action != scrshot_snapshot )
			Con_Reportf( "Write %s\n", cls.shotname );
	}
	else Con_Printf( S_ERROR "Unable to write %s\n", cls.shotname );

	cls.envshot_vieworg = NULL;
	cls.scrshot_action = scrshot_inactive;
	cls.envshot_disable_vis = false;
	cls.envshot_viewsize = 0;
	cls.shotname[0] = '\0';
}

/*
================
SCR_DrawPlaque
================
*/
void SCR_DrawPlaque( void )
{
	if(( cl_allow_levelshots->value && !cls.changelevel ) || cl.background )
	{
		int levelshot = ref.dllFuncs.GL_LoadTexture( cl_levelshot_name->string, NULL, 0, TF_IMAGE );
		ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
		ref.dllFuncs.R_DrawStretchPic( 0, 0, refState.width, refState.height, 0, 0, 1, 1, levelshot );
		if( !cl.background ) CL_DrawHUD( CL_LOADING );
	}
}

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque( qboolean is_background )
{
	float	oldclear;

	S_StopAllSounds( true );
	cl.audio_prepped = false;			// don't play ambients
	cl.video_prepped = false;
	oldclear = gl_clear->value;

	if( CL_IsInMenu( ) && !cls.changedemo && !is_background )
	{
		UI_SetActiveMenu( false );
		if( cls.state == ca_disconnected && !(GameState->curstate == STATE_RUNFRAME && GameState->nextstate != STATE_RUNFRAME) )
			SCR_UpdateScreen();
	}

	if( cls.state == ca_disconnected || cls.disable_screen )
		return; // already set

	if( cls.key_dest == key_console )
		return;

	gl_clear->value = 0.0f;
	if( is_background ) IN_MouseSavePos( );
	cls.draw_changelevel = !is_background;
	SCR_UpdateScreen();
	cls.disable_screen = host.realtime;
	cls.disable_servercount = cl.servercount;
	cl.background = is_background;		// set right state before svc_serverdata is came
	gl_clear->value = oldclear;
//	SNDDMA_LockSound();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque( void )
{
	cls.disable_screen = 0.0f;
	Con_ClearNotify();
//	SNDDMA_UnlockSound();
}

/*
=================
SCR_AddDirtyPoint
=================
*/
void SCR_AddDirtyPoint( int x, int y )
{
	if( x < scr_dirty.x1 ) scr_dirty.x1 = x;
	if( x > scr_dirty.x2 ) scr_dirty.x2 = x;
	if( y < scr_dirty.y1 ) scr_dirty.y1 = y;
	if( y > scr_dirty.y2 ) scr_dirty.y2 = y;
}

/*
================
SCR_DirtyScreen
================
*/
void SCR_DirtyScreen( void )
{
	SCR_AddDirtyPoint( 0, 0 );
	SCR_AddDirtyPoint( refState.width - 1, refState.height - 1 );
}

/*
================
SCR_TileClear
================
*/
void SCR_TileClear( void )
{
	int	i, top, bottom, left, right;
	dirty_t	clear;

	if( scr_viewsize->value >= 120 )
		return; // full screen rendering

	// erase rect will be the union of the past three frames
	// so tripple buffering works properly
	clear = scr_dirty;

	for( i = 0; i < 2; i++ )
	{
		if( scr_old_dirty[i].x1 < clear.x1 )
			clear.x1 = scr_old_dirty[i].x1;
		if( scr_old_dirty[i].x2 > clear.x2 )
			clear.x2 = scr_old_dirty[i].x2;
		if( scr_old_dirty[i].y1 < clear.y1 )
			clear.y1 = scr_old_dirty[i].y1;
		if( scr_old_dirty[i].y2 > clear.y2 )
			clear.y2 = scr_old_dirty[i].y2;
	}

	scr_old_dirty[1] = scr_old_dirty[0];
	scr_old_dirty[0] = scr_dirty;

	scr_dirty.x1 = 9999;
	scr_dirty.x2 = -9999;
	scr_dirty.y1 = 9999;
	scr_dirty.y2 = -9999;

	if( clear.y2 <= clear.y1 )
		return; // nothing disturbed

	top = clgame.viewport[1];
	bottom = top + clgame.viewport[3] - 1;
	left = clgame.viewport[0];
	right = left + clgame.viewport[2] - 1;

	if( clear.y1 < top )
	{	
		// clear above view screen
		i = clear.y2 < top-1 ? clear.y2 : top - 1;
		ref.dllFuncs.R_DrawTileClear( cls.tileImage, clear.x1, clear.y1, clear.x2 - clear.x1 + 1, i - clear.y1 + 1 );
		clear.y1 = top;
	}

	if( clear.y2 > bottom )
	{	
		// clear below view screen
		i = clear.y1 > bottom + 1 ? clear.y1 : bottom + 1;
		ref.dllFuncs.R_DrawTileClear( cls.tileImage, clear.x1, i, clear.x2 - clear.x1 + 1, clear.y2 - i + 1 );
		clear.y2 = bottom;
	}

	if( clear.x1 < left )
	{
		// clear left of view screen
		i = clear.x2 < left - 1 ? clear.x2 : left - 1;
		ref.dllFuncs.R_DrawTileClear( cls.tileImage, clear.x1, clear.y1, i - clear.x1 + 1, clear.y2 - clear.y1 + 1 );
		clear.x1 = left;
	}

	if( clear.x2 > right )
	{	
		// clear left of view screen
		i = clear.x1 > right + 1 ? clear.x1 : right + 1;
		ref.dllFuncs.R_DrawTileClear( cls.tileImage, i, clear.y1, clear.x2 - i + 1, clear.y2 - clear.y1 + 1 );
		clear.x2 = right;
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void )
{
	if( !V_PreRender( )) return;

	switch( cls.state )
	{
	case ca_disconnected:
		Con_RunConsole ();
		break;
	case ca_connecting:
	case ca_connected:
	case ca_validate:
		SCR_DrawPlaque();
		break;
	case ca_active:
		Con_RunConsole ();
		V_RenderView();
		break;
	case ca_cinematic:
		SCR_DrawCinematic();
		break;
	default:
		Host_Error( "SCR_UpdateScreen: bad cls.state\n" );
		break;
	}

	V_PostRender();
}

qboolean SCR_LoadFixedWidthFont( const char *fontname )
{
	int	i, fontWidth;

	if( cls.creditsFont.valid )
		return true; // already loaded

	if( !FS_FileExists( fontname, false ))
		return false;

	cls.creditsFont.hFontTexture = ref.dllFuncs.GL_LoadTexture( fontname, NULL, 0, TF_IMAGE|TF_KEEP_SOURCE );
	R_GetTextureParms( &fontWidth, NULL, cls.creditsFont.hFontTexture );
	cls.creditsFont.charHeight = clgame.scrInfo.iCharHeight = fontWidth / 16;
	cls.creditsFont.type = FONT_FIXED;
	cls.creditsFont.valid = true;

	// build fixed rectangles
	for( i = 0; i < 256; i++ )
	{
		cls.creditsFont.fontRc[i].left = (i * (fontWidth / 16)) % fontWidth;
		cls.creditsFont.fontRc[i].right = cls.creditsFont.fontRc[i].left + fontWidth / 16;
		cls.creditsFont.fontRc[i].top = (i / 16) * (fontWidth / 16);
		cls.creditsFont.fontRc[i].bottom = cls.creditsFont.fontRc[i].top + fontWidth / 16;
		cls.creditsFont.charWidths[i] = clgame.scrInfo.charWidths[i] = fontWidth / 16;
	}

	return true;
}

qboolean SCR_LoadVariableWidthFont( const char *fontname )
{
	int	i, fontWidth;
	byte	*buffer;
	fs_offset_t	length;
	qfont_t	*src;

	if( cls.creditsFont.valid )
		return true; // already loaded

	if( !FS_FileExists( fontname, false ))
		return false;

	cls.creditsFont.hFontTexture = ref.dllFuncs.GL_LoadTexture( fontname, NULL, 0, TF_IMAGE );
	R_GetTextureParms( &fontWidth, NULL, cls.creditsFont.hFontTexture );

	// half-life font with variable chars witdh
	buffer = FS_LoadFile( fontname, &length, false );

	// setup creditsfont	
	if( buffer && length >= sizeof( qfont_t ))
	{
		src = (qfont_t *)buffer;
		cls.creditsFont.charHeight = clgame.scrInfo.iCharHeight = src->rowheight;
		cls.creditsFont.type = FONT_VARIABLE;

		// build rectangles
		for( i = 0; i < 256; i++ )
		{
			cls.creditsFont.fontRc[i].left = (word)src->fontinfo[i].startoffset % fontWidth;
			cls.creditsFont.fontRc[i].right = cls.creditsFont.fontRc[i].left + src->fontinfo[i].charwidth;
			cls.creditsFont.fontRc[i].top = (word)src->fontinfo[i].startoffset / fontWidth;
			cls.creditsFont.fontRc[i].bottom = cls.creditsFont.fontRc[i].top + src->rowheight;
			cls.creditsFont.charWidths[i] = clgame.scrInfo.charWidths[i] = src->fontinfo[i].charwidth;
		}
		cls.creditsFont.valid = true;
	}
	if( buffer ) Mem_Free( buffer );

	return true;
}

/*
================
SCR_LoadCreditsFont

INTERNAL RESOURCE
================
*/
void SCR_LoadCreditsFont( void )
{
	const char *path = "gfx/creditsfont.fnt";
	dword crc;

	// replace default gfx.wad textures by current charset's font
	if( !CRC32_File( &crc, "gfx.wad" ) || crc == 0x49eb9f16 )
	{
		const char *path2 = va("creditsfont_%s.fnt", Cvar_VariableString( "con_charset" ) );
		if( FS_FileExists( path2, false ) )
			path = path2;
	}

	if( !SCR_LoadVariableWidthFont( path ))
	{
		if( !SCR_LoadFixedWidthFont( "gfx/conchars" ))
			Con_DPrintf( S_ERROR "failed to load HUD font\n" );
	}
}

/*
================
SCR_InstallParticlePalette

INTERNAL RESOURCE
================
*/
void SCR_InstallParticlePalette( void )
{
	rgbdata_t	*pic;
	int	i;

	// first check 'palette.lmp' then 'palette.pal'
	pic = FS_LoadImage( DEFAULT_INTERNAL_PALETTE, NULL, 0 );
	if( !pic ) pic = FS_LoadImage( DEFAULT_EXTERNAL_PALETTE, NULL, 0 );

	// NOTE: imagelib required this fakebuffer for loading internal palette
	if( !pic ) pic = FS_LoadImage( "#valve.pal", (byte *)&i, 768 );

	if( pic )
	{
		for( i = 0; i < 256; i++ )
		{
			clgame.palette[i].r = pic->palette[i*4+0];
			clgame.palette[i].g = pic->palette[i*4+1];
			clgame.palette[i].b = pic->palette[i*4+2];
		}
		FS_FreeImage( pic );
	}
	else
	{
		// someone deleted internal palette from code...
		for( i = 0; i < 256; i++ )
		{
			clgame.palette[i].r = i;
			clgame.palette[i].g = i;
			clgame.palette[i].b = i;
		}
	}
}

/*
================
SCR_RegisterTextures

INTERNAL RESOURCE
================
*/
void SCR_RegisterTextures( void )
{
	// register gfx.wad images

	if( FS_FileExists( "gfx/paused.lmp", false ))
		cls.pauseIcon = ref.dllFuncs.GL_LoadTexture( "gfx/paused.lmp", NULL, 0, TF_IMAGE );
	else if( FS_FileExists( "gfx/pause.lmp", false ))
		cls.pauseIcon = ref.dllFuncs.GL_LoadTexture( "gfx/pause.lmp", NULL, 0, TF_IMAGE );

	if( FS_FileExists( "gfx/lambda.lmp", false ))
	{
		if( cl_allow_levelshots->value )
			cls.loadingBar = ref.dllFuncs.GL_LoadTexture( "gfx/lambda.lmp", NULL, 0, TF_IMAGE|TF_LUMINANCE );
		else cls.loadingBar = ref.dllFuncs.GL_LoadTexture( "gfx/lambda.lmp", NULL, 0, TF_IMAGE );
	}
	else if( FS_FileExists( "gfx/loading.lmp", false ))
	{
		if( cl_allow_levelshots->value )
			cls.loadingBar = ref.dllFuncs.GL_LoadTexture( "gfx/loading.lmp", NULL, 0, TF_IMAGE|TF_LUMINANCE );
		else cls.loadingBar = ref.dllFuncs.GL_LoadTexture( "gfx/loading.lmp", NULL, 0, TF_IMAGE );
	}
	
	cls.tileImage = ref.dllFuncs.GL_LoadTexture( "gfx/backtile.lmp", NULL, 0, TF_NOMIPMAP );
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f( void )
{
	Cvar_SetValue( "viewsize", Q_min( scr_viewsize->value + 10, 120 ));
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f( void )
{
	Cvar_SetValue( "viewsize", Q_max( scr_viewsize->value - 10, 30 ));
}

/*
==================
SCR_VidInit
==================
*/
void SCR_VidInit( void )
{
	if( !ref.initialized ) // don't call VidInit too soon
		return;

	memset( &clgame.ds, 0, sizeof( clgame.ds )); // reset a draw state
	memset( &gameui.ds, 0, sizeof( gameui.ds )); // reset a draw state
	memset( &clgame.centerPrint, 0, sizeof( clgame.centerPrint ));

	// update screen sizes for menu
	if( gameui.globals )
	{
		gameui.globals->scrWidth = refState.width;
		gameui.globals->scrHeight = refState.height;
	}

	VGui_Startup( NULL, refState.width, refState.height ); // initialized already, so pass NULL

	CL_ClearSpriteTextures(); // now all hud sprites are invalid
	
	// vid_state has changed
	if( gameui.hInstance ) gameui.dllFuncs.pfnVidInit();
	if( clgame.hInstance ) clgame.dllFuncs.pfnVidInit();

	// restart console size
	Con_VidInit ();
}

/*
==================
SCR_Init
==================
*/
void SCR_Init( void )
{
	if( scr_init ) return;

	scr_centertime = Cvar_Get( "scr_centertime", "2.5", 0, "centerprint hold time" );
	cl_levelshot_name = Cvar_Get( "cl_levelshot_name", "*black", 0, "contains path to current levelshot" );
	cl_allow_levelshots = Cvar_Get( "allow_levelshots", "0", FCVAR_ARCHIVE, "allow engine to use indivdual levelshots instead of 'loading' image" );
	scr_loading = Cvar_Get( "scr_loading", "0", 0, "loading bar progress" );
	scr_download = Cvar_Get( "scr_download", "-1", 0, "downloading bar progress" );
	cl_testlights = Cvar_Get( "cl_testlights", "0", 0, "test dynamic lights" );
	cl_envshot_size = Cvar_Get( "cl_envshot_size", "256", FCVAR_ARCHIVE, "envshot size of cube side" );
	v_dark = Cvar_Get( "v_dark", "0", 0, "starts level from dark screen" );
	scr_viewsize = Cvar_Get( "viewsize", "120", FCVAR_ARCHIVE, "screen size" );
	
	// register our commands
	Cmd_AddCommand( "skyname", CL_SetSky_f, "set new skybox by basename" );
	Cmd_AddCommand( "loadsky", CL_SetSky_f, "set new skybox by basename" );
	Cmd_AddCommand( "viewpos", SCR_Viewpos_f, "prints current player origin" );
	Cmd_AddCommand( "sizeup", SCR_SizeUp_f, "screen size up to 10 points" );
	Cmd_AddCommand( "sizedown", SCR_SizeDown_f, "screen size down to 10 points" );

	if( !UI_LoadProgs( ))
	{
		Con_Printf( S_ERROR "can't initialize gameui DLL: %s\n", COM_GetLibraryError() ); // there is non fatal for us
		host.allow_console = true; // we need console, because menu is missing
	}

	SCR_VidInit();
	SCR_LoadCreditsFont ();
	SCR_RegisterTextures ();
	SCR_InstallParticlePalette ();
	SCR_InitCinematic();
	CL_InitNetgraph();

	if( host.allow_console && Sys_CheckParm( "-toconsole" ))
		Cbuf_AddText( "toggleconsole\n" );
	else UI_SetActiveMenu( true );

	scr_init = true;
}

void SCR_Shutdown( void )
{
	if( !scr_init ) return;

	Cmd_RemoveCommand( "timerefresh" );
	Cmd_RemoveCommand( "skyname" );
	Cmd_RemoveCommand( "viewpos" );
	UI_SetActiveMenu( false );
	UI_UnloadProgs();

	scr_init = false;
}
