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
#include "build.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#if XASH_POSIX
#define XASHLIB "libxash." OS_LIB_EXT
#include <unistd.h> // execve
#elif XASH_WIN32
#define XASHLIB "xash.dll"
#define SDL2LIB "SDL2.dll"
#define dlerror() GetStringLastError()
#include <shellapi.h> // CommandLineToArgvW
#include <process.h> // _execve
#else
#error // port me!
#endif

#ifdef XASH_WIN32
extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if XASH_WIN32 || XASH_POSIX
#define USE_EXECVE_FOR_CHANGE_GAME 1
#else
#define USE_EXECVE_FOR_CHANGE_GAME 0
#endif

#define E_GAME	"XASH3D_GAME" // default env dir to start from
#define GAME_PATH	"valve"	// default dir to start from

typedef void (*pfnChangeGame)( const char *progname );
typedef int  (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

extern char        **environ;
static pfnInit     Xash_Main;
static pfnShutdown Xash_Shutdown = NULL;
static char        szGameDir[128]; // safe place to keep gamedir
static int         szArgc;
static char        **szArgv;
static HINSTANCE   hEngine;

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
#if XASH_WIN32
	HMODULE hSdl;

	if ( ( hSdl = LoadLibraryEx( SDL2LIB, NULL, LOAD_LIBRARY_AS_DATAFILE ) ) == NULL )
		Xash_Error("Unable to load the " SDL2LIB ": %s", dlerror() );
	else
		FreeLibrary( hSdl );
#endif

	if(( hEngine = LoadLibrary( XASHLIB )) == NULL )
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

#if USE_EXECVE_FOR_CHANGE_GAME
#if XASH_WIN32
	_putenv_s( E_GAME, progname );

	Sys_UnloadEngine();
	_execve( szArgv[0], szArgv, _environ );
#else
	char envstr[256];
	snprintf( envstr, sizeof( envstr ), E_GAME "=%s", progname );
	putenv( envstr );

	Sys_UnloadEngine();
	execve( szArgv[0], szArgv, environ );
#endif
#else
	if( Xash_Shutdown == NULL )
		Xash_Error( "Sys_ChangeGame: missed 'Host_Shutdown' export\n" );

	Sys_UnloadEngine();
	strncpy( szGameDir, progname, sizeof( szGameDir ) - 1 );
	Sys_LoadEngine ();
	Xash_Main( szArgc, szArgv, szGameDir, 1, Sys_ChangeGame );
#endif
}

_inline int Sys_Start( void )
{
	int ret;
	pfnChangeGame changeGame = NULL;
	const char *game = getenv( E_GAME );

	if( !game )
		game = GAME_PATH;

	Sys_LoadEngine();

	if( Xash_Shutdown )
		changeGame = Sys_ChangeGame;

	ret = Xash_Main( szArgc, szArgv, game, 0, changeGame );

	Sys_UnloadEngine();

	return ret;
}

#if !XASH_WIN32
int main( int argc, char **argv )
{
	szArgc = argc;
	szArgv = argv;

	return Sys_Start();
}
#else
int __stdcall WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow )
{
	LPWSTR* lpArgv;
	int ret, i;

	lpArgv = CommandLineToArgvW( GetCommandLineW(), &szArgc );
	szArgv = ( char** )malloc( (szArgc + 1) * sizeof( char* ));

	for( i = 0; i < szArgc; ++i )
	{
		size_t size = wcslen(lpArgv[i]) + 1;

		// just in case, allocate some more memory
		szArgv[i] = ( char * )malloc( size * sizeof( wchar_t ));
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
