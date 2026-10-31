#include "filesystem.h"

extern "C"
{
extern int GetFSAPI( int version, fs_api_t *api, fs_globals_t **globals, fs_interface_t *engfuncs );
extern void *CreateInterface( const char *interface, int *retval );

struct {const char *name; void *func;} lib_filesystem_stdio_exports[] = {
	{ "GetFSAPI", (void*)GetFSAPI },
	{ "CreateInterface", (void*)CreateInterface },
	{ 0, 0 }
};
}

