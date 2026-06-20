/*
sys_gamecube.c - GameCube platform backend
Copyright (C) 2026 xash3d-gc contributors

Platform layer ported from Division-Zero-GX/xash3d-wii.
*/
#include "platform/platform.h"
#include <stdio.h>
#include <unistd.h>

#if XASH_GAMECUBE
#include <ogc/system.h>
#include <fat.h>
#include <unistd.h>
#include "dll_gamecube.h"
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

void GCube_Init( void )
{
#if XASH_GAMECUBE
	if( !fatInitDefault())
		Con_Reportf( S_WARN "SD card init failed, using DVD paths only\n" );
	setup_gamecube_dll_functions();
#endif
}

void GCube_Shutdown( void )
{
#if XASH_GAMECUBE
	fatUnmount( "sd" );
#endif
}
