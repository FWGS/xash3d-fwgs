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
#ifdef TARGET_OS_IPHONE
#include <SDL.h>
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include "platform/ios/lib_ios.h"

static void *IOS_LoadLibraryInternal( const char *dllname )
{
	void *pHandle;
	string errorstring = "";
	char path[MAX_SYSPATH];
	
	// load frameworks from Documents directory
	// frameworks should be signed with same key with application
	// Useful for debug to prevent rebuilding app on every library update
	// NOTE: Apple polices forbids loading code from shared places
#ifdef ENABLE_FRAMEWORK_SIDELOAD
	Q_snprintf( path, MAX_SYSPATH, "%s.framework/lib", dllname );
	if( pHandle = dlopen( path, RTLD_LAZY ) )
		return pHandle;
	Q_snprintf( errorstring, MAX_STRING, dlerror() );
#endif
	
#ifdef DLOPEN_FRAMEWORKS
	// load frameworks as it should be located in Xcode builds
	Q_snprintf( path, MAX_SYSPATH, "%s%s.framework/lib", SDL_GetBasePath(), dllname );
#else
	// load libraries from app root to allow re-signing ipa with custom utilities
	Q_snprintf( path, MAX_SYSPATH, "%s%s", SDL_GetBasePath(), dllname );
#endif
	pHandle = dlopen( path, RTLD_LAZY );
	if( !pHandle )
	{
		COM_PushLibraryError(errorstring);
		COM_PushLibraryError(dlerror());
	}
	return pHandle;
}
extern char *g_szLibrarySuffix;
void *IOS_LoadLibrary( const char *dllname )
{

	string name;
	char *postfix = g_szLibrarySuffix;
	char *pHandle;

	if( !postfix ) postfix = GI->gamefolder;

	Q_snprintf( name, MAX_STRING, "%s_%s", dllname, postfix );
	pHandle = IOS_LoadLibraryInternal( name );
	if( pHandle )
		return pHandle;
	return IOS_LoadLibraryInternal( dllname );
}
#endif // TARGET_OS_IPHONE
