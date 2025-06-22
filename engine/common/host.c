/*
host.c - dedicated and normal host
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

#include "build.h"
#ifdef XASH_SDL
#include <SDL.h>
#endif // XASH_SDL
#include <stdarg.h>  // va_args
#if !XASH_WIN32
#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <VrBase.h>
#include <VrRenderer.h>
#include <VrInput.h>

#endif
#if XASH_EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif
#include "common.h"
#include "base_cmd.h"
#include "client.h"
#include "server.h"
#include "netchan.h"
#include "protocol.h"
#include "mod_local.h"
#include "xash3d_mathlib.h"
#include "input.h"
#include "enginefeatures.h"
#include "render_api.h"	// decallist_t
#include "tests.h"
#include <cl_tent.h>

static pfnChangeGame	pChangeGame = NULL;
host_parm_t		host;	// host parms

#if XASH_ANDROID
static jmp_buf return_from_main_buf;

/*
===============
Host_ExitInMain

On some platforms (e.g. Android) we can't exit with exit(3) as calling it would
kill wrapper process (e.g. app_process) too early, before all resources would
be freed, contexts released, files closed, etc, etc...

To fix this, we create jmp_buf in Host_Main function, when jumping into with
non-zero value will immediately return from it with `error_on_exit`.
===============
*/
void Host_ExitInMain( void )
{
	longjmp( return_from_main_buf, 1 );
}
#endif // XASH_ANDROID

#ifdef XASH_ENGINE_TESTS
struct tests_stats_s tests_stats;
#endif

CVAR_DEFINE( host_developer, "developer", "0", FCVAR_FILTERABLE, "engine is in development-mode" );
CVAR_DEFINE_AUTO( sys_timescale, "1.0", FCVAR_FILTERABLE, "scale frame time" );

static CVAR_DEFINE_AUTO( sys_ticrate, "100", FCVAR_SERVER, "framerate in dedicated mode" );
static CVAR_DEFINE_AUTO( host_serverstate, "0", FCVAR_READ_ONLY, "displays current server state" );
static CVAR_DEFINE_AUTO( host_gameloaded, "0", FCVAR_READ_ONLY, "inidcates a loaded game.dll" );
static CVAR_DEFINE_AUTO( host_clientloaded, "0", FCVAR_READ_ONLY, "inidcates a loaded client.dll" );
CVAR_DEFINE_AUTO( host_limitlocal, "0", 0, "apply cl_cmdrate and rate to loopback connection" );
CVAR_DEFINE( host_maxfps, "fps_max", "72", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "host fps upper limit" );
CVAR_DEFINE_AUTO( fps_override, "1", FCVAR_FILTERABLE, "unlock higher framerate values, not supported" );
static CVAR_DEFINE_AUTO( host_framerate, "0", FCVAR_FILTERABLE, "locks frame timing to this value in seconds" );
static CVAR_DEFINE( host_sleeptime, "sleeptime", "1", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "milliseconds to sleep for each frame. higher values reduce fps accuracy" );
static CVAR_DEFINE_AUTO( host_sleeptime_debug, "0", 0, "print sleeps between frames" );
CVAR_DEFINE_AUTO( host_allow_materials, "0", FCVAR_LATCH|FCVAR_ARCHIVE, "allow texture replacements from materials/ folder" );
CVAR_DEFINE( con_gamemaps, "con_mapfilter", "1", FCVAR_ARCHIVE, "when true show only maps in game folder" );

typedef struct feature_message_s
{
	uint32_t mask;
	const char *msg;
	const char *arg;
} feature_message_t;

static const feature_message_t bugcomp_features[] =
{
{ BUGCOMP_PENTITYOFENTINDEX_FLAG, "pfnPEntityOfEntIndex bugfix revert", "peoei" },
{ BUGCOMP_MESSAGE_REWRITE_FACILITY_FLAG, "GoldSrc Message Rewrite Facility", "gsmrf" },
{ BUGCOMP_SPATIALIZE_SOUND_WITH_ATTN_NONE, "spatialize sounds with zero attenuation", "sp_attn_none" },
{ BUGCOMP_GET_GAME_DIR_FULL_PATH, "Return full path in GET_GAME_DIR()", "get_game_dir_full" }
};

static const feature_message_t engine_features[] =
{
{ ENGINE_WRITE_LARGE_COORD, "Big World Support" },
{ ENGINE_QUAKE_COMPATIBLE, "Quake Compatibility" },
{ ENGINE_LOAD_DELUXEDATA, "Deluxemap Support" },
{ ENGINE_PHYSICS_PUSHER_EXT, "Improved MOVETYPE_PUSH" },
{ ENGINE_LARGE_LIGHTMAPS, "Large Lightmaps" },
{ ENGINE_COMPENSATE_QUAKE_BUG, "Stupid Quake Bug Compensation" },
{ ENGINE_IMPROVED_LINETRACE, "Improved Trace Line" },
{ ENGINE_COMPUTE_STUDIO_LERP, "Studio MOVETYPE_STEP Lerping" },
{ ENGINE_LINEAR_GAMMA_SPACE, "Linear Gamma Space" },
{ ENGINE_STEP_POSHISTORY_LERP, "MOVETYPE_STEP Position History Based Lerping" },
};

static void Sys_MakeVersionString( char *out, size_t len )
{
	Q_snprintf( out, len, XASH_ENGINE_NAME " %i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
}

static void Sys_PrintUsage( const char *exename )
{
	string version_str;
	const char *usage_str;

	Sys_MakeVersionString( version_str, sizeof( version_str ));

#if XASH_MESSAGEBOX != MSGBOX_STDERR
	#if XASH_WIN32
		#define XASH_EXE "(xash).exe"
	#else
		#define XASH_EXE "(xash)"
	#endif
#else
	#define XASH_EXE "%s"
#endif
#define O( x, y ) "  "x"  "y"\n"

	usage_str = S_USAGE XASH_EXE " [options] [+command] [+command2 arg] ...\n"

"\nCommon options:\n"
	O("-dev [level]       ", "set log verbosity 0-2")
	O("-log [file name]   ", "write log to \"engine.log\" or [file name] if specified")
	O("-logtime           ", "enable writing timestamps to the log file")
	O("-nowriteconfig     ", "disable config save")
	O("-noch              ", "disable crashhandler")
#if XASH_WIN32 // !!!!
	O("-minidumps         ", "enable writing minidumps when game is crashed")
#endif
	O("-rodir <path>      ", "set read-only base directory")
	O("-bugcomp [opts]    ", "enable precise bug compatibility")
	O("                   ", "will break games that don't require it")
	O("                   ", "refer to engine documentation for more info")
	O("-disablehelp       ", "disable this message")
#if !XASH_DEDICATED
	O("-dedicated         ", "run engine in dedicated mode")
#endif

"\nNetworking options:\n"
	O("-noip              ", "disable IPv4")
	O("-ip <ip>           ", "set IPv4 address")
	O("-port <port>       ", "set IPv4 port")
#if !XASH_DEDICATED
	O("-clientport <port> ", "set IPv4 client port")
#endif
	O("-noip6             ", "disable IPv6")
	O("-ip6 <ip>          ", "set IPv6 address")
	O("-port6 <port>      ", "set IPv6 port")
#if !XASH_DEDICATED
	O("-clientport6 <port>", "set IPv6 client port")
#endif
	O("-clockwindow <cw>  ", "adjust clockwindow used to ignore client commands")
	O("                   ", "to prevent speed hacks")

"\nGame options:\n"
	O("-game <directory>  ", "set game directory to start engine with")
	O("-dll <path>        ", "override server DLL path")
#if !XASH_DEDICATED
	O("-clientlib <path>  ", "override client DLL path")
	O("-console           ", "run engine with console enabled")
	O("-toconsole         ", "run engine witn console open")
	O("-oldfont           ", "enable unused Quake font in Half-Life")
	O("-width <n>         ", "set window width")
	O("-height <n>        ", "set window height")
	O("-borderless        ", "run engine in fullscreen borderless mode")
	O("-fullscreen        ", "run engine in fullscreen mode")
	O("-windowed          ", "run engine in windowed mode")
	O("-ref <name>        ", "use selected renderer dll")
	O("-gldebug           ", "enable OpenGL debug log")
#if XASH_WIN32
	O("-noavi             ", "disable AVI support")
	O("-nointro           ", "disable intro video")
#endif
	O("-noenginejoy       ", "disable engine builtin joystick support")
	O("-noenginemouse     ", "disable engine builtin mouse support")
	O("-nosound           ", "disable sound output")
	O("-timedemo          ", "run timedemo and exit")
#endif

"\nPlatform-specific options:\n"
#if !XASH_MOBILE_PLATFORM
	O("-daemonize         ", "run engine as a daemon")
#endif
#if XASH_SDL == 2
	O("-sdl_renderer <n>  ","use alternative SDL_Renderer for software")
#endif // XASH_SDL
#if XASH_ANDROID && !XASH_SDL
	O("-nativeegl         ","use native egl implementation. Use if screen does not update or black")
#endif // XASH_ANDROID
#if XASH_DOS
	O("-novesa            ","disable vesa")
#endif // XASH_DOS
#if XASH_VIDEO == VIDEO_FBDEV
	O("-fbdev <path>      ","open selected framebuffer")
	O("-ttygfx            ","set graphics mode in tty")
	O("-doublebuffer      ","enable doublebuffering")
#endif // XASH_VIDEO == VIDEO_FBDEV
#if XASH_SOUND == SOUND_ALSA
	O("-alsadev <dev>     ","open selected ALSA device")
#endif // XASH_SOUND == SOUND_ALSA
	;
#undef O
#undef XASH_EXE

	// HACKHACK: pretty output in dedicated
#if XASH_MESSAGEBOX != MSGBOX_STDERR
	Platform_MessageBox( version_str, usage_str, false );
#else
	fprintf( stderr, "%s\n", version_str );
	fprintf( stderr, usage_str, exename );
#endif

	Sys_Quit( NULL );
}

CVAR_DEFINE_AUTO( vr_camera_x, "0", FCVAR_MOVEVARS, "Offset x of the camera" );
CVAR_DEFINE_AUTO( vr_camera_y, "0", FCVAR_MOVEVARS, "Offset y of the camera" );
CVAR_DEFINE_AUTO( vr_camera_z, "0", FCVAR_MOVEVARS, "Offset z of the camera" );
CVAR_DEFINE_AUTO( vr_gamemode, "0", FCVAR_MOVEVARS, "Are we in the 3D VR mode?" );
CVAR_DEFINE_AUTO( vr_hmd_pitch, "0", FCVAR_MOVEVARS, "Camera pitch angle" );
CVAR_DEFINE_AUTO( vr_hmd_yaw, "0", FCVAR_MOVEVARS, "Camera yaw angle" );
CVAR_DEFINE_AUTO( vr_hmd_roll, "0", FCVAR_MOVEVARS, "Camera roll angle" );
CVAR_DEFINE_AUTO( vr_offset_x, "0", FCVAR_MOVEVARS, "Offset x of the camera" );
CVAR_DEFINE_AUTO( vr_offset_y, "0", FCVAR_MOVEVARS, "Offset y of the camera" );
CVAR_DEFINE_AUTO( vr_player_dir_x, "0", FCVAR_MOVEVARS, "Direction x of the player" );
CVAR_DEFINE_AUTO( vr_player_dir_y, "0", FCVAR_MOVEVARS, "Direction y of the player" );
CVAR_DEFINE_AUTO( vr_player_dir_z, "0", FCVAR_MOVEVARS, "Direction z of the player" );
CVAR_DEFINE_AUTO( vr_player_pos_x, "0", FCVAR_MOVEVARS, "Position x of the player" );
CVAR_DEFINE_AUTO( vr_player_pos_y, "0", FCVAR_MOVEVARS, "Position y of the player" );
CVAR_DEFINE_AUTO( vr_player_pos_z, "0", FCVAR_MOVEVARS, "Position z of the player" );
CVAR_DEFINE_AUTO( vr_player_pitch, "0", FCVAR_MOVEVARS, "Pinch angle of the player" );
CVAR_DEFINE_AUTO( vr_player_yaw, "0", FCVAR_MOVEVARS, "Yaw angle of the player" );
CVAR_DEFINE_AUTO( vr_stereo_side, "0", FCVAR_MOVEVARS, "Eye being drawn" );
CVAR_DEFINE_AUTO( vr_weapon_roll, "0", FCVAR_MOVEVARS, "Weapon roll angle" );
CVAR_DEFINE_AUTO( vr_weapon_x, "0", FCVAR_MOVEVARS, "Weapon position x" );
CVAR_DEFINE_AUTO( vr_weapon_y, "0", FCVAR_MOVEVARS, "Weapon position y" );
CVAR_DEFINE_AUTO( vr_weapon_z, "0", FCVAR_MOVEVARS, "Weapon position z" );
CVAR_DEFINE_AUTO( vr_xhair_x, "0", FCVAR_MOVEVARS, "Cross-hair 2d position x" );
CVAR_DEFINE_AUTO( vr_xhair_y, "0", FCVAR_MOVEVARS, "Cross-hair 2d position y" );
CVAR_DEFINE_AUTO( vr_zoomed, "0", FCVAR_MOVEVARS, "Flag if the scene zoomed" );


CVAR_DEFINE_AUTO( vr_6dof, "0", FCVAR_ARCHIVE, "Use 6DoF world tracking" );
CVAR_DEFINE_AUTO( vr_button_a, "+duck", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_b, "+jump", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_x, "drop", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_y, "impulse 201;nightvision;+vr_scoreboard", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_grip_left, "+voicerecord", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_joystick_left, "exec touch/cmd/cmd", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_trigger_left, "+use;buy", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_grip_right, "+reload", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_joystick_right, "+attack2", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_button_trigger_right, "+attack", FCVAR_ARCHIVE, "Controller mapping" );
CVAR_DEFINE_AUTO( vr_thumbstick_deadzone_left, "0.15", FCVAR_ARCHIVE, "Deadzone of thumbstick to filter drift" );
CVAR_DEFINE_AUTO( vr_thumbstick_deadzone_right, "0.8", FCVAR_ARCHIVE, "Deadzone of thumbstick to filter drift" );
CVAR_DEFINE_AUTO( vr_thumbstick_snapturn, "45", FCVAR_ARCHIVE, "Angle to rotate by a thumbstick" );
CVAR_DEFINE_AUTO( vr_worldscale, "30", FCVAR_ARCHIVE, "Sets the world scale for stereo separation" );

static void Sys_PrintBugcompUsage( const char *exename )
{
	string version_str;
	char usage_str[4096];
	char *p = usage_str;
	int i;

	Sys_MakeVersionString( version_str, sizeof( version_str ));

	p += Q_snprintf( p, sizeof( usage_str ) - ( usage_str - p ), "Known bugcomp flags are:\n" );
	for( i = 0; i < ARRAYSIZE( bugcomp_features ); i++ )
		p += Q_snprintf( p, sizeof( usage_str ) - ( usage_str - p ), "   %s: %s\n", bugcomp_features[i].arg, bugcomp_features[i].msg );
	p += Q_snprintf( p, sizeof( usage_str ) - ( usage_str - p ), "\nIt is possible to combine multiple flags with '+' characters.\nExample: -bugcomp flag1+flag2+flag3...\n" );

	// HACKHACK: pretty output in dedicated
#if XASH_MESSAGEBOX != MSGBOX_STDERR
	Platform_MessageBox( version_str, usage_str, false );
#else
	fprintf( stderr, "%s\n", version_str );
	fprintf( stderr, usage_str, exename );
#endif

	Sys_Quit( NULL );
}

/*
================
Host_PrintEngineFeatures
================
*/
static void Host_PrintFeatures( uint32_t flags, const char *s, const feature_message_t *features, size_t size )
{
	size_t i;

	for( i = 0; i < size; i++ )
	{
		if( FBitSet( flags, features[i].mask ))
			Con_Printf( "^3%s:^7 %s is enabled\n", s, features[i].msg );
	}
}

/*
==============
Host_ValidateEngineFeatures

validate features bits and set host.features
==============
*/
void Host_ValidateEngineFeatures( uint32_t mask, uint32_t features )
{
	// don't allow unsupported bits
	features &= mask;

	// force bits for some games
	if( !Q_stricmp( GI->gamefolder, "cstrike" ) || !Q_stricmp( GI->gamefolder, "czero" ))
		SetBits( features, ENGINE_STEP_POSHISTORY_LERP );

	// print requested first
	Host_PrintFeatures( features, "EXT", engine_features, ARRAYSIZE( engine_features ));

	// now warn about incompatible bits
	if( FBitSet( features, ENGINE_STEP_POSHISTORY_LERP|ENGINE_COMPUTE_STUDIO_LERP ) == ( ENGINE_STEP_POSHISTORY_LERP|ENGINE_COMPUTE_STUDIO_LERP ))
		Con_Printf( S_WARN "%s: incompatible ENGINE_STEP_POSHISTORY_LERP and ENGINE_COMPUTE_STUDIO_LERP are enabled!\n", __func__ );

	// finally set global variable
	host.features = features;
}

/*
==============
Host_IsQuakeCompatible

==============
*/
qboolean Host_IsQuakeCompatible( void )
{
	// feature set
	if( FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		return true;

#if !XASH_DEDICATED
	// quake demo playing
	if( cls.demoplayback == DEMO_QUAKE1 )
		return true;
#endif // XASH_DEDICATED

	return false;
}

/*
================
Host_EndGame
================
*/
void Host_EndGame( qboolean abort, const char *message, ... )
{
	va_list		argptr;
	static char	string[MAX_SYSPATH];

	va_start( argptr, message );
	Q_vsnprintf( string, sizeof( string ), message, argptr );
	va_end( argptr );

	Con_Printf( "Host_EndGame: %s\n", string );

	SV_Shutdown( "\n" );
#if !XASH_DEDICATED
	CL_Disconnect();

	// recreate world if needs
	CL_ClearEdicts ();
#endif

	// release all models
	Mod_FreeAll();

	if( abort ) Host_AbortCurrentFrame ();
}

/*
==================
Host_CalcSleep
==================
*/
static int Host_CalcSleep( void )
{
	if( Host_IsDedicated( ))
	{
		// let the dedicated server some sleep
		return host_sleeptime.value;
	}

	switch( host.status )
	{
	case HOST_NOFOCUS:
		if( SV_Active() && CL_IsInGame())
			return host_sleeptime.value;
		// fallthrough
	case HOST_SLEEP:
		return 20;
	}

	return host_sleeptime.value;
}

static void Host_NewInstance( const char *name, const char *finalmsg )
{
	if( !pChangeGame ) return;

	host.change_game = true;

	if( !Sys_NewInstance( name, finalmsg ))
		pChangeGame( name ); // call from hl.exe
}

/*
=================
Host_ChangeGame_f

Change game modification
=================
*/
static void Host_ChangeGame_f( void )
{
	int	i;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "game <directory>\n" );
		return;
	}

	// validate gamedir
	for( i = 0; i < FI->numgames; i++ )
	{
		if( !Q_stricmp( FI->games[i]->gamefolder, Cmd_Argv( 1 )))
			break;
	}

	if( i == FI->numgames )
	{
		Con_Printf( "%s not exist\n", Cmd_Argv( 1 ));
	}
	else if( !Q_stricmp( GI->gamefolder, Cmd_Argv( 1 )))
	{
		Con_Printf( "%s already active\n", Cmd_Argv( 1 ));
	}
	else
	{
		char finalmsg[MAX_VA_STRING];

		Q_snprintf( finalmsg, sizeof( finalmsg ), "change game to '%s'", FI->games[i]->title );
		Host_NewInstance( Cmd_Argv( 1 ), finalmsg );
	}
}

/*
===============
Host_Exec_f
===============
*/
static void Host_Exec_f( void )
{
	string cfgpath;
	byte *f;
	fs_offset_t len;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "exec <filename>\n" );
		return;
	}

	Q_strncpy( cfgpath, Cmd_Argv( 1 ), sizeof( cfgpath ));
	COM_DefaultExtension( cfgpath, ".cfg", sizeof( cfgpath )); // append as default

#ifndef XASH_DEDICATED
	if( !Cmd_CurrentCommandIsPrivileged() )
	{
		const char *unprivilegedWhitelist[] =
		{
			NULL, "mapdefault.cfg", "scout.cfg", "sniper.cfg",
			"soldier.cfg", "demoman.cfg", "medic.cfg", "hwguy.cfg",
			"pyro.cfg", "spy.cfg", "engineer.cfg", "civilian.cfg"
		};
		int i;
		char temp[MAX_VA_STRING];
		qboolean allow = false;

		Q_snprintf( temp, sizeof( temp ), "%s.cfg", clgame.mapname );
		unprivilegedWhitelist[0] = temp;

		for( i = 0; i < ARRAYSIZE( unprivilegedWhitelist ); i++ )
		{
			if( !Q_strcmp( cfgpath, unprivilegedWhitelist[i] ))
			{
				allow = true;
				break;
			}
		}

		if( !allow )
		{
			Con_Printf( "exec %s: not privileged or in whitelist\n", cfgpath );
			return;
		}
	}
#endif // XASH_DEDICATED

	// don't execute game.cfg in singleplayer
	if( SV_GetMaxClients() == 1 && !Q_stricmp( "game.cfg", cfgpath ))
		return;

	f = FS_LoadFile( cfgpath, &len, false );
	if( !f )
	{
		Con_Reportf( "couldn't exec %s\n", Cmd_Argv( 1 ));
		return;
	}

	// len is fs_offset_t, which can be larger than size_t
	if( len >= SIZE_MAX )
	{
		Con_Reportf( "%s: %s is too long\n", __func__, Cmd_Argv( 1 ));
		return;
	}

	if( !Q_stricmp( "config.cfg", cfgpath ))
		host.config_executed = true;

	if( !host.apply_game_config )
		Con_Printf( "execing %s\n", Cmd_Argv( 1 ));

	// adds \n at end of the file
	// FS_LoadFile always null terminates
	if( f[len - 1] != '\n' )
	{
		Cbuf_InsertTextLen( f, len, len + 1 );
		Cbuf_InsertTextLen( "\n", 1, 1 );
	}
	else Cbuf_InsertTextLen( f, len, len );

	Mem_Free( f );
}

/*
===============
Host_MemStats_f
===============
*/
static void Host_MemStats_f( void )
{
	switch( Cmd_Argc( ))
	{
	case 1:
		Mem_PrintList( 1<<30 );
		Mem_PrintStats();
		break;
	case 2:
		Mem_PrintList( Q_atoi( Cmd_Argv( 1 )) * 1024 );
		Mem_PrintStats();
		break;
	default:
		Con_Printf( S_USAGE "memlist <all>\n" );
		break;
	}
}

static void Host_Minimize_f( void )
{
#ifdef XASH_SDL
	if( host.hWnd ) SDL_MinimizeWindow( host.hWnd );
#endif
}

/*
=================
Host_RegisterDecal
=================
*/
static qboolean Host_RegisterDecal( const char *name, int *count )
{
	char	shortname[MAX_QPATH];
	int	i;

	if( !COM_CheckString( name ))
		return 0;

	COM_FileBase( name, shortname, sizeof( shortname ));

	for( i = 1; i < MAX_DECALS && host.draw_decals[i][0]; i++ )
	{
		if( !Q_stricmp( host.draw_decals[i], shortname ))
			return true;
	}

	if( i == MAX_DECALS )
	{
		Con_DPrintf( S_ERROR "MAX_DECALS limit exceeded (%d)\n", MAX_DECALS );
		return false;
	}

	// register new decal
	Q_strncpy( host.draw_decals[i], shortname, sizeof( host.draw_decals[i] ));
	*count += 1;

	return true;
}

/*
=================
Host_InitDecals
=================
*/
static void Host_InitDecals( void )
{
	int	i, num_decals = 0;
	search_t	*t;

	memset( host.draw_decals, 0, sizeof( host.draw_decals ));

	// lookup all the decals in decals.wad (basedir, gamedir, falldir)
	t = FS_Search( "decals.wad/*.*", true, false );

	for( i = 0; t && i < t->numfilenames; i++ )
	{
		if( !Host_RegisterDecal( t->filenames[i], &num_decals ))
			break;
	}

	if( t ) Mem_Free( t );
	Con_Reportf( "%s: %i decals\n", __func__, num_decals );
}

/*
===================
Host_GetCommands

Add them exactly as if they had been typed at the console
===================
*/
static void Host_GetCommands( void )
{
	char	*cmd;

	while( ( cmd = Platform_Input() ) )
	{
		Cbuf_AddText( cmd );
		Cbuf_Execute();
	}
}

/*
===================
Host_CalcFPS

compute actual FPS for various modes
===================
*/
static double Host_CalcFPS( void )
{
	double	fps = 0.0;

	if( Host_IsDedicated( ))
	{
		fps = sys_ticrate.value;
	}
#if !XASH_DEDICATED
	else if( CL_IsPlaybackDemo() || CL_IsRecordDemo( )) // NOTE: we should play demos with same fps as it was recorded
	{
		fps = CL_GetDemoFramerate();
	}
	else if( Host_IsLocalGame( ))
	{
		if( !gl_vsync.value )
			fps = host_maxfps.value;
	}
	else if( !SV_Active() && CL_Protocol() == PROTO_GOLDSRC && cls.state != ca_disconnected && cls.state < ca_validate )
	{
		return 31.0;
	}
	else
	{
		if( !gl_vsync.value )
		{
			double max_fps = fps_override.value ? MAX_FPS_HARD : MAX_FPS_SOFT;

			fps = host_maxfps.value;
			if( fps == 0.0 ) fps = max_fps;
			fps = bound( MIN_FPS, fps, max_fps );
		}
	}
#endif

	return fps;
}

static qboolean Host_Autosleep( double dt, double scale )
{
	double targetframetime, fps;
	int sleep;

	fps = Host_CalcFPS();

	if( fps <= 0 )
		return true;

	// limit fps to withing tolerable range
	fps = bound( MIN_FPS, fps, MAX_FPS_HARD );

	if( Host_IsDedicated( ))
		targetframetime = ( 1.0 / ( fps + 1.0 ));
	else targetframetime = ( 1.0 / fps );

	sleep = Host_CalcSleep();
	if( sleep == 0 ) // no sleeps between frames, much simpler code
	{
		if( dt < targetframetime * scale )
			return false;
	}
	else
	{
		static double timewindow; // allocate a time window for sleeps
		static int counter; // for debug
		static double realsleeptime;
		const double sleeptime = sleep * 0.000001;

		if( dt < targetframetime * scale )
		{
			// if we have allocated time window, try to sleep
			if( timewindow > realsleeptime )
			{
				// Platform_Sleep isn't guaranteed to sleep an exact amount of microseconds
				// so we measure the real sleep time and use it to decrease the window
				double t1 = Sys_DoubleTime(), t2;
				Platform_NanoSleep( sleep * 1000 ); // in usec!
				t2 = Sys_DoubleTime();
				realsleeptime = t2 - t1;

				timewindow -= realsleeptime;

				if( host_sleeptime_debug.value )
				{
					counter++;

					Con_NPrintf( counter, "%d: %.4f %.4f", counter, timewindow, realsleeptime );
				}
			}

			return false;
		}

		// if we exhausted this time window, allocate a new one after new frame
		if( timewindow <= realsleeptime )
		{
			double targetsleeptime = targetframetime - host.pureframetime * 2;

			if( targetsleeptime > 0 )
				timewindow = targetsleeptime;
			else timewindow = 0;

			realsleeptime = sleeptime; // reset in case CPU was too busy

			if( host_sleeptime_debug.value )
			{
				counter = 0;

				Con_NPrintf( 0, "tgt = %.4f, pft = %.4f, wnd = %.4f", targetframetime, host.pureframetime, timewindow );
			}
		}
	}

	return true;
}

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
static qboolean Host_FilterTime( double time )
{
	static double	oldtime;
	double dt;
	double scale = sys_timescale.value;

	host.realtime += time * scale;
	dt = host.realtime - oldtime;

	// clamp the fps in multiplayer games
	if( !Host_Autosleep( dt, scale ))
		return false;

	host.frametime = host.realtime - oldtime;
	host.realframetime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );
	oldtime = host.realtime;

	// NOTE: allow only in singleplayer while demos are not active
	if( host_framerate.value > 0.0f && Host_IsLocalGame() && !CL_IsPlaybackDemo() && !CL_IsRecordDemo( ))
		host.frametime = bound( MIN_FRAMETIME, host_framerate.value * scale, MAX_FRAMETIME );
	else host.frametime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );

	return true;
}

/*
=================
Host_Frame
=================
*/
void Host_Frame( double time )
{
	//Prepare VR frame
	static bool firstFrame = true;
	engine_t* engine = VR_GetEngine();
	if (firstFrame) {
		VR_EnterVR(engine);
		IN_VRInit(engine);
		firstFrame = false;
	}
	if (!VR_GetConfig(VR_CONFIG_VIEWPORT_VALID)) {
		VR_InitRenderer(engine, false);
		VR_SetConfigFloat(VR_CONFIG_CANVAS_ASPECT, 1);
		VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, 5);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}
	bool gameMode = !host.mouse_visible && cls.state == ca_active && cls.key_dest == key_game;
	VR_SetConfig(VR_CONFIG_MODE, gameMode ? VR_MODE_STEREO_6DOF : VR_MODE_MONO_SCREEN);
	Cvar_LazySet("vr_gamemode", gameMode ? 1 : 0);

	double t1;

	// decide the simulation time
	if( !Host_FilterTime( time ))
		return;

	t1 = Sys_DoubleTime();

	if (!VR_InitFrame(engine)) {
		return;
	}

	if( host.framecount == 0 )
		Con_DPrintf( "Time to first frame: %.3f seconds\n", t1 - host.starttime );

	//Host_InputFrame ();  // input frame
	Host_VRInput ();  // VR input
	Host_ClientBegin (); // begin client
	Host_GetCommands (); // dedicated in
	Host_ServerFrame (); // server frame

	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		Cvar_SetValue("vr_stereo_side", eye);
		VR_BeginFrame(engine, eye);
		Host_ClientFrame (); // client frame
		VR_EndFrame(engine, eye);
	}
	VR_FinishFrame(engine);

	HTTP_Run();			 // both server and client

	host.framecount++;
	host.pureframetime = Sys_DoubleTime() - t1;
}

/*
=================
Host_Error
=================
*/
void GAME_EXPORT Host_Error( const char *error, ... )
{
	static char	hosterror1[MAX_SYSPATH];
	static char	hosterror2[MAX_SYSPATH];
	static qboolean	recursive = false;
	va_list		argptr;

	va_start( argptr, error );
	Q_vsnprintf( hosterror1, sizeof( hosterror1 ), error, argptr );
	va_end( argptr );

	CL_WriteMessageHistory (); // before Q_error call

	if( host.framecount < 3 )
	{
		Sys_Error( "%sInit: %s", __func__, hosterror1 );
	}
	else if( host.framecount == host.errorframe )
	{
		Sys_Error( "%sMulti: %s", __func__, hosterror2 );
	}
	else
	{
		Con_Printf( "%s: %s", __func__, hosterror1 );
		if( host.allow_console )
		{
			UI_SetActiveMenu( false );
			Key_SetKeyDest( key_console );
		}
		else Platform_MessageBox( "Host Error", hosterror1, true );
	}

	// host is shutting down. don't invoke infinite loop
	if( host.status == HOST_SHUTDOWN ) return;

	if( recursive )
	{
		Con_Printf( "%sRecursive: %s", __func__, hosterror2 );
		Sys_Error( "%s", hosterror1 );
	}

	recursive = true;
	Q_strncpy( hosterror2, hosterror1, sizeof( hosterror2 ));
	host.errorframe = host.framecount; // to avoid multply calls per frame

	// clearing cmd buffer to prevent execute any commands
	COM_InitHostState();
	Cbuf_Clear();

	SV_Shutdown( "Server was killed due to an error\n" );
	CL_Drop(); // drop clients

	// recreate world if needs
	CL_ClearEdicts ();

	// release all models
	Mod_FreeAll();

	recursive = false;
	Host_AbortCurrentFrame();
}

static void Host_Error_f( void )
{
	const char *error = Cmd_Argv( 1 );

	if( !*error ) error = "Invoked host error";
	Host_Error( "%s\n", error );
}

static void Sys_Error_f( void )
{
	const char *error = Cmd_Argv( 1 );

	if( !*error ) error = "Invoked sys error";
	Sys_Error( "%s\n", error );
}

/*
=================
Host_Crash_f
=================
*/
static void Host_Crash_f( void )
{
	*(volatile int *)0 = 0xffffffff;
}

/*
=================
Host_Userconfigd_f
=================
*/
static void Host_Userconfigd_f( void )
{
	search_t *t;
	int i;

	t = FS_Search( "userconfig.d/*.cfg", true, false );
	if( !t ) return;

	for( i = 0; i < t->numfilenames; i++ )
	{
		Cbuf_AddTextf( "exec %s\n", t->filenames[i] );
	}

	Mem_Free( t );
}

#if XASH_ENGINE_TESTS
static void Host_RunTests( int stage )
{
	switch( stage )
	{
	case 0: // early engine load
		memset( &tests_stats, 0, sizeof( tests_stats ));
		TEST_LIST_0;
#if !XASH_DEDICATED
		TEST_LIST_0_CLIENT;
#endif /* XASH_DEDICATED */
		break;
	case 1: // after FS load
		TEST_LIST_1;
#if !XASH_DEDICATED
		TEST_LIST_1_CLIENT;
#endif
		Msg( "Done! %d passed, %d failed\n", tests_stats.passed, tests_stats.failed );
		error_on_exit = tests_stats.failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
		Sys_Quit( NULL );
	}
}
#endif

static int Host_CheckBugcomp_splitstr_handler( char *prev, char *next, void *userdata )
{
	size_t i;
	uint32_t *flags = userdata;

	*next = '\0';

	if( !COM_CheckStringEmpty( prev ))
		return 0;

	for( i = 0; i < ARRAYSIZE( bugcomp_features ); i++ )
	{
		if( !Q_stricmp( bugcomp_features[i].arg, prev ))
		{
			SetBits( *flags, bugcomp_features[i].mask );
			break;
		}
	}

	if( i == ARRAYSIZE( bugcomp_features ))
	{
		Con_Printf( S_ERROR "Unknown bugcomp flag %s\n", prev );
		Con_Printf( "Valid flags are:\n" );
		for( i = 0; i < ARRAYSIZE( bugcomp_features ); i++ )
			Con_Printf( "\t%s: %s\n", bugcomp_features[i].arg, bugcomp_features[i].msg );
	}

	return 0;
}

static uint32_t Host_CheckBugcomp( void )
{
	uint32_t flags = 0;
	string args;

	if( !Sys_CheckParm( "-bugcomp" ))
		return 0;

	if( Sys_GetParmFromCmdLine( "-bugcomp", args ) && isalpha( args[0] ))
	{
		Q_splitstr( args, '+', &flags, Host_CheckBugcomp_splitstr_handler );
	}
	else
	{
		// no argument specified -bugcomp just enables everything
		flags = -1;
	}

	Host_PrintFeatures( flags, "BUGCOMP", bugcomp_features, ARRAYSIZE( bugcomp_features ));

	return flags;
}

static void Host_DetermineExecutableName( char *out, size_t size )
{
#if XASH_WIN32
	char temp[MAX_SYSPATH];

	if( GetModuleFileName( NULL, temp, sizeof( temp )))
		COM_FileBase( temp, out, size );
#else
	if( host.argc > 0 )
		COM_FileBase( host.argv[0], out, size );
	else
		Q_strncpy( out, "xash", size );
#endif
}

/*
=================
Host_InitCommon
=================
*/
static void Host_InitCommon( int argc, char **argv, const char *progname, qboolean bChangeGame, char *exename, size_t exename_size )
{
	const char *basedir = progname[0] == '#' ? progname + 1 : progname;
	char dev_level[4], ticrate[16];
	int developer = DEFAULT_DEV;

	// some commands may turn engine into infinite loop,
	// e.g. xash.exe +game xash -game xash
	// so we clear all cmd_args, but leave dbg states as well
	Sys_ParseCommandLine( argc, argv );
	Host_DetermineExecutableName( exename, exename_size );

	if( !Sys_CheckParm( "-disablehelp" ))
	{
		string arg;

		if( Sys_CheckParm( "-help" ) || Sys_CheckParm( "-h" ) || Sys_CheckParm( "--help" ))
			Sys_PrintUsage( exename );

		if( Sys_GetParmFromCmdLine( "-bugcomp", arg ) && !Q_stricmp( arg, "help" ))
			Sys_PrintBugcompUsage( exename );
	}

	if( !Sys_CheckParm( "-noch" ))
		Sys_SetupCrashHandler( argv[0] );

#if XASH_DLL_LOADER
	host.enabledll = !Sys_CheckParm( "-nodll" );
#endif

	host.change_game = bChangeGame || Sys_CheckParm( "-changegame" );
	host.config_executed = false;
	host.status = HOST_INIT; // initialzation started
	host.type = HOST_DEDICATED; // predict state
#ifndef XASH_DEDICATED
	if( !Sys_CheckParm( "-dedicated" ))
		host.type = HOST_NORMAL;
#endif

	Memory_Init(); // init memory subsystem

	host.mempool = Mem_AllocPool( "Zone Engine" );

	host.allow_console = DEFAULT_ALLOWCONSOLE;

	if( Sys_CheckParm( "-dev" ))
	{
		host.allow_console = true;
		developer = DEV_NORMAL;

		if( Sys_GetParmFromCmdLine( "-dev", dev_level ))
		{
			if( Q_isdigit( dev_level ))
				developer = bound( DEV_NONE, abs( Q_atoi( dev_level )), DEV_EXTENDED );
		}
	}

#if XASH_ENGINE_TESTS
	if( Sys_CheckParm( "-runtests" ))
	{
		host.allow_console = true;
		developer = DEV_EXTENDED;
	}
#endif

	// always enable console for Quake and dedicated
	if( !host.allow_console && ( Host_IsDedicated() || Sys_CheckParm( "-console" ) || !Q_strnicmp( exename, "quake", 5 )))
		host.allow_console = true;

	// member console allowing
	host.allow_console_init = host.allow_console;

	// timeBeginPeriod( 1 ); // a1ba: Do we need this?

	// NOTE: this message couldn't be passed into game console but it doesn't matter
//	Con_Reportf( "Sys_LoadLibrary: Loading xash.dll - ok\n" );

	// get default screen res
	VID_InitDefaultResolution();

	// init host state machine
	COM_InitHostState();

	// init hashed commands
	BaseCmd_Init();

	// startup cmds and cvars subsystem
	Cmd_Init();
	Cvar_Init();

	// share developer level across all dlls
	Q_snprintf( dev_level, sizeof( dev_level ), "%i", developer );
	Cvar_DirectSet( &host_developer, dev_level );
	Cvar_RegisterVariable( &sys_ticrate );

	if( Sys_GetParmFromCmdLine( "-sys_ticrate", ticrate ))
	{
		double fps = bound( MIN_FPS, atof( ticrate ), MAX_FPS_HARD );
		Cvar_SetValue( "sys_ticrate", fps );
	}

	Con_Init(); // early console running to catch all the messages

#if XASH_ENGINE_TESTS
	if( Sys_CheckParm( "-runtests" ))
		Host_RunTests( 0 );
#endif

#if XASH_DEDICATED
	Platform_SetupSigtermHandling();
#endif
	Platform_Init( Host_IsDedicated( ) || developer >= DEV_EXTENDED, basedir );
	FS_Init( basedir );

	Sys_InitLog();

	// print current developer level to simplify processing users feedback
	if( developer > 0 )
	{
		int i;

		Con_Printf( "Program args: " S_YELLOW );
		for( i = 0; i < host.argc; i++ )
			Con_Printf( "%s ", host.argv[i] );
		Con_Printf( S_DEFAULT "\n" );

		Con_Printf( "Developer level: " S_YELLOW "%i" S_DEFAULT "\n", developer );
	}

	host.bugcomp = Host_CheckBugcomp();

	Cmd_AddCommand( "exec", Host_Exec_f, "execute a script file" );
	Cmd_AddCommand( "memlist", Host_MemStats_f, "prints memory pool information" );
	Cmd_AddRestrictedCommand( "userconfigd", Host_Userconfigd_f, "execute all scripts from userconfig.d" );

	Image_Init();
	Sound_Init();

#if XASH_ENGINE_TESTS
	if( Sys_CheckParm( "-runtests" ))
		Host_RunTests( 1 );
#endif

	FS_LoadGameInfo( NULL );
	Cvar_PostFSInit();

	Image_CheckPaletteQ1 ();

	// NOTE: only once resource without which engine can't continue work
	if( !FS_FileExists( "gfx/conchars", false ))
		Sys_Error( "%s: couldn't load gfx.wad\n", __func__ );

	Host_InitDecals ();	// reload decals

	HPAK_Init();

	IN_Init();
	Key_Init();

	Host_VRInit();
}

static void Host_FreeCommon( void )
{
	Image_Shutdown();
	Sound_Shutdown();
	Netchan_Shutdown();
	HPAK_FlushHostQueue();
	FS_Shutdown();
}

static void Sys_Quit_f( void )
{
	Sys_Quit( "command" );
}

/*
=================
Host_Main
=================
*/
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func )
{
	static double	oldtime, newtime;
	string demoname, exename;

	host.starttime = Sys_DoubleTime();

	pChangeGame = func;	// may be NULL

	Host_InitCommon( argc, argv, progname, bChangeGame, exename, sizeof( exename ));

	// init commands and vars
	if( host_developer.value >= DEV_EXTENDED )
	{
		Cmd_AddRestrictedCommand ( "sys_error", Sys_Error_f, "just throw a fatal error to test shutdown procedures");
		Cmd_AddRestrictedCommand ( "host_error", Host_Error_f, "just throw a host error to test shutdown procedures");
		Cmd_AddRestrictedCommand ( "crash", Host_Crash_f, "a way to force a bus error for development reasons");
	}

	Cvar_RegisterVariable( &host_allow_materials );
	Cvar_RegisterVariable( &host_serverstate );
	Cvar_RegisterVariable( &host_maxfps );
	Cvar_RegisterVariable( &fps_override );
	Cvar_RegisterVariable( &host_framerate );
	Cvar_RegisterVariable( &host_sleeptime );
	Cvar_RegisterVariable( &host_sleeptime_debug );
	Cvar_RegisterVariable( &host_gameloaded );
	Cvar_RegisterVariable( &host_clientloaded );
	Cvar_RegisterVariable( &host_limitlocal );
	Cvar_RegisterVariable( &con_gamemaps );
	Cvar_RegisterVariable( &sys_timescale );

	Cvar_Getf( "buildnum", FCVAR_READ_ONLY, "returns a current build number", "%i", Q_buildnum_compat());
	Cvar_Getf( "ver", FCVAR_READ_ONLY, "shows an engine version", "%i/%s (hw build %i)", PROTOCOL_VERSION, XASH_COMPAT_VERSION, Q_buildnum_compat());
	Cvar_Getf( "host_ver", FCVAR_READ_ONLY, "detailed info about this build", "%i " XASH_VERSION " %s %s %s", Q_buildnum(), Q_buildos(), Q_buildarch(), g_buildcommit);
	Cvar_Getf( "host_lowmemorymode", FCVAR_READ_ONLY, "indicates if engine compiled for low RAM consumption (0 - normal, 1 - low engine limits, 2 - low protocol limits)", "%i", XASH_LOW_MEMORY );

	Mod_Init();
	NET_Init();
	NET_InitMasters();
	Netchan_Init();

	// allow to change game from the console
	if( pChangeGame != NULL )
	{
		Cmd_AddRestrictedCommand( "game", Host_ChangeGame_f, "change game" );
		Cvar_Get( "host_allow_changegame", "1", FCVAR_READ_ONLY, "allows to change games" );
	}
	else
	{
		Cvar_Get( "host_allow_changegame", "0", FCVAR_READ_ONLY, "allows to change games" );
	}

	SV_Init();
	CL_Init();

	HTTP_Init();
	SoundList_Init();

	if( Host_IsDedicated( ))
	{
#ifdef _WIN32
		Wcon_InitConsoleCommands ();
#endif

		// disable texture replacements for dedicated
		Cvar_FullSet( "host_allow_materials", "0", FCVAR_READ_ONLY );

		Cmd_AddRestrictedCommand( "quit", Sys_Quit_f, "quit the game" );
		Cmd_AddRestrictedCommand( "exit", Sys_Quit_f, "quit the game" );
	}
	else Cmd_AddRestrictedCommand( "minimize", Host_Minimize_f, "minimize main window to tray" );

	host.errorframe = 0;

	if( progname[0] == '#' )
		progname++;

	// post initializations
	switch( host.type )
	{
	case HOST_NORMAL:
#ifdef _WIN32
		Wcon_ShowConsole( false ); // hide console
#endif
		// execute startup config and cmdline
		if( FS_FileExists( va( "%s.rc", progname ), false )) // e.g. valve.rc
			Cbuf_AddTextf( "exec %s.rc", progname );
		else if( FS_FileExists( va( "%s.rc", exename ), false )) // e.g. quake.rc
			Cbuf_AddTextf( "exec %s.rc", exename );
		else if( FS_FileExists( va( "%s.rc", GI->gamefolder ), false )) // e.g. game.rc (ran from default launcher)
			Cbuf_AddTextf( "exec %s.rc", GI->gamefolder );
		Cbuf_Execute();

		if( !host.config_executed )
		{
			Cbuf_AddText( "exec config.cfg\n" );
			Cbuf_Execute();
		}
		// exec all files from userconfig.d
		Host_Userconfigd_f();
		break;
	case HOST_DEDICATED:
		// allways parse commandline in dedicated-mode
		host.stuffcmds_pending = true;
		break;
	}

	host.change_game = false;	// done
	Cmd_RemoveCommand( "setgl" );
	Cbuf_ExecStuffCmds();	// execute stuffcmds (commandline)
	SCR_CheckStartupVids();	// must be last
	FS_CheckConfig();

	if( Sys_GetParmFromCmdLine( "-timedemo", demoname ))
		Cbuf_AddTextf( "timedemo %s\n", demoname );

	oldtime = Sys_DoubleTime() - 0.1;

	if( Host_IsDedicated( ))
	{
		// in dedicated server input system can't set HOST_FRAME status
		// so set it here as we're finished initializing
		host.status = HOST_FRAME;

		if( GameState->nextstate == STATE_RUNFRAME )
#if XASH_WIN32 // FIXME: implement autocomplete on *nix
			Con_Printf( "Type 'map <mapname>' to start game... (TAB-autocomplete is working too)\n" );
#else // !XASH_WIN32
			Con_Printf( "Type 'map <mapname>' to start game...\n" );
#endif // !XASH_WIN32

		// execute server.cfg after commandline
		// so we have a chance to set servercfgfile
		Cbuf_AddTextf( "exec %s\n", Cvar_VariableString( "servercfgfile" ));
		Cbuf_Execute();
	}

	// check after all configs were executed
	HPAK_CheckIntegrity( hpk_custom_file.string );

#if XASH_ANDROID
	if( setjmp( return_from_main_buf ))
		return error_on_exit;
#endif // XASH_ANDROID

	// main window message loop
	while( !host.crashed )
	{
		newtime = Sys_DoubleTime ();
		COM_Frame( newtime - oldtime );
		oldtime = newtime;
	}

	// never reached
	return 0;
}

void EXPORT Host_Shutdown( void );
void EXPORT Host_Shutdown( void )
{
	Host_ShutdownWithReason( "launcher shutdown" );
}

/*
=================
Host_Shutdown
=================
*/
void Host_ShutdownWithReason( const char *reason )
{
	qboolean error = host.status == HOST_ERR_FATAL;

	if( host.shutdown_issued )
		return;

	host.shutdown_issued = true;

	if( reason != NULL )
		Con_Printf( S_NOTE "Issuing host shutdown due to reason \"%s\"\n", reason );

	if( host.status != HOST_ERR_FATAL )
		host.status = HOST_SHUTDOWN; // prepare host to normal shutdown

#if !XASH_DEDICATED
	if( host.type == HOST_NORMAL && !error )
		Host_WriteConfig();
#endif

	SV_Shutdown( "Server shutdown\n" );
	SV_UnloadProgs();
	SV_ShutdownFilter();
	CL_Shutdown();

	SoundList_Shutdown();
	Mod_Shutdown();
	NET_Shutdown();
	HTTP_Shutdown();
	Host_FreeCommon();
	Platform_Shutdown();

	BaseCmd_Shutdown();
	Cmd_Shutdown();
	Cvar_Shutdown();

	// must be last, console uses this
	Mem_FreePool( &host.mempool );

	// restore filter
	Sys_RestoreCrashHandler();
	Sys_CloseLog( reason );
}

void Host_VRInit( void )
{
	Cvar_RegisterVariable( &vr_camera_x );
	Cvar_RegisterVariable( &vr_camera_y );
	Cvar_RegisterVariable( &vr_camera_z );
	Cvar_RegisterVariable( &vr_gamemode );
	Cvar_RegisterVariable( &vr_hmd_pitch );
	Cvar_RegisterVariable( &vr_hmd_yaw );
	Cvar_RegisterVariable( &vr_hmd_roll );
	Cvar_RegisterVariable( &vr_offset_x );
	Cvar_RegisterVariable( &vr_offset_y );
	Cvar_RegisterVariable( &vr_player_dir_x );
	Cvar_RegisterVariable( &vr_player_dir_y );
	Cvar_RegisterVariable( &vr_player_dir_z );
	Cvar_RegisterVariable( &vr_player_pos_x );
	Cvar_RegisterVariable( &vr_player_pos_y );
	Cvar_RegisterVariable( &vr_player_pos_z );
	Cvar_RegisterVariable( &vr_player_pitch );
	Cvar_RegisterVariable( &vr_player_yaw );
	Cvar_RegisterVariable( &vr_stereo_side );
	Cvar_RegisterVariable( &vr_thumbstick_deadzone_left );
	Cvar_RegisterVariable( &vr_thumbstick_deadzone_right );
	Cvar_RegisterVariable( &vr_thumbstick_snapturn );
	Cvar_RegisterVariable( &vr_weapon_roll );
	Cvar_RegisterVariable( &vr_weapon_x );
	Cvar_RegisterVariable( &vr_weapon_y );
	Cvar_RegisterVariable( &vr_weapon_z );
	Cvar_RegisterVariable( &vr_worldscale );
	Cvar_RegisterVariable( &vr_xhair_x );
	Cvar_RegisterVariable( &vr_xhair_y );

	Cvar_RegisterVariable( &vr_6dof );
	Cvar_RegisterVariable( &vr_button_a );
	Cvar_RegisterVariable( &vr_button_b );
	Cvar_RegisterVariable( &vr_button_x );
	Cvar_RegisterVariable( &vr_button_y );
	Cvar_RegisterVariable( &vr_button_grip_left );
	Cvar_RegisterVariable( &vr_button_joystick_left );
	Cvar_RegisterVariable( &vr_button_trigger_left );
	Cvar_RegisterVariable( &vr_button_grip_right );
	Cvar_RegisterVariable( &vr_button_joystick_right );
	Cvar_RegisterVariable( &vr_button_trigger_right );
	Cvar_RegisterVariable( &vr_zoomed );
}

void Host_VRInput( void )
{
	// Get VR input
	bool rightHanded = Cvar_VariableValue("cl_righthand") > 0;
	int primaryController = rightHanded ? 1 : 0;
	int secondaryController = rightHanded ? 0 : 1;
	XrPosef hmd = VR_GetView(0);
	XrPosef pose = IN_VRGetPose(primaryController);
	XrVector3f angles = XrQuaternionf_ToEulerAngles(pose.orientation);
	bool cursorActive = IN_VRIsActive(primaryController);
	int lbuttons = IN_VRGetButtonState(secondaryController);
	int rbuttons = IN_VRGetButtonState(primaryController);
	XrVector2f left = IN_VRGetJoystickState(secondaryController);
	XrVector2f right = IN_VRGetJoystickState(primaryController);

	// Get euler angles
	bool zoomed = Cvar_VariableValue("vr_zoomed") > 0;
	XrVector3f euler = XrQuaternionf_ToEulerAngles(zoomed ? hmd.orientation : pose.orientation);
	XrVector3f hmdEuler = XrQuaternionf_ToEulerAngles(hmd.orientation);
	vec3_t hmdAngles = {hmdEuler.x, hmdEuler.y, hmdEuler.z};
	vec3_t weaponAngles = {euler.x, euler.y, euler.z};
	vec3_t weaponPosition = {pose.position.x, pose.position.y, pose.position.z};
	vec3_t hmdPosition = {hmd.position.x, hmd.position.y, hmd.position.z};

	// Menu control
	vec2_t cursor = {};
	bool gameMode = Host_VRConfig();
	Host_VRCursor(cursorActive, angles.x, angles.y, cursor);
	bool pressedInUI = Host_VRMenuInput(cursorActive, gameMode, !rightHanded, lbuttons, rbuttons, cursor);

	// Do not pass button actions which started in UI
	if (gameMode && pressedInUI) {
		lbuttons = 0;
		rbuttons = 0;
	}

	// In-game input
	static float hmdAltitude = 0;
	if (gameMode) {
		Host_VRButtonMapping(!rightHanded, lbuttons, rbuttons, left.x, left.y);
		Host_VRWeaponCrosshair();
		Host_VRMovement(zoomed, hmdAltitude, hmdAngles, hmdPosition, weaponAngles, weaponPosition);
		Host_VRRotations(zoomed, hmdAngles, weaponAngles, right.x, right.y);
	} else {
		// Measure player when not in game mode
		hmdAltitude = hmd.position.y;

		// No game actions when UI is shown
		Host_VRButtonMapping(!rightHanded, 0, 0, 0, 0);
	}
}

void Host_VRButtonMap( int button, int currentButtons, int lastButtons, const char* action )
{
	// Detect if action should be called
	bool down = currentButtons & button;
	bool wasDown = lastButtons & button;
	bool process = false;
	bool invert = false;
	if (down && !wasDown) {
		process = true;
	} else if (!down && wasDown) {
		process = true;
		invert = true;
	}

	// Process commands separated by a semicolon
	if (process) {
		int index = 0;
		char command[256];
		for (int i = 0; i <= strlen(action); i++) {
			if ((action[i] == ';') || (action[i] == '\000')) {
				command[index++] = '\n';
				command[index] = '\000';
				if (invert) {
					if (command[0] == '+') {
						command[0] = '-';
						Host_VRCustomCommand( command );
					}
				} else {
					Host_VRCustomCommand( command );
				}
				index = 0;
			} else {
				command[index++] = action[i];
			}
		}
	}
}

void Host_VRButtonMapping( bool swapped, int lbuttons, int rbuttons, float thumbstickX, float thumbstickY )
{
	int leftPrimaryButton = swapped ? ovrButton_A : ovrButton_X;
	int leftSecondaryButton = swapped ? ovrButton_B : ovrButton_Y;
	int rightPrimaryButton = !swapped ? ovrButton_A : ovrButton_X;
	int rightSecondaryButton = !swapped ? ovrButton_B : ovrButton_Y;

	static int lastlbuttons = 0;
	Host_VRButtonMap(leftPrimaryButton, lbuttons, lastlbuttons, Cvar_VariableString("vr_button_x"));
	Host_VRButtonMap(leftSecondaryButton, lbuttons, lastlbuttons, Cvar_VariableString("vr_button_y"));
	Host_VRButtonMap(ovrButton_Trigger, lbuttons, lastlbuttons, Cvar_VariableString("vr_button_trigger_left"));
	Host_VRButtonMap(ovrButton_Joystick, lbuttons, lastlbuttons, Cvar_VariableString("vr_button_joystick_left"));
	Host_VRButtonMap(ovrButton_GripTrigger, lbuttons, lastlbuttons, Cvar_VariableString("vr_button_grip_left"));
	lastlbuttons = lbuttons;
	static int lastrbuttons = 0;
	Host_VRButtonMap(rightPrimaryButton, rbuttons, lastrbuttons, Cvar_VariableString("vr_button_a"));
	Host_VRButtonMap(rightSecondaryButton, rbuttons, lastrbuttons, Cvar_VariableString("vr_button_b"));
	Host_VRButtonMap(ovrButton_Trigger, rbuttons, lastrbuttons, Cvar_VariableString("vr_button_trigger_right"));
	Host_VRButtonMap(ovrButton_Joystick, rbuttons, lastrbuttons, Cvar_VariableString("vr_button_joystick_right"));
	Host_VRButtonMap(ovrButton_GripTrigger, rbuttons, lastrbuttons, Cvar_VariableString("vr_button_grip_right"));
	lastrbuttons = rbuttons;

	// Thumbstick movement
	float deadzone = Cvar_VariableValue("vr_thumbstick_deadzone_left");
	if (fabs(thumbstickX) < deadzone) thumbstickX = 0;
	if (fabs(thumbstickY) < deadzone) thumbstickY = 0;
	clgame.dllFuncs.pfnMoveEvent( thumbstickY, thumbstickX );
}

bool Host_VRConfig()
{
	// Ensure VR compatible layout is used
	bool gameMode = Cvar_VariableValue("vr_gamemode") > 0.5f;
	Cvar_LazySet("con_fontscale", gameMode ? 1.5f : 1.0f);
	Cvar_LazySet("hud_scale", 2);
	Cvar_LazySet("touch_enable", 0);
	Cvar_LazySet("xhair_enable", 1);

	// Ensure voice input is enabled
	Cvar_LazySet("sv_voicequality", 5);
	Cvar_LazySet("voice_inputfromfile", 1);
	Cvar_LazySet("voice_scale", 5);

	return gameMode;
}

void Host_VRCursor( bool cursorActive, float x, float y, vec2_t cursor )
{
	// Calculate cursor position
	float width = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_WIDTH);
	float height = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_HEIGHT);
	float supersampling = VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);
	float cx = width / 2;
	float cy = height / 2;
	float speed = (cx + cy) / 2;
	float mx = cx - tan(ToRadians(y - VR_GetConfigFloat(VR_CONFIG_MENU_YAW))) * speed;
	float my = cy + tan(ToRadians(x)) * speed * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
	cursor[0] = supersampling > 0.1f ? mx * supersampling : mx;
	cursor[1] = supersampling > 0.1f ? my * supersampling : my;

	// Show cursor
	VR_SetConfig(VR_CONFIG_MOUSE_X, cursor[0]);
	VR_SetConfig(VR_CONFIG_MOUSE_Y, height - cursor[1]);
	VR_SetConfig(VR_CONFIG_MOUSE_SIZE, cursorActive ? 8 : 0);
}

void Host_VRCustomCommand( char* action )
{
	if (strcmp(action, "+vr_scoreboard\n") == 0) {
		Cbuf_AddText( "showscoreboard2 0.213333 0.835556 0.213333 0.835556 0 0 0 128\n" );
	} else if (strcmp(action, "-vr_scoreboard\n") == 0) {
		Cbuf_AddText( "hidescoreboard2\n" );
	} else {
		Cbuf_AddText( action );
	}
}

extern bool sdl_keyboard_requested;

bool Host_VRMenuInput( bool cursorActive, bool gameMode, bool swapped, int lbuttons, int rbuttons, vec2_t cursor )
{
	// Deactivate temporary input when client restored focus
	static struct timeval lastFocus;
	if (host.status != HOST_NOFOCUS) {
		gettimeofday(&lastFocus, NULL);
	}
	struct timeval currentTime;
	gettimeofday(&currentTime, NULL);

	// Get event type
	touchEventType t = event_motion;
	bool down = rbuttons & ovrButton_Trigger && (currentTime.tv_sec - lastFocus.tv_sec < 2);
	static bool pressedInUI = false;
	static bool lastDown = false;
	if (down && !lastDown) {
		t = event_down;
		if (!gameMode) {
			pressedInUI = true;
		}
	} else if (!down && lastDown) {
		t = event_up;
		pressedInUI = false;
	}
	lastDown = down;

	// Send the input event as a touch
	static float initialTouchX = 0;
	static float initialTouchY = 0;
	cursor[0] /= (float)refState.width;
	cursor[1] /= (float)refState.height;
	if (!gameMode && cursorActive) {
		IN_TouchEvent(t, 0, cursor[0], cursor[1], initialTouchX - cursor[0], initialTouchY - cursor[1]);
		if (t == event_up && sdl_keyboard_requested) {
			IN_TouchEvent(event_motion, 0, cursor[0], cursor[1], initialTouchX - cursor[0], initialTouchY - cursor[1]);
			sdl_keyboard_requested = false;
			SDL_StartTextInput();
		}
	}
	initialTouchX = cursor[0];
	initialTouchY = cursor[1];

	// Escape key
	int buttons = swapped ? rbuttons : lbuttons;
	bool escape = buttons & ovrButton_Enter;
	static bool lastEscape = false;
	if (escape && !lastEscape) {
		Key_Event(K_ESCAPE, true);
		Key_Event(K_ESCAPE, false);
	}
	lastEscape = escape;

	// Thumbstick close key
	bool thumbstick = lbuttons & ovrButton_Joystick;
	static bool lastThumbstick = false;
	if (thumbstick && !lastThumbstick) {
		Cbuf_AddText( "touch_setclientonly 0\n" );
		if (!gameMode) {
			pressedInUI = true;
		}
	} else if (!thumbstick && lastThumbstick) {
		pressedInUI = false;
	}
	lastThumbstick = thumbstick;

	return pressedInUI;
}

void Host_VRMovement( bool zoomed, float hmdAltitude, vec3_t hmdAngles, vec3_t hmdPosition, vec3_t weaponAngles, vec3_t weaponPosition )
{
	float scale = Cvar_VariableValue("vr_worldscale");

	// Recenter if player position changed way too much
	vec3_t currentPosition;
	static vec3_t lastPosition = {};
	currentPosition[0] = Cvar_VariableValue("vr_player_pos_x");
	currentPosition[1] = Cvar_VariableValue("vr_player_pos_y");
	currentPosition[2] = Cvar_VariableValue("vr_player_pos_z");
	if (VectorDistance(currentPosition, lastPosition) > scale) {
		VR_Recenter(VR_GetEngine());
	}
	VectorCopy(currentPosition, lastPosition);

	// Camera movement
	float hmdYaw = DEG2RAD(hmdAngles[YAW]);
	float dx = hmdPosition[0] * scale;
	float dz = hmdPosition[2] * scale;
	Cvar_SetValue("vr_camera_x", zoomed ? 0 : dx * cos(hmdYaw) - dz * sin(hmdYaw));
	Cvar_SetValue("vr_camera_y", zoomed ? 0 : dx * sin(hmdYaw) + dz * cos(hmdYaw));
	Cvar_SetValue("vr_camera_z", zoomed ? 0 : (hmdPosition[1] - hmdAltitude) * scale);

	// Weapon movement
	float weaponYaw = DEG2RAD(weaponAngles[YAW]);
	dx = (weaponPosition[0] - hmdPosition[0]) * scale;
	dz = (weaponPosition[2] - hmdPosition[2]) * scale;
	//TODO: revisit this (hmdYaw is broken on X axis, weaponYav on Y axis but this isn't also correct)
	float weaponX = dx * cos(weaponYaw) - dz * sin(weaponYaw);
	float weaponY = dx * sin(hmdYaw) + dz * cos(hmdYaw);
	Cvar_SetValue("vr_weapon_x", zoomed ? INT_MAX : weaponX);
	Cvar_SetValue("vr_weapon_y", zoomed ? INT_MAX : weaponY);
	Cvar_SetValue("vr_weapon_z", zoomed ? INT_MAX : (weaponPosition[1] - hmdAltitude) * scale);
}

void Host_VRRotations( bool zoomed, vec3_t hmdAngles, vec3_t weaponAngles, float thumbstickX, float thumbstickY )
{
	// Weapon rotation
	static float lastYaw = 0;
	static float lastPitch = 0;
	float yaw = weaponAngles[YAW] - lastYaw;
	float pitch = weaponAngles[PITCH] - lastPitch;
	float diff = lastPitch - Cvar_VariableValue("vr_player_pitch");
	if ((fabs(diff) > 1) && !zoomed) {
		pitch += diff + 0.02f;
	}
	lastYaw = weaponAngles[YAW];
	lastPitch = weaponAngles[PITCH];
	Cvar_SetValue("vr_weapon_roll", weaponAngles[ROLL]);

	// Snap turn
	float snapTurnStep = 0;
	float deadzone = Cvar_VariableValue("vr_thumbstick_deadzone_right");
	bool snapTurnDown = fabs(thumbstickX) > deadzone;
	static bool lastSnapTurnDown = false;
	if (snapTurnDown && !lastSnapTurnDown) {
		float angle = Cvar_VariableValue("vr_thumbstick_snapturn");
		snapTurnStep = thumbstickX > 0 ? -angle : angle;
		yaw += snapTurnStep;
	}
	lastSnapTurnDown = snapTurnDown;
	clgame.dllFuncs.pfnLookEvent( yaw, pitch );

	// Weapon changing
	bool weaponChangeDown = fabs(thumbstickY) > deadzone;
	static bool lastWeaponChangeDown = false;
	if (weaponChangeDown && !lastWeaponChangeDown) {
		Cbuf_AddText( thumbstickY > 0 ? "invnext\n" : "invprev\n" );
		Cbuf_AddText( "+attack\n" );
	} else if (!weaponChangeDown && lastWeaponChangeDown) {
		Cbuf_AddText( "-attack\n" );
	}
	lastWeaponChangeDown = weaponChangeDown;

	// HMD view
	static float lastWeaponYaw = 0;
	hmdAngles[YAW] += Cvar_VariableValue("vr_player_yaw") - lastWeaponYaw;
	Cvar_SetValue("vr_hmd_pitch", hmdAngles[PITCH]);
	Cvar_SetValue("vr_hmd_yaw", hmdAngles[YAW] + snapTurnStep);
	Cvar_SetValue("vr_hmd_roll", hmdAngles[ROLL]);
	lastWeaponYaw = weaponAngles[YAW];
}

void Host_VRWeaponCrosshair()
{
	// Get player position and direction
	vec3_t vecSrc, vecDir, vecEnd;
	vecDir[0] = Cvar_VariableValue("vr_player_dir_x");
	vecDir[1] = Cvar_VariableValue("vr_player_dir_y");
	vecDir[2] = Cvar_VariableValue("vr_player_dir_z");
	vecSrc[0] = Cvar_VariableValue("vr_player_pos_x");
	vecSrc[1] = Cvar_VariableValue("vr_player_pos_y");
	vecSrc[2] = Cvar_VariableValue("vr_player_pos_z");

	// Set cross-hair position far away
	for (int j = 0; j < 3; j++) {
		vecEnd[j] = vecSrc[j] + 4096.0f * vecDir[j];
	}

	// Test if there is a closer surface cross-hair should point to
	pmtrace_t trace = CL_TraceLine( vecSrc, vecEnd, PM_STUDIO_IGNORE );
	if( trace.fraction != 1.0f ) {
		for (int j = 0; j < 3; j++) {
			vecEnd[j] = trace.endpos[j];
		}
	}

	// Convert the position into screen coordinates
	vec3_t screenPos;
	TriWorldToScreen(vecEnd, screenPos);
	Cvar_SetValue("vr_xhair_x", screenPos[0]);
	Cvar_SetValue("vr_xhair_y", screenPos[1]);
}
