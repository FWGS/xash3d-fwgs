#pragma once
#include "xash3d_types.h"
#include <stdint.h>

void R_SpeedsInit( void );

void R_ShowExtendedProfilingData(uint32_t prev_frame_index, uint64_t gpu_frame_begin_ns, uint64_t gpu_frame_end_ns);

// Called from the engine into ref_api to get the latest speeds info
qboolean R_SpeedsMessage( char *out, size_t size );

typedef struct {
	int value;
	const char *name;
	const char *unit;
	// int low_watermark, high_watermark;
} r_speeds_metric_t;

r_speeds_metric_t *R_SpeedsRegisterMetric( const char *name, const char *unit );
