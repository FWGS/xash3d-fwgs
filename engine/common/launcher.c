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
#elif XASH_WIN32
extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#define E_GAME	"XASH3D_GAME" // default env dir to start from
#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR	"valve"
#endif

static char        szGameDir[128]; // safe place to keep gamedir
static int         szArgc;
static char        **szArgv;

static void Sys_ChangeGame( const char *progname )
{
	// a1ba: may never be called within engine
	// if platform supports execv() function
	Q_strncpy( szGameDir, progname, sizeof( szGameDir ) - 1 );
	Host_Shutdown( );
	exit( Host_Main( szArgc, szArgv, szGameDir, 1, &Sys_ChangeGame ) );
}

_inline int Sys_Start( void )
{
	int ret;
	const char *game = getenv( E_GAME );

	if( !game )
		game = XASH_GAMEDIR;

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
#elif XASH_IOS
	{
		void IOS_LaunchDialog( void );
		IOS_LaunchDialog();
	}
#endif

	ret = Host_Main( szArgc, szArgv, game, 0, Sys_ChangeGame );

	return ret;
}

#if !XASH_WIN32
int main( int argc, char **argv )
{
#if XASH_PSVITA
	// inject -dev -console into args if required
	szArgc = PSVita_GetArgv( argc, argv, &szArgv );
#else
	szArgc = argc;
	szArgv = argv;
#endif // XASH_PSVITA
	return Sys_Start();
}
#else
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

	ret = Sys_Start();

	for( ; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );

	return ret;
}
#endif // XASH_WIN32


#endif
