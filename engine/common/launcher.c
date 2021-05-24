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

#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "common.h"
#ifdef XASH_SDLMAIN
#include "SDL.h"
#endif

#if XASH_EMSCRIPTEN
#include <emscripten.h>
#endif

#if XASH_WIN32
#define XASH_NOCONHOST 1
#endif

#if XASH_NSWITCH
#include <switch.h>
#endif

static char szGameDir[128]; // safe place to keep gamedir
static int g_iArgc;
static char **g_pszArgv;

void Launcher_ChangeGame( const char *progname )
{
#if XASH_NSWITCH
	char argv[4096];
	const char *exe = g_pszArgv[0];
	// envSetNextLoad wants a single command line string
	// TODO: carry over the old argv
	snprintf( argv, sizeof( argv ), "%s -game %s", exe, progname );
	// just restart the entire thing
	printf( "envSetNextLoad exe: `%s`\n", exe );
	printf( "envSetNextLoad argv:\n`%s`\n", argv );
	Host_Shutdown( );
	envSetNextLoad( exe, argv );
	exit( 0 );
#else
	strncpy( szGameDir, progname, sizeof( szGameDir ) - 1 );
	Host_Shutdown( );
	exit( Host_Main( g_iArgc, g_pszArgv, szGameDir, 1, &Launcher_ChangeGame ) );
#endif
}

#if XASH_NOCONHOST
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
int __stdcall WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow)
{
	int szArgc;
	char **szArgv;
	LPWSTR* lpArgv = CommandLineToArgvW(GetCommandLineW(), &szArgc);
	int i = 0;

	szArgv = (char**)malloc(szArgc*sizeof(char*));
	for (; i < szArgc; ++i)
	{
		size_t size = wcslen(lpArgv[i]) + 1;
		szArgv[i] = (char*)malloc(size);
		wcstombs(szArgv[i], lpArgv[i], size);
	}
	szArgv[i] = NULL;

	LocalFree(lpArgv);

	main( szArgc, szArgv );

	for( i = 0; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );
}
#endif
int main( int argc, char** argv )
{
	char gamedir_buf[32] = "";
	const char *gamedir = getenv( "XASH3D_GAMEDIR" );

	if( !COM_CheckString( gamedir ) )
	{
		gamedir = "valve";
	}
	else
	{
		strncpy( gamedir_buf, gamedir, 32 );
		gamedir = gamedir_buf;
	}

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
	return Host_Main( g_iArgc, g_pszArgv, gamedir, 0, &Launcher_ChangeGame );
}

#endif
