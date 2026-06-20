/*
r_refcontext_vars.c - shared cvar storage and GetRefAPI for static GameCube link
*/
#include "ref_api.h"
#include "com_strings.h"

extern const ref_interface_t gReffuncs;
extern ref_api_t      gEngfuncs;
extern ref_globals_t *gpGlobals;
extern ref_client_t  *gp_cl;
extern ref_host_t    *gp_host;
extern struct movevars_s *gp_movevars;
extern dlight_t      *gp_dlights;
extern convar_t r_dlight_virtual_radius;
extern convar_t r_lighting_extended;

#define ENGINE_GET_PARM_ (*gEngfuncs.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( (parm), 0 )

DEFINE_ENGINE_SHARED_CVAR_LIST()

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	*funcs = gReffuncs;
	gEngfuncs = *engfuncs;
	gpGlobals = globals;

	gp_cl = (ref_client_t *)ENGINE_GET_PARM( PARM_GET_CLIENT_PTR );
	gp_host = (ref_host_t *)ENGINE_GET_PARM( PARM_GET_HOST_PTR );
	gp_movevars = (struct movevars_s *)ENGINE_GET_PARM( PARM_GET_MOVEVARS_PTR );
	gp_dlights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_DLIGHTS_PTR );

	RETRIEVE_ENGINE_SHARED_CVAR_LIST();

	gEngfuncs.Cvar_RegisterVariable( &r_dlight_virtual_radius );
	gEngfuncs.Cvar_RegisterVariable( &r_lighting_extended );

	return REF_API_VERSION;
}
