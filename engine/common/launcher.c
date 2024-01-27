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

#if XASH_ENABLE_MAIN
#if XASH_SDLMAIN
#include <SDL.h>
#endif
#if XASH_EMSCRIPTEN
#include <emscripten.h>
#endif
#include "build.h"
#include "common.h"

#define E_GAME	"XASH3D_GAME" // default env dir to start from
#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR	"valve"
#endif

#if XASH_WIN32
#error "Single-binary or dedicated builds aren't supported for Windows!"
#endif

static char        szGameDir[128]; // safe place to keep gamedir
static int         szArgc;

#if XASH_PSP
#define MAX_NARGVS 50
static char        *szArgv[MAX_NARGVS];
#else
static char        **szArgv;
#endif

static void Sys_ChangeGame( const char *progname )
{
	// a1ba: may never be called within engine
	// if platform supports execv() function
	Q_strncpy( szGameDir, progname, sizeof( szGameDir ));
	Host_Shutdown( );
	exit( Host_Main( szArgc, szArgv, szGameDir, 1, &Sys_ChangeGame ) );
}

static int Sys_Start( void )
{
	const char *game = getenv( E_GAME );

	if( !game )
		game = XASH_GAMEDIR;

	Q_strncpy( szGameDir, game, sizeof( szGameDir ));

#if XASH_EMSCRIPTEN
#ifdef EMSCRIPTEN_LIB_FS
	// For some unknown reason emscripten refusing to load libraries later
	COM_LoadLibrary( "menu", 0 );
	COM_LoadLibrary( "server", 0 );
	COM_LoadLibrary( "client", 0 );
#endif
#if XASH_DEDICATED
	// NodeJS support for debug
	EM_ASM(try {
		FS.mkdir( '/xash' );
		FS.mount( NODEFS, { root: '.'}, '/xash' );
		FS.chdir( '/xash' );
	} catch( e ) { };);
#endif
#elif XASH_IOS
	IOS_LaunchDialog();
#elif XASH_PSP

	return Host_Main( szArgc, szArgv, game, 0, Sys_ChangeGame );
}

int main( int argc, char **argv )
{
#if XASH_PSP
	Platform_ReadCmd( "start.cmd", &szArgc, szArgv );
#elif XASH_PSVITA
	// inject -dev -console into args if required
	szArgc = PSVita_GetArgv( argc, argv, &szArgv );
#else
	szArgc = argc;
	szArgv = argv;
#endif // XASH_PSVITA

	return Sys_Start();
}
#endif // XASH_ENABLE_MAIN
