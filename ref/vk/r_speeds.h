#pragma once
#include "xash3d_types.h"
#include <stdint.h>

void R_SpeedsInit( void );

void R_ShowExtendedProfilingData(uint32_t prev_frame_index, uint64_t gpu_frame_begin_ns, uint64_t gpu_frame_end_ns);

// Called from the engine into ref_api to get the latest speeds info
qboolean R_SpeedsMessage( char *out, size_t size );

void R_SpeedsRegisterMetric( int* p_value, const char *name, const char *unit );
