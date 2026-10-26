/*
engine.cpp - engine startup for Sailfish OS, game_launch counterpart
Copyright (C) 2026 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine.h"

typedef void (*pfnChangeGame)( const char *progname );
typedef int (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

int Engine_Main( int argc, char **argv )
{
	char basedir[4096];

	snprintf( basedir, sizeof( basedir ), "%s/" ENGINE_BASEDIR, getenv( "HOME" ));
	setenv( "XASH3D_BASEDIR", basedir, 0 );
	setenv( "XASH3D_RODIR", "/usr/share/" XASH_PACKAGE_NAME "/lib/rodir", 0 );

	// found through the binary's RPATH
	void *engine = dlopen( "libxash.so", RTLD_NOW | RTLD_LOCAL );

	if( !engine )
	{
		fprintf( stderr, "Unable to load libxash.so: %s\n", dlerror( ));
		return 1;
	}

	pfnInit Host_Main = (pfnInit)dlsym( engine, "Host_Main" );
	pfnShutdown Host_Shutdown = (pfnShutdown)dlsym( engine, "Host_Shutdown" );

	if( !Host_Main )
	{
		fprintf( stderr, "libxash.so missed 'Host_Main' export: %s\n", dlerror( ));
		dlclose( engine );
		return 1;
	}

	// no change game, the launcher passes game specific arguments, like -dll for mobile_hacks mods
	int ret = Host_Main( argc, argv, "valve", 0, NULL );

	if( Host_Shutdown )
		Host_Shutdown();

	dlclose( engine );
	return ret;
}
