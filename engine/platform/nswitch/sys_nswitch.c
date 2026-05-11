/*
switch.c - switch backend
Copyright (C) 2021-2023 fgsfds

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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <switch.h>
#include <solder.h>
#include <SDL.h>

static int nxlink_sock = -1;

/* HACKHACK: force-export stuff required by the dynamic libs */

// this is required by some std::filesystem crap in libstdc++
// we don't have it defined in our libc
long pathconf( const char *path, int name ) { return -1; }

// part of libunwind; required by any dynamic lib that uses C++ exceptions
extern void *_Unwind_Resume;
extern void *_Unwind_GetIPInfo;

// these are macros in our libc, so we need to wrap them
static int tolower_fn( int c ) { return tolower( c ); }
static int toupper_fn( int c ) { return toupper( c ); }
static int isalnum_fn( int c ) { return isalnum( c ); }
static int isalpha_fn( int c ) { return isalpha( c ); }

static const solder_export_t aux_exports[] =
{
	SOLDER_EXPORT( "tolower", tolower_fn ),
	SOLDER_EXPORT( "toupper", toupper_fn ),
	SOLDER_EXPORT( "isalnum", isalnum_fn ),
	SOLDER_EXPORT( "isalpha", isalpha_fn ),
	SOLDER_EXPORT_SYMBOL( mkdir ),
	SOLDER_EXPORT_SYMBOL( remove ),
	SOLDER_EXPORT_SYMBOL( rename ),
	SOLDER_EXPORT_SYMBOL( pathconf ),
	SOLDER_EXPORT_SYMBOL( fsync ),
	SOLDER_EXPORT_SYMBOL( strchrnul ),
	SOLDER_EXPORT_SYMBOL( stpcpy ),
	SOLDER_EXPORT_SYMBOL( _Unwind_Resume ),
	SOLDER_EXPORT_SYMBOL( _Unwind_GetIPInfo ),
};

const solder_export_t *__solder_aux_exports = aux_exports;
const size_t __solder_num_aux_exports = sizeof( aux_exports ) / sizeof( *aux_exports );

/* end of export crap */

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

#if XASH_MESSAGEBOX == MSGBOX_NSWITCH
void Platform_MessageBox( const char *title, const char *message, qboolean unused )
{
	// TODO: maybe figure out how to show an actual messagebox or an on-screen console
	//       without murdering the renderer
	// assume this is a fatal error
	FILE *f = fopen( "fatal.log", "w" );
	if ( f )
	{
		fprintf( f, "%s:\n%s\n", title, message );
		fclose( f );
	}
	// dump to nxlink as well
	fprintf( stderr, "%s:\n%s\n", title, message );
}
#endif // XASH_MESSAGEBOX == MSGBOX_NSWITCH

// this gets executed before main(), do not delete
void userAppInit( void )
{
	socketInitializeDefault( );
#ifdef NSWITCH_DEBUG
	nxlink_sock = nxlinkStdio( );
#endif
	if ( solder_init( 0 ) < 0 )
	{
		fprintf( stderr, "solder_init() failed: %s\n", solder_dlerror() );
		fflush( stderr );
		exit( 1 );
	}
}

// this gets executed on exit(), do not delete
void userAppExit( void )
{
	solder_quit( );
	if ( nxlink_sock >= 0 )
	{
		close( nxlink_sock );
		nxlink_sock = -1;
	}
	socketExit( );
}

void NSwitch_Init( void )
{
	printf( "%s\n", __func__ );
}

void NSwitch_Shutdown( void )
{
	printf( "%s\n", __func__ );
}
