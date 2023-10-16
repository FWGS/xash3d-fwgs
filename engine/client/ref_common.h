/*
ref_common.h - Xash3D render dll API
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#if !defined REF_COMMON_H && !defined REF_DLL
#define REF_COMMON_H

#include "ref_api.h"

#define RP_LOCALCLIENT( e )	((e) != NULL && (e)->index == ( cl.playernum + 1 ) && e->player )

struct ref_state_s
{
	qboolean initialized;

	HINSTANCE hInstance;
	ref_interface_t dllFuncs;

	// depends on build configuration
	int numRenderers;
	const char **shortNames;
	const char **readableNames;
};

extern struct ref_state_s ref;
extern ref_globals_t refState;

// handy API wrappers
void R_GetTextureParms( int *w, int *h, int texnum );
#define REF_GET_PARM( parm, arg ) ref.dllFuncs.RefGetParm( (parm), (arg) )
#define GL_LoadTextureInternal( name, pic, flags ) ref.dllFuncs.GL_LoadTextureFromBuffer( (name), (pic), (flags), false )
#define GL_UpdateTextureInternal( name, pic, flags ) ref.dllFuncs.GL_LoadTextureFromBuffer( (name), (pic), (flags), true )
#define R_GetBuiltinTexture( name ) ref.dllFuncs.GL_LoadTexture( (name), 0, 0, 0 )

void GL_RenderFrame( const struct ref_viewpass_s *rvp );

// common engine and renderer cvars
extern convar_t	r_decals;
extern convar_t	r_adjust_fov;
extern convar_t gl_clear;

qboolean R_Init( void );
void R_Shutdown( void );
void R_UpdateRefState( void );

extern triangleapi_t gTriApi;

#endif // REF_COMMON_H
