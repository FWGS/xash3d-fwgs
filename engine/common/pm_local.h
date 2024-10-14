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
float PM_TraceModel( playermove_t *pmove, physent_t *pe, float *start, float *end, trace_t *trace );
pmtrace_t *PM_TraceLine( playermove_t *pmove, float *start, float *end, int flags, int usehull, int ignore_pe );
pmtrace_t *PM_TraceLineEx( playermove_t *pmove, float *start, float *end, int flags, int usehull, pfnIgnore pmFilter );
struct msurface_s *PM_TraceSurfacePmove( playermove_t *pmove, int ground, float *vstart, float *vend );
const char *PM_TraceTexture( playermove_t *pmove, int ground, float *vstart, float *vend );
int PM_PointContentsPmove( playermove_t *pmove, const float *p, int *truecontents );
void PM_StuckTouch( playermove_t *pmove, int hitent, pmtrace_t *tr );

static inline void PM_ConvertTrace( trace_t *out, pmtrace_t *in, edict_t *ent )
{
	out->allsolid = in->allsolid;
	out->startsolid = in->startsolid;
	out->inopen = in->inopen;
	out->inwater = in->inwater;
	out->fraction = in->fraction;
	out->plane.dist = in->plane.dist;
	out->hitgroup = in->hitgroup;
	out->ent = ent;

	VectorCopy( in->endpos, out->endpos );
	VectorCopy( in->plane.normal, out->plane.normal );
}

static inline void PM_ClearPhysEnts( playermove_t *pmove )
{
	pmove->nummoveent = 0;
	pmove->numphysent = 0;
	pmove->numvisent = 0;
	pmove->numtouch = 0;
}

static inline void PM_InitTrace( trace_t *trace, const vec3_t end )
{
	memset( trace, 0, sizeof( *trace ));
	VectorCopy( end, trace->endpos );
	trace->allsolid = true;
	trace->fraction = 1.0f;
}

static inline void PM_InitPMTrace( pmtrace_t *trace, const vec3_t end )
{
	memset( trace, 0, sizeof( *trace ));
	VectorCopy( end, trace->endpos );
	trace->allsolid = true;
	trace->fraction = 1.0f;
}

//
// pm_surface.c
//
msurface_t *PM_RecursiveSurfCheck( model_t *model, mnode_t *node, vec3_t p1, vec3_t p2 );
msurface_t *PM_TraceSurface( physent_t *pe, vec3_t start, vec3_t end );
int PM_TestLineExt( playermove_t *pmove, physent_t *ents, int numents, const vec3_t start, const vec3_t end, int flags );

#endif//PM_LOCAL_H
