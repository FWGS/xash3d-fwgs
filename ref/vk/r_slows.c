#include "r_slows.h"
#include "vk_light.h" // For stats
#include "shaders/ray_interop.h" // stats: struct LightCluster
#include "vk_overlay.h"
#include "vk_framectl.h"
#include "vk_cvar.h"

#include "profiler.h"

#include "crclib.h" // CRC32 for stable random colors
#include "xash3d_mathlib.h" // Q_min

#define MAX_SPEEDS_MESSAGE 1024
#define MAX_FRAMES_HISTORY 256
#define TARGET_FRAME_TIME (1000.f / 60.f)

// Valid bits for `r_speeds` argument:
enum {
	SPEEDS_BIT_OFF = 0,       // `r_speeds 0` turns off all performance stats display
	SPEEDS_BIT_SIMPLE = 1,    // `r_speeds 1` displays only basic info about frame time
	SPEEDS_BIT_STATS = 2,     // `r_speeds 2` displays additional metrics, i.e. lights counts, dynamic geometry upload sizes, etc (TODO)
	SPEEDS_BIT_GPU_USAGE = 4, // `r_speeds 4` displays overall GPU usage stats (TODO)
	SPEEDS_BIT_FRAME = 8,     // `r_speeds 8` diplays details instrumental profiler frame data, e.g. specific functions times graphs, etc

	// These bits can be combined, e.g. `r_speeds 9`, 8+1, will display 1: basic timing info and 8: frame graphs
};


static struct {
	float frame_times[MAX_FRAMES_HISTORY];
	uint32_t frame_num;

	aprof_event_t *paused_events;
	int paused_events_count;
	int pause_requested;

	struct {
		int glyph_width, glyph_height;
	} font_metrics;

	struct {
		uint64_t ref_cpu_time;
		uint64_t ref_cpu_wait_time;
		char message[MAX_SPEEDS_MESSAGE];
	} frame;
} g_slows;

static void speedsPrintf( const char *msg, ... ) {
	va_list	argptr;
	char	text[MAX_SPEEDS_MESSAGE];

	va_start( argptr, msg );
	Q_vsprintf( text, msg, argptr );
	va_end( argptr );

	Q_strncat( g_slows.frame.message, text, sizeof( g_slows.frame.message ));
}

static float linearstep(float min, float max, float v) {
	if (v <= min) return 0;
	if (v >= max) return 1;
	return (v - min) / (max - min);
}

#define P(fmt, ...) gEngine.Con_Reportf(fmt, ##__VA_ARGS__)

static uint32_t getHash(const char *s) {
	dword crc;
	CRC32_Init(&crc);
	CRC32_ProcessBuffer(&crc, s, Q_strlen(s));
	return CRC32_Final(crc);
}

static void drawTimeBar(uint64_t begin_time_ns, float time_scale_ms, int64_t begin_ns, int64_t end_ns, int y, int height, const char *label, const rgba_t color) {
	const float delta_ms = (end_ns - begin_ns) * 1e-6;
	const int width = delta_ms  * time_scale_ms;
	const int x = (begin_ns - begin_time_ns) * 1e-6 * time_scale_ms;

	rgba_t text_color = {255-color[0], 255-color[1], 255-color[2], 255};
	CL_FillRGBA(x, y, width, height, color[0], color[1], color[2], color[3]);

	// Tweak this if scope names escape the block boundaries
	char tmp[64];
	tmp[0] = '\0';
	const int glyph_width = g_slows.font_metrics.glyph_width;
	Q_snprintf(tmp, Q_min(sizeof(tmp), width / glyph_width), "%s %.3fms", label, delta_ms);
	gEngine.Con_DrawString(x, y, tmp, text_color);
}

static void drawProfilerScopes(int draw, const aprof_event_t *events, uint64_t begin_time, float time_scale_ms, uint32_t begin, uint32_t end, int y) {
#define MAX_STACK_DEPTH 16
	struct {
		int scope_id;
		uint64_t begin_ns;
	} stack[MAX_STACK_DEPTH];
	int depth = 0;
	int max_depth = 0;

	int under_waiting = 0;
	g_slows.frame.ref_cpu_time = 0;
	g_slows.frame.ref_cpu_wait_time = 0;

	for (; begin != end; begin = (begin + 1) % APROF_EVENT_BUFFER_SIZE) {
		const aprof_event_t event = events[begin];
		const int event_type = APROF_EVENT_TYPE(event);
		const uint64_t timestamp_ns = APROF_EVENT_TIMESTAMP(event);
		const int scope_id = APROF_EVENT_SCOPE_ID(event);
		switch (event_type) {
			case APROF_EVENT_FRAME_BOUNDARY:
				g_slows.frame.ref_cpu_time = 0;
				g_slows.frame.ref_cpu_wait_time = 0;
				under_waiting = 0;
				break;

			case APROF_EVENT_SCOPE_BEGIN: {
					if (depth < MAX_STACK_DEPTH) {
						stack[depth].begin_ns = timestamp_ns;
						stack[depth].scope_id = scope_id;
					}
					++depth;
					if (max_depth < depth)
						max_depth = depth;

					const aprof_scope_t *const scope = g_aprof.scopes + scope_id;
					if (scope->flags & APROF_SCOPE_FLAG_WAIT)
						under_waiting++;

					break;
				}

			case APROF_EVENT_SCOPE_END: {
					ASSERT(depth > 0);
					--depth;

					ASSERT(stack[depth].scope_id == scope_id);

					const aprof_scope_t *const scope = g_aprof.scopes + scope_id;
					const uint32_t hash = getHash(scope->name);

					const uint64_t delta_ns = timestamp_ns - stack[depth].begin_ns;

					// This is a top level scope that should be counter towards cpu usage
					const int is_top_level = ((scope->flags & APROF_SCOPE_FLAG_DECOR) == 0) && (depth == 0 || (g_aprof.scopes[stack[depth-1].scope_id].flags & APROF_SCOPE_FLAG_DECOR));

					// Only count top level scopes towards CPU time, and only if it's not waiting
					if (is_top_level && under_waiting == 0)
						g_slows.frame.ref_cpu_time += delta_ns;

					// If this is a top level waiting scope (under any depth)
					if (under_waiting == 1) {
						// Count it towards waiting time
						g_slows.frame.ref_cpu_wait_time += delta_ns;

						// If this is not a top level scope, then we might count its top level parent
						// towards cpu usage time, which is not correct. Subtract this waiting time from it.
						if (!is_top_level)
							g_slows.frame.ref_cpu_time -= delta_ns;
					}

					if (scope->flags & APROF_SCOPE_FLAG_WAIT)
						under_waiting--;

					if (draw) {
						const rgba_t color = {hash >> 24, (hash>>16)&0xff, hash&0xff, 127};
						const int bar_height = g_slows.font_metrics.glyph_height;
						drawTimeBar(begin_time, time_scale_ms, stack[depth].begin_ns, timestamp_ns, y + depth * bar_height, bar_height, scope->name, color);
					}
					break;
				}

			default:
				break;
		}
	}

	if (max_depth > MAX_STACK_DEPTH)
		gEngine.Con_NPrintf(4, S_ERROR "Profiler stack overflow: reached %d, max available %d\n", max_depth, MAX_STACK_DEPTH);
}

static void handlePause( uint32_t prev_frame_index ) {
	if (!g_slows.pause_requested || g_slows.paused_events)
		return;

	const uint32_t frame_begin = prev_frame_index;
	const uint32_t frame_end = g_aprof.events_last_frame + 1;

	g_slows.paused_events_count = frame_end >= frame_begin ? frame_end - frame_begin : (frame_end + APROF_EVENT_BUFFER_SIZE - frame_begin);
	g_slows.paused_events = Mem_Malloc(vk_core.pool, g_slows.paused_events_count * sizeof(g_slows.paused_events[0]));

	if (frame_end >= frame_begin) {
		memcpy(g_slows.paused_events, g_aprof.events + frame_begin, g_slows.paused_events_count * sizeof(g_slows.paused_events[0]));
	} else {
		const int first_chunk = (APROF_EVENT_BUFFER_SIZE - frame_begin) * sizeof(g_slows.paused_events[0]);
		memcpy(g_slows.paused_events, g_aprof.events + frame_begin, first_chunk);
		memcpy(g_slows.paused_events + first_chunk, g_aprof.events, frame_end * sizeof(g_slows.paused_events[0]));
	}
}

static int drawFrameTimeGraph( const int frame_bar_y, const float frame_bar_y_scale ) {
	const float width = (float)vk_frame.width / MAX_FRAMES_HISTORY;

	// 60fps
	CL_FillRGBA(0, frame_bar_y + frame_bar_y_scale * TARGET_FRAME_TIME, vk_frame.width, 1, 0, 255, 0, 50);

	// 30fps
	CL_FillRGBA(0, frame_bar_y + frame_bar_y_scale * TARGET_FRAME_TIME * 2, vk_frame.width, 1, 255, 0, 0, 50);

	for (int i = 0; i < MAX_FRAMES_HISTORY; ++i) {
		const float frame_time = g_slows.frame_times[(g_slows.frame_num + i) % MAX_FRAMES_HISTORY];

		// > 60 fps => 0, 30..60 fps -> 1..0, <30fps => 1
		const float time = linearstep(TARGET_FRAME_TIME, TARGET_FRAME_TIME*2.f, frame_time);
		const int red = 255 * time;
		const int green = 255 * (1 - time);
		CL_FillRGBA(i * width, frame_bar_y, width, frame_time * frame_bar_y_scale, red, green, 0, 127);
	}

	return frame_bar_y + frame_bar_y_scale * TARGET_FRAME_TIME * 2;
}

static int drawFrames( int draw, uint32_t prev_frame_index, int y, const uint64_t gpu_frame_begin_ns, const uint64_t gpu_frame_end_ns ) {
	// Draw latest 2 frames; find their boundaries
	uint32_t rewind_frame = prev_frame_index;
	const int max_frames_to_draw = 2;
	for (int frame = 1; frame < max_frames_to_draw;) {
		rewind_frame = (rewind_frame - 1) % APROF_EVENT_BUFFER_SIZE; // NOTE: only correct for power-of-2 buffer sizes
		const aprof_event_t event = g_aprof.events[rewind_frame];

		// Exhausted all events
		if (event == 0 || rewind_frame == g_aprof.events_write)
			break;

		// Note the frame
		if (APROF_EVENT_TYPE(event) == APROF_EVENT_FRAME_BOUNDARY) {
			++frame;
			prev_frame_index = rewind_frame;
		}
	}

	const aprof_event_t *const events = g_slows.paused_events ? g_slows.paused_events : g_aprof.events;
	const int event_begin = g_slows.paused_events ? 0 : prev_frame_index;
	const int event_end = g_slows.paused_events ? g_slows.paused_events_count - 1 : g_aprof.events_last_frame;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(events[event_begin]);
	const uint64_t frame_end_time = APROF_EVENT_TIMESTAMP(events[event_end]);
	const uint64_t delta_ns = frame_end_time - frame_begin_time;
	const float time_scale_ms = (double)vk_frame.width / (delta_ns / 1e6);
	drawProfilerScopes(draw, events, frame_begin_time, time_scale_ms, event_begin, event_end, y);

	// Draw GPU last frame bar
	if (draw) {
		y += g_slows.font_metrics.glyph_height * 6;
		const int bar_height = g_slows.font_metrics.glyph_height;
		const rgba_t color = {255, 255, 0, 127};
		drawTimeBar(frame_begin_time, time_scale_ms, gpu_frame_begin_ns, gpu_frame_end_ns, y, bar_height, "GPU TIME", color);
	}
	return y;
}

static void getCurrentFontMetrics(void) {
	// hidpi scaling
	float scale = gEngine.pfnGetCvarFloat("con_fontscale");
	if (scale <= 0.f)
		scale = 1.f;

	// TODO these numbers are mostly fine for the "default" font. Unfortunately
	// we don't have any access to real font metrics from here, ref_api_t doesn't give us anything about fonts. ;_;
	g_slows.font_metrics.glyph_width = 8 * scale;
	g_slows.font_metrics.glyph_height = 20 * scale;
}

void R_ShowExtendedProfilingData(uint32_t prev_frame_index, uint64_t gpu_frame_begin_ns, uint64_t gpu_frame_end_ns) {
	APROF_SCOPE_DECLARE_BEGIN(__FUNCTION__, __FUNCTION__);

	g_slows.frame.message[0] = '\0';

	const uint32_t speeds_bits = r_speeds->value;

	if (speeds_bits)
		speedsPrintf( "Renderer: ^1Vulkan%s^7\n", vk_frame.rtx_enabled ? " RT" : "" );

	const uint32_t events = g_aprof.events_last_frame - prev_frame_index;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(g_aprof.events[prev_frame_index]);
	const unsigned long long delta_ns = APROF_EVENT_TIMESTAMP(g_aprof.events[g_aprof.events_last_frame]) - frame_begin_time;
	const float frame_time = delta_ns * 1e-6;

	g_slows.frame_times[g_slows.frame_num] = frame_time;
	g_slows.frame_num = (g_slows.frame_num + 1) % MAX_FRAMES_HISTORY;

	handlePause( prev_frame_index );

	{
		getCurrentFontMetrics();
		int y = 100;
		const float frame_bar_y_scale = 2.f; // ms to pixels (somehow)
																				 //
		const int draw = speeds_bits & SPEEDS_BIT_FRAME;
		if (draw)
			y = drawFrameTimeGraph( y, frame_bar_y_scale ) + 20;

		y = drawFrames( draw, prev_frame_index, y, gpu_frame_begin_ns, gpu_frame_end_ns );
	}

	if (speeds_bits & SPEEDS_BIT_SIMPLE) {
		const uint64_t gpu_time_ns = gpu_frame_end_ns - gpu_frame_begin_ns;
		speedsPrintf("frame: %.03fms GPU: %.03fms\n", frame_time, gpu_time_ns * 1e-6);
		speedsPrintf("  (ref) CPU: %.03fms wait: %.03fms\n", g_slows.frame.ref_cpu_time * 1e-6, g_slows.frame.ref_cpu_wait_time * 1e-6);
	}

	if (speeds_bits & SPEEDS_BIT_STATS) {
		const int dirty = g_lights.stats.dirty_cells;
		speedsPrintf("profiler events: %u, wraps: %d\n", events, g_aprof.current_frame_wraparounds, frame_time);
		speedsPrintf("Dirty light cells: %d\n\tsize = %dKiB, ranges = %d\n", dirty, (int)(dirty * sizeof(struct LightCluster) / 1024), g_lights.stats.ranges_uploaded);
	}

	APROF_SCOPE_END(__FUNCTION__);
}

static void togglePause( void ) {
	if (g_slows.paused_events) {
		Mem_Free(g_slows.paused_events);
		g_slows.paused_events = NULL;
		g_slows.paused_events_count = 0;
		g_slows.pause_requested = 0;
	} else {
		g_slows.pause_requested = 1;
	}

}

void R_SlowsInit( void ) {
	gEngine.Cmd_AddCommand("r_slows_toggle_pause", togglePause, "Toggle frame profiler pause");
}

// grab r_speeds message
qboolean R_SpeedsMessage( char *out, size_t size )
{
	if( gEngine.drawFuncs->R_SpeedsMessage != NULL )
	{
		if( gEngine.drawFuncs->R_SpeedsMessage( out, size ))
			return true;
		// otherwise pass to default handler
	}

	if( r_speeds->value <= 0 ) return false;
	if( !out || !size ) return false;

	Q_strncpy( out, g_slows.frame.message, size );

	return true;
}
