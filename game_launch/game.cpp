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
#include <dlfcn.h>
#define XASHLIB OS_LIB_PREFIX "xash." OS_LIB_EXT
#define FreeLibrary( x ) dlclose( x )
#elif XASH_WIN32
#include <shellapi.h> // CommandLineToArgvW
#define XASHLIB L"xash.dll"
#define SDL2LIB L"SDL2.dll"

extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#else
#error // port me!
#endif

#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve" // !!! Replace with your default (base) game directory !!!
#endif

typedef void (*pfnChangeGame)( const char *progname );
typedef int  (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

static pfnInit     Host_Main;
static pfnShutdown Host_Shutdown = NULL;
static char        szGameDir[128]; // safe place to keep gamedir
static int         szArgc;
static char        **szArgv;
static HINSTANCE   hEngine;

static void Launch_Error( const char *szFmt, ... )
{
	static char	buffer[16384];	// must support > 1k messages
	va_list		args;

	va_start( args, szFmt );
	vsnprintf( buffer, sizeof(buffer), szFmt, args );
	va_end( args );

#if XASH_WIN32
	MessageBoxA( NULL, buffer, "Xash Error", MB_OK );
#else
	fprintf( stderr, "Xash Error: %s\n", buffer );
#endif

	exit( 1 );
}

#if XASH_WIN32
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
	HMODULE hSDL = LoadLibraryExW( SDL2LIB, NULL, LOAD_LIBRARY_AS_DATAFILE );

	if( !hSDL )
	{
		Launch_Error("Unable to load %ls: %s", SDL2LIB, GetStringLastError( ));
		return;
	}

	FreeLibrary( hSDL );

	hEngine = LoadLibraryW( XASHLIB );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %ls: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Main = (pfnInit)GetProcAddress( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%ls missed 'Host_Main' export: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)GetProcAddress( hEngine, "Host_Shutdown" );
#elif XASH_POSIX
	hEngine = dlopen( XASHLIB, RTLD_NOW );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %s: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Main = (pfnInit)dlsym( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%s missed 'Host_Main' export: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)dlsym( hEngine, "Host_Shutdown" );
#else
#error "port me!"
#endif
}

static void Sys_UnloadEngine( void )
{
	if( Host_Shutdown )
		Host_Shutdown( );

	if( hEngine )
		FreeLibrary( hEngine );

	hEngine = NULL;
	Host_Main = NULL;
	Host_Shutdown = NULL;
}

static void Sys_ChangeGame( const char *progname )
{
	// presence of this function tells the engine to allow change game
	// but it's never called
	return;
}

static int Sys_Start( void )
{
	int ret;

#if XASH_SAILFISH
	const char *home = getenv( "HOME" );
	char buf[1024];

	snprintf( buf, sizeof( buf ), "%s/xash", home );
	setenv( "XASH3D_BASEDIR", buf, true );
	setenv( "XASH3D_RODIR", "/usr/share/harbour-xash3d-fwgs/rodir", true );
#endif // XASH_SAILFISH

	strncpy( szGameDir, XASH_GAMEDIR, sizeof( szGameDir ) - 1 );

	Sys_LoadEngine();

	ret = Host_Main( szArgc, szArgv, szGameDir, 0, XASH_DISABLE_MENU_CHANGEGAME ? NULL : Sys_ChangeGame );

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
