/*
perf_gamecube.c - GameCube performance profiling and telemetry
Copyright (C) 2026 xash3d-gc contributors
*/
#include "platform/platform.h"
#include "common.h"
#include "cvar.h"
#include "crtlib.h"
#include "mem_gamecube.h"
#include "client.h"
#include "perf_gamecube.h"
#include <string.h>

#if XASH_GAMECUBE

/* Global performance metrics */
static GC_PerfMetrics gc_perf_metrics;

/* Performance cvars */
CVAR_DEFINE_AUTO( gc_perf_enable, "1", FCVAR_ARCHIVE, "Enable performance profiling" );
CVAR_DEFINE_AUTO( gc_perf_draw, "1", FCVAR_ARCHIVE, "Draw performance overlay" );
CVAR_DEFINE_AUTO( gc_perf_window, "60", FCVAR_ARCHIVE, "FPS window size" );

/* Memory profiling */
static size_t gc_mem_hwm = 0;
static size_t gc_mem_last = 0;

/* Initialize performance metrics */
void GC_PerfInit( void )
{
    memset( &gc_perf_metrics, 0, sizeof( gc_perf_metrics ) );
    gc_perf_metrics.last_frame_time = Platform_DoubleTime();
    gc_perf_metrics.last_fps_update = Platform_DoubleTime();
    
    /* Register cvars */
    Cvar_RegisterVariable( &gc_perf_enable );
    Cvar_RegisterVariable( &gc_perf_draw );
    Cvar_RegisterVariable( &gc_perf_window );
    
    Con_Reportf( "Xash3D GameCube: performance profiling initialized\n" );
}

/* Shutdown performance metrics */
void GC_PerfShutdown( void )
{
    Con_Reportf( "Xash3D GameCube: performance profiling shutdown\n" );
}

/* Update performance metrics each frame */
void GC_PerfUpdate( void )
{
    double now = Platform_DoubleTime();
    double frame_time = (now - gc_perf_metrics.last_frame_time) * 1000.0; /* ms */
    double fps = (frame_time > 0.0) ? (1000.0 / frame_time) : 0.0;
    
    /* Update frame time */
    gc_perf_metrics.frame_time = frame_time;
    gc_perf_metrics.last_frame_time = now;
    gc_perf_metrics.frame_count++;
    
    /* Update FPS window */
    gc_perf_metrics.fps_window[gc_perf_metrics.fps_window_index] = (float)fps;
    gc_perf_metrics.fps_window_index = (gc_perf_metrics.fps_window_index + 1) % gc_perf_metrics.fps_window_count;
    if( gc_perf_metrics.fps_window_count < (int)atoi(gc_perf_window.string) )
        gc_perf_metrics.fps_window_count++;
    
    /* Calculate FPS statistics */
    if( gc_perf_metrics.fps_window_count > 0 )
    {
        float sum = 0.0f;
        float min = gc_perf_metrics.fps_window[0];
        float max = gc_perf_metrics.fps_window[0];
        
        for( int i = 0; i < gc_perf_metrics.fps_window_count; i++ )
        {
            sum += gc_perf_metrics.fps_window[i];
            if( gc_perf_metrics.fps_window[i] < min ) min = gc_perf_metrics.fps_window[i];
            if( gc_perf_metrics.fps_window[i] > max ) max = gc_perf_metrics.fps_window[i];
        }
        
        gc_perf_metrics.avg_fps = sum / gc_perf_metrics.fps_window_count;
        gc_perf_metrics.min_fps = min;
        gc_perf_metrics.max_fps = max;
    }
    
    gc_perf_metrics.fps = (float)fps;
    
    /* Update memory stats */
    GC_MemArena_GetStats( (GC_MemArenaStats*)&gc_perf_metrics );
    gc_perf_metrics.memory_used = Mem_TotalRealSize();
    gc_perf_metrics.memory_hwm = gc_mem_hwm;
    gc_perf_metrics.memory_free = (gc_perf_metrics.memory_used < GC_MEMORY_BUDGET_BYTES) ? 
                                   (GC_MEMORY_BUDGET_BYTES - gc_perf_metrics.memory_used) : 0;
    gc_perf_metrics.budget_exceeded = (gc_perf_metrics.memory_used > GC_MEMORY_BUDGET_BYTES);
    
    /* Update average frame time */
    static double frame_time_sum = 0.0;
    static int frame_time_count = 0;
    frame_time_sum += frame_time;
    frame_time_count++;
    gc_perf_metrics.avg_frame_time = frame_time_sum / frame_time_count;
    
    /* Update FPS every second */
    if( now - gc_perf_metrics.last_fps_update >= 1.0 )
    {
        gc_perf_metrics.last_fps_update = now;
        /* Dropped frames detection (if frame time exceeds 100ms) */
        if( frame_time > 100.0 )
            gc_perf_metrics.dropped_frames++;
    }
}

/* Get current performance metrics */
const GC_PerfMetrics* GC_PerfGetMetrics( void )
{
    return &gc_perf_metrics;
}

/* Report performance metrics to console */
void GC_PerfReport( void )
{
    const GC_PerfMetrics* m = GC_PerfGetMetrics();
    
    Con_Reportf( "Xash3D GameCube: performance report\n" );
    Con_Reportf( "  FPS: %.1f (avg: %.1f, min: %.1f, max: %.1f)\n",
        m->fps, m->avg_fps, m->min_fps, m->max_fps );
    Con_Reportf( "  Frame time: %.2f ms (avg: %.2f ms)\n",
        m->frame_time, m->avg_frame_time );
    Con_Reportf( "  Memory: %s used / %s budget (%s free)\n",
        Q_memprint( m->memory_used ),
        Q_memprint( GC_MEMORY_BUDGET_BYTES ),
        Q_memprint( m->memory_free ) );
    Con_Reportf( "  Memory HWM: %s\n", Q_memprint( m->memory_hwm ) );
    Con_Reportf( "  Budget exceeded: %s\n", m->budget_exceeded ? "YES" : "NO" );
    Con_Reportf( "  Frames: %d (dropped: %d)\n", m->frame_count, m->dropped_frames );
}

/* Performance profiling console commands */
static void GC_PerfCmd_Report_f( void )
{
    if( !gc_perf_enable.string || atoi(gc_perf_enable.string) == 0 )
    {
        Con_Reportf( "Xash3D GameCube: performance profiling disabled (gc_perf_enable=0)\n" );
        return;
    }
    
    GC_PerfReport();
}

static void GC_PerfCmd_Reset_f( void )
{
    memset( &gc_perf_metrics, 0, sizeof( gc_perf_metrics ) );
    gc_perf_metrics.last_frame_time = Platform_DoubleTime();
    gc_perf_metrics.last_fps_update = Platform_DoubleTime();
    Con_Reportf( "Xash3D GameCube: performance metrics reset\n" );
}

void GC_PerfCmd_Init( void )
{
    Cmd_AddCommand( "gc_perf_report", GC_PerfCmd_Report_f, "Report performance metrics" );
    Cmd_AddCommand( "gc_perf_reset", GC_PerfCmd_Reset_f, "Reset performance metrics" );
    
    Con_Reportf( "Xash3D GameCube: performance profiling commands registered\n" );
}

/* Performance telemetry display */
void GC_PerfDraw( void )
{
    if( !gc_perf_enable.string || !gc_perf_draw.string || atoi(gc_perf_enable.string) == 0 || atoi(gc_perf_draw.string) == 0 )
        return;
    
    const GC_PerfMetrics* m = GC_PerfGetMetrics();
    
    /* Draw FPS overlay in top-left corner */
    Con_DrawString( 8, 8, va( "FPS: %.1f (%.1f/%.1f)", m->fps, m->avg_fps, m->min_fps ), 0 );
    
    /* Draw memory usage */
    Con_DrawString( 8, 24, va( "MEM: %s/%s", Q_memprint( m->memory_used ), Q_memprint( GC_MEMORY_BUDGET_BYTES ) ), 0 );
    
    /* Draw frame time */
    Con_DrawString( 8, 40, va( "FRM: %.1f ms", m->frame_time ), 0 );
    
    /* Draw budget status */
    Con_DrawString( 8, 56, va( "BUDGET: %s", m->budget_exceeded ? "EXCEEDED" : "OK" ), 0 );
}

/* Memory profiling */
void GC_PerfMemSample( const char *stage )
{
    if( !gc_perf_enable.string || atoi(gc_perf_enable.string) == 0 )
        return;
    
    size_t total = Mem_TotalRealSize();
    size_t delta = (total >= gc_mem_last) ? (total - gc_mem_last) : 0;
    
    if( total > gc_mem_hwm )
        gc_mem_hwm = total;
    
    gc_mem_last = total;
    
    Con_Reportf( "Xash3D GameCube: perf stage=%s total=%s delta=%s hwm=%s\n",
        stage, Q_memprint( total ), Q_memprint( delta ), Q_memprint( gc_mem_hwm ) );
}

/* Frame time measurement */
static double gc_perf_frame_start_time = 0.0;

void GC_PerfFrameStart( void )
{
    gc_perf_frame_start_time = Platform_DoubleTime();
}

void GC_PerfFrameEnd( void )
{
    double now = Platform_DoubleTime();
    double frame_time = (now - gc_perf_frame_start_time) * 1000.0; /* ms */
    
    if( frame_time > 0.0 )
    {
        Con_Reportf( "Xash3D GameCube: frame time %.2f ms\n", frame_time );
    }
}

/* Performance profiling markers */
void GC_PerfMarker( const char *name )
{
    if( !gc_perf_enable.string || atoi(gc_perf_enable.string) == 0 )
        return;
    
    static double last_marker_time = 0.0;
    double now = Platform_DoubleTime();
    double delta = (last_marker_time > 0.0) ? ((now - last_marker_time) * 1000.0) : 0.0;
    
    Con_Reportf( "Xash3D GameCube: perf marker %s delta=%.2f ms\n", name, delta );
    last_marker_time = now;
}

#endif /* XASH_GAMECUBE */
