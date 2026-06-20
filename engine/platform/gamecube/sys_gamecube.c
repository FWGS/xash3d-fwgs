/*
sys_gamecube.c - GameCube platform backend
Copyright (C) 2026 xash3d-gc contributors

Platform layer ported from Division-Zero-GX/xash3d-wii.
*/
#include "platform/platform.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#if XASH_GAMECUBE
#include <ogc/system.h>
#include <fat.h>
#include <dirent.h>
#include "dll_gamecube.h"

#define GC_DATA_PATH "xash3d"
#endif

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
	(void)parms;
}

void Posix_Daemonize( void )
{
}

void Posix_SetupSigtermHandling( void )
{
}

char *Posix_Input( void )
{
	return NULL;
}

#if XASH_MESSAGEBOX == MSGBOX_GAMECUBE
void Platform_MessageBox( const char *title, const char *message, qboolean unused )
{
	(void)unused;
	fprintf( stderr, "%s:\n%s\n", title, message );
}
#endif

void Platform_Sleep( int msec )
{
	usleep( msec * 1000 );
}

double Platform_DoubleTime( void )
{
#if XASH_GAMECUBE
	return (double)SYS_Time() / 1000.0;
#else
	return 0.0;
#endif
}

void *Platform_GetNativeObject( const char *name )
{
	(void)name;
	return NULL;
}

void Platform_MouseMove( float *x, float *y )
{
	if( x ) *x = 0.0f;
	if( y ) *y = 0.0f;
}

platform_orientation_t Platform_GetDisplayOrientation( void )
{
	return ORIENTATION_UNKNOWN;
}

#if XASH_GAMECUBE
struct passwd {
	char *pw_name;
};

uid_t geteuid( void )
{
	return 0;
}

struct passwd *getpwuid( uid_t uid )
{
	(void)uid;
	return NULL;
}

int execv( const char *path, char *const argv[] )
{
	(void)path;
	(void)argv;
	return -1;
}
#endif

void GCube_EarlyInit( void )
{
#if XASH_GAMECUBE
	/* Make startup and fatal errors visible in Dolphin before video is ready. */
	SYS_STDIO_Report( true );
	SYS_Report( "Xash3D GameCube: bootstrap\n" );
#endif
}

void GCube_Init( void )
{
#if XASH_GAMECUBE
	char xashdir[MAX_SYSPATH];

	if( !fatInitDefault())
		Con_Reportf( S_WARN "SD card init failed, using DVD paths only\n" );

	if( GCube_GetBasePath( xashdir, sizeof( xashdir )))
	{
		if( chdir( xashdir ) == 0 )
			Con_Reportf( "GameCube data directory: %s\n", xashdir );
		else
			Con_Reportf( S_WARN "Failed to chdir to %s\n", xashdir );
	}

	setup_gamecube_dll_functions();
#endif
}

qboolean GCube_GetBasePath( char *buf, size_t buflen )
{
#if XASH_GAMECUBE
	static const char *paths[] =
	{
		"sd:/" GC_DATA_PATH,
		"sd:/",
		"dvd:/" GC_DATA_PATH,
		"dvd:/",
	};
	DIR *dir;
	size_t i;

	for( i = 0; i < ARRAYSIZE( paths ); i++ )
	{
		dir = opendir( paths[i] );
		if( !dir )
			continue;

		closedir( dir );
		Q_strncpy( buf, paths[i], buflen );
		return true;
	}
#endif
	(void)buf;
	(void)buflen;
	return false;
}

#define GC_MAX_ARGV 8
static char *gc_argv[GC_MAX_ARGV];

int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int fake_argc = 0;

	if( in_argc > 1 )
	{
		*out_argv = in_argv;
		return in_argc;
	}

	gc_argv[fake_argc++] = "xash";
	gc_argv[fake_argc++] = "-dev";
	gc_argv[fake_argc++] = "2";
	gc_argv[fake_argc++] = "-log";
	gc_argv[fake_argc++] = "-toconsole";

	*out_argv = gc_argv;
	return fake_argc;
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	fatUnmount( "sd" );
#endif
}
