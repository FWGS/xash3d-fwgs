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
#include <errno.h> // errno
#include <string.h> // strerror
#if !XASH_WIN32
#include <unistd.h> // fork
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif
#if XASH_EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif
#include <errno.h>
#include "common.h"
#include "base_cmd.h"
#include "client.h"
#include "netchan.h"
#include "protocol.h"
#include "mod_local.h"
#include "mathlib.h"
#include "input.h"
#include "enginefeatures.h"
#include "render_api.h"	// decallist_t


typedef void (*pfnChangeGame)( const char *progname );

pfnChangeGame	pChangeGame = NULL;
host_parm_t		host;	// host parms
sysinfo_t		SI;

CVAR_DEFINE( host_developer, "developer", "0", 0, "engine is in development-mode" );
CVAR_DEFINE_AUTO( sys_ticrate, "100", 0, "framerate in dedicated mode" );

convar_t	*host_serverstate;
convar_t	*host_gameloaded;
convar_t	*host_clientloaded;
convar_t	*host_limitlocal;
convar_t	*host_maxfps;
convar_t	*host_framerate;
convar_t	*host_sleeptime;
convar_t	*con_gamemaps;
convar_t	*build, *ver;

int Host_CompareFileTime( int ft1, int ft2 )
{
	if( ft1 < ft2 )
	{
		return -1;
	}
	else if( ft1 > ft2 )
	{
		return 1;
	}
	return 0;
}

void Host_ShutdownServer( void )
{
	SV_Shutdown( "Server was killed\n" );
}

/*
================
Host_PrintEngineFeatures
================
*/
void Host_PrintEngineFeatures( void )
{
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
		Con_Reportf( "^3EXT:^7 big world support enabled\n" );

	if( FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
		Con_Reportf( "^3EXT:^7 deluxemap support enabled\n" );

	if( FBitSet( host.features, ENGINE_PHYSICS_PUSHER_EXT ))
		Con_Reportf( "^3EXT:^7 Improved MOVETYPE_PUSH is used\n" );

	if( FBitSet( host.features, ENGINE_LARGE_LIGHTMAPS ))
		Con_Reportf( "^3EXT:^7 Large lightmaps enabled\n" );

	if( FBitSet( host.features, ENGINE_COMPENSATE_QUAKE_BUG ))
		Con_Reportf( "^3EXT:^7 Compensate quake bug enabled\n" );
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
================
Host_AbortCurrentFrame

aborts the current host frame and goes on with the next one
================
*/
void Host_AbortCurrentFrame( void )
{
	longjmp( host.abortframe, 1 );
}

/*
==================
Host_CheckSleep
==================
*/
void Host_CheckSleep( void )
{
	int sleeptime = host_sleeptime->value;

	if( Host_IsDedicated() )
	{
		// let the dedicated server some sleep
		Sys_Sleep( sleeptime );
	}
	else
	{
		if( host.status == HOST_NOFOCUS )
		{
			if( SV_Active() && CL_IsInGame( ))
				Sys_Sleep( sleeptime ); // listenserver
			else Sys_Sleep( 20 ); // sleep 20 ms otherwise
		}
		else if( host.status == HOST_SLEEP )
		{
			// completely sleep in minimized state
			Sys_Sleep( 20 );
		}
		else
		{
			Sys_Sleep( sleeptime );
		}
	}
}

void Host_NewInstance( const char *name, const char *finalmsg )
{
	if( !pChangeGame ) return;

	host.change_game = true;
	Q_strncpy( host.finalmsg, finalmsg, sizeof( host.finalmsg ));
	pChangeGame( name ); // call from hl.exe
}

/*
=================
Host_ChangeGame_f

Change game modification
=================
*/
void Host_ChangeGame_f( void )
{
	int	i;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "game <directory>\n" );
		return;
	}

	// validate gamedir
	for( i = 0; i < SI.numgames; i++ )
	{
		if( !Q_stricmp( SI.games[i]->gamefolder, Cmd_Argv( 1 )))
			break;
	}

	if( i == SI.numgames )
	{
		Con_Printf( "%s not exist\n", Cmd_Argv( 1 ));
	}
	else if( !Q_stricmp( GI->gamefolder, Cmd_Argv( 1 )))
	{
		Con_Printf( "%s already active\n", Cmd_Argv( 1 ));	
	}
	else
	{
		const char *arg1 = va( "%s%s", (host.type == HOST_NORMAL) ? "" : "#", Cmd_Argv( 1 ));
		const char *arg2 = va( "change game to '%s'", SI.games[i]->title );

		Host_NewInstance( arg1, arg2 );
	}
}

/*
===============
Host_Exec_f
===============
*/
void Host_Exec_f( void )
{
	string	cfgpath;
	byte *f;
	char *txt;
	fs_offset_t	len;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "exec <filename>\n" );
		return;
	}

	if( !Q_stricmp( "game.cfg", Cmd_Argv( 1 )))
	{
		// don't execute game.cfg in singleplayer
		if( SV_GetMaxClients() == 1 )
			return;
	}

	Q_strncpy( cfgpath, Cmd_Argv( 1 ), sizeof( cfgpath )); 
	COM_DefaultExtension( cfgpath, ".cfg" ); // append as default

	f = FS_LoadFile( cfgpath, &len, false );
	if( !f )
	{
		Con_Reportf( "couldn't exec %s\n", Cmd_Argv( 1 ));
		return;
	}

	if( !Q_stricmp( "config.cfg", Cmd_Argv( 1 )))
		host.config_executed = true;

	// adds \n\0 at end of the file
	txt = Z_Calloc( len + 2 );
	memcpy( txt, f, len );
	Q_strncat( txt, "\n", len + 2 );
	Mem_Free( f );

	if( !host.apply_game_config )
		Con_Printf( "execing %s\n", Cmd_Argv( 1 ));
	Cbuf_InsertText( txt );
	Mem_Free( txt );
}

/*
===============
Host_MemStats_f
===============
*/
void Host_MemStats_f( void )
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

void Host_Minimize_f( void )
{
#ifdef XASH_SDL
	if( host.hWnd ) SDL_MinimizeWindow( host.hWnd );
#endif
}

/*
=================
Host_IsLocalGame

singleplayer game detect
=================
*/
qboolean Host_IsLocalGame( void )
{
	if( SV_Active( ))
	{
		return ( SV_GetMaxClients() == 1 ) ? true : false;
	}
	else
	{
		return ( CL_GetMaxClients() == 1 ) ? true : false;
	}
}

qboolean Host_IsLocalClient( void )
{
	// only the local client have the active server
	if( CL_Initialized( ) && SV_Initialized( ))
		return true;
	return false;
}

/*
=================
Host_RegisterDecal
=================
*/
qboolean Host_RegisterDecal( const char *name, int *count )
{
	char	shortname[MAX_QPATH];
	int	i;

	if( !COM_CheckString( name ))
		return 0;

	COM_FileBase( name, shortname );

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
void Host_InitDecals( void )
{
	int	i, num_decals = 0;
	search_t	*t;

	// NOTE: only once resource without which engine can't continue work
	if( !FS_FileExists( "gfx/conchars", false ))
		Sys_Error( "W_LoadWadFile: couldn't load gfx.wad\n" );

	memset( host.draw_decals, 0, sizeof( host.draw_decals ));

	// lookup all the decals in decals.wad (basedir, gamedir, falldir)
	t = FS_Search( "decals.wad/*.*", true, false );

	for( i = 0; t && i < t->numfilenames; i++ )
	{
		if( !Host_RegisterDecal( t->filenames[i], &num_decals ))
			break;
	}

	if( t ) Mem_Free( t );
	Con_Reportf( "InitDecals: %i decals\n", num_decals );
}

/*
===================
Host_GetCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetCommands( void )
{
	char	*cmd;

	while( ( cmd = Sys_Input() ) )
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
double Host_CalcFPS( void )
{
	double	fps = 0.0;

	if( Host_IsDedicated() )
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
		fps = host_maxfps->value;
	}
	else
	{
		fps = host_maxfps->value;
		if( fps == 0.0 ) fps = MAX_FPS;
		fps = bound( MIN_FPS, fps, MAX_FPS );
	}

	// probably left part of this condition is redundant :-)
	if( host.type != HOST_DEDICATED && Host_IsLocalGame( ) && !CL_IsTimeDemo( ))
	{
		// ajdust fps for vertical synchronization
		if( CVAR_TO_BOOL( gl_vsync ))
		{
			if( vid_displayfrequency->value != 0.0f )
				fps = vid_displayfrequency->value;
			else fps = 60.0; // default
		}
	}
#endif

	return fps;
}

/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
qboolean Host_FilterTime( float time )
{
	static double	oldtime;
	double		fps;

	host.realtime += time;
	fps = Host_CalcFPS( );

	// clamp the fps in multiplayer games
	if( fps != 0.0 )
	{
		// limit fps to withing tolerable range
		fps = bound( MIN_FPS, fps, MAX_FPS );

		if( Host_IsDedicated() )
		{
			if(( host.realtime - oldtime ) < ( 1.0 / ( fps + 1.0 )))
				return false;
		}
		else
		{
			if(( host.realtime - oldtime ) < ( 1.0 / fps ))
				return false;		
		}
	}

	host.frametime = host.realtime - oldtime;
	host.realframetime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );
	oldtime = host.realtime;

	// NOTE: allow only in singleplayer while demos are not active
	if( host_framerate->value > 0.0f && Host_IsLocalGame() && !CL_IsPlaybackDemo() && !CL_IsRecordDemo( ))
		host.frametime = bound( MIN_FRAMETIME, host_framerate->value, MAX_FRAMETIME );
	else host.frametime = bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME );

	return true;
}

/*
=================
Host_Frame
=================
*/
void Host_Frame( float time )
{
	Host_CheckSleep();

	// decide the simulation time
	if( !Host_FilterTime( time ))
		return;

	Host_InputFrame ();  // input frame
	Host_ClientBegin (); // begin client
	Host_GetCommands (); // dedicated in
	Host_ServerFrame (); // server frame
	Host_ClientFrame (); // client frame
	HTTP_Run();			 // both server and client

	host.framecount++;
}

/*
=================
Host_Error
=================
*/
void Host_Error( const char *error, ... )
{
	static char	hosterror1[MAX_SYSPATH];
	static char	hosterror2[MAX_SYSPATH];
	static qboolean	recursive = false;
	va_list		argptr;

	if( host.mouse_visible && !CL_IsInMenu( ))
	{
		// hide VGUI mouse
#ifdef XASH_SDL
		SDL_ShowCursor( 0 );
#endif
		host.mouse_visible = false;
	}

	va_start( argptr, error );
	Q_vsprintf( hosterror1, error, argptr );
	va_end( argptr );

	CL_WriteMessageHistory (); // before Q_error call

	if( host.framecount < 3 )
	{
		Sys_Error( "Host_InitError: %s", hosterror1 );
	}
	else if( host.framecount == host.errorframe )
	{
		Sys_Error( "Host_MultiError: %s", hosterror2 );
		return;
	}
	else
	{
		if( host.allow_console )
		{
			UI_SetActiveMenu( false );
			Key_SetKeyDest( key_console );
			Con_Printf( "Host_Error: %s", hosterror1 );
		}
		else MSGBOX2( hosterror1 );
	}

	// host is shutting down. don't invoke infinite loop
	if( host.status == HOST_SHUTDOWN ) return;

	if( recursive )
	{ 
		Con_Printf( "Host_RecursiveError: %s", hosterror2 );
		Sys_Error( "%s", hosterror1 );
		return; // don't multiple executes
	}

	recursive = true;
	Q_strncpy( hosterror2, hosterror1, MAX_SYSPATH );
	host.errorframe = host.framecount; // to avoid multply calls per frame
	Q_sprintf( host.finalmsg, "Server crashed: %s", hosterror1 );

	// clearing cmd buffer to prevent execute any commands
	COM_InitHostState();
	Cbuf_Clear();

	Host_ShutdownServer();
	CL_Drop(); // drop clients

	// recreate world if needs
	CL_ClearEdicts ();

	// release all models
	Mod_FreeAll();

	recursive = false;
	Host_AbortCurrentFrame();
}

void Host_Error_f( void )
{
	const char *error = Cmd_Argv( 1 );

	if( !*error ) error = "Invoked host error";
	Host_Error( "%s\n", error );
}

void Sys_Error_f( void )
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
Host_InitCommon
=================
*/
void Host_InitCommon( int argc, char **argv, const char *progname, qboolean bChangeGame )
{
	char		dev_level[4];
	int		developer = DEFAULT_DEV;
	const char *baseDir;
	char ticrate[16];

	// some commands may turn engine into infinite loop,
	// e.g. xash.exe +game xash -game xash
	// so we clear all cmd_args, but leave dbg states as well
	Sys_ParseCommandLine( argc, argv );

	if( !Sys_CheckParm( "-noch" ) )
		Sys_SetupCrashHandler();

	// to be accessed later
	if( ( host.daemonized = Sys_CheckParm( "-daemonize" ) ) )
	{
#if defined(_POSIX_VERSION) && !defined(XASH_MOBILE_PLATFORM)
		pid_t daemon;

		daemon = fork();

		if( daemon < 0 )
		{
			Host_Error( "fork() failed: %s\n", strerror( errno ) );
		}

		if( daemon > 0 )
		{
			// parent
			Con_Reportf( "Child pid: %i\n", daemon );
			exit( 0 );
		}
		else
		{
			// don't be closed by parent
			if( setsid() < 0 )
			{
				Host_Error( "setsid() failed: %s\n", strerror( errno ) );
			}

			// set permissions
			umask( 0 );

			// engine will still use stdin/stdout,
			// so just redirect them to /dev/null
			close( STDIN_FILENO );
			close( STDOUT_FILENO );
			close( STDERR_FILENO );
			open("/dev/null", O_RDONLY); // becomes stdin
			open("/dev/null", O_RDWR); // stdout
			open("/dev/null", O_RDWR); // stderr

			// fallthrough
		}
#elif defined(XASH_MOBILE_PLATFORM)
		Sys_Error( "Can't run in background on mobile platforms!" );
#else
		Sys_Error( "Daemonize not supported on this platform!" );
#endif
	}

	if( ( baseDir = getenv( "XASH3D_BASEDIR" ) ) )
	{
		Q_strncpy( host.rootdir, baseDir, sizeof(host.rootdir) );
	}
	else
	{
#if TARGET_OS_IOS
		const char *IOS_GetDocsDir();
		Q_strncpy( host.rootdir, IOS_GetDocsDir(), sizeof(host.rootdir) );
#elif XASH_SDL == 2
		char *szBasePath;

		if( !( szBasePath = SDL_GetBasePath() ) )
			Sys_Error( "couldn't determine current directory: %s", SDL_GetError() );
		Q_strncpy( host.rootdir, szBasePath, sizeof( host.rootdir ) );
		SDL_free( szBasePath );
#else
		if( !getcwd( host.rootdir, sizeof(host.rootdir) ) )
		{
			Sys_Error( "couldn't determine current directory: %s", strerror( errno ) );
			host.rootdir[0] = 0;
		}
#endif
	}

	if( host.rootdir[Q_strlen( host.rootdir ) - 1] == '/' )
		host.rootdir[Q_strlen( host.rootdir ) - 1] = 0;

	// get readonly root. The order is: check for arg, then env.
	// if still not got it, rodir is disabled.
	host.rodir[0] = 0;
	if( !Sys_GetParmFromCmdLine( "-rodir", host.rodir ))
	{
		char *roDir;

		if(( roDir = getenv( "XASH3D_RODIR" )))
			Q_strncpy( host.rodir, roDir, sizeof( host.rodir ));
	}

	if( host.rodir[0] && host.rodir[Q_strlen( host.rodir ) - 1] == '/' )
		host.rodir[Q_strlen( host.rodir ) - 1] = 0;

	host.enabledll = !Sys_CheckParm( "-nodll" );

#ifdef DLL_LOADER
	if( host.enabledll )
		Setup_LDT_Keeper( ); // Must call before creating any thread
#endif

	host.change_game = bChangeGame;
	host.config_executed = false;
	host.status = HOST_INIT; // initialzation started

	Memory_Init(); // init memory subsystem

	host.mempool = Mem_AllocPool( "Zone Engine" );

	// HACKHACK: Quake console is always allowed
	// TODO: determine if we are running QWrap more reliable
	if( Sys_CheckParm( "-console" ) || !Q_stricmp( SI.exeName, "quake" ))
		host.allow_console = true;

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

	host.con_showalways = true;

#if XASH_DEDICATED
	host.type = HOST_DEDICATED; // predict state
#else
	if( Sys_CheckParm("-dedicated") || progname[0] == '#' )
	{
		host.type = HOST_DEDICATED;
	}
	else
	{
		host.type = HOST_NORMAL;
	}
#endif

#ifdef XASH_SDL
#ifndef SDL_INIT_EVENTS
#define SDL_INIT_EVENTS 0
#endif

	if( SDL_Init( SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS ) )
	{
		Sys_Warn( "SDL_Init failed: %s", SDL_GetError() );
		host.type = HOST_DEDICATED;
	}

#if XASH_SDL == 2
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
	SDL_StopTextInput();
#endif // XASH_SDL == 2
#endif // XASH_SDL

	if ( !host.rootdir[0] || SetCurrentDirectory( host.rootdir ) != 0)
		Con_Reportf( "%s is working directory now\n", host.rootdir );
	else
		Sys_Error( "Changing working directory to %s failed.\n", host.rootdir );

	Sys_InitLog();

	// set default gamedir
	if( progname[0] == '#' )
		progname++;

	Q_strncpy( SI.exeName, progname, sizeof( SI.exeName ));
	Q_strncpy( SI.basedirName, progname, sizeof( SI.exeName ));

	if( Host_IsDedicated() )
	{
		Sys_MergeCommandLine( );

		host.allow_console = true;
	}
	else
	{
		// don't show console as default
		if( developer <= DEV_NORMAL )
			host.con_showalways = false;
	}

	// member console allowing
	host.allow_console_init = host.allow_console;

#ifdef _WIN32
	Wcon_CreateConsole(); // system console used by dedicated server or show fatal errors
#endif
	// timeBeginPeriod( 1 ); // a1ba: Do we need this?

	// NOTE: this message couldn't be passed into game console but it doesn't matter
	Con_Reportf( "Sys_LoadLibrary: Loading xash.dll - ok\n" );

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
		double fps = bound( MIN_FPS, atof( ticrate ), MAX_FPS );
		Cvar_SetValue( "sys_ticrate", fps );
	}

	Con_Init(); // early console running to catch all the messages
	Cmd_AddCommand( "exec", Host_Exec_f, "execute a script file" );
	Cmd_AddCommand( "memlist", Host_MemStats_f, "prints memory pool information" );

	FS_Init();
	Image_Init();
	Sound_Init();

	FS_LoadGameInfo( NULL );
	Q_strncpy( host.gamefolder, GI->gamefolder, sizeof( host.gamefolder ));

	// DEPRECATED: by FWGS fork
#if 0
	if( GI->secure )
	{
		// clear all developer levels when game is protected
		Cvar_DirectSet( &host_developer, "0" );
		host.allow_console_init = false;
		host.con_showalways = false;
		host.allow_console = false;
	}
#endif
	HPAK_Init();

	IN_Init();
	Key_Init();
}

void Host_FreeCommon( void )
{
	Image_Shutdown();
	Sound_Shutdown();
	Netchan_Shutdown();
	HPAK_FlushHostQueue();
	FS_Shutdown();
}

/*
=================
Host_Main
=================
*/
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func )
{
	static double	oldtime, newtime;

	pChangeGame = func;	// may be NULL

	Host_InitCommon( argc, argv, progname, bChangeGame );

	// init commands and vars
	if( host_developer.value >= DEV_EXTENDED )
	{
		Cmd_AddCommand ( "sys_error", Sys_Error_f, "just throw a fatal error to test shutdown procedures");
		Cmd_AddCommand ( "host_error", Host_Error_f, "just throw a host error to test shutdown procedures");
		Cmd_AddCommand ( "crash", Host_Crash_f, "a way to force a bus error for development reasons");
	}

	host_serverstate = Cvar_Get( "host_serverstate", "0", FCVAR_READ_ONLY, "displays current server state" );
	host_maxfps = Cvar_Get( "fps_max", "72", FCVAR_ARCHIVE, "host fps upper limit" );
	host_framerate = Cvar_Get( "host_framerate", "0", 0, "locks frame timing to this value in seconds" );  
	host_sleeptime = Cvar_Get( "sleeptime", "1", FCVAR_ARCHIVE, "milliseconds to sleep for each frame. higher values reduce fps accuracy" );
	host_gameloaded = Cvar_Get( "host_gameloaded", "0", FCVAR_READ_ONLY, "inidcates a loaded game.dll" );
	host_clientloaded = Cvar_Get( "host_clientloaded", "0", FCVAR_READ_ONLY, "inidcates a loaded client.dll" );
	host_limitlocal = Cvar_Get( "host_limitlocal", "0", 0, "apply cl_cmdrate and rate to loopback connection" );
	con_gamemaps = Cvar_Get( "con_mapfilter", "1", FCVAR_ARCHIVE, "when true show only maps in game folder" );

	build = Cvar_Get( "buildnum", va( "%i", Q_buildnum_compat()), FCVAR_READ_ONLY, "returns a current build number" );
	ver = Cvar_Get( "ver", va( "%i/%s (hw build %i)", PROTOCOL_VERSION, XASH_COMPAT_VERSION, Q_buildnum_compat()), FCVAR_READ_ONLY, "shows an engine version" );
	Cvar_Get( "host_ver", va( "%i %s %s %s %s", Q_buildnum(), XASH_VERSION, Q_buildos(), Q_buildarch(), Q_buildcommit() ), FCVAR_READ_ONLY, "detailed info about this build" );

	Mod_Init();
	NET_Init();
	NET_InitMasters();
	Netchan_Init();

	// allow to change game from the console
	if( pChangeGame != NULL )
	{
		Cmd_AddCommand( "game", Host_ChangeGame_f, "change game" );
		Cvar_Get( "host_allow_changegame", "1", FCVAR_READ_ONLY, "allows to change games" );
	}
	else
	{
		Cvar_Get( "host_allow_changegame", "0", FCVAR_READ_ONLY, "allows to change games" );
	}

	SV_Init();
	CL_Init();

	HTTP_Init();
	ID_Init();

	if( Host_IsDedicated() )
	{
#ifdef _WIN32
		Wcon_InitConsoleCommands ();
#endif

		Cmd_AddCommand( "quit", Sys_Quit, "quit the game" );
		Cmd_AddCommand( "exit", Sys_Quit, "quit the game" );
	}
	else Cmd_AddCommand( "minimize", Host_Minimize_f, "minimize main window to tray" );

	host.errorframe = 0;

	// post initializations
	switch( host.type )
	{
	case HOST_NORMAL:
#ifdef _WIN32
		Wcon_ShowConsole( false ); // hide console
#endif
		// execute startup config and cmdline
		Cbuf_AddText( va( "exec %s.rc\n", SI.rcName ));
		Cbuf_Execute();
		if( !host.config_executed )
		{
			Cbuf_AddText( "exec config.cfg\n" );
			Cbuf_Execute();
		}
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

	oldtime = Sys_DoubleTime() - 0.1;

	if( Host_IsDedicated() && GameState->nextstate == STATE_RUNFRAME )
		Con_Printf( "type 'map <mapname>' to run server... (TAB-autocomplete is working too)\n" );

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

/*
=================
Host_Shutdown
=================
*/
void EXPORT Host_Shutdown( void )
{
	if( host.shutdown_issued ) return;
	host.shutdown_issued = true;

	if( host.status != HOST_ERR_FATAL ) host.status = HOST_SHUTDOWN; // prepare host to normal shutdown
	if( !host.change_game ) Q_strncpy( host.finalmsg, "Server shutdown", sizeof( host.finalmsg ));

#if !XASH_DEDICATED
	if( host.type == HOST_NORMAL )
		Host_WriteConfig();
#endif

	SV_Shutdown( "Server shutdown\n" );
	SV_ShutdownFilter();
	CL_Shutdown();

	Mod_Shutdown();
	NET_Shutdown();
	HTTP_Shutdown();
	Host_FreeCommon();
#ifdef _WIN32
	Wcon_DestroyConsole();
#endif

	// must be last, console uses this
	Mem_FreePool( &host.mempool );

	// restore filter
	Sys_RestoreCrashHandler();
	Sys_CloseLog();
}
