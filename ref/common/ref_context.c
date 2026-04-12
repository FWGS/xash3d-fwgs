/*
ref_context.c - shared renderer context
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "ref_common.h"

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;
ref_host_t    *gp_host;

int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals );
int EXPORT GetRefAPI( int version, ref_interface_t *funcs, ref_api_t *engfuncs, ref_globals_t *globals )
{
	if( version != REF_API_VERSION )
		return 0;

	// fill in our callbacks
	*funcs = gReffuncs;
	gEngfuncs = *engfuncs;
	gpGlobals = globals;

	gp_cl = (ref_client_t *)ENGINE_GET_PARM( PARM_GET_CLIENT_PTR );
	gp_host = (ref_host_t *)ENGINE_GET_PARM( PARM_GET_HOST_PTR );

	return REF_API_VERSION;
}
