/*
switch.c - switch backend
Copyright (C) 2021 fgsfds

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <switch.h>
#include <solder.h>
#include <SDL.h>

#ifdef NSWITCH_DEBUG
static int nxlink_sock = -1;
#endif

// HACKHACK: force these lads to link in
const solder_export_t solder_extra_exports[] =
{
	SOLDER_EXPORT_SYMBOL( vsprintf ),
	SOLDER_EXPORT_SYMBOL( isalpha ),
	SOLDER_EXPORT_SYMBOL( isalnum ),
	SOLDER_EXPORT_SYMBOL( tolower ),
	SOLDER_EXPORT_SYMBOL( toupper ),
	SOLDER_EXPORT_SYMBOL( stpcpy ),
};

void userAppInit( void )
{
	socketInitializeDefault();
#ifdef NSWITCH_DEBUG
	nxlink_sock = nxlinkStdio();
#endif
	if ( solder_init( 0 ) < 0 )
	{
		fprintf( stderr, "solder_init() failed: %s\n", solder_dlerror() );
		fflush( stderr );
		exit(1);
	}
}

void userAppExit( void )
{
	solder_quit();
#ifdef NSWITCH_DEBUG
	if ( nxlink_sock >= 0 )
	{
		close( nxlink_sock );
		nxlink_sock = -1;
	}
#endif
	socketExit();
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute `%s` -- not supported\n", path );
}

void NSwitch_Init( void )
{
	printf( "NSwitch_Init\n" );
}

void NSwitch_Shutdown( void )
{
	printf( "NSwitch_Shutdown\n" );
	// force deinit everything SDL-related to avoid issues with changing games
	if ( SDL_WasInit( 0 ) )
		SDL_Quit();
}

int NSwitch_GetScreenWidth( void )
{
	if ( appletGetOperationMode() == AppletOperationMode_Console )
		return 1920; // docked
	return 1280; // undocked
}

int NSwitch_GetScreenHeight( void )
{
	if ( appletGetOperationMode() == AppletOperationMode_Console )
		return 1080; // docked
	return 720; // undocked
}
