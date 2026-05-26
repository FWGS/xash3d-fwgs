#ifndef FILESYSTEM_TEST_COMMON_H
#define FILESYSTEM_TEST_COMMON_H

#include "port.h"
#include "build.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "filesystem.h"

#if XASH_POSIX
#include <dlfcn.h>
#define LoadLibrary( x ) dlopen( x, RTLD_NOW )
#define GetProcAddress( x, y ) dlsym( x, y )
#define FreeLibrary( x ) dlclose( x )
#elif XASH_WIN32
#include <windows.h>
#endif

// each test program includes this header exactly once, so static linkage is fine
static void *g_hModule;
static FSAPI g_pfnGetFSAPI;
static fs_api_t g_fs;
static fs_globals_t *g_nullglobals;
#ifdef __cplusplus
static pfnCreateInterface_t g_pfnCreateInterface;
#endif

static qboolean LoadFilesystem( void )
{
	g_hModule = LoadLibrary( "filesystem_stdio." OS_LIB_EXT );
	if( !g_hModule )
		return false;

	g_pfnGetFSAPI = (FSAPI)GetProcAddress( g_hModule, GET_FS_API );
	if( !g_pfnGetFSAPI )
		return false;

	if( !g_pfnGetFSAPI( FS_API_VERSION, &g_fs, &g_nullglobals, NULL ))
		return false;

#ifdef __cplusplus
	if( !g_nullglobals )
		return false;

	g_pfnCreateInterface = (pfnCreateInterface_t)GetProcAddress( g_hModule, "CreateInterface" );
	if( !g_pfnCreateInterface )
		return false;

	int temp = -1;
	if( !g_pfnCreateInterface( FILESYSTEM_INTERFACE_VERSION, &temp ) || temp != 0 )
		return false;

	temp = -1;
	if( !g_pfnCreateInterface( FS_API_CREATEINTERFACE_TAG, &temp ) || temp != 0 )
		return false;
#endif

	return true;
}

#endif // FILESYSTEM_TEST_COMMON_H
