/*
 * Replay-only GameCube pad TAS for Dolphin probes.
 * Scripts live at valve/tas/<name>.tas (disc-baked).
 */
#pragma once

#include "common.h"

#if XASH_GAMECUBE

#define GC_TAS_MAX_NAME     32
#define GC_TAS_MAX_SEGMENTS 256
#define GC_TAS_MAX_FRAMES   4096

typedef struct gc_tas_segment_s
{
	uint         frames;
	unsigned short buttons;
	signed char  stick_x;
	signed char  stick_y;
	signed char  cstick_x;
	signed char  cstick_y;
} gc_tas_segment_t;

/* Lazy-load valve/tas/<name>.tas when -gctas is set. Returns true if armed. */
qboolean GC_TasTryLoad( void );

/* True when a script is loaded and ready to drive synthetic pad. */
qboolean GC_TasActive( void );

/* Advance one host frame while ca_active+SIGNONS; returns held pad mask. */
unsigned short GC_TasPollButtons( void );

/* Current segment sticks (0 if inactive / complete). */
void GC_TasGetSticks( signed char *stick_x, signed char *stick_y,
	signed char *cstick_x, signed char *cstick_y );

/* Script finished (or inactive). */
qboolean GC_TasComplete( void );

/* Reset playback cursor (e.g. reconnect before signon). */
void GC_TasResetPlayback( void );

#endif /* XASH_GAMECUBE */
