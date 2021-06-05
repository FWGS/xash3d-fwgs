/*
game.cpp -- executable to run Xash Engine
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#if defined(__APPLE__) || defined(__unix__)
	#define XASHLIB    "libxash." OS_LIB_EXT
#elif __HAIKU__
	#include <libgen.h>
	#define XASHLIB     "libxash." OS_LIB_EXT
	#define E_GAME      "XASH3D_GAME"
	#define E_BASEDIR   "XASH3D_BASEDIR"
	#define E_MIRRORDIR "XASH3D_MIRRORDIR"
#elif _WIN32
	#if !__MINGW32__ && _MSC_VER >= 1200
		#define USE_WINMAIN
	#endif
	#define XASHLIB "xash.dll"
	#define dlerror() GetStringLastError()
	#include <shellapi.h> // CommandLineToArgvW
#endif

#ifdef WIN32
extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#define GAME_PATH	"valve"	// default dir to start from

typedef void (*pfnChangeGame)( const char *progname );
typedef int  (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

static pfnInit     Xash_Main;
static pfnShutdown Xash_Shutdown = NULL;
static char        szGameDir[128]; // safe place to keep gamedir
static int         szArgc;
static char        **szArgv;
static HINSTANCE	hEngine;

static void Xash_Error( const char *szFmt, ... )
{
	static char	buffer[16384];	// must support > 1k messages
	va_list		args;

	va_start( args, szFmt );
	vsnprintf( buffer, sizeof(buffer), szFmt, args );
	va_end( args );

#if defined( _WIN32 )
	MessageBoxA( NULL, buffer, "Xash Error", MB_OK );
#else
	fprintf( stderr, "Xash Error: %s\n", buffer );
#endif
	exit( 1 );
}

#ifdef _WIN32
static const char *GetStringLastError()
{
	static char buf[1024];

	FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ),
		buf, sizeof( buf ), NULL );

	return buf;
}
#endif

static void Sys_LoadEngine( void )
{
#ifdef __HAIKU__
	char path[PATH_MAX];
	char *engine = getenv( E_MIRRORDIR );
	strncpy( path, engine, PATH_MAX );
	strncat( path, "/"XASHLIB, PATH_MAX );
	if(( hEngine = LoadLibrary( path )) == NULL )
#else
	if(( hEngine = LoadLibrary( XASHLIB )) == NULL )
#endif
	{
		Xash_Error("Unable to load the " XASHLIB ": %s", dlerror() );
	}

	if(( Xash_Main = (pfnInit)GetProcAddress( hEngine, "Host_Main" )) == NULL )
	{
		Xash_Error( XASHLIB " missed 'Host_Main' export: %s", dlerror() );
	}

	// this is non-fatal for us but change game will not working
	Xash_Shutdown = (pfnShutdown)GetProcAddress( hEngine, "Host_Shutdown" );
}

static void Sys_UnloadEngine( void )
{
	if( Xash_Shutdown ) Xash_Shutdown( );
	if( hEngine ) FreeLibrary( hEngine );

	Xash_Main = NULL;
	Xash_Shutdown = NULL;
}

static void Sys_ChangeGame( const char *progname )
{
	if( !progname || !progname[0] )
		Xash_Error( "Sys_ChangeGame: NULL gamedir" );

	if( Xash_Shutdown == NULL )
		Xash_Error( "Sys_ChangeGame: missed 'Host_Shutdown' export\n" );

	strncpy( szGameDir, progname, sizeof( szGameDir ) - 1 );

	Sys_UnloadEngine ();
	Sys_LoadEngine ();

	Xash_Main( szArgc, szArgv, szGameDir, 1, Sys_ChangeGame );
}

_inline int Sys_Start( void )
{
	int ret;
	pfnChangeGame changeGame = NULL;

	Sys_LoadEngine();

#ifndef XASH_DISABLE_MENU_CHANGEGAME
	if( Xash_Shutdown )
		changeGame = Sys_ChangeGame;
#endif

#ifdef __HAIKU__
	const char* game = getenv( E_GAME );
	ret = Xash_Main( szArgc, szArgv, game, 0, changeGame );
#else
	ret = Xash_Main( szArgc, szArgv, GAME_PATH, 0, changeGame );
#endif

	Sys_UnloadEngine();

	return ret;
}

#ifndef USE_WINMAIN
int main( int argc, char **argv )
{
	szArgc = argc;
	szArgv = argv;

#ifdef __HAIKU__
	// To make it able to start from Deskbar
	chdir( dirname( argv[0]  )  );
	char path[PATH_MAX];
	getcwd( path, PATH_MAX  );
	const char *game = getenv( E_GAME  );
	if( !game  )
		setenv( E_GAME, GAME_PATH, 1  );
	const char *basedir = getenv( E_BASEDIR  );
	if( !basedir  )
		setenv( E_BASEDIR, path, 1  );
	const char *mirrordir = getenv( E_MIRRORDIR  );
	// The mirror "extras/" directory structure for Haiku OS which is needeed for the HPGK-package:
	// extras.pak
	// libmenu.so
	// libxash.so
	// valve/
	//  cl_dlls/
	//   client_haiku_amd64.so
	//  dlls/
	//   hl_haiku_amd64.so
	if( !mirrordir  )
	{
		strncpy( path, getenv( E_BASEDIR  ), PATH_MAX  );
		strncat( path, "/extras", PATH_MAX  );
		setenv( E_MIRRORDIR, path, 1  );
	}
#endif

	return Sys_Start();
}
#else
//#pragma comment(lib, "shell32.lib")
int __stdcall WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow )
{
	LPWSTR* lpArgv;
	int ret, i;

	lpArgv = CommandLineToArgvW( GetCommandLineW(), &szArgc );
	szArgv = ( char** )malloc( (szArgc + 1) * sizeof( char* ));

	for( i = 0; i < szArgc; ++i )
	{
		int size = wcslen(lpArgv[i]) + 1;
		szArgv[i] = ( char* )malloc( size );
		wcstombs( szArgv[i], lpArgv[i], size );
	}
	szArgv[szArgc] = 0;

	LocalFree( lpArgv );

	ret = Sys_Start();

	for( ; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );

	return ret;
}
#endif
