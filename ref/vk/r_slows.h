#pragma once
#include <stdint.h>

void R_SlowsInit( void );

void R_ShowExtendedProfilingData(uint32_t prev_frame_event_index, uint64_t gpu_time_ns);
