/*
android_lib.c - dynamic library code for Android OS
Copyright (C) 2018 Flying With Gauss

This program is free software: you can redistribute it and/sor modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include "platform/android/lib_android.h"
#include "platform/android/dlsym-weak.h" // Android < 5.0

void *ANDROID_LoadLibrary( const char *path )
{
	const char *libdir[2], *name = COM_FileWithoutPath( path );
	char fullpath[MAX_SYSPATH];
	void *handle;

	libdir[0] = getenv( "XASH3D_GAMELIBDIR" ); // TODO: remove this once distributing games from APKs will be deprecated
	libdir[1] = NULL; // TODO: put here data directory where libraries will be downloaded to

	for( int i = 0; i < ARRAYSIZE( libdir ); i++ )
	{
		// this is an APK directory, get base path
		const char *p = i == 0 ? name : path;

		if( !libdir[i] )
			continue;

		Q_snprintf( fullpath, sizeof( fullpath ), "%s/%s", libdir[i], p );

		handle = dlopen( fullpath, RTLD_NOW );

		if( handle )
		{
			Con_Reportf( "%s: loading library %s successful\n", __func__, fullpath );
			return handle;
		}

		COM_PushLibraryError( dlerror() );
	}

	// find in system search path, that includes our APK
	handle = dlopen( name, RTLD_NOW );
	if( handle )
	{
		Con_Reportf( "%s: loading library %s from LD_LIBRARY_PATH successful\n", __func__, name );
		return handle;
	}
	COM_PushLibraryError( dlerror() );

	return NULL;
}

void *ANDROID_GetProcAddress( void *hInstance, const char *name )
{
	void *p = dlsym( hInstance, name );

#ifndef XASH_64BIT
	if( p ) return p;

	p = dlsym_weak( hInstance, name );
#endif // XASH_64BIT

	return p;
}
