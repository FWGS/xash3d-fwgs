#pragma once
// #include "xash3d_types.h"
#include "const.h"
#include "cvardef.h"
#include "ref_api.h"
#include "crtlib.h"

extern ref_api_t gEngine;
extern ref_globals_t* gGlobals;

#define ASSERT( exp ) if(!( exp )) gEngine.Host_Error( "assert " #exp " failed at %s:%i\n", __FILE__, __LINE__ )

#define Mem_Malloc( pool, size ) gEngine._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) gEngine._Mem_Calloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngine._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) gEngine._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngine._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngine._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngine._Mem_EmptyPool( pool, __FILE__, __LINE__ )
#define Mem_IsAllocated( mem ) gEngine._Mem_IsAllocatedExt( NULL, mem )
#define Mem_Check() gEngine._Mem_Check( __FILE__, __LINE__ )



// #define MAX(x, y) ({
//     typeof(x) _x = (x);
//     typeof(y) _y = (y);
//     (void) &(x) == &(y);
//     (x) > (y) ? (x) :(y);
// })


