//#include "xash3d_types.h"
#include "const.h" // required for ref_api.h
#include "cvardef.h"
#include "com_model.h"
#include "ref_api.h"
#include "crtlib.h"

#define ASSERT(x) if(!( x )) gEngine.Host_Error( "assert " #x " failed at %s:%i\n", __FILE__, __LINE__ )

#define Mem_Malloc( pool, size ) gEngine._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) gEngine._Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngine._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) gEngine._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngine._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngine._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngine._Mem_EmptyPool( pool, __FILE__, __LINE__ )

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;
