#include "r_slows.h"
#include "vk_light.h" // For stats
#include "shaders/ray_interop.h" // stats: struct LightCluster
#include "vk_overlay.h"
#include "vk_framectl.h"

#include "profiler.h"

#define MAX_FRAMES_HISTORY 256
#define TARGET_FRAME_TIME (1000.f / 60.f)

static struct {
	float frame_times[MAX_FRAMES_HISTORY];
	uint32_t frame_num;
} g_slows;


static float linearstep(float min, float max, float v) {
	if (v <= min) return 0;
	if (v >= max) return 1;
	return (v - min) / (max - min);
}

// FIXME move this to r_speeds or something like that
void R_ShowExtendedProfilingData(uint32_t prev_frame_index) {
	{
		const int dirty = g_lights.stats.dirty_cells;
		gEngine.Con_NPrintf(4, "Dirty light cells: %d, size = %dKiB, ranges = %d\n", dirty, (int)(dirty * sizeof(struct LightCluster) / 1024), g_lights.stats.ranges_uploaded);
	}

	const uint32_t events = g_aprof.events_last_frame - prev_frame_index;
	const unsigned long long delta_ns = APROF_EVENT_TIMESTAMP(g_aprof.events[g_aprof.events_last_frame]) - APROF_EVENT_TIMESTAMP(g_aprof.events[prev_frame_index]);
	const float frame_time = delta_ns / 1e6;

	gEngine.Con_NPrintf(5, "aprof events this frame: %u, wraps: %d, frame time: %.03fms\n", events, g_aprof.current_frame_wraparounds, frame_time);

	g_slows.frame_times[g_slows.frame_num] = frame_time;
	g_slows.frame_num = (g_slows.frame_num + 1) % MAX_FRAMES_HISTORY;

	const float width = (float)vk_frame.width / MAX_FRAMES_HISTORY;
	for (int i = 0; i < MAX_FRAMES_HISTORY; ++i) {
		const float frame_time = g_slows.frame_times[(g_slows.frame_num + i) % MAX_FRAMES_HISTORY];

		// > 60 fps => 0, 30..60 fps -> 1..0, <30fps => 1
		const float time = linearstep(TARGET_FRAME_TIME, TARGET_FRAME_TIME*2.f, frame_time);
		const int red = 255 * time;
		const int green = 255 * (1 - time);
		CL_FillRGBA(i * width, 100, width, frame_time * 2.f, red, green, 0, 127);
	}

	/* gEngine.Con_NPrintf(5, "Perf scopes:"); */
	/* for (int i = 0; i < g_aprof.num_scopes; ++i) { */
	/* 	const aprof_scope_t *const scope = g_aprof.scopes + i; */
	/* 	gEngine.Con_NPrintf(6 + i, "%s: c%d t%.03f(%.03f)ms s%.03f(%.03f)ms", scope->name, */
	/* 		scope->frame.count, */
	/* 		scope->frame.duration / 1e6, */
	/* 		(scope->frame.duration / 1e6) / scope->frame.count, */
	/* 		(scope->frame.duration - scope->frame.duration_children) / 1e6, */
	/* 		(scope->frame.duration - scope->frame.duration_children) / 1e6 / scope->frame.count); */
	/* } */
}
