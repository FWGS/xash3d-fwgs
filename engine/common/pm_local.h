/*
pm_local.h - player move interface
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

#ifndef PM_LOCAL_H
#define PM_LOCAL_H

#include "pm_defs.h"

typedef int (*pfnIgnore)( physent_t *pe );	// custom trace filter

//
// pm_debug.c
//
void PM_ParticleLine( const vec3_t start, const vec3_t end, int pcolor, float life, float zvel );
void PM_DrawBBox( const vec3_t mins, const vec3_t maxs, const vec3_t origin, int pcolor, float life );

//
// pm_trace.c
//
void Pmove_Init( void );
void PM_InitBoxHull( void );
hull_t *PM_HullForBsp( physent_t *pe, playermove_t *pmove, float *offset );
qboolean PM_RecursiveHullCheck( hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, pmtrace_t *trace );
pmtrace_t PM_PlayerTraceExt( playermove_t *pm, vec3_t p1, vec3_t p2, int flags, int numents, physent_t *ents, int ignore_pe, pfnIgnore pmFilter );
int PM_TestPlayerPosition( playermove_t *pmove, vec3_t pos, pmtrace_t *ptrace, pfnIgnore pmFilter );
int PM_HullPointContents( hull_t *hull, int num, const vec3_t p );
int PM_TruePointContents( playermove_t *pmove, const vec3_t p );
int PM_PointContents( playermove_t *pmove, const vec3_t p );
void PM_ConvertTrace( trace_t *out, pmtrace_t *in, edict_t *ent );

//
// pm_surface.c
//
const char *PM_TraceTexture( physent_t *pe, vec3_t vstart, vec3_t vend );
msurface_t *PM_RecursiveSurfCheck( model_t *model, mnode_t *node, vec3_t p1, vec3_t p2 );
msurface_t *PM_TraceSurface( physent_t *pe, vec3_t start, vec3_t end );
int PM_TestLineExt( playermove_t *pmove, physent_t *ents, int numents, const vec3_t start, const vec3_t end, int flags );

#endif//PM_LOCAL_H
