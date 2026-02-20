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
#include <stdarg.h>  // va_args
#if !XASH_WIN32
#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

host_parm_t host;	// host parms
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

#ifdef XASH_ENGINE_TESTS
struct tests_stats_s tests_stats;
#endif

CVAR_DEFINE( host_developer, "developer", "0", FCVAR_FILTERABLE, "engine is in development-mode" );
CVAR_DEFINE_AUTO( sys_timescale, "1.0", FCVAR_FILTERABLE, "scale frame time" );

static CVAR_DEFINE_AUTO( sys_ticrate, "100", FCVAR_SERVER, "framerate in dedicated mode" );
static CVAR_DEFINE_AUTO( sv_hibernate_when_empty, "1", 0, "lower CPU usage when server has no players" );
static CVAR_DEFINE_AUTO( sv_hibernate_when_empty_sleep, "500", 0, "sleeptime value when sv_hibernate_when_empty is active" );
static CVAR_DEFINE_AUTO( sv_hibernate_when_empty_include_bots, "0", 0, "count bots as online players when sv_hibernate_when_empty is active" );
static CVAR_DEFINE_AUTO( host_serverstate, "0", FCVAR_READ_ONLY, "displays current server state" );
static CVAR_DEFINE_AUTO( host_gameloaded, "0", FCVAR_READ_ONLY, "inidcates a loaded game.dll" );
static CVAR_DEFINE_AUTO( host_clientloaded, "0", FCVAR_READ_ONLY, "inidcates a loaded client.dll" );
CVAR_DEFINE_AUTO( host_limitlocal, "0", 0, "apply cl_cmdrate and rate to loopback connection" );
CVAR_DEFINE( host_maxfps, "fps_max", "72", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "host fps upper limit" );
CVAR_DEFINE_AUTO( fps_override, "0", FCVAR_FILTERABLE, "unlock higher framerate values, not supported" );
static CVAR_DEFINE_AUTO( host_framerate, "0", FCVAR_FILTERABLE, "locks frame timing to this value in seconds" );
static CVAR_DEFINE( host_sleeptime, "sleeptime", "1", FCVAR_ARCHIVE|FCVAR_FILTERABLE, "milliseconds to sleep for each frame. higher values reduce fps accuracy" );
static CVAR_DEFINE_AUTO( host_sleeptime_debug, "0", 0, "print sleeps between frames" );
CVAR_DEFINE_AUTO( host_allow_materials, "0", FCVAR_LATCH|FCVAR_ARCHIVE, "allow texture replacements from materials/ folder" );
CVAR_DEFINE( con_gamemaps, "con_mapfilter", "1", FCVAR_ARCHIVE, "when true show only maps in game folder" );
CVAR_DEFINE_AUTO( cl_background, "0", FCVAR_READ_ONLY, "if set to 1, client running a background map" );
CVAR_DEFINE_AUTO( sv_background, "0", FCVAR_READ_ONLY, "if set to 1, server running a background map" );

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

static void Host_MakeVersionString( char *out, size_t len )
{
	Q_snprintf( out, len, XASH_ENGINE_NAME " %i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
}

static void Host_PrintUsage( const char *exename )
{
	string version_str;
	const char *usage_str;

	Host_MakeVersionString( version_str, sizeof( version_str ));

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
	O("-language <lang>   ", "mount localization game directory")
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
	O("-menulib <path>    ", "override menu DLL path")
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
	O("-noavi             ", "disable AVI support")
	O("-nointro           ", "disable intro video")
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
#if XASH_VIDEO == VIDEO_DOS
	O("-novesa            ","disable vesa")
#endif // XASH_VIDEO == VIDEO_DOS
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

static void Host_PrintBugcompUsage( const char *exename )
{
	string version_str;
	char usage_str[4096];
	char *p = usage_str;
	int i;

	Host_MakeVersionString( version_str, sizeof( version_str ));

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
		if( sv_hibernate_when_empty.value )
		{
			int players, bots;

			SV_GetPlayerCount( &players, &bots );

			if( sv_hibernate_when_empty_include_bots.value )
				players += bots;

			if( players == 0 )
				return sv_hibernate_when_empty_sleep.value;
		}

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
	host.change_game = true;

	if( !Sys_NewInstance( name, finalmsg ))
		Con_Printf( S_ERROR "Failed to restart the engine\n" );
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

/*
=================
Host_RegisterDecal
=================
*/
static qboolean Host_RegisterDecal( const char *name, int *count )
{
	char	shortname[MAX_QPATH];
	int	i;

	if( COM_StringEmptyOrNULL( name ))
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
	double targetframetime;
	int sleep;
	double fps = Host_CalcFPS();

	if( fps <= 0 )
		return true;

	// limit fps to withing tolerable range
	fps = bound( MIN_FPS, fps, MAX_FPS_HARD );

	if( Host_IsDedicated( ))
		targetframetime = ( 1.0 / ( fps + 1.0 ));
	else
		targetframetime = ( 1.0 / fps );

	sleep = Host_CalcSleep();
	if( sleep <= 0 ) // no sleeps between frames, much simpler code
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
				double t = Platform_DoubleTime();

				Platform_NanoSleep( sleep * 1000 * 100 ); // sleeptime 1 ~ 100 usecs

				realsleeptime = Platform_DoubleTime() - t;
				timewindow -= realsleeptime;

				if( host_sleeptime_debug.value )
				{
					counter++;

					Con_NPrintf( counter, "%d: %.6f %.6f", counter, timewindow, realsleeptime );
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
			else
				timewindow = 0;

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
	double t1;

	// decide the simulation time
	if( !Host_FilterTime( time ))
		return;

	t1 = Platform_DoubleTime();

	if( host.framecount == 0 )
		Con_DPrintf( "Time to first frame: %.3f seconds\n", t1 - host.starttime );

	Host_InputFrame ();  // input frame
	Host_ClientBegin (); // begin client
	Host_GetCommands (); // dedicated in
	Host_ServerFrame (); // server frame
	Host_ClientFrame (); // client frame
	HTTP_Run();			 // both server and client

	host.framecount++;
	host.pureframetime = Platform_DoubleTime() - t1;
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

	if( COM_StringEmpty( prev ))
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
	int ticrate, developer = DEFAULT_DEV;

	// some commands may turn engine into infinite loop,
	// e.g. xash.exe +game xash -game xash
	// so we clear all cmd_args, but leave dbg states as well
	Sys_ParseCommandLine( argc, (const char **)argv );
	Host_DetermineExecutableName( exename, exename_size );

	if( !Sys_CheckParm( "-disablehelp" ))
	{
		string arg;

		if( Sys_CheckParm( "-help" ) || Sys_CheckParm( "-h" ) || Sys_CheckParm( "--help" ))
			Host_PrintUsage( exename );

		if( Sys_GetParmFromCmdLine( "-bugcomp", arg ) && !Q_stricmp( arg, "help" ))
			Host_PrintBugcompUsage( exename );
	}

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

	host.allow_console = DEFAULT_ALLOWCONSOLE || DEFAULT_DEV > 0;

	if( Sys_CheckParm( "-dev" ))
	{
		host.allow_console = true;
		developer = DEV_NORMAL;

		if( Sys_GetIntFromCmdLine( "-dev", &developer ))
			developer = bound( DEV_NONE, developer, DEV_EXTENDED );
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

	// init host state machine
	COM_InitHostState();

	// init hashed commands
	BaseCmd_Init();

	// startup cmds and cvars subsystem
	Cmd_Init();
	Cvar_Init();

	// share developer level across all dlls
	Cvar_DirectSetValue( &host_developer, developer );
	Cvar_RegisterVariable( &sys_ticrate );

	if( Sys_GetIntFromCmdLine( "-sys_ticrate", &ticrate ))
		Cvar_DirectSetValue( &sys_ticrate, bound( MIN_FPS, ticrate, MAX_FPS_HARD ));

	Sys_InitLog();
	Con_Init(); // early console running to catch all the messages

	if( !Sys_CheckParm( "-noch" ))
		Sys_SetupCrashHandler( argv[0] );

#if XASH_ENGINE_TESTS
	if( Sys_CheckParm( "-runtests" ))
		Host_RunTests( 0 );
#endif

#if XASH_DEDICATED
	Platform_SetupSigtermHandling();
#endif
	Platform_Init( Host_IsDedicated( ) || developer >= DEV_EXTENDED, basedir );
	FS_Init( basedir );

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

#if !XASH_DEDICATED
	Cmd_AddRestrictedCommand( "host_writeconfig", Host_WriteConfig, "save current configuration" );
#endif

	Image_Init();
	Sound_Init();

#if XASH_ENGINE_TESTS
	if( Sys_CheckParm( "-runtests" ))
		Host_RunTests( 1 );
#endif

	FS_LoadGameInfo();
	Cvar_PostFSInit();

	Image_CheckPaletteQ1 ();

	// NOTE: only once resource without which engine can't continue work
	if( !FS_FileExists( "gfx/conchars", false ))
		Sys_Error( "%s: couldn't load gfx.wad\n", __func__ );

	Host_InitDecals ();	// reload decals

	HPAK_Init();

	IN_Init();
	Key_Init();
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
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame pChangeGame )
{
	static double oldtime;
	string demoname, exename;

	if( setjmp( return_from_main_buf ))
		return error_on_exit;

	host.starttime = Platform_DoubleTime();

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
	Cvar_RegisterVariable( &sv_hibernate_when_empty );
	Cvar_RegisterVariable( &sv_hibernate_when_empty_include_bots );
	Cvar_RegisterVariable( &sv_hibernate_when_empty_sleep );
	Cvar_RegisterVariable( &sv_background );
	Cvar_RegisterVariable( &cl_background );

	Cvar_Getf( "buildnum", FCVAR_READ_ONLY, "returns a current build number", "%i", Q_buildnum_compat());
	Cvar_Getf( "ver", FCVAR_READ_ONLY, "shows an engine version", "%i/%s (hw build %i)", PROTOCOL_VERSION, XASH_COMPAT_VERSION, Q_buildnum_compat());
	Cvar_Getf( "host_ver", FCVAR_READ_ONLY, "detailed info about this build", "%i " XASH_VERSION " %s %s %s", Q_buildnum(), Q_buildos(), Q_buildarch(), g_buildcommit);
	Cvar_Getf( "host_lowmemorymode", FCVAR_READ_ONLY, "indicates if engine compiled for low RAM consumption (0 - normal, 1 - low engine limits, 2 - low protocol limits)", "%i", XASH_LOW_MEMORY );

	Cvar_Get( "host_hl25_extended_structs",
#if SUPPORT_HL25_EXTENDED_STRUCTS
		"1",
#else
		"0",
#endif
		FCVAR_READ_ONLY, "indicates if engine was compiled with extended msurface_t struct" );

	Mod_Init();
	NET_Init();
	NET_InitMasters();
	Netchan_Init();

	// allow to change game from the console
	if( pChangeGame != NULL && Sys_CanRestart( ))
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
	else Cmd_AddRestrictedCommand( "minimize", Platform_Minimize_f, "minimize main window to tray" );

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
			Cbuf_AddTextf( "exec %s.rc\n", progname );
		else if( FS_FileExists( va( "%s.rc", exename ), false )) // e.g. quake.rc
			Cbuf_AddTextf( "exec %s.rc\n", exename );
		else if( FS_FileExists( va( "%s.rc", GI->gamefolder ), false )) // e.g. game.rc (ran from default launcher)
			Cbuf_AddTextf( "exec %s.rc\n", GI->gamefolder );
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
	Cbuf_ExecStuffCmds();	// execute stuffcmds (commandline)
	SCR_CheckStartupVids();	// must be last

	if( Sys_GetParmFromCmdLine( "-timedemo", demoname ))
		Cbuf_AddTextf( "timedemo %s\n", demoname );

	oldtime = Platform_DoubleTime() - 0.1;

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

	// main window message loop
	while( host.status != HOST_CRASHED )
	{
		double newtime = Platform_DoubleTime();
		COM_Frame( newtime - oldtime );
		oldtime = newtime;
	}

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
