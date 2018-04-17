/*
library.c - custom dlls loader
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#define _GNU_SOURCE

#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"

char lasterror[1024] = "";
const char *COM_GetLibraryError()
{
	return lasterror;
}

void COM_ResetLibraryError()
{
	lasterror[0] = 0;
}

void COM_PushLibraryError( const char *error )
{
	Q_strncat( lasterror, error, sizeof( lasterror ) );
	Q_strncat( lasterror, "\n", sizeof( lasterror ) );
}

void *COM_FunctionFromName_SR( void *hInstance, const char *pName )
{
#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	if( !Q_memcmp( pName, "ofs:",4 ) )
		return svgame.dllFuncs.pfnGameInit + Q_atoi(pName + 4);
#endif
	return COM_FunctionFromName( hInstance, pName );
}

#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
char *COM_OffsetNameForFunction( void *function )
{
	static string sname;
	Q_snprintf( sname, MAX_STRING, "ofs:%d", (int)(void*)(function - (void*)svgame.dllFuncs.pfnGameInit) );
	MsgDev( D_NOTE, "COM_OffsetNameForFunction %s\n", sname );
	return sname;
}
#endif

#ifndef _WIN32

#ifdef __ANDROID__
#include "platform/android/dlsym-weak.h"
#endif


#ifdef NO_LIBDL

#ifndef DLL_LOADER
#error Enable at least one dll backend!!!
#endif

void *dlsym(void *handle, const char *symbol )
{
	MsgDev( D_NOTE, "dlsym( %p, \"%s\" ): stub\n", handle, symbol );
	return NULL;
}
void *dlopen(const char *name, int flag )
{
	MsgDev( D_NOTE, "dlopen( \"%s\", %d ): stub\n", name, flag );
	return NULL;
}
int dlclose(void *handle)
{
	MsgDev( D_NOTE, "dlsym( %p ): stub\n", handle );
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



#endif
#ifdef XASH_SDL
#include <SDL_filesystem.h>
#endif

#if TARGET_OS_IPHONE

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
static void *IOS_LoadLibrary( const char *dllname )
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

#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void *COM_LoadLibrary( const char *dllname, int build_ordinals_table, qboolean directpath )
{
	dll_user_t *hInst = NULL;
	void *pHandle = NULL;

	// platforms where gameinfo mechanism is impossible
	// or not implemented
#if TARGET_OS_IPHONE
	{
		return IOS_LoadLibrary( dllname );
	}
#elif defined( __EMSCRIPTEN__ )
	{
#ifdef EMSCRIPTEN_LIB_FS
		char path[MAX_SYSPATH];
		string prefix;
		Q_strcpy(prefix, getenv( "LIBRARY_PREFIX" ) );
		Q_snprintf( path, MAX_SYSPATH, "%s%s%s",  prefix, dllname, getenv( "LIBRARY_SUFFIX" ) );
		pHandle = dlopen( path, RTLD_LAZY );
		if( !pHandle )
		{
			COM_PushLibraryError( va("Loading %s:\n", path ) );
			COM_PushLibraryError( dlerror() );
		}
		return pHandle;
#else
		// get handle of preloaded library outside fs
		return EM_ASM_INT( return DLFCN.loadedLibNames[Pointer_stringify($0)], (int)dllname );
#endif
	}
#elif defined( __ANDROID__ )
	{
		char path[MAX_SYSPATH];
		const char *libdir[2];
		int i;

		libdir[0] = getenv("XASH3D_GAMELIBDIR");
		libdir[1] = getenv("XASH3D_ENGLIBDIR");

		for( i = 0; i < 2; i++ )
		{
			Q_snprintf( path, MAX_SYSPATH, "%s/lib%s"POSTFIX"."OS_LIB_EXT, libdir[i], dllname );
			pHandle = dlopen( path, RTLD_LAZY );
			if( pHandle )
				return pHandle;

			COM_PushLibraryError( dlerror() );
		}

		// HACKHACK: keep old behaviour for compability
		pHandle = dlopen( dllname, RTLD_LAZY );
		if( pHandle )
			return pHandle;

		COM_PushLibraryError( dlerror() );
	}
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
	return dlsym( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *pName )
{
	void *function;
#ifdef DLL_LOADER
	void *wm;
	if( host.enabledll && (wm = Loader_GetDllHandle( hInstance )) )
		return Loader_GetProcAddress(hInstance, pName);
	else
#endif
	if( !( function = dlsym( hInstance, pName ) ) )
	{
#ifdef __ANDROID__
		// Shitty Android's dlsym don't resolve weak symbols
		if( !( function = dlsym_weak( hInstance, pName ) ) )
#endif
		{
			MsgDev(D_ERROR, "FunctionFromName: Can't get symbol %s: %s\n", pName, dlerror());
		}
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
	// Note: dladdr() is a glibc extension
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
#elif defined XASH_64BIT
#include <dbghelp.h>
void *COM_LoadLibrary( const char *dllname, int build_ordinals_table )
{
	return LoadLibraryA( dllname );
}
void COM_FreeLibrary( void *hInstance )
{
	FreeLibrary( hInstance );
}


void *COM_GetProcAddress( void *hInstance, const char *name )
{
	return GetProcAddress( hInstance, name );
}

void *COM_FunctionFromName( void *hInstance, const char *name )
{
	return GetProcAddress( hInstance, name );
}

const char *COM_NameForFunction( void *hInstance, void *function )
{
#if 0
	static qboolean initialized = false;
	if( initialized )
	{
		char message[1024];
		int len = 0;
		size_t i;
		HANDLE process = GetCurrentProcess();
		HANDLE thread = GetCurrentThread();
		IMAGEHLP_LINE64 line;
		DWORD dline = 0;
		DWORD options;
		CONTEXT context;
		STACKFRAME64 stackframe;
		DWORD image;
		char buffer[sizeof( IMAGEHLP_SYMBOL64) + MAX_SYM_NAME * sizeof(TCHAR)];
		PIMAGEHLP_SYMBOL64 symbol = ( PIMAGEHLP_SYMBOL64)buffer;
		memset( symbol, 0, sizeof(IMAGEHLP_SYMBOL64) + MAX_SYM_NAME );
		symbol->SizeOfStruct = sizeof( IMAGEHLP_SYMBOL64);
		symbol->MaxNameLength = MAX_SYM_NAME;
		DWORD displacement = 0;

		options = SymGetOptions();
		SymSetOptions( options );

		SymInitialize( process, NULL, TRUE );

		if( SymGetSymFromAddr64( process, function, &displacement, symbol ) )
		{
			Msg( "%s\n", symbol->Name );
			return copystring( symbol->Name );
		}

	}
#endif

#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	return COM_OffsetNameForFunction( function );
#endif

	return NULL;
}

#endif
