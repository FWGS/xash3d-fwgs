//#include "xash3d_types.h"
#include "const.h"
#include "cvardef.h"
#include "ref_api.h"

#define ASSERT(x) if(!( x )) gEngine.Host_Error( "assert " #x " failed at %s:%i\n", __FILE__, __LINE__ )

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;
