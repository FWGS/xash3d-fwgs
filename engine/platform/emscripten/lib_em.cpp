#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "common.h"
#include "library.h"
#include "filesystem.h"
#include "server.h"
#include <dlfcn.h>

void *EMSCRIPTEN_LoadLibrary( const char *dllname )
{
	return nullptr;

	void *pHandle = NULL;

#ifdef EMSCRIPTEN_LIB_FS
	char path[MAX_SYSPATH], buf[MAX_VA_STRING];
	string prefix;
	strcpy(prefix, getenv( "LIBRARY_PREFIX" ) );
	
	Q_snprintf( path, MAX_SYSPATH, "%s%s", dllname, "_emscripten_javascript.so");
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
	return (void*)EM_ASM_INT( return DLFCN.loadedLibNames[Pointer_stringify($0)], (int)dllname );
#endif
}

#endif // __EMSCRIPTEN__
