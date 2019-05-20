/*
lib_posix.c - dynamic library code for POSIX systems
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

#ifndef _WIN32 // see win_lib.c
#define _GNU_SOURCE

#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include "platform/android/lib_android.h"
#include "platform/emscripten/lib_em.h"
#include "platform/ios/lib_ios.h"

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

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst = NULL;
	void *pHandle = NULL;

	COM_ResetLibraryError();

	// platforms where gameinfo mechanism is impossible
#if TARGET_OS_IPHONE
	return IOS_LoadLibrary( dllname );
#elif defined( __EMSCRIPTEN__ )
	return EMSCRIPTEN_LoadLibrary( dllname );
#elif defined( __ANDROID__ )
	return ANDROID_LoadLibrary( dllname );
#endif

	// platforms where gameinfo mechanism is working goes here
	// and use FS_FindLibrary
	hInst = FS_FindLibrary( dllname, false );
	if( !hInst )
	{
		// HACKHACK: direct load dll
#ifdef DLL_LOADER
		if( host.enabledll && ( pHandle = Loader_LoadLibrary(dllname)) )
		{
			return pHandle;
		}
#endif

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

#ifdef DLL_LOADER
	if( host.enabledll && ( !Q_stricmp( FS_FileExtension( hInst->shortPath ), "dll" ) ) )
	{
		if( hInst->encrypted )
		{
			COM_PushLibraryError( va( "Library %s is encrypted. Cannot load", hInst->shortPath ) );
			Mem_Free( hInst );
			return NULL;
		}

		if( !( hInst->hInstance = Loader_LoadLibrary( hInst->fullPath ) ) )
		{
			COM_PushLibraryError( va( "Failed to load DLL with DLL loader: %s", hInst->shortPath ) );
			Mem_Free( hInst );
			return NULL;
		}
	}
	else
#endif
	{
		if( !( hInst->hInstance = dlopen( hInst->fullPath, RTLD_LAZY ) ) )
		{
			COM_PushLibraryError( dlerror() );
			Mem_Free( hInst );
			return NULL;
		}
	}

	pHandle = hInst->hInstance;

	Mem_Free( hInst );

	return pHandle;
}

void COM_FreeLibrary( void *hInstance )
{
#ifdef DLL_LOADER
	void *wm;
	if( host.enabledll && (wm = Loader_GetDllHandle( hInstance )) )
		return Loader_FreeLibrary( hInstance );
	else
#endif
#if !defined __EMSCRIPTEN__ || defined EMSCRIPTEN_LIB_FS
	dlclose( hInstance );
#endif
}

void *COM_GetProcAddress( void *hInstance, const char *name )
{
#ifdef DLL_LOADER
	void *wm;
	if( host.enabledll && (wm = Loader_GetDllHandle( hInstance )) )
		return Loader_GetProcAddress(hInstance, name);
	else
#endif
#if defined(__ANDROID__)
	return ANDROID_GetProcAddress( hInstance, name );
#else
	return dlsym( hInstance, name );
#endif
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

#ifdef XASH_DYNAMIC_DLADDR
int d_dladdr( void *sym, Dl_info *info )
{
	static int (*dladdr_real) ( void *sym, Dl_info *info );

	if( !dladdr_real )
		dladdr_real = dlsym( (void*)(size_t)(-1), "dladdr" );

	Q_memset( info, 0, sizeof( *info ) );

	if( !dladdr_real )
		return -1;

	return dladdr_real(  sym, info );
}
#endif

const char *COM_NameForFunction( void *hInstance, void *function )
{
#ifdef DLL_LOADER
	void *wm;
	if( host.enabledll && (wm = Loader_GetDllHandle( hInstance )) )
		return Loader_GetFuncName_int(wm, function);
	else
#endif
	// NOTE: dladdr() is a glibc extension
	{
		Dl_info info = {0};
		dladdr((void*)function, &info);
		if(info.dli_sname)
			return info.dli_sname;
	}
#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#else
	return NULL;
#endif
}

#endif // _WIN32
