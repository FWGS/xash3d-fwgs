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
#include "platform/platform.h"

#define WINDOW_NAME			XASH_ENGINE_NAME " Window" // Half-Life

convar_t	*gl_extensions;
convar_t	*gl_texture_anisotropy;
convar_t	*gl_texture_lodbias;
convar_t	*gl_texture_nearest;
convar_t	*gl_wgl_msaa_samples;
convar_t	*gl_lightmap_nearest;
convar_t	*gl_keeptjunctions;
convar_t	*gl_emboss_scale;
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

convar_t	*window_xpos;
convar_t	*window_ypos;
convar_t	*r_speeds;
convar_t	*r_fullbright;
convar_t	*r_norefresh;
convar_t	*r_showtree;
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
convar_t	*gl_round_down;
convar_t	*r_vbo;
convar_t	*r_vbo_dlightmode;

convar_t	*vid_displayfrequency;
convar_t	*vid_fullscreen;
convar_t	*vid_brightness;
convar_t	*vid_gamma;
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

	Con_Reportf( "GL_CheckExtension: %s ", name );
	GL_SetExtension( r_ext, true );

	if( cvarname )
	{
		// system config disable extensions
		parm = Cvar_Get( cvarname, "1", FCVAR_GLCONFIG, va( CVAR_GLCONFIG_DESCRIPTION, name ));
	}

	if(( parm && !CVAR_TO_BOOL( parm )) || ( !CVAR_TO_BOOL( gl_extensions ) && r_ext != GL_OPENGL_110 ))
	{
		Con_Reportf( "- disabled\n" );
		GL_SetExtension( r_ext, false );
		return; // nothing to process at
	}

	extensions_string = glConfig.extensions_string;

	if(( name[2] == '_' || name[3] == '_' ) && !Q_strstr( extensions_string, name ))
	{
		GL_SetExtension( r_ext, false );	// update render info
		Con_Reportf( "- ^1failed\n" );
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
		Con_Reportf( "- ^2enabled\n" );
	else Con_Reportf( "- ^1failed\n" );
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
	Con_Reportf( "VID_StartupGamma: gamma %g brightness %g\n", vid_gamma->value, vid_brightness->value );
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

	host.window_center_x = w / 2;
	host.window_center_y = h / 2;

	Cvar_SetValue( "width", w );
	Cvar_SetValue( "height", h );

	// check for 4:3 or 5:4
	if( w * 3 != h * 4 && w * 4 != h * 5 )
		glState.wideScreen = true;
	else glState.wideScreen = false;
}

/*
=================
VID_GetModeString
=================
*/
const char *VID_GetModeString( int vid_mode )
{
	vidmode_t *vidmode;
	if( vid_mode < 0 || vid_mode > R_MaxVideoModes() )
		return NULL;

	if( !( vidmode = R_GetVideoMode( vid_mode ) ) )
		return NULL;

	return vidmode->desc;
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
		if( VID_SetMode( ))
		{
			SCR_VidInit(); // tell the client.dll what vid_mode has changed
		}
		else
		{
			Sys_Error( "Can't re-initialize video subsystem\n" );
		}
		host.renderinfo_changed = false;
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
	pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
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
	Con_Printf( "MODE: %ix%i\n", glState.width, glState.height );
	Con_Printf( "\n" );
	Con_Printf( "VERTICAL SYNC: %s\n", gl_vsync->value ? "enabled" : "disabled" );
	Con_Printf( "Color %d bits, Alpha %d bits, Depth %d bits, Stencil %d bits\n", glConfig.color_bits,
		glConfig.alpha_bits, glConfig.depth_bits, glConfig.stencil_bits );
}

static void VID_Mode_f( void )
{
	int w, h;

	switch( Cmd_Argc() )
	{
	case 2:
	{
		vidmode_t *vidmode;

		vidmode = R_GetVideoMode( Q_atoi( Cmd_Argv( 1 )) );
		if( !vidmode )
		{
			Con_Print( S_ERROR "unable to set mode, backend returned null" );
			return;
		}

		w = vidmode->width;
		h = vidmode->height;
		break;
	}
	case 3:
	{
		w = Q_atoi( Cmd_Argv( 1 ));
		h = Q_atoi( Cmd_Argv( 2 ));
		break;
	}
	default:
		Msg( S_USAGE "vid_mode <modenum>|<width height>\n" );
		return;
	}

	R_ChangeDisplaySettings( w, h, Cvar_VariableInteger( "fullscreen" ) );
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
	Cvar_Get( "width", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen width" );
	Cvar_Get( "height", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "screen height" );
	r_speeds = Cvar_Get( "r_speeds", "0", FCVAR_ARCHIVE, "shows renderer speeds" );
	r_fullbright = Cvar_Get( "r_fullbright", "0", FCVAR_CHEAT, "disable lightmaps, get fullbright for entities" );
	r_norefresh = Cvar_Get( "r_norefresh", "0", 0, "disable 3D rendering (use with caution)" );
	r_showtree = Cvar_Get( "r_showtree", "0", FCVAR_ARCHIVE, "build the graph of visible BSP tree" );
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
	gl_emboss_scale = Cvar_Get( "gl_emboss_scale", "0", FCVAR_ARCHIVE|FCVAR_LATCH, "fake bumpmapping scale" );
	gl_showtextures = Cvar_Get( "r_showtextures", "0", FCVAR_CHEAT, "show all uploaded textures" );
	gl_finish = Cvar_Get( "gl_finish", "0", FCVAR_ARCHIVE, "use glFinish instead of glFlush" );
	gl_nosort = Cvar_Get( "gl_nosort", "0", FCVAR_ARCHIVE, "disable sorting of translucent surfaces" );
	gl_clear = Cvar_Get( "gl_clear", "0", FCVAR_ARCHIVE, "clearing screen after each frame" );
	gl_test = Cvar_Get( "gl_test", "0", 0, "engine developer cvar for quick testing new features" );
	gl_wireframe = Cvar_Get( "gl_wireframe", "0", FCVAR_ARCHIVE|FCVAR_SPONLY, "show wireframe overlay" );
	gl_wgl_msaa_samples = Cvar_Get( "gl_wgl_msaa_samples", "0", FCVAR_GLCONFIG, "samples number for multisample anti-aliasing" );
	gl_msaa = Cvar_Get( "gl_msaa", "1", FCVAR_ARCHIVE, "enable or disable multisample anti-aliasing" );
	gl_stencilbits = Cvar_Get( "gl_stencilbits", "8", FCVAR_GLCONFIG, "pixelformat stencil bits (0 - auto)" );
	gl_round_down = Cvar_Get( "gl_round_down", "2", FCVAR_RENDERINFO, "round texture sizes to nearest POT value" );
	// these cvar not used by engine but some mods requires this
	gl_polyoffset = Cvar_Get( "gl_polyoffset", "2.0", FCVAR_ARCHIVE, "polygon offset for decals" );

	// make sure gl_vsync is checked after vid_restart
	SetBits( gl_vsync->flags, FCVAR_CHANGED );

	vid_gamma = Cvar_Get( "gamma", "2.5", FCVAR_ARCHIVE, "gamma amount" );
	vid_brightness = Cvar_Get( "brightness", "0.0", FCVAR_ARCHIVE, "brightness factor" );
	vid_fullscreen = Cvar_Get( "fullscreen", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable fullscreen mode" );
	vid_displayfrequency = Cvar_Get ( "vid_displayfrequency", "0", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "fullscreen refresh rate" );
	vid_highdpi = Cvar_Get( "vid_highdpi", "1", FCVAR_RENDERINFO|FCVAR_VIDRESTART, "enable High-DPI mode" );

	Cmd_AddCommand( "r_info", R_RenderInfo_f, "display renderer info" );

	// a1ba: planned to be named vid_mode for compability
	// but supported mode list is filled by backends, so numbers are not portable any more
	Cmd_AddCommand( "vid_setmode", VID_Mode_f, "display video mode" );

	// give initial OpenGL configuration
	host.apply_opengl_config = true;
	Cbuf_AddText( "exec opengl.cfg\n" );
	Cbuf_Execute();
	host.apply_opengl_config = false;

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

static void SetWidthAndHeightFromCommandLine()
{
	int width, height;

	Sys_GetIntFromCmdLine( "-width", &width );
	Sys_GetIntFromCmdLine( "-height", &height );

	if( width < 1 || height < 1 )
	{
		// Not specified or invalid, so don't bother.
		return;
	}

	R_SaveVideoMode( width, height );
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
R_CheckVBO

register VBO cvars and get default value
===============
*/
static void R_CheckVBO( void )
{
	const char *def = "1";
	const char *dlightmode = "1";
	int flags = FCVAR_ARCHIVE;
	qboolean disable = false;

	// some bad GLES1 implementations breaks dlights completely
	if( glConfig.max_texture_units < 3 )
		disable = true;

#ifdef XASH_MOBILE_PLATFORM
	// VideoCore4 drivers have a problem with mixing VBO and client arrays
	// Disable it, as there is no suitable workaround here
	if( Q_stristr( glConfig.renderer_string, "VideoCore IV" ) || Q_stristr( glConfig.renderer_string, "vc4" ) )
		disable = true;

	// dlightmode 1 is not too much tested on android
	// so better to left it off
	dlightmode = "0";
#endif

	if( disable )
	{
		// do not keep in config unless dev > 3 and enabled
		flags = 0;
		def = "0";
	}

	r_vbo = Cvar_Get( "r_vbo", def, flags, "draw world using VBO" );
	r_vbo_dlightmode = Cvar_Get( "r_vbo_dlightmode", dlightmode, FCVAR_ARCHIVE, "vbo dlight rendering mode(0-1)" );

	// check if enabled manually
	if( CVAR_TO_BOOL(r_vbo) )
		r_vbo->flags |= FCVAR_ARCHIVE;
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

	GL_InitCommands();
	GL_InitRandomTable();

	// Set screen resolution and fullscreen mode if passed in on command line.
	// This is done after executing opengl.cfg, as the command line values should take priority.
	SetWidthAndHeightFromCommandLine();
	SetFullscreenModeFromCommandLine();

	GL_SetDefaultState();

	// create the window and set up the context
	if( !R_Init_Video( ))
	{
		GL_RemoveCommands();
		R_Free_Video();

		Sys_Error( "Can't initialize video subsystem\nProbably driver was not installed" );
		return false;
	}

	host.renderinfo_changed = false;
	r_temppool = Mem_AllocPool( "Render Zone" );

	GL_SetDefaults();
	R_CheckVBO();
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
	R_Free_Video();
}

/*
=================
GL_ErrorString
convert errorcode to string
=================
*/
const char *GL_ErrorString( int err )
{
	switch( err )
	{
	case GL_STACK_OVERFLOW:
		return "GL_STACK_OVERFLOW";
	case GL_STACK_UNDERFLOW:
		return "GL_STACK_UNDERFLOW";
	case GL_INVALID_ENUM:
		return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE:
		return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION:
		return "GL_INVALID_OPERATION";
	case GL_OUT_OF_MEMORY:
		return "GL_OUT_OF_MEMORY";
	default:
		return "UNKNOWN ERROR";
	}
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

	if( !CVAR_TO_BOOL( gl_check_errors ))
		return;

	if(( err = pglGetError( )) == GL_NO_ERROR )
		return;

	Con_Printf( S_OPENGL_ERROR "%s (called at %s:%i)\n", GL_ErrorString( err ), filename, fileline );
}
