/*
lib_psp.c - dynamic library code for Sony PSP system
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
#include "platform/platform.h"
#if XASH_LIB == LIB_PSP


#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include "platform/psp/lib_psp.h"

#ifdef XASH_NO_LIBDL
#ifndef XASH_DLL_LOADER
#error Enable at least one dll backend!!!
#endif // XASH_DLL_LOADER

void *dlsym(void *handle, const char *symbol )
{
	Con_DPrintf( "dlsym( %p, \"%s\" ): stub\n", handle, symbol );
	return NULL;
}

void *dlopen(const char *name, int flag )
{
	Con_DPrintf( "dlopen( \"%s\", %d ): stub\n", name, flag );
	return NULL;
}

int dlclose(void *handle)
{
	Con_DPrintf( "dlsym( %p ): stub\n", handle );
	return 0;
}

char *dlerror( void )
{
	return "Loading ELF libraries not supported in this build!\n";
}

int dladdr( const void *addr, Dl_info *info )
{
	return 0;
}
#endif // XASH_NO_LIBDL

qboolean COM_CheckLibraryDirectDependency( const char *name, const char *depname, qboolean directpath )
{
	// TODO: implement
	return true;
}

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst = NULL;
	void *pHandle = NULL;

	COM_ResetLibraryError();

	// platforms where gameinfo mechanism is working goes here
	// and use FS_FindLibrary
	hInst = FS_FindLibrary( dllname, directpath );
	if( !hInst )
	{
		// try to find by linker(LD_LIBRARY_PATH, DYLD_LIBRARY_PATH, LD_32_LIBRARY_PATH and so on...)
		if( !pHandle )
		{
			pHandle = dlopen( dllname, RTLD_LAZY );
			if( pHandle )
				return pHandle;

			COM_PushLibraryError( va( "Failed to find library %s", dllname ));
			COM_PushLibraryError( dlerror() );
			return NULL;
		}
	}

	if( hInst->custom_loader )
	{
		COM_PushLibraryError( va( "Custom library loader is not available. Extract library %s and fix gameinfo.txt!", hInst->fullPath ));
		Mem_Free( hInst );
		return NULL;
	}

	if( !( hInst->hInstance = dlopen( hInst->fullPath, RTLD_LAZY ) ) )
	{
		COM_PushLibraryError( dlerror() );
		Mem_Free( hInst );
		return NULL;
	}

	pHandle = hInst->hInstance;

	Mem_Free( hInst );

	return pHandle;
}

void COM_FreeLibrary( void *hInstance )
{
	dlclose( hInstance );
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
	return dlsym( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	void *function;
	if( !( function = COM_GetProcAddress( hInstance, pName ) ) )
	{
		Con_Reportf( S_ERROR "FunctionFromName: Can't get symbol %s: %s\n", pName, dlerror());
	}
	return function;
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
	// NOTE: dladdr() is a glibc extension
	Dl_info info = {0};
	dladdr((void*)function, &info);
	if(info.dli_sname)
		return info.dli_sname;
#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#else
	return NULL;
#endif
}

#endif // _WIN32
