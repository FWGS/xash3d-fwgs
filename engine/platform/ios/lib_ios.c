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
#include <SDL.h>
#include "crtlib.h"
#include "fscallback.h"
#include "library.h"
#include "platform/ios/lib_ios.h"
#include <dlfcn.h>

#define EXT_LENGTH 5

static void *IOS_LoadLibraryInternal( const char *dllname )
{
	void *pHandle;
	string errorstring = "";
	char path[MAX_SYSPATH];

	Q_snprintf( path, MAX_SYSPATH, "%s%s", SDL_GetBasePath(), dllname );
	pHandle = dlopen( path, RTLD_LAZY );

	if( !pHandle )
	{
		COM_PushLibraryError(errorstring);
		COM_PushLibraryError(dlerror());
	}

	return pHandle;
}
const char *g_szLibrarySuffix;
void *IOS_LoadLibrary( const char *dllname )
{
	//Immediately load if the library is filesystem_stdio.dylib or we will crash when accessing gamedir
	if ( !Q_strcmp( dllname, "filesystem_stdio.dylib" ) )
	{
		return IOS_LoadLibraryInternal( dllname );
	}

	string name;
	string strippedname;
	const char *postfix = g_szLibrarySuffix;
	char *pHandle;
	
	if( !postfix )
	{
		if ( Q_strcmp(FS_Gamedir(), "valve" ) )
		{
			postfix = FS_Gamedir( );
		}
		else postfix = "";
	}

	Q_strncpy( strippedname, dllname, strlen(dllname) - EXT_LENGTH );
	
	Q_snprintf( name, MAX_STRING, "%s_%s.dylib", strippedname, postfix );
	pHandle = IOS_LoadLibraryInternal( name );

	if( pHandle )
		return pHandle;

	return IOS_LoadLibraryInternal( dllname );
}
