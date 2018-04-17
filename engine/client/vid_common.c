/*
vid_common.c - common vid component
Copyright (C) 2018 a1batross, Uncle Mike

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
#include "gl_local.h"
#include "mod_local.h"
#include "input.h"
#include "vid_common.h"

#define WINDOW_NAME			"Xash3D Window" // Half-Life

convar_t	*gl_extensions;
convar_t	*gl_texture_anisotropy;
convar_t	*gl_texture_lodbias;
convar_t	*gl_texture_nearest;
convar_t	*gl_lightmap_nearest;
convar_t	*gl_keeptjunctions;
convar_t	*gl_showtextures;
convar_t	*gl_detailscale;
convar_t	*gl_check_errors;
convar_t	*gl_polyoffset;
convar_t	*gl_wireframe;
convar_t	*gl_finish;
convar_t	*gl_nosort;
convar_t	*gl_vsync;
convar_t	*gl_clear;
convar_t	*gl_test;
convar_t	*gl_msaa;
convar_t	*gl_stencilbits;

convar_t	*scr_width;
convar_t	*scr_height;
convar_t	*window_xpos;
convar_t	*window_ypos;
convar_t	*r_speeds;
convar_t	*r_fullbright;
convar_t	*r_norefresh;
convar_t	*r_lighting_extended;
convar_t	*r_lighting_modulate;
convar_t	*r_lighting_ambient;
convar_t	*r_detailtextures;
convar_t	*r_drawentities;
convar_t	*r_adjust_fov;
convar_t	*r_decals;
convar_t	*r_novis;
convar_t	*r_nocull;
convar_t	*r_lockpvs;
convar_t	*r_lockfrustum;
convar_t	*r_traceglow;
convar_t	*r_dynamic;
convar_t	*r_lightmap;

convar_t	*vid_displayfrequency;
convar_t	*vid_fullscreen;
convar_t	*vid_brightness;
convar_t	*vid_gamma;
convar_t	*vid_mode;
convar_t	*vid_highdpi;

byte		*r_temppool;

ref_globals_t	tr;
glconfig_t	glConfig;
glstate_t		glState;
glwstate_t	glw_state;

/*
=================
GL_SetExtension
=================
*/
void GL_SetExtension( int r_ext, int enable )
{
	if( r_ext >= 0 && r_ext < GL_EXTCOUNT )
		glConfig.extension[r_ext] = enable ? GL_TRUE : GL_FALSE;
	else Con_Printf( S_ERROR "GL_SetExtension: invalid extension %d\n", r_ext );
}

/*
=================
GL_Support
=================
*/
qboolean GL_Support( int r_ext )
{
	if( r_ext >= 0 && r_ext < GL_EXTCOUNT )
		return glConfig.extension[r_ext] ? true : false;
	Con_Printf( S_ERROR "GL_Support: invalid extension %d\n", r_ext );

	return false;
}

/*
=================
GL_MaxTextureUnits
=================
*/
int GL_MaxTextureUnits( void )
{
	if( GL_Support( GL_SHADER_GLSL100_EXT ))
		return Q_min( Q_max( glConfig.max_texture_coords, glConfig.max_teximage_units ), MAX_TEXTURE_UNITS );
	return glConfig.max_texture_units;
}

/*
=================
GL_CheckExtension
=================
*/
void GL_CheckExtension( const char *name, const dllfunc_t *funcs, const char *cvarname, int r_ext )
{
	const dllfunc_t	*func;
	convar_t		*parm = NULL;
	const char	*extensions_string;

	MsgDev( D_NOTE, "GL_CheckExtension: %s ", name );
	GL_SetExtension( r_ext, true );

	if( cvarname )
	{
		// system config disable extensions
		parm = Cvar_Get( cvarname, "1", FCVAR_GLCONFIG, va( CVAR_GLCONFIG_DESCRIPTION, name ));
	}

	if(( parm && !CVAR_TO_BOOL( parm )) || ( !CVAR_TO_BOOL( gl_extensions ) && r_ext != GL_OPENGL_110 ))
	{
		MsgDev( D_NOTE, "- disabled\n" );
		GL_SetExtension( r_ext, false );
		return; // nothing to process at
	}

	extensions_string = glConfig.extensions_string;

	if(( name[2] == '_' || name[3] == '_' ) && !Q_strstr( extensions_string, name ))
	{
		GL_SetExtension( r_ext, false );	// update render info
		MsgDev( D_NOTE, "- ^1failed\n" );
		return;
	}

	// clear exports
	for( func = funcs; func && func->name; func++ )
		*func->func = NULL;

	for( func = funcs; func && func->name != NULL; func++ )
	{
		// functions are cleared before all the extensions are evaluated
		if((*func->func = (void *)GL_GetProcAddress( func->name )) == NULL )
			GL_SetExtension( r_ext, false ); // one or more functions are invalid, extension will be disabled
	}

	if( GL_Support( r_ext ))
		MsgDev( D_NOTE, "- ^2enabled\n" );
	else MsgDev( D_NOTE, "- ^1failed\n" );
}

/*
===============
GL_SetDefaultTexState
===============
*/
static void GL_SetDefaultTexState( void )
{
	int	i;

	memset( glState.currentTextures, -1, MAX_TEXTURE_UNITS * sizeof( *glState.currentTextures ));
	memset( glState.texCoordArrayMode, 0, MAX_TEXTURE_UNITS * sizeof( *glState.texCoordArrayMode ));
	memset( glState.genSTEnabled, 0, MAX_TEXTURE_UNITS * sizeof( *glState.genSTEnabled ));

	for( i = 0; i < MAX_TEXTURE_UNITS; i++ )
	{
		glState.currentTextureTargets[i] = GL_NONE;
		glState.texIdentityMatrix[i] = true;
	}
}

/*
===============
GL_SetDefaultState
===============
*/
static void GL_SetDefaultState( void )
{
	memset( &glState, 0, sizeof( glState ));
	GL_SetDefaultTexState ();

	// init draw stack
	tr.draw_list = &tr.draw_stack[0];
	tr.draw_stack_pos = 0;
}

/*
=================
VID_StartupGamma
=================
*/
void VID_StartupGamma( void )
{
	BuildGammaTable( vid_gamma->value, vid_brightness->value );
	MsgDev( D_NOTE, "VID_StartupGamma: gamma %g brightness %g\n", vid_gamma->value, vid_brightness->value );
	ClearBits( vid_brightness->flags, FCVAR_CHANGED );
	ClearBits( vid_gamma->flags, FCVAR_CHANGED );
}

/*
=================
VID_InitDefaultResolution
=================
*/
void VID_InitDefaultResolution( void )
{
	// we need to have something valid here
	// until video subsystem initialized
	glState.width = 640;
	glState.height = 480;
}

/*
=================
R_SaveVideoMode
=================
*/
void R_SaveVideoMode( int w, int h )
{
	glState.width = w;
	glState.height = h;

	Cvar_FullSet( "width", va( "%i", w ), FCVAR_READ_ONLY | FCVAR_RENDERINFO );
	Cvar_FullSet( "height", va( "%i", h ), FCVAR_READ_ONLY | FCVAR_RENDERINFO );

	if( vid_mode->value >= 0 && vid_mode->value <= R_MaxVideoModes() )
		glState.wideScreen = R_GetVideoMode( vid_mode->value ).wideScreen;

	MsgDev( D_NOTE, "Set: [%dx%d]\n", w, h );
}

/*
=================
R_DescribeVIDMode
=================
*/
qboolean R_DescribeVIDMode( int width, int height )
{
	int	i;

	for( i = 0; i < R_MaxVideoModes(); i++ )
	{
		vidmode_t vidmode = R_GetVideoMode( i );
		if( vidmode.width == width && vidmode.height == height )
		{
			// found specified mode
			Cvar_SetValue( "vid_mode", i );
			return true;
		}
	}

	return false;
}

/*
=================
VID_GetModeString
=================
*/
const char *VID_GetModeString( int vid_mode )
{
	if( vid_mode < 0 || vid_mode > R_MaxVideoModes() )
		return NULL;

	return R_GetVideoMode( vid_mode ).desc;
}

/*
==================
VID_CheckChanges

check vid modes and fullscreen
==================
*/
void VID_CheckChanges( void )
{
	if( FBitSet( cl_allow_levelshots->flags, FCVAR_CHANGED ))
	{
		GL_FreeTexture( cls.loadingBar );
		SCR_RegisterTextures(); // reload 'lambda' image
		ClearBits( cl_allow_levelshots->flags, FCVAR_CHANGED );
	}

	if( host.renderinfo_changed )
	{
		if( !VID_SetMode( ))
		{
			Sys_Error( "Can't re-initialize video subsystem\n" );
		}
		else
		{
			host.renderinfo_changed = false;
			SCR_VidInit(); // tell the client.dll what vid_mode has changed
		}
	}
}

/*
===============
GL_SetDefaults
===============
*/
static void GL_SetDefaults( void )
{
	pglFinish();

	pglClearColor( 0.5f, 0.5f, 0.5f, 1.0f );

	pglDisable( GL_DEPTH_TEST );
	pglDisable( GL_CULL_FACE );
	pglDisable( GL_SCISSOR_TEST );
	pglDepthFunc( GL_LEQUAL );
	pglColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	if( glState.stencilEnabled )
	{
		pglDisable( GL_STENCIL_TEST );
		pglStencilMask( ( GLuint ) ~0 );
		pglStencilFunc( GL_EQUAL, 0, ~0 );
		pglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
	}

	pglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	pglPolygonOffset( -1.0f, -2.0f );

	GL_CleanupAllTextureUnits();

	pglDisable( GL_BLEND );
	pglDisable( GL_ALPHA_TEST );
	pglDisable( GL_POLYGON_OFFSET_FILL );
	pglAlphaFunc( GL_GREATER, 0.0f );
	pglEnable( GL_TEXTURE_2D );
	pglShadeModel( GL_SMOOTH );
	pglFrontFace( GL_CCW );

	pglPointSize( 1.2f );
	pglLineWidth( 1.2f );

	GL_Cull( GL_NONE );
}

/*
=================
R_RenderInfo_f
=================
*/
void R_RenderInfo_f( void )
{
	Con_Printf( "\n" );
	Con_Printf( "GL_VENDOR: %s\n", glConfig.vendor_string );
	Con_Printf( "GL_RENDERER: %s\n", glConfig.renderer_string );
	Con_Printf( "GL_VERSION: %s\n", glConfig.version_string );

	// don't spam about extensions
	if( host_developer.value >= DEV_EXTENDED )
	{
		Con_Printf( "GL_EXTENSIONS: %s\n", glConfig.extensions_string );
	}

	Con_Printf( "GL_MAX_TEXTURE_SIZE: %i\n", glConfig.max_2d_texture_size );

	if( GL_Support( GL_ARB_MULTITEXTURE ))
		Con_Printf( "GL_MAX_TEXTURE_UNITS_ARB: %i\n", glConfig.max_texture_units );
	if( GL_Support( GL_TEXTURE_CUBEMAP_EXT ))
		Con_Printf( "GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB: %i\n", glConfig.max_cubemap_size );
	if( GL_Support( GL_ANISOTROPY_EXT ))
		Con_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT: %.1f\n", glConfig.max_texture_anisotropy );
	if( GL_Support( GL_TEXTURE_2D_RECT_EXT ))
		Con_Printf( "GL_MAX_RECTANGLE_TEXTURE_SIZE: %i\n", glConfig.max_2d_rectangle_size );
	if( GL_Support( GL_TEXTURE_ARRAY_EXT ))
		Con_Printf( "GL_MAX_ARRAY_TEXTURE_LAYERS_EXT: %i\n", glConfig.max_2d_texture_layers );
	if( GL_Support( GL_SHADER_GLSL100_EXT ))
	{
		Con_Printf( "GL_MAX_TEXTURE_COORDS_ARB: %i\n", glConfig.max_texture_coords );
		Con_Printf( "GL_MAX_TEXTURE_IMAGE_UNITS_ARB: %i\n", glConfig.max_teximage_units );
		Con_Printf( "GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB: %i\n", glConfig.max_vertex_uniforms );
		Con_Printf( "GL_MAX_VERTEX_ATTRIBS_ARB: %i\n", glConfig.max_vertex_attribs );
	}

	Con_Printf( "\n" );
	Con_Printf( "MODE: %s\n", R_GetVideoMode(vid_mode->value).desc );
	Con_Printf( "\n" );
	Con_Printf( "VERTICAL SYNC: %s\n", gl_vsync->value ? "enabled" : "disabled" );
	Con_Printf( "Color %d bits, Alpha %d bits, Depth %d bits, Stencil %d bits\n", glConfig.color_bits,
		glConfig.alpha_bits, glConfig.depth_bits, glConfig.stencil_bits );
}

//=======================================================================

/*
=================
GL_InitCommands
=================
*/
void GL_InitCommands( void )
{
	// system screen width and height (don't suppose for change from console at all)
	scr_width = Cvar_Get( "width", "640", FCVAR_RENDERINFO, "screen width" );
	scr_height = Cvar_Get( "height", "480", FCVAR_RENDERINFO, "screen height" );
	r_speeds = Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	r_fullbright = Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	r_norefresh = Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	r_lighting_extended = Cvar_Get( "r_lighting_extended", "1", FCVAR_ARCHIVE, "allow to get lighting from world and bmodels" );
	r_lighting_modulate = Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	r_lighting_ambient = Cvar_Get( "r_lighting_ambient", "0.3", FCVAR_ARCHIVE, "map ambient lighting scale" );
	r_adjust_fov = Cvar_Get( "r_adjust_fov", "1", FCVAR_ARCHIVE, "making FOV adjustment for wide-screens" );
	r_novis = Cvar_Get( "r_novis", "0", 0, "ignore vis information (perfomance test)" );
	r_nocull = Cvar_Get( "r_nocull", "0", 0, "ignore frustrum culling (perfomance test)" );
	r_detailtextures = Cvar_Get( "r_detailtextures", "1", FCVAR_ARCHIVE, "enable detail textures support, use '2' for autogenerate detail.txt" );
	r_lockpvs = Cvar_Get( "r_lockpvs", "0", FCVAR_CHEAT, "lockpvs area at current point (pvs test)" );
	r_lockfrustum = Cvar_Get( "r_lockfrustum", "0", FCVAR_CHEAT, "lock frustrum area at current point (cull test)" );
	r_dynamic = Cvar_Get( "r_dynamic", "1", FCVAR_ARCHIVE, "allow dynamic lighting (dlights, lightstyles)" );
	r_traceglow = Cvar_Get( "r_traceglow", "1", FCVAR_ARCHIVE, "cull flares behind models" );
	r_lightmap = Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	r_drawentities = Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT, "render entities" );
	r_decals = Cvar_Get( "r_decals", "4096", FCVAR_ARCHIVE, "sets the maximum number of decals" );
	window_xpos = Cvar_Get( "_window_xpos", "130", FCVAR_RENDERINFO, "window position by horizontal" );
	window_ypos = Cvar_Get( "_window_ypos", "48", FCVAR_RENDERINFO, "window position by vertical" );

	gl_extensions = Cvar_Get( "gl_allow_extensions", "1", FCVAR_GLCONFIG, "allow gl_extensions" );
	gl_texture_nearest = Cvar_Get( "gl_texture_nearest", "0", FCVAR_ARCHIVE, "disable texture filter" );
	gl_lightmap_nearest = Cvar_Get( "gl_lightmap_nearest", "0", FCVAR_ARCHIVE, "disable lightmap filter" );
	gl_check_errors = Cvar_Get( "gl_check_errors", "1", FCVAR_ARCHIVE, "ignore video engine errors" );
	gl_vsync = Cvar_Get( "gl_vsync", "0", FCVAR_ARCHIVE,  "enable vertical syncronization" );
	gl_detailscale = Cvar_Get( "gl_detailscale", "4.0", FCVAR_ARCHIVE, "default scale applies while auto-generate list of detail textures" );
	gl_texture_anisotropy = Cvar_Get( "gl_anisotropy", "8", FCVAR_ARCHIVE, "textures anisotropic filter" );
	gl_texture_lodbias =  Cvar_Get( "gl_texture_lodbias", "0.0", FCVAR_ARCHIVE, "LOD bias for mipmapped textures (perfomance|quality)" );
	gl_keeptjunctions = Cvar_Get( "gl_keeptjunctions", "1", FCVAR_ARCHIVE, "removing tjuncs causes blinking pixels" );
	gl_showtextures = Cvar_Get( "r_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	gl_finish = Cvar_Get( "gl_finish", "0", FCVAR_ARCHIVE, "use glFinish instead of glFlush" );
	gl_nosort = Cvar_Get( "gl_nosort", "0", FCVAR_ARCHIVE, "disable sorting of translucent surfaces" );
	gl_clear = Cvar_Get( "gl_clear", "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
	gl_test = Cvar_Get( "gl_test", "0", 0, "engine developer cvar for quick testing new features" );
	gl_wireframe = Cvar_Get( "gl_wireframe", "0", FCVAR_ARCHIVE|FCVAR_SPONLY, "show wireframe overlay" );
	gl_msaa = Cvar_Get( "gl_msaa", "0", FCVAR_GLCONFIG, "MSAA samples. Use with caution, engine may fail with some values" );
	gl_stencilbits = Cvar_Get( "gl_stencilbits", "8", FCVAR_GLCONFIG, "pixelformat stencil bits (0 - auto)" );

	// these cvar not used by engine but some mods requires this
	gl_polyoffset = Cvar_Get( "gl_polyoffset", "2.0", FCVAR_ARCHIVE, "polygon offset for decals" );

	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	vid_gamma = Cvar_Get( "gamma", "2.5", FCVAR_ARCHIVE, "gamma amount" );
	vid_brightness = Cvar_Get( "brightness", "0.0", FCVAR_ARCHIVE, "brighntess factor" );
	vid_mode = Cvar_Get( "vid_mode", "-1", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "display resolution mode" );
	vid_fullscreen = Cvar_Get( "fullscreen", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable fullscreen mode" );
	vid_displayfrequency = Cvar_Get ( "vid_displayfrequency", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "fullscreen refresh rate" );
	vid_highdpi = Cvar_Get( "vid_highdpi", "1", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "Enable High-DPI mode" );

	Cmd_AddCommand( "r_info", R_RenderInfo_f, "display renderer info" );

	// apply actual video mode to window
	Cbuf_AddText( "exec video.cfg\n" );
	Cbuf_Execute();
}

/*
=================
GL_RemoveCommands
=================
*/
void GL_RemoveCommands( void )
{
	Cmd_RemoveCommand( "r_info");
}

#ifdef WIN32
typedef enum _XASH_DPI_AWARENESS
{
	XASH_DPI_UNAWARE = 0,
	XASH_SYSTEM_DPI_AWARE = 1,
	XASH_PER_MONITOR_DPI_AWARE = 2
} XASH_DPI_AWARENESS;

void WIN_SetDPIAwareness( void )
{
	HMODULE hModule;
	HRESULT ( __stdcall *pSetProcessDpiAwareness )( XASH_DPI_AWARENESS );
	BOOL ( __stdcall *pSetProcessDPIAware )( void );
	BOOL bSuccess = FALSE;

	if( ( hModule = LoadLibrary( "shcore.dll" ) ) )
	{
		if( ( pSetProcessDpiAwareness = (void*)GetProcAddress( hModule, "SetProcessDpiAwareness" ) ) )
		{
			// I hope SDL don't handle WM_DPICHANGED message
			HRESULT hResult = pSetProcessDpiAwareness( XASH_SYSTEM_DPI_AWARE );

			if( hResult == S_OK )
			{
				MsgDev( D_NOTE, "SetDPIAwareness: Success\n" );
				bSuccess = TRUE;
			}
			else if( hResult = E_INVALIDARG ) MsgDev( D_NOTE, "SetDPIAwareness: Invalid argument\n" );
			else if( hResult == E_ACCESSDENIED ) MsgDev( D_NOTE, "SetDPIAwareness: Access Denied\n" );
		}
		else MsgDev( D_NOTE, "SetDPIAwareness: Can't get SetProcessDpiAwareness\n" );
		FreeLibrary( hModule );
	}
	else MsgDev( D_NOTE, "SetDPIAwareness: Can't load shcore.dll\n" );


	if( !bSuccess )
	{
		MsgDev( D_NOTE, "SetDPIAwareness: Trying SetProcessDPIAware...\n" );

		if( ( hModule = LoadLibrary( "user32.dll" ) ) )
		{
			if( ( pSetProcessDPIAware = ( void* )GetProcAddress( hModule, "SetProcessDPIAware" ) ) )
			{
				// I hope SDL don't handle WM_DPICHANGED message
				BOOL hResult = pSetProcessDPIAware();

				if( hResult )
				{
					MsgDev( D_NOTE, "SetDPIAwareness: Success\n" );
					bSuccess = TRUE;
				}
				else MsgDev( D_NOTE, "SetDPIAwareness: fail\n" );
			}
			else MsgDev( D_NOTE, "SetDPIAwareness: Can't get SetProcessDPIAware\n" );
			FreeLibrary( hModule );
		}
		else MsgDev( D_NOTE, "SetDPIAwareness: Can't load user32.dll\n" );
	}
}
#endif

static void SetWidthAndHeightFromCommandLine()
{
	int width;
	int height;

	Sys_GetIntFromCmdLine( "-width", &width );
	Sys_GetIntFromCmdLine( "-height", &height );

	if( width < 1 || height < 1 )
	{
		// Not specified or invalid, so don't bother.
		return;
	}

	Cvar_SetValue( "vid_mode", VID_NOMODE );
	Cvar_SetValue( "width", width );
	Cvar_SetValue( "height", height );
}

static void SetFullscreenModeFromCommandLine( )
{
#ifndef __ANDROID__
	if ( Sys_CheckParm("-fullscreen") )
	{
		Cvar_Set( "fullscreen", "1" );
	}
	else if ( Sys_CheckParm( "-windowed" ) )
	{
		Cvar_Set( "fullscreen", "0" );
	}
#endif
}

/*
===============
R_Init
===============
*/
qboolean R_Init( void )
{
	if( glw_state.initialized )
		return true;

	// give initial OpenGL configuration
	Cbuf_AddText( "exec opengl.cfg\n" );

	GL_InitCommands();
	GL_InitRandomTable();

	// Set screen resolution and fullscreen mode if passed in on command line.
	// This is done after executing opengl.cfg, as the command line values should take priority.
	SetWidthAndHeightFromCommandLine();
	SetFullscreenModeFromCommandLine();

	GL_SetDefaultState();

#ifdef WIN32
	WIN_SetDPIAwareness( );
#endif

	// create the window and set up the context
	if( !R_Init_OpenGL( ))
	{
		GL_RemoveCommands();
		R_Free_OpenGL();

		Sys_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		return false;
	}

	host.renderinfo_changed = false;
	r_temppool = Mem_AllocPool( "Render Zone" );

	GL_InitExtensions();
	GL_SetDefaults();
	R_InitImages();
	R_SpriteInit();
	R_StudioInit();
	R_AliasInit();
	R_ClearDecals();
	R_ClearScene();

	// initialize screen
	SCR_Init();

	return true;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( void )
{
	model_t	*mod;
	int	i;

	if( !glw_state.initialized )
		return;

	// release SpriteTextures
	for( i = 1, mod = clgame.sprites; i < MAX_CLIENT_SPRITES; i++, mod++ )
	{
		if( !mod->name[0] ) continue;
		Mod_UnloadSpriteModel( mod );
	}
	memset( clgame.sprites, 0, sizeof( clgame.sprites ));

	GL_RemoveCommands();
	R_ShutdownImages();

	Mem_FreePool( &r_temppool );

	// shut down OS specific OpenGL stuff like contexts, etc.
	R_Free_OpenGL();
}

/*
=================
GL_CheckForErrors

obsolete
=================
*/
void GL_CheckForErrors_( const char *filename, const int fileline )
{
	int	err;
	char	*str;

	if( !gl_check_errors->value )
		return;

	if(( err = pglGetError( )) == GL_NO_ERROR )
		return;

	switch( err )
	{
	case GL_STACK_OVERFLOW:
		str = "GL_STACK_OVERFLOW";
		break;
	case GL_STACK_UNDERFLOW:
		str = "GL_STACK_UNDERFLOW";
		break;
	case GL_INVALID_ENUM:
		str = "GL_INVALID_ENUM";
		break;
	case GL_INVALID_VALUE:
		str = "GL_INVALID_VALUE";
		break;
	case GL_INVALID_OPERATION:
		str = "GL_INVALID_OPERATION";
		break;
	case GL_OUT_OF_MEMORY:
		str = "GL_OUT_OF_MEMORY";
		break;
	default:
		str = "UNKNOWN ERROR";
		break;
	}

	Con_Printf( S_OPENGL_ERROR "%s (called at %s:%i)\n", str, filename, fileline );
}
