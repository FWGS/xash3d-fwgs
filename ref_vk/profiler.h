#pragma once

#include <stdint.h>
#include <assert.h>

#define APROF_SCOPE_DECLARE(scope) \
	static aprof_scope_id_t _aprof_scope_id_##scope = -1

// scope_name is expected to be static and alive for the entire duration of the program
#define APROF_SCOPE_INIT(scope, scope_name) \
	_aprof_scope_id_##scope = aprof_scope_init(scope_name)

#define APROF_SCOPE_BEGIN(scope) \
	aprof_scope_event(_aprof_scope_id_##scope, 1)

#define APROF_TOKENPASTE(x, y) x ## y
#define APROF_TOKENPASTE2(x, y) APROF_TOKENPASTE(x, y)

#define APROF_SCOPE_BEGIN_EARLY(scope) \
	const int APROF_TOKENPASTE2(_aprof_dummy, __LINE__) = (aprof_scope_event(_aprof_scope_id_##scope, 1), 0)

#define APROF_SCOPE_END(scope) \
	aprof_scope_event(_aprof_scope_id_##scope, 0)


typedef int aprof_scope_id_t;

aprof_scope_id_t aprof_scope_init(const char *scope_name);
void aprof_scope_event(aprof_scope_id_t, int begin);
void aprof_scope_frame( void );

typedef struct {
	const char *name;

	struct {
		uint64_t duration;
		uint64_t duration_children;
		int count;
	} frame;
} aprof_scope_t;

#define APROF_MAX_SCOPES 256
#define APROF_MAX_STACK_DEPTH 32

typedef struct {
	aprof_scope_id_t scope;
	uint64_t time_begin;
} aprof_stack_frame_t;

typedef struct {
	aprof_scope_t scopes[APROF_MAX_SCOPES];
	int num_scopes;

	aprof_stack_frame_t stack[APROF_MAX_STACK_DEPTH];
	int stack_depth;

	// TODO event log for chrome://trace (or similar) export and analysis
} aprof_state_t;

extern aprof_state_t g_aprof;

#if defined(APROF_IMPLEMENT)

#ifdef __linux__
#include <time.h>
static uint64_t _aprof_time_now( void ) {
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return tp.tv_nsec + tp.tv_sec * 1000000000ull;
}
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
static LARGE_INTEGER _aprof_frequency;
static uint64_t _aprof_time_now( void ) {
	LARGE_INTEGER pc;
	QueryPerformanceCounter(&pc);
	return pc.QuadPart * 1000000000ull / _aprof_frequency.QuadPart;
}
#else
#error aprof is not implemented for this os
#endif

aprof_state_t g_aprof = {0};

aprof_scope_id_t aprof_scope_init(const char *scope_name) {
#if defined(_WIN32)
	if (_aprof_frequency.QuadPart == 0)
		QueryPerformanceFrequency(&_aprof_frequency);
#endif

	if (g_aprof.num_scopes == APROF_MAX_SCOPES)
		return -1;

	g_aprof.scopes[g_aprof.num_scopes].name = scope_name;
	return g_aprof.num_scopes++;
}

void aprof_scope_event(aprof_scope_id_t scope_id, int begin) {
	const uint64_t now = _aprof_time_now();
	if (scope_id < 0 || scope_id >= g_aprof.num_scopes)
		return;

	// TODO improve performance by just writing into an event array here
	// analysis should be done on-demand later
	if (begin) {
		const int s = g_aprof.stack_depth;
		if (g_aprof.stack_depth == APROF_MAX_STACK_DEPTH)
			return;

		g_aprof.stack[s].scope = scope_id;
		g_aprof.stack[s].time_begin = now;
		g_aprof.stack_depth++;
	} else {
		aprof_scope_t *scope;
		const aprof_stack_frame_t *const frame = g_aprof.stack + g_aprof.stack_depth - 1;
		uint64_t frame_duration;

		assert(g_aprof.stack_depth > 0);
		if (g_aprof.stack_depth == 0)
			return;

		assert(frame->scope == scope_id);

		scope = g_aprof.scopes + frame->scope;
		frame_duration = now - frame->time_begin;
		scope->frame.duration += frame_duration;
		scope->frame.count++;

		if (g_aprof.stack_depth > 1) {
			const aprof_stack_frame_t *const parent_frame = g_aprof.stack + g_aprof.stack_depth - 2;
			aprof_scope_t *const parent_scope = g_aprof.scopes + parent_frame->scope;

			assert(parent_frame->scope >= 0);
			assert(parent_frame->scope < g_aprof.num_scopes);

			parent_scope->frame.duration_children += frame_duration;
		}

		g_aprof.stack_depth--;
	}
}

void aprof_scope_frame( void ) {
	assert(g_aprof.stack_depth == 0);
	for (int i = 0; i < g_aprof.num_scopes; ++i) {
		aprof_scope_t *const scope = g_aprof.scopes + i;
		memset(&scope->frame, 0, sizeof(scope->frame));
	}
}

#endif
