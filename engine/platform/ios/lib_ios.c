/*
ios_lib.c - dynamic library code for iOS
Copyright (C) 2017-2018 mittorn

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <string.h>
#include <unistd.h>
#include <SDL.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include "crtlib.h"
#include "library.h"
#include "platform/ios/lib_ios.h"
#include "common.h"

const char *g_szLibrarySuffix;

static void *IOS_LoadLibraryInternal( const char *dllname )
{
	void *pHandle;
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", SDL_GetBasePath(), dllname );
	pHandle = dlopen( path, RTLD_LAZY );

	if( !pHandle )
		COM_PushLibraryError(dlerror( ));

	return pHandle;
}

static qboolean IOS_LibraryExistsInternal( const char *name )
{
	struct stat buf;
	char path[MAX_SYSPATH];

	Q_snprintf( path, sizeof( path ), "%s%s", SDL_GetBasePath(), name );

	return stat( path, &buf ) == 0;
}

static const char *IOS_GetLibraryPostfix( void )
{
	if( g_szLibrarySuffix )
		return g_szLibrarySuffix;

	return Q_strcmp( host.default_gamedir, FS_Gamedir( )) ? FS_Gamedir( ) : "";
}

static void IOS_PrepareGameLibraryPath( const char *dllname, char *out, size_t outsize )
{
	string strippedname;

	Q_strncpy( strippedname, dllname, sizeof( strippedname ));
	COM_StripExtension( strippedname );

	Q_snprintf( out, outsize, "%s_%s.dylib", strippedname, IOS_GetLibraryPostfix( ));
}

void *IOS_LoadLibrary( const char *dllname )
{
	// filesystem_stdio is a special case, as engine won't work correctly without it
	// but it's always located at known path
	if( !Q_strcmp( dllname, "filesystem_stdio.dylib" ))
		return IOS_LoadLibraryInternal( dllname );

	string name;
	char *pHandle;

	IOS_PrepareGameLibraryPath( dllname, name, sizeof( name ));
	pHandle = IOS_LoadLibraryInternal( name );

	if( !pHandle )
		pHandle = IOS_LoadLibraryInternal( dllname );

	return pHandle;
}

qboolean IOS_LibraryExists( const char *dllname )
{
	string name;

	IOS_PrepareGameLibraryPath( dllname, name, sizeof( name ));

	return IOS_LibraryExistsInternal( name ) || IOS_LibraryExistsInternal( dllname );
}
