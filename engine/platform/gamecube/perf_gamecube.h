/*
perf_gamecube.h - GameCube performance profiling and telemetry
Copyright (C) 2026 xash3d-gc contributors
*/
#pragma once

#if XASH_GAMECUBE

/* Performance metrics structure */
typedef struct {
    float fps;                    /* Current frames per second */
    float avg_fps;               /* Average FPS over recent window */
    float min_fps;               /* Minimum FPS in recent window */
    float max_fps;               /* Maximum FPS in recent window */
    double frame_time;           /* Current frame time in ms */
    double avg_frame_time;       /* Average frame time in ms */
    size_t memory_used;          /* Current memory usage */
    size_t memory_hwm;           /* High-water mark memory usage */
    size_t memory_free;          /* Free memory */
    qboolean budget_exceeded;    /* Memory budget exceeded flag */
    int frame_count;             /* Frame counter */
    int dropped_frames;          /* Dropped frames count */
    double last_frame_time;      /* Last frame timestamp */
    double last_fps_update;      /* Last FPS update timestamp */
    float fps_window[60];        /* FPS history window */
    int fps_window_index;        /* Current position in FPS window */
    int fps_window_count;        /* Number of samples in window */
} GC_PerfMetrics;

/* Initialize performance metrics */
void GC_PerfInit( void );

/* Shutdown performance metrics */
void GC_PerfShutdown( void );

/* Update performance metrics each frame */
void GC_PerfUpdate( void );

/* Get current performance metrics */
const GC_PerfMetrics* GC_PerfGetMetrics( void );

/* Report performance metrics to console */
void GC_PerfReport( void );

/* Performance profiling console commands */
void GC_PerfCmd_Init( void );

/* Performance telemetry display */
void GC_PerfDraw( void );

/* Memory profiling */
void GC_PerfMemSample( const char *stage );

/* Frame time measurement */
void GC_PerfFrameStart( void );
void GC_PerfFrameEnd( void );

/* Performance profiling markers */
void GC_PerfMarker( const char *name );

#else

static inline void GC_PerfInit( void ) { }
static inline void GC_PerfShutdown( void ) { }
static inline void GC_PerfUpdate( void ) { }
static inline const GC_PerfMetrics* GC_PerfGetMetrics( void ) { return NULL; }
static inline void GC_PerfReport( void ) { }
static inline void GC_PerfCmd_Init( void ) { }
static inline void GC_PerfDraw( void ) { }
static inline void GC_PerfMemSample( const char *stage ) { (void)stage; }
static inline void GC_PerfFrameStart( void ) { }
static inline void GC_PerfFrameEnd( void ) { }
static inline void GC_PerfMarker( const char *name ) { (void)name; }

#endif