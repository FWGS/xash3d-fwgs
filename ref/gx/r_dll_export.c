#include "ref_api.h"
#include "dll_gamecube.h"

extern int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );

static dllexport_t gamecube_ref_exports[] =
{
	{ "GetRefAPI", (void *)GetRefAPI },
	{ NULL, NULL },
};

int setup_gamecube_ref_exports( void )
{
	return dll_register( "libref_gx.so", gamecube_ref_exports );
}
