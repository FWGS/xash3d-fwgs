#include "common.h"
#include "server.h"
#include "gamecube/dll_gamecube.h"

extern void EXPORT GiveFnptrsToDll( enginefuncs_t *engfuncs, globalvars_t *pGlobals );
extern int EXPORT GetEntityAPI( DLL_FUNCTIONS *pFunctionTable, int interfaceVersion );
extern int EXPORT GetEntityAPI2( DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion );
#if !XASH_GAMECUBE_HLSDK_SERVER_STATIC
extern void EXPORT custom( entvars_t *pev );
#endif

static dllexport_t gamecube_server_exports[] =
{
	{ "GiveFnptrsToDll", (void *)GiveFnptrsToDll },
	{ "GetEntityAPI", (void *)GetEntityAPI },
	{ "GetEntityAPI2", (void *)GetEntityAPI2 },
#if !XASH_GAMECUBE_HLSDK_SERVER_STATIC
	{ "custom", (void *)custom },
#endif
	{ NULL, NULL },
};

int setup_gamecube_server_exports( void )
{
	int ret = 0;

	ret |= dll_register( "dlls/hl_gamecube_ppc.so", gamecube_server_exports );
	ret |= dll_register( "dlls/hl_unknown_ppc.so", gamecube_server_exports );
	ret |= dll_register( "dlls/hl.so", gamecube_server_exports );
	ret |= dll_register( "dlls/hl.dll", gamecube_server_exports );
	ret |= dll_register( "bin/progs.so", gamecube_server_exports );
	ret |= dll_register( "hl.so", gamecube_server_exports );
	ret |= dll_register( "hl", gamecube_server_exports );
	ret |= dll_register( "server", gamecube_server_exports );

	return ret;
}
