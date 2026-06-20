#include "common.h"
#include "client.h"
#include "gamecube/dll_gamecube.h"

extern void EXPORT GetClientAPI( cldll_func_t *funcs );

static dllexport_t gamecube_client_exports[] =
{
	{ "GetClientAPI", (void *)GetClientAPI },
	{ NULL, NULL },
};

int setup_gamecube_client_exports( void )
{
	int ret = 0;

	ret |= dll_register( "cl_dlls/client.so", gamecube_client_exports );
	ret |= dll_register( "client.so", gamecube_client_exports );
	ret |= dll_register( "client", gamecube_client_exports );

	return ret;
}
