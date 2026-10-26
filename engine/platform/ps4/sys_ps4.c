/*
sys_ps4.c - PlayStation 4 system utils
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/SystemService.h>
#include <orbis/Net.h>
#include "platform/ps4/crash_ps4.h"

// application package mount point
#define PS4_APP_DIR      "/app0"

// default writable data directory
#define PS4_DATA_DIR     "/data/xash"

// optional command line, one or more arguments separated by whitespace
#define PS4_CMDLINE_FILE "xash3d.cmdline"
#define PS4_DEFAULT_CMDLINE "-log -console"

// everything printed by engine goes here, written without stdio buffering
// so it survives crashes
#define PS4_LOG_FILE     "stdout.txt"

#define PS4_MAX_ARGV 64

static char ps4_basedir[256];
static char *ps4_argv[PS4_MAX_ARGV];
static char ps4_logpath[300];
static qboolean ps4_log_sync; // -logsync, -gnmdebug: every write goes to disk, survives a console freeze


// 3rdparty/ps4compat/ps4_cwd.c
const char *PS4_GetStatLayout( void );

// 3rdparty/ps4compat/ps4_stdio.c
extern void ( *ps4_stdout_hook )( const char *text, size_t len );

/*
==================
PS4_LogWrite

Appends raw text to log file, also receives stdout and stderr, see ps4_stdio.c
==================
*/
static void PS4_LogWrite( const char *text, size_t len )
{
	int fd;

	if( !ps4_logpath[0] )
		return;

	fd = open( ps4_logpath, O_WRONLY | O_CREAT | O_APPEND, 0666 );
	if( fd < 0 )
		return;

	if( write( fd, text, len ) < 0 )
	{
		// nothing we can do
	}

	if( ps4_log_sync )
		sceKernelFsync( fd );

	close( fd );
}

/*
==================
PS4_Log

Appends message to log file, opening it each time, so nothing is lost on crash
==================
*/
void PS4_Log( const char *fmt, ... )
{
	char buf[2048];
	va_list va;
	int len;

	if( !ps4_logpath[0] )
		return;

	ps4_last_log_time = sceKernelGetProcessTime( );

	va_start( va, fmt );
	len = vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	if( len <= 0 )
		return;

	if( len >= (int)sizeof( buf ))
		len = sizeof( buf ) - 1;

	PS4_LogWrite( buf, len );
}

static qboolean PS4_DirExists( const char *path )
{
	struct stat st;

	return stat( path, &st ) == 0 && S_ISDIR( st.st_mode );
}

/*
=============================================================================

	PATHS AND COMMAND LINE

=============================================================================
*/
static void PS4_MakeDir( const char *path )
{
	if( mkdir( path, 0777 ) < 0 && errno != EEXIST )
		PS4_Log( "mkdir %s failed: %s\n", path, strerror( errno ));
}

/*
==================
PS4_FindBaseDir

Prefer /data/xash, fall back to USB storage
==================
*/
static void PS4_FindBaseDir( void )
{
	const char *env = getenv( "XASH3D_BASEDIR" );
	char path[256];

	if( !COM_StringEmptyOrNULL( env ))
	{
		Q_strncpy( ps4_basedir, env, sizeof( ps4_basedir ));
		return;
	}

	// game data already in internal storage
	Q_snprintf( path, sizeof( path ), "%s/%s", PS4_DATA_DIR, XASH_GAMEDIR );
	if( PS4_DirExists( path ))
	{
		Q_strncpy( ps4_basedir, PS4_DATA_DIR, sizeof( ps4_basedir ));
		return;
	}

	for( int i = 0; i < 8; i++ )
	{
		Q_snprintf( path, sizeof( path ), "/mnt/usb%d/xash/%s", i, XASH_GAMEDIR );
		if( PS4_DirExists( path ))
		{
			Q_snprintf( ps4_basedir, sizeof( ps4_basedir ), "/mnt/usb%d/xash", i );
			return;
		}
	}

	// nothing found, use internal storage anyway so user sees where to put data
	Q_strncpy( ps4_basedir, PS4_DATA_DIR, sizeof( ps4_basedir ));
}

qboolean PS4_GetBasePath( char *buf, const size_t buflen )
{
	if( COM_StringEmpty( ps4_basedir ))
		PS4_FindBaseDir();

	Q_strncpy( buf, ps4_basedir, buflen );
	return true;
}

static void PS4_SetupDataDir( void )
{
	char path[512];

	PS4_GetBasePath( path, sizeof( path ));
	PS4_MakeDir( path );

	// start new log on each launch
	Q_snprintf( ps4_logpath, sizeof( ps4_logpath ), "%s/%s", ps4_basedir, PS4_LOG_FILE );
	unlink( ps4_logpath );

	PS4_Log( "Xash3D FWGS PS4: base directory %s, stat layout %s\n", ps4_basedir, PS4_GetStatLayout( ));

	// libraries print to stdout directly,
	// applications can't redirect it, so it's captured by ps4_stdio.c
	ps4_stdout_hook = PS4_LogWrite;

	// emulated by 3rdparty/ps4compat/ps4_cwd.c, so relative paths work in engine image too
	if( chdir( ps4_basedir ) < 0 )
		PS4_Log( "chdir %s failed: %s\n", ps4_basedir, strerror( errno ));

	// engine resources are shipped in the package, filesystem opens them from there
	setenv( "XASH3D_EXTRAS_PAK1", PS4_APP_DIR "/" XASH_GAMEDIR "/extras.pk3", true );

	// the game directory has to exist for the game to write its files
	Q_snprintf( path, sizeof( path ), "%s/%s", ps4_basedir, XASH_GAMEDIR );
	PS4_MakeDir( path );
}

/*
==================
PS4_GetArgv

There is no command line on PS4, read additional arguments from file
==================
*/
int PS4_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	static char cmdline[2048];
	static char args[PS4_MAX_ARGV][256];
	char path[512];
	int argc = 0;

	PS4_SetupDataDir();

	PS4_InstallCrashHandler( );

	ps4_argv[argc++] = ( in_argc > 0 && in_argv[0] ) ? in_argv[0] : (char *)PS4_APP_DIR "/eboot.bin";

	Q_snprintf( path, sizeof( path ), "%s/%s", ps4_basedir, PS4_CMDLINE_FILE );
	FILE *f = fopen( path, "rb" );

	if( f )
	{
		size_t len = fread( cmdline, 1, sizeof( cmdline ) - 1, f );
		char *p = cmdline;

		fclose( f );
		cmdline[len] = 0;

		for( int i = 0; argc < PS4_MAX_ARGV - 1; i++ )
		{
			p = COM_ParseFile( p, args[i], sizeof( args[i] ));

			if( !p )
				break;

			ps4_argv[argc++] = args[i];
		}
	}
	else
	{
		// the log is always written, so it's possible to see what went wrong;
		// "-log -dev 2" in xash3d.cmdline gives verbose log and messages on screen
		ps4_argv[argc++] = (char *)"-log";
		ps4_argv[argc++] = (char *)"-console";

		// create the file with defaults, so it can be found and edited
		// (for example, "-language russian" mounts valve_russian)
		f = fopen( path, "wb" );
		if( f )
		{
			fputs( PS4_DEFAULT_CMDLINE "\n", f );
			fclose( f );
		}
	}

	ps4_argv[argc] = NULL;

	for( int i = 0; i < argc; i++ )
	{
		PS4_Log( "argv[%d] = %s\n", i, ps4_argv[i] );

		if( !Q_strcmp( ps4_argv[i], "-ps4watchdog" ))
			ps4_watchdog_enabled = true;

		if( !Q_strcmp( ps4_argv[i], "-logsync" ) || !Q_strcmp( ps4_argv[i], "-gnmdebug" ))
			ps4_log_sync = true;
	}

	*out_argv = ps4_argv;
	return argc;
}

/*
==================
PS4_RunOnBigStack

Default main thread stack is too small for the engine
==================
*/
#define PS4_MAIN_STACK_SIZE ( 16 * 1024 * 1024 )

typedef struct
{
	int ( *func )( void *arg );
	void *arg;
	int ret;
} ps4_thread_args_t;

static void *PS4_ThreadEntry( void *arg )
{
	ps4_thread_args_t *args = arg;

	PS4_StartWatchdog( );
	args->ret = args->func( args->arg );
	return NULL;
}

int PS4_RunOnBigStack( int ( *func )( void *arg ), void *arg )
{
	ps4_thread_args_t args = { func, arg, 0 };
	pthread_attr_t attr;
	pthread_t thread;
	int ret;

	pthread_attr_init( &attr );
	pthread_attr_setstacksize( &attr, PS4_MAIN_STACK_SIZE );

	ret = pthread_create( &thread, &attr, PS4_ThreadEntry, &args );
	pthread_attr_destroy( &attr );

	if( ret != 0 )
	{
		PS4_Log( "pthread_create failed: %d, running on main thread\n", ret );
		return func( arg );
	}

	PS4_Log( "started engine thread\n" );
	pthread_join( thread, NULL );
	PS4_Log( "engine thread finished: %d\n", args.ret );

	// returning from main runs libc and module teardown, which crashes with CE-34878
	// ask the system to close the application instead
	PS4_Log( "exiting\n" );
	sceSystemServiceLoadExec( "exit", NULL );

	// shouldn't get here
	_exit( args.ret );
	return args.ret;
}

/*
==================
Platform_DoubleTime

clock_gettime( CLOCK_MONOTONIC ) of the toolchain uses the clock id of its own libc,
which the kernel reads as another clock (process CPU time), so the game would run slow.
Time since the start of the process is taken from the kernel directly
==================
*/
double Platform_DoubleTime( void )
{
	static uint64_t start;
	const uint64_t now = sceKernelGetProcessTime( ); // microseconds

	if( !start )
		start = now;

	return (double)( now - start ) / 1000000.0;
}

void Platform_Sleep( int msec )
{
	sceKernelUsleep( (uint32_t)msec * 1000 );
}

void PS4_Init( void )
{
	OrbisNetDnsInfo dns = { 0 };
	int ret;

	PS4_Log( "PS4_Init\n" );

	// libSceNet must be initialized for DNS server lookup used by libc resolver
	if(( ret = sceNetInit( )) < 0 )
		PS4_Log( "sceNetInit failed: 0x%08x\n", ret );

	if(( ret = sceNetGetDnsInfo( &dns, 0 )) < 0 )
		PS4_Log( "sceNetGetDnsInfo failed: 0x%08x\n", ret );
	else
	{
		const uint8_t *a = (const uint8_t *)&dns.primary_dns, *b = (const uint8_t *)&dns.secondary_dns;
		PS4_Log( "DNS servers: %d.%d.%d.%d, %d.%d.%d.%d\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3] );
	}

	// libScePad, libSceAudioIn and the system dialogs need user service
	sceUserServiceInitialize( NULL );

	PS4_InitKeyboard( );
}

/*
================
PS4_FramePresented

system keeps showing startup splash (sce_sys/pic0.png) over the game
until application hides it. Frames presented during engine initialization
are black, so splash is hidden only when the main loop has drawn something
================
*/
static int ps4_splash_frames = -1; // -1: still initializing, 0: hidden

void PS4_AllowHideSplash( void )
{
	if( ps4_splash_frames < 0 )
		ps4_splash_frames = 3;
}

void PS4_FramePresented( void )
{
	int ret;

	if( ps4_splash_frames <= 0 || --ps4_splash_frames > 0 )
		return;

	ret = sceSystemServiceHideSplashScreen( );
	PS4_Log( "sceSystemServiceHideSplashScreen: 0x%x\n", ret );
}

void PS4_Shutdown( void )
{
	PS4_Log( "PS4_Shutdown\n" );
}
