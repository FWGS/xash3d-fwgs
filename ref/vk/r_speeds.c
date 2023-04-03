#include "r_speeds.h"
#include "vk_overlay.h"
#include "vk_framectl.h"
#include "vk_cvar.h"
#include "vk_combuf.h"

#include "profiler.h"

#include "crclib.h" // CRC32 for stable random colors
#include "xash3d_mathlib.h" // Q_min
#include <limits.h>

#define MAX_SPEEDS_MESSAGE 1024
#define MAX_SPEEDS_METRICS (APROF_MAX_SCOPES + 4)
#define TARGET_FRAME_TIME (1000.f / 60.f)
#define MAX_GRAPHS 8

// Valid bits for `r_speeds` argument:
enum {
	SPEEDS_BIT_OFF = 0,       // `r_speeds 0` turns off all performance stats display
	SPEEDS_BIT_SIMPLE = 1,    // `r_speeds 1` displays only basic info about frame time
	SPEEDS_BIT_STATS = 2,     // `r_speeds 2` displays additional metrics, i.e. lights counts, dynamic geometry upload sizes, etc (TODO)
	SPEEDS_BIT_GPU_USAGE = 4, // `r_speeds 4` displays overall GPU usage stats (TODO)
	SPEEDS_BIT_FRAME = 8,     // `r_speeds 8` diplays details instrumental profiler frame data, e.g. specific functions times graphs, etc

	// These bits can be combined, e.g. `r_speeds 9`, 8+1, will display 1: basic timing info and 8: frame graphs
};

typedef struct {
	int *p_value;
	const char *name;
	r_speeds_metric_type_t type;
	int low_watermark, high_watermark, max_value;
	int graph_index;
} r_speeds_metric_t;

typedef struct {
	float *data;
	int data_count;
	int data_write;
	int source_metric;

	int height;
	int max_value; // Computed automatically every frame
	rgba_t color;
} r_speeds_graph_t;

static struct {
	cvar_t *r_speeds_graphs;
	cvar_t *r_speeds_graphs_width;

	aprof_event_t *paused_events;
	int paused_events_count;
	int pause_requested;

	struct {
		int glyph_width, glyph_height;
		float scale;
	} font_metrics;

	r_speeds_metric_t metrics[MAX_SPEEDS_METRICS];
	int metrics_count;

	r_speeds_graph_t graphs[MAX_GRAPHS];
	int graphs_count;

	struct {
		int frame_time_us, cpu_time_us, cpu_wait_time_us, gpu_time_us;
		struct {
			int initialized;
			int time_us;
		} scopes[APROF_MAX_SCOPES];
		char message[MAX_SPEEDS_MESSAGE];
	} frame;
} g_speeds;

static void speedsStrcat( const char *msg ) {
	Q_strncat( g_speeds.frame.message, msg, sizeof( g_speeds.frame.message ));
}

static void speedsPrintf( const char *msg, ... ) _format(1);
static void speedsPrintf( const char *msg, ... ) {
	va_list	argptr;
	char	text[MAX_SPEEDS_MESSAGE];

	va_start( argptr, msg );
	Q_vsprintf( text, msg, argptr );
	va_end( argptr );

	speedsStrcat(text);
}

static void metricTypeSnprintf(char *buf, int buf_size, int value, r_speeds_metric_type_t type) {
	switch (type) {
		case kSpeedsMetricCount:
			Q_snprintf(buf, buf_size, "%d", value);
			break;
		case kSpeedsMetricBytes:
			// TODO different units for different ranges, e.g. < 10k: bytes, < 10M: KiB, >10M: MiB
			Q_snprintf(buf, buf_size, "%dKiB", value / 1024);
			break;
		case kSpeedsMetricMicroseconds:
			Q_snprintf(buf, buf_size, "%.03fms", value * 1e-3f);
			break;
	}
}

static float linearstep(float min, float max, float v) {
	if (v <= min) return 0;
	if (v >= max) return 1;
	return (v - min) / (max - min);
}

#define P(fmt, ...) gEngine.Con_Reportf(fmt, ##__VA_ARGS__)

// TODO better "random" colors for scope bars
static uint32_t getHash(const char *s) {
	dword crc;
	CRC32_Init(&crc);
	CRC32_ProcessBuffer(&crc, s, Q_strlen(s));
	return CRC32_Final(crc);
}

static void getColorForString( const char *s, rgba_t out_color ) {
	const uint32_t hash = getHash(s);
	out_color[0] = (hash >> 16) & 0xff;
	out_color[1] = (hash >> 8) & 0xff;
	out_color[2] = hash & 0xff;
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
	const int glyph_width = g_speeds.font_metrics.glyph_width;
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
	uint64_t ref_cpu_time = 0;
	uint64_t ref_cpu_wait_time = 0;

	for (; begin != end; begin = (begin + 1) % APROF_EVENT_BUFFER_SIZE) {
		const aprof_event_t event = events[begin];
		const int event_type = APROF_EVENT_TYPE(event);
		const uint64_t timestamp_ns = APROF_EVENT_TIMESTAMP(event);
		const int scope_id = APROF_EVENT_SCOPE_ID(event);
		switch (event_type) {
			case APROF_EVENT_FRAME_BOUNDARY:
				ref_cpu_time = 0;
				ref_cpu_wait_time = 0;
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
					ASSERT(scope_id >= 0);
					ASSERT(scope_id < APROF_MAX_SCOPES);

					const aprof_scope_t *const scope = g_aprof.scopes + scope_id;
					const uint64_t delta_ns = timestamp_ns - stack[depth].begin_ns;

					if (!g_speeds.frame.scopes[scope_id].initialized) {
						R_SpeedsRegisterMetric(&g_speeds.frame.scopes[scope_id].time_us, scope->name, kSpeedsMetricMicroseconds);
						g_speeds.frame.scopes[scope_id].initialized = 1;
					}

					g_speeds.frame.scopes[scope_id].time_us += delta_ns / 1000;

					// This is a top level scope that should be counter towards cpu usage
					const int is_top_level = ((scope->flags & APROF_SCOPE_FLAG_DECOR) == 0) && (depth == 0 || (g_aprof.scopes[stack[depth-1].scope_id].flags & APROF_SCOPE_FLAG_DECOR));

					// Only count top level scopes towards CPU time, and only if it's not waiting
					if (is_top_level && under_waiting == 0)
						ref_cpu_time += delta_ns;

					// If this is a top level waiting scope (under any depth)
					if (under_waiting == 1) {
						// Count it towards waiting time
						ref_cpu_wait_time += delta_ns;

						// If this is not a top level scope, then we might count its top level parent
						// towards cpu usage time, which is not correct. Subtract this waiting time from it.
						if (!is_top_level)
							ref_cpu_time -= delta_ns;
					}

					if (scope->flags & APROF_SCOPE_FLAG_WAIT)
						under_waiting--;

					if (draw) {
						rgba_t color = {0, 0, 0, 127};
						getColorForString(scope->name, color);
						const int bar_height = g_speeds.font_metrics.glyph_height;
						drawTimeBar(begin_time, time_scale_ms, stack[depth].begin_ns, timestamp_ns, y + depth * bar_height, bar_height, scope->name, color);
					}
					break;
				}

			default:
				break;
		}
	}

	g_speeds.frame.cpu_time_us = ref_cpu_time / 1000;
	g_speeds.frame.cpu_wait_time_us = ref_cpu_wait_time / 1000;

	if (max_depth > MAX_STACK_DEPTH)
		gEngine.Con_NPrintf(4, S_ERROR "Profiler stack overflow: reached %d, max available %d\n", max_depth, MAX_STACK_DEPTH);
}

static void handlePause( uint32_t prev_frame_index ) {
	if (!g_speeds.pause_requested || g_speeds.paused_events)
		return;

	const uint32_t frame_begin = prev_frame_index;
	const uint32_t frame_end = g_aprof.events_last_frame + 1;

	g_speeds.paused_events_count = frame_end >= frame_begin ? frame_end - frame_begin : (frame_end + APROF_EVENT_BUFFER_SIZE - frame_begin);
	g_speeds.paused_events = Mem_Malloc(vk_core.pool, g_speeds.paused_events_count * sizeof(g_speeds.paused_events[0]));

	if (frame_end >= frame_begin) {
		memcpy(g_speeds.paused_events, g_aprof.events + frame_begin, g_speeds.paused_events_count * sizeof(g_speeds.paused_events[0]));
	} else {
		const int first_chunk = (APROF_EVENT_BUFFER_SIZE - frame_begin) * sizeof(g_speeds.paused_events[0]);
		memcpy(g_speeds.paused_events, g_aprof.events + frame_begin, first_chunk);
		memcpy(g_speeds.paused_events + first_chunk, g_aprof.events, frame_end * sizeof(g_speeds.paused_events[0]));
	}
}

static int drawGraph( r_speeds_graph_t *const graph, int frame_bar_y ) {
	const int min_width = 100 * g_speeds.font_metrics.scale;
	const int graph_width = clampi32(
		g_speeds.r_speeds_graphs_width->value < 1
		? vk_frame.width / 2 // half frame width if invalid
		: (int)(g_speeds.r_speeds_graphs_width->value * g_speeds.font_metrics.scale), // scaled value if valid
		min_width, vk_frame.width); // clamp to min_width..frame_width
	const int graph_height = graph->height * g_speeds.font_metrics.scale;

	const r_speeds_metric_t *const metric = g_speeds.metrics + graph->source_metric;
	const int graph_max_value = metric->max_value ? Q_max(metric->max_value, graph->max_value) : graph->max_value;

	const float width_factor = (float)graph_width / graph->data_count;
	const float height_scale = (float)graph_height / graph_max_value;

	rgba_t text_color = {0xed, 0x9f, 0x01, 0xff};

	// Draw graph name
	const char *name = metric->name;
	gEngine.Con_DrawString(0, frame_bar_y, name, text_color);
	frame_bar_y += g_speeds.font_metrics.glyph_height;

	// Draw background that podcherkivaet graph area
	CL_FillRGBABlend(0, frame_bar_y, graph_width, graph_height, graph->color[0], graph->color[1], graph->color[2], 32);

	// Draw max value
	{
		char buf[16];
		metricTypeSnprintf(buf, sizeof(buf), graph_max_value, metric->type);
		gEngine.Con_DrawString(0, frame_bar_y, buf, text_color);
	}

	// Draw zero
	gEngine.Con_DrawString(0, frame_bar_y + graph->height - g_speeds.font_metrics.glyph_height, "0", text_color);
	frame_bar_y += graph_height;

	if (metric->low_watermark && metric->low_watermark < graph_max_value) {
		const int y = frame_bar_y - metric->low_watermark * height_scale;
		CL_FillRGBA(0, y, graph_width, 1, 0, 255, 0, 50);
	}

	if (metric->high_watermark && metric->high_watermark < graph_max_value) {
		const int y = frame_bar_y - metric->high_watermark * height_scale;
		CL_FillRGBA(0, y, graph_width, 1, 255, 0, 0, 50);
	}

	// Invert graph origin for better math below
	int max_value = INT_MIN;
	const qboolean watermarks = metric->low_watermark && metric->high_watermark;
	for (int i = 0; i < graph->data_count; ++i) {
		int value = Q_max(0, graph->data[(graph->data_write + i) % graph->data_count]);
		max_value = Q_max(max_value, value);

		value = Q_min(graph_max_value, value);

		int red = 0xed, green = 0x9f, blue = 0x01;
		if (watermarks) {
			// E.g: > 60 fps => 0, 30..60 fps -> 1..0, <30fps => 1
			const float k = linearstep(metric->low_watermark, metric->high_watermark, value);
			red = value < metric->low_watermark ? 0 : 255;
			green = 255 * (1 - k);
		}

		const int x0 = (float)i * width_factor;
		const int x1 = (float)(i+1) * width_factor;
		const int y_pos = value * height_scale;
		const int height = watermarks ? y_pos : 2 * g_speeds.font_metrics.scale;
		const int y = frame_bar_y - y_pos;

		CL_FillRGBA(x0, y, x1-x0, height, red, green, blue, 127);
	}

	graph->max_value = max_value ? max_value : 1;

	return frame_bar_y;
}

static int drawFrames( int draw, uint32_t prev_frame_index, int y, const vk_combuf_scopes_t *gpurofl) {
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

	const aprof_event_t *const events = g_speeds.paused_events ? g_speeds.paused_events : g_aprof.events;
	const int event_begin = g_speeds.paused_events ? 0 : prev_frame_index;
	const int event_end = g_speeds.paused_events ? g_speeds.paused_events_count - 1 : g_aprof.events_last_frame;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(events[event_begin]);
	const uint64_t frame_end_time = APROF_EVENT_TIMESTAMP(events[event_end]);
	const uint64_t delta_ns = frame_end_time - frame_begin_time;
	const float time_scale_ms = (double)vk_frame.width / (delta_ns / 1e6);
	drawProfilerScopes(draw, events, frame_begin_time, time_scale_ms, event_begin, event_end, y);

	// Draw GPU last frame bar
	if (draw) {
		y += g_speeds.font_metrics.glyph_height * 6;
		const int bar_height = g_speeds.font_metrics.glyph_height;

		for (int i = 0; i < gpurofl->entries_count; ++i) {
			const int scope_index = gpurofl->entries[i];
			const uint64_t begin_ns = gpurofl->timestamps[scope_index*2 + 0];
			const uint64_t end_ns = gpurofl->timestamps[scope_index*2 + 1];
			const char *name = gpurofl->scopes[scope_index].name;

			rgba_t color = {255, 255, 0, 127};
			getColorForString(name, color);
			drawTimeBar(frame_begin_time, time_scale_ms, begin_ns, end_ns, y + i * bar_height, bar_height, name, color);
		}
	}
	return y;
}

static void printMetrics( void ) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		const r_speeds_metric_t *const metric = g_speeds.metrics + i;
		char buf[32];
		metricTypeSnprintf(buf, sizeof(buf), (*metric->p_value), metric->type);
		speedsPrintf("%s: %s\n", metric->name, buf);
	}
}

static void clearMetrics( void ) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		const r_speeds_metric_t *const metric = g_speeds.metrics + i;
		*metric->p_value = 0;
	}
}

static void getCurrentFontMetrics(void) {
	// hidpi scaling
	float scale = gEngine.pfnGetCvarFloat("con_fontscale");
	if (scale <= 0.f)
		scale = 1.f;

	// TODO these numbers are mostly fine for the "default" font. Unfortunately
	// we don't have any access to real font metrics from here, ref_api_t doesn't give us anything about fonts. ;_;
	g_speeds.font_metrics.glyph_width = 8 * scale;
	g_speeds.font_metrics.glyph_height = 20 * scale;
	g_speeds.font_metrics.scale = scale;
}

static int drawGraphs( int y ) {
	for (int i = 0; i < g_speeds.graphs_count; ++i) {
		r_speeds_graph_t *const graph = g_speeds.graphs + i;
		graph->data[graph->data_write] = *g_speeds.metrics[graph->source_metric].p_value;
		graph->data_write = (graph->data_write + 1) % graph->data_count;
		y = drawGraph(graph, y) + 10;
	}

	return y;
}


static void togglePause( void ) {
	if (g_speeds.paused_events) {
		Mem_Free(g_speeds.paused_events);
		g_speeds.paused_events = NULL;
		g_speeds.paused_events_count = 0;
		g_speeds.pause_requested = 0;
	} else {
		g_speeds.pause_requested = 1;
	}
}

typedef struct {
	const char *s;
	int len;
} const_string_view_t;

static int findMetricIndexByName( const_string_view_t name) {
	for (int i = 0; i < g_speeds.metrics_count; ++i) {
		if (Q_strncmp(g_speeds.metrics[i].name, name.s, name.len) == 0)
			return i;
	}

	return -1;
}

static void speedsGraphAddByMetricName( const_string_view_t name ) {
	const int metric_index = findMetricIndexByName(name);
	if (metric_index < 0) {
		gEngine.Con_Printf(S_ERROR "Metric \"%.*s\" not found\n", name.len, name.s);
		return;
	}

	r_speeds_metric_t *const metric = g_speeds.metrics + metric_index;
	if (metric->graph_index >= 0) {
		gEngine.Con_Printf(S_WARN "Metric \"%.*s\" already has graph @%d\n", name.len, name.s, metric->graph_index);
		return;
	}

	if (g_speeds.graphs_count == MAX_GRAPHS) {
		gEngine.Con_Printf(S_ERROR "Cannot add graph for metric \"%.*s\", no free graphs slots (max=%d)\n", name.len, name.s, MAX_GRAPHS);
		return;
	}

	gEngine.Con_Printf("Adding profiler graph for metric %.*s(%d) at graph index %d\n", name.len, name.s, metric_index, g_speeds.graphs_count);

	metric->graph_index = g_speeds.graphs_count++;
	r_speeds_graph_t *const graph = g_speeds.graphs + metric->graph_index;

	// TODO make these customizable
	graph->data_count = 256;
	graph->height = 100;
	graph->max_value = 1; // Will be computed automatically on first frame
	graph->color[3] = 255;
	getColorForString(metric->name, graph->color);

	ASSERT(!graph->data);
	graph->data = Mem_Calloc(vk_core.pool, graph->data_count * sizeof(float));
	graph->data_write = 0;
	graph->source_metric = metric_index;
}

static void speedsGraphsRemoveAll( void ) {
	gEngine.Con_Printf("Removing all %d profiler graphs\n", g_speeds.graphs_count);
	for (int i = 0; i < g_speeds.graphs_count; ++i) {
		r_speeds_graph_t *const graph = g_speeds.graphs + i;
		ASSERT(graph->data);
		Mem_Free(graph->data);
		graph->data = NULL;

		ASSERT(graph->source_metric >= 0);
		ASSERT(graph->source_metric < g_speeds.metrics_count);
		g_speeds.metrics[graph->source_metric].graph_index = -1;
	}

	g_speeds.graphs_count = 0;
}

static void processGraphCvar( void ) {
	if (!(g_speeds.r_speeds_graphs->flags & FCVAR_CHANGED))
		return;

	// TODO only remove graphs that are not present in the new list
	speedsGraphsRemoveAll();

	const char *p = g_speeds.r_speeds_graphs->string;
	while (*p) {
		const char *next = Q_strchrnul(p, ',');
		const const_string_view_t name = {p, next - p};
		speedsGraphAddByMetricName( name );
		if (!*next)
			break;
		p = next + 1;
	}

	g_speeds.r_speeds_graphs->flags &= ~FCVAR_CHANGED;
}

void R_SpeedsInit( void ) {
	g_speeds.r_speeds_graphs = gEngine.Cvar_Get("r_speeds_graphs", "", FCVAR_GLCONFIG, "List of metrics to plot as graphs, separated by commas");
	g_speeds.r_speeds_graphs_width = gEngine.Cvar_Get("r_speeds_graphs_width", "", FCVAR_GLCONFIG, "Graphs width in pixels");

	gEngine.Cmd_AddCommand("r_speeds_toggle_pause", togglePause, "Toggle frame profiler pause");

	R_SpeedsRegisterMetric(&g_speeds.frame.frame_time_us, "frame", kSpeedsMetricMicroseconds);
	R_SpeedsRegisterMetric(&g_speeds.frame.cpu_time_us, "cpu", kSpeedsMetricMicroseconds);
	R_SpeedsRegisterMetric(&g_speeds.frame.cpu_wait_time_us, "cpu_wait", kSpeedsMetricMicroseconds);
	R_SpeedsRegisterMetric(&g_speeds.frame.gpu_time_us, "gpu", kSpeedsMetricMicroseconds);
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

	Q_strncpy( out, g_speeds.frame.message, size );

	return true;
}

void R_SpeedsRegisterMetric(int* p_value, const char *name, r_speeds_metric_type_t type) {
	ASSERT(g_speeds.metrics_count < MAX_SPEEDS_METRICS);

	r_speeds_metric_t *metric = g_speeds.metrics + (g_speeds.metrics_count++);
	metric->p_value = p_value;
	metric->name = name;
	metric->type = type;
	metric->graph_index = -1;

	// TODO how to make universally adjustable?
	if (Q_strcmp("frame", name) == 0) {
		metric->low_watermark = TARGET_FRAME_TIME * 1000;
		metric->high_watermark = TARGET_FRAME_TIME * 2000;
		metric->max_value = TARGET_FRAME_TIME * 3000;
	} else {
		metric->low_watermark = metric->high_watermark = metric->max_value;
	}
}

void R_SpeedsDisplayMore(uint32_t prev_frame_index, const struct vk_combuf_scopes_s *gpurofl) {
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);

	const uint64_t gpu_frame_begin_ns = gpurofl->timestamps[0];
	const uint64_t gpu_frame_end_ns = gpurofl->timestamps[1];

	// Reads current font/DPI scale, many functions below use it
	getCurrentFontMetrics();

	g_speeds.frame.message[0] = '\0';

	const uint32_t speeds_bits = r_speeds->value;

	if (speeds_bits)
		speedsPrintf( "Renderer: ^1Vulkan%s^7\n", vk_frame.rtx_enabled ? " RT" : "" );

	const uint32_t events = g_aprof.events_last_frame - prev_frame_index;
	const uint64_t frame_begin_time = APROF_EVENT_TIMESTAMP(g_aprof.events[prev_frame_index]);
	const unsigned long long delta_ns = APROF_EVENT_TIMESTAMP(g_aprof.events[g_aprof.events_last_frame]) - frame_begin_time;

	g_speeds.frame.frame_time_us = delta_ns / 1000;
	g_speeds.frame.gpu_time_us = (gpu_frame_end_ns - gpu_frame_begin_ns) / 1000;

	handlePause( prev_frame_index );

	{
		int y = 100;
		const int draw = speeds_bits & SPEEDS_BIT_FRAME;
		y = drawFrames( draw, prev_frame_index, y, gpurofl );

		if (draw)
			y = drawGraphs(y + 10);
	}

	if (speeds_bits & SPEEDS_BIT_SIMPLE) {
		speedsPrintf("frame: %.03fms GPU: %.03fms\n", g_speeds.frame.frame_time_us * 1e-3f, g_speeds.frame.gpu_time_us * 1e-3);
		speedsPrintf("  (ref) CPU: %.03fms wait: %.03fms\n", g_speeds.frame.cpu_time_us * 1e-3, g_speeds.frame.cpu_wait_time_us * 1e-3);
	}

	if (speeds_bits & SPEEDS_BIT_STATS) {
		speedsPrintf("profiler events: %u, wraps: %d\n", events, g_aprof.current_frame_wraparounds);
		printMetrics();
	}

	processGraphCvar();

	clearMetrics();

	APROF_SCOPE_END(function);
}
