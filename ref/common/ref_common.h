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
extern const ref_interface_t gReffuncs;

#define ENGINE_GET_PARM_ (*gEngfuncs.EngineGetParm)
#define ENGINE_GET_PARM( parm ) ENGINE_GET_PARM_( (parm), 0 )

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

#endif // REF_COMMON_H
