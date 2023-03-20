#include "const.h" // required for ref_api.h
#include "cvardef.h"
#include "com_model.h"
#include "ref_api.h"
#include "com_strings.h"
#include "crtlib.h"

#define ASSERT(x) if(!( x )) gEngine.Host_Error( "assert %s failed at %s:%d\n", #x, __FILE__, __LINE__ )
// TODO ASSERTF(x, fmt, ...)

#define Mem_Malloc( pool, size ) gEngine._Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size ) gEngine._Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) gEngine._Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem ) gEngine._Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name ) gEngine._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool ) gEngine._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool ) gEngine._Mem_EmptyPool( pool, __FILE__, __LINE__ )

#define PRINT_NOT_IMPLEMENTED_ARGS(msg, ...) do { \
		static int called = 0; \
		if ((called&1023) == 0) { \
			gEngine.Con_Printf( S_ERROR "VK NOT_IMPLEMENTED(x%d): %s " msg "\n", called, __FUNCTION__, ##__VA_ARGS__ ); \
		} \
		++called; \
	} while(0)

#define PRINT_NOT_IMPLEMENTED() do { \
		static int called = 0; \
		if ((called&1023) == 0) { \
			gEngine.Con_Printf( S_ERROR "VK NOT_IMPLEMENTED(x%d): %s\n", called, __FUNCTION__ ); \
		} \
		++called; \
	} while(0)

#define PRINT_THROTTLED(delay, prefix, msg, ...) do { \
		static int called = 0; \
		static double next_message_time = 0.; \
		if (gpGlobals->realtime > next_message_time) { \
			gEngine.Con_Printf( prefix "(x%d) " msg "\n", called, ##__VA_ARGS__ ); \
			next_message_time = gpGlobals->realtime + delay; \
		} \
		++called; \
	} while(0)

#define ERROR_THROTTLED(delay, msg, ...) PRINT_THROTTLED(delay, S_ERROR, msg, ##__VA_ARGS__)

#define ALIGN_UP(ptr, align) ((((ptr) + (align) - 1) / (align)) * (align))

#define COUNTOF(a) (sizeof(a)/sizeof((a)[0]))

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;
