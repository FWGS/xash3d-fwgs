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
#include "com_strings.h"

DEFINE_ENGINE_SHARED_CVAR_LIST()

ref_api_t      gEngfuncs;
ref_globals_t *gpGlobals;
ref_client_t  *gp_cl;
ref_host_t    *gp_host;
struct movevars_s *gp_movevars;
uint16_t       rtable[MOD_FRAMES][MOD_FRAMES];
dlight_t      *gp_dlights;
int            g_lightstylevalue[MAX_LIGHTSTYLES];
poolhandle_t   r_temppool;

void _Mem_Free( void *data, const char *filename, int fileline )
{
	gEngfuncs._Mem_Free( data, filename, fileline );
}

void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return gEngfuncs._Mem_Alloc( poolptr, size, clear, filename, fileline );
}

void *_Mem_Realloc( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
{
	return gEngfuncs._Mem_Realloc( poolptr, memptr, size, clear, filename, fileline );
}

void GL_InitRandomTable( void )
{
	int tu, tv;

	for( tu = 0; tu < MOD_FRAMES; tu++ )
	{
		for( tv = 0; tv < MOD_FRAMES; tv++ )
		{
			rtable[tu][tv] = gEngfuncs.COM_RandomLong( 0, 0x7FFF );
		}
	}

	gEngfuncs.COM_SetRandomSeed( 0 );
}

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
	gp_movevars = (struct movevars_s *)ENGINE_GET_PARM( PARM_GET_MOVEVARS_PTR );
	gp_dlights = (dlight_t *)ENGINE_GET_PARM( PARM_GET_DLIGHTS_PTR );

	RETRIEVE_ENGINE_SHARED_CVAR_LIST();

	gEngfuncs.Cvar_RegisterVariable( &r_dlight_virtual_radius );
	gEngfuncs.Cvar_RegisterVariable( &r_lighting_extended );

	return REF_API_VERSION;
}
