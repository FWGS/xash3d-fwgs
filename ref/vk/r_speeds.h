#pragma once
#include "xash3d_types.h"
#include <stdint.h>

void R_SpeedsInit( void );

struct vk_combuf_scopes_s;
void R_SpeedsDisplayMore(uint32_t prev_frame_index, const struct vk_combuf_scopes_s *gpurofl);

// Called from the engine into ref_api to get the latest speeds info
qboolean R_SpeedsMessage( char *out, size_t size );

typedef enum {
	kSpeedsMetricCount,
	kSpeedsMetricBytes,
	kSpeedsMetricMicroseconds,
} r_speeds_metric_type_t;

void R_SpeedsRegisterMetric(int* p_value, const char *name, r_speeds_metric_type_t type);
