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

void *ANDROID_LoadLibrary( const char *dllname )
{
	char path[MAX_SYSPATH];
	const char *libdir[2];
	int i;
	void *pHandle = NULL;

	libdir[0] = getenv("XASH3D_GAMELIBDIR");
	libdir[1] = getenv("XASH3D_ENGLIBDIR");

	for( i = 0; i < 2; i++ )
	{
		if( !libdir[i] )
			continue;

		Q_snprintf( path, MAX_SYSPATH, "%s/lib%s"POSTFIX"."OS_LIB_EXT, libdir[i], dllname );
		pHandle = dlopen( path, RTLD_LAZY );
		if( pHandle )
			return pHandle;

		COM_PushLibraryError( dlerror() );
	}

	// HACKHACK: keep old behaviour for compability
	if( Q_strstr( dllname, "." OS_LIB_EXT ) || Q_strstr( dllname, PATH_SPLITTER ))
	{
		pHandle = dlopen( dllname, RTLD_LAZY );
		if( pHandle )
			return pHandle;
	}
	else
	{
		Q_snprintf( path, MAX_SYSPATH, "lib%s"POSTFIX"."OS_LIB_EXT, dllname );
		pHandle = dlopen( path, RTLD_LAZY );
		if( pHandle )
			return pHandle;
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
