/*
ref_common.h - shared renderer code
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef REF_COMMON_H
#define REF_COMMON_H

#include "xash3d_mathlib.h"
#include "ref_api.h"

//
// ref_context.c
//
extern ref_api_t      gEngfuncs;
extern ref_globals_t *gpGlobals;
extern ref_client_t  *gp_cl;
extern ref_host_t    *gp_host;
extern struct movevars_s *gp_movevars;
extern const ref_interface_t gReffuncs;

DECLARE_ENGINE_SHARED_CVAR_LIST()

#define Assert( x ) if( !( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )

#define ENGINE_GET_PARM_ (*gEngfuncs.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( (parm), 0 )

extern uint16_t rtable[MOD_FRAMES][MOD_FRAMES];
void GL_InitRandomTable( void );

void _Mem_Free( void *data, const char *filename, int fileline );
void *_Mem_Alloc( poolhandle_t poolptr, size_t size, qboolean clear, const char *filename, int fileline )
	ALLOC_CHECK( 2 ) MALLOC_LIKE( _Mem_Free, 1 ) WARN_UNUSED_RESULT;
void *_Mem_Realloc( poolhandle_t poolptr, void *memptr, size_t size, qboolean clear, const char *filename, int fileline )
	ALLOC_CHECK( 3 ) WARN_UNUSED_RESULT;

#define Mem_Malloc( pool, size )       _Mem_Alloc( pool, size, false, __FILE__, __LINE__ )
#define Mem_Calloc( pool, size )       _Mem_Alloc( pool, size, true, __FILE__, __LINE__ )
#define Mem_Realloc( pool, ptr, size ) _Mem_Realloc( pool, ptr, size, true, __FILE__, __LINE__ )
#define Mem_Free( mem )                _Mem_Free( mem, __FILE__, __LINE__ )
#define Mem_AllocPool( name )          gEngfuncs._Mem_AllocPool( name, __FILE__, __LINE__ )
#define Mem_FreePool( pool )           gEngfuncs._Mem_FreePool( pool, __FILE__, __LINE__ )
#define Mem_EmptyPool( pool )          gEngfuncs._Mem_EmptyPool( pool, __FILE__, __LINE__ )

extern dlight_t *gp_dlights;
extern int g_lightstylevalue[MAX_LIGHTSTYLES];
extern poolhandle_t r_temppool;

//
// ref_common cvars
//
extern convar_t r_dlight_virtual_radius;
extern convar_t r_lighting_extended;

//
// ref_math.c
//
void Matrix4x4_Concat( matrix4x4 out, const matrix4x4 in1, const matrix4x4 in2 );
void Matrix4x4_ConcatTranslate( matrix4x4 out, float x, float y, float z );
void Matrix4x4_ConcatRotate( matrix4x4 out, float angle, float x, float y, float z );
void Matrix4x4_CreateProjection( matrix4x4 out, float xMax, float xMin, float yMax, float yMin, float zNear, float zFar );
void Matrix4x4_CreateOrtho( matrix4x4 m, float xLeft, float xRight, float yBottom, float yTop, float zNear, float zFar );
void Matrix4x4_CreateModelview( matrix4x4 out );
void Matrix4x4_ToArrayFloatGL( const matrix4x4 in, float out[16] );

//
// ref_light.c
//
void CL_RunLightStyles( lightstyle_t *ls );
void R_PushDlightsForBmodel( model_t *model, int framecount, const matrix4x4 object_matrix );
int R_PushDlights( model_t *model, int framecount );
colorVec R_LightVec( const vec3_t start, const vec3_t end, vec3_t lspot, vec3_t lvec );
colorVec R_LightPoint( const vec3_t p0 );
void R_EntityDynamicLight( cl_entity_t *ent, alight_t *plight, qboolean draw_world, double time, vec3_t lightspot, vec3_t lightvec );
void R_GatherPlayerLight( cl_entity_t *view );
void R_UpdateSurfaceCachedLight( msurface_t *surf );

//
// ref_image.c
//
byte *GL_ResampleTexture( const byte *source, int in_w, int in_h, int out_w, int out_h, qboolean isNormalMap );

#endif // REF_COMMON_H
