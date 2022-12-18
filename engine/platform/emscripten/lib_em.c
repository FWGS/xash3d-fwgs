/*
em_lib.h - dynamic library code for iOS
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
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"

void *EMSCRIPTEN_LoadLibrary( const char *dllname )
{
	void *pHandle = NULL;

#ifdef EMSCRIPTEN_LIB_FS
	char path[MAX_SYSPATH], buf[MAX_VA_STRING];
	string prefix;
	Q_strcpy(prefix, getenv( "LIBRARY_PREFIX" ) );
	Q_snprintf( path, MAX_SYSPATH, "%s%s%s",  prefix, dllname, getenv( "LIBRARY_SUFFIX" ) );
	pHandle = dlopen( path, RTLD_LAZY );
	if( !pHandle )
	{
		Q_snprintf( buf, sizeof( buf ), "Loading %s:\n", path );
		COM_PushLibraryError( buf );
		COM_PushLibraryError( dlerror() );
	}
	return pHandle;
#else
	// get handle of preloaded library outside fs
	return EM_ASM_INT( return DLFCN.loadedLibNames[Pointer_stringify($0)], (int)dllname );
#endif
}

#endif // __EMSCRIPTEN__
