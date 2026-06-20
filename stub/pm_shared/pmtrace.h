#pragma once
#ifndef PMTRACE_H
#define PMTRACE_H

#include "xash3d_types.h"

typedef struct pmplane_s
{
	vec3_t normal;
	float dist;
} pmplane_t;

typedef struct pmtrace_s
{
	qboolean allsolid;
	qboolean startsolid;
	qboolean inopen, inwater;
	float fraction;
	vec3_t endpos;
	pmplane_t plane;
	int ent;
	vec3_t deltavelocity;
	int hitgroup;
} pmtrace_t;

#endif /* PMTRACE_H */
