/*
launcher.c - direct xash3d launcher
Copyright (C) 2015 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifdef SINGLE_BINARY

#include "build.h"
#include "common.h"
#ifdef XASH_SDLMAIN
#include "SDL.h"
#endif

#if XASH_EMSCRIPTEN
#include <emscripten.h>
#endif

#if XASH_WIN32
#include <process.h> // _execve
#else
#include <unistd.h> // execve
#endif

extern char **environ;
static char szGameDir[128]; // safe place to keep gamedir
static int g_iArgc;
static char **g_pszArgv;

#if XASH_WIN32 || XASH_POSIX
#define USE_EXECVE_FOR_CHANGE_GAME 1
#else
#define USE_EXECVE_FOR_CHANGE_GAME 0
#endif

#define E_GAME	"XASH3D_GAME" // default env dir to start from
#define GAME_PATH	"valve"	// default dir to start from

void Launcher_ChangeGame( const char *progname )
{
	char envstr[256];

#if USE_EXECVE_FOR_CHANGE_GAME
	Host_Shutdown();

#if XASH_WIN32
	_putenv_s( E_GAME, progname );
	_execve( g_pszArgv[0], g_pszArgv, _environ );
#else
	snprintf( envstr, sizeof( envstr ), E_GAME "=%s", progname );
	putenv( envstr );
	execve( g_pszArgv[0], g_pszArgv, environ );
#endif

#else
	Q_strncpy( szGameDir, progname, sizeof( szGameDir ) - 1 );
	Host_Shutdown( );
	exit( Host_Main( g_iArgc, g_pszArgv, szGameDir, 1, &Launcher_ChangeGame ) );
#endif
}

#if XASH_WIN32
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
int __stdcall WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow)
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

	ret = main( szArgc, szArgv );

	for( i = 0; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );

	return ret;
}
#endif // XASH_WIN32

int main( int argc, char** argv )
{
	const char *game = getenv( E_GAME );

	if( !game )
		game = GAME_PATH;

	Q_strncpy( szGameDir, game, sizeof( szGameDir ));

#if XASH_EMSCRIPTEN
#ifdef EMSCRIPTEN_LIB_FS
	// For some unknown reason emscripten refusing to load libraries later
	COM_LoadLibrary("menu", 0 );
	COM_LoadLibrary("server", 0 );
	COM_LoadLibrary("client", 0 );
#endif
#if XASH_DEDICATED
	// NodeJS support for debug
	EM_ASM(try{
		FS.mkdir('/xash');
		FS.mount(NODEFS, { root: '.'}, '/xash' );
		FS.chdir('/xash');
	}catch(e){};);
#endif
#endif

	g_iArgc = argc;
	g_pszArgv = argv;
#if XASH_IOS
	{
		void IOS_LaunchDialog( void );
		IOS_LaunchDialog();
	}
#endif
	return Host_Main( g_iArgc, g_pszArgv, szGameDir, 0, &Launcher_ChangeGame );
}

#endif
