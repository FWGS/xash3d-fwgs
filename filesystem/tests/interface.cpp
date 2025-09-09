#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "port.h"
#include "build.h"
#include "VFileSystem009.h"
#include "filesystem.h"

#if XASH_POSIX
#include <dlfcn.h>
#define LoadLibrary( x ) dlopen( x, RTLD_NOW )
#define GetProcAddress( x, y ) dlsym( x, y )
#define FreeLibrary( x ) dlclose( x )
typedef void *HMODULE;
#elif XASH_WIN32
#include <windows.h>
#endif

HMODULE g_hModule;
FSAPI g_pfnGetFSAPI;
pfnCreateInterface_t g_pfnCreateInterface;
fs_api_t g_fs;
fs_globals_t *g_nullglobals;

static bool LoadFilesystem()
{
	int temp = -1;

	g_hModule = LoadLibrary( "filesystem_stdio." OS_LIB_EXT );
	if( !g_hModule )
		return false;

	// check our C-style interface existence
	g_pfnGetFSAPI = reinterpret_cast<FSAPI>( GetProcAddress( g_hModule, GET_FS_API ));
	if( !g_pfnGetFSAPI )
		return false;

	g_nullglobals = NULL;
	if( !g_pfnGetFSAPI( FS_API_VERSION, &g_fs, &g_nullglobals, NULL ))
		return false;

	if( !g_nullglobals )
		return false;

	// check Valve-style interface existence
	g_pfnCreateInterface = reinterpret_cast<pfnCreateInterface_t>( GetProcAddress( g_hModule, "CreateInterface" ));
	if( !g_pfnCreateInterface )
		return false;

	if( !g_pfnCreateInterface( FILESYSTEM_INTERFACE_VERSION, &temp ) || temp != 0 )
		return false;

	temp = -1;

	if( !g_pfnCreateInterface( FS_API_CREATEINTERFACE_TAG, &temp ) || temp != 0 )
		return false;

	return true;
}

int main()
{
	if( !LoadFilesystem() )
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
