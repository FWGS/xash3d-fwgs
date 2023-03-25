#pragma once

#include "xash3d_types.h"

struct cl_entity_s;
struct beam_s;

qboolean R_BeamCull( const vec3_t start, const vec3_t end, qboolean pvsOnly );

void R_BeamDrawCustomEntity( struct cl_entity_s *ent, float frametime );
void R_BeamDraw( struct beam_s *pbeam, float frametime );

qboolean R_BeamInit(void);
