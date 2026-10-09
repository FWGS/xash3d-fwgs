/*
mem_gamecube.c - GameCube main-memory telemetry and map-load staging
Copyright (C) 2026 xash3d-gc contributors
*/
#include "common.h"

#if XASH_GAMECUBE
#include "mem_gamecube.h"
#include <stdlib.h>
#include <malloc.h>
#include <ogc/system.h>

void *R_GCBorrowMapLoadStaticArena( size_t size, size_t *capacity );
qboolean R_GCReleaseMapLoadStaticArena( void *ptr );

static char gc_mem_map[MAX_QPATH] = "(none)";
static size_t gc_mem_last;
static size_t gc_mem_hwm;

/* Map-load memory pressure tracking */
static size_t gc_mapload_pressure_base;
static size_t gc_mapload_pressure_peak;
static size_t gc_mapload_pressure_delta;
static qboolean gc_mapload_pressure_active;

/* Contiguous staging buffer for maps/*.bsp. Borrowed after menu/client trim. */
static byte *gc_mapload_buf;
static size_t gc_mapload_buf_size;
static size_t gc_mapload_tail_highwater;
static qboolean gc_mapload_buf_in_use;
static int gc_mapload_memopt_depth;
static qboolean gc_mapload_memopt_session; /* stays on after playstart until cleared */
static qboolean gc_newgame_bootstrap_memopt = true;

void GC_MemSetMap( const char *mapname )
{
	if( mapname && mapname[0] )
		Q_strncpy( gc_mem_map, mapname, sizeof( gc_mem_map ));
	else Q_strncpy( gc_mem_map, "(none)", sizeof( gc_mem_map ));
}

/* Zone pools only count Mem_Alloc traffic.  Direct malloc users (HLSDK DLLs,
 * XFBs, BSP pins, sprite/HUD sys-malloc) share the same newlib heap, so report
 * the real heap too: used/free inside the sbrk'd arena and what is still left
 * between the arena top and MEM1 end. */
static void GC_MemHeapStats( size_t *used, size_t *free_in_heap, size_t *unclaimed )
{
	struct mallinfo mi = mallinfo();
	uintptr_t lo = (uintptr_t)SYS_GetArena1Lo();
	uintptr_t hi = (uintptr_t)SYS_GetArena1Hi();

	*used = (size_t)mi.uordblks;
	*free_in_heap = (size_t)mi.fordblks;
	*unclaimed = hi > lo ? (size_t)( hi - lo ) : 0;
}

void GC_MemSample( const char *stage )
{
	size_t total = Mem_TotalRealSize();
	size_t delta = (total >= gc_mem_last) ? (total - gc_mem_last) : 0;

	if( total > gc_mem_hwm )
		gc_mem_hwm = total;

	gc_mem_last = total;

	/* G72: unchanged totals are noise during map load; keep FAIL path chatty. */
	if( delta == 0 )
		return;

	{
		size_t heap_used, heap_free, heap_unclaimed;

		GC_MemHeapStats( &heap_used, &heap_free, &heap_unclaimed );
		Con_Reportf( "Xash3D GameCube: mem stage=%s total=%s delta=%s hwm=%s map=%s heap_used=%s heap_free=%s unclaimed=%s\n",
			stage, Q_memprint( total ), Q_memprint( delta ), Q_memprint( gc_mem_hwm ), gc_mem_map,
			Q_memprint( heap_used ), Q_memprint( heap_free ), Q_memprint( heap_unclaimed ));
	}
}

void GC_MemFail( const char *subsystem, size_t size, const char *file, int line )
{
	size_t heap_used, heap_free, heap_unclaimed;

	GC_MemHeapStats( &heap_used, &heap_free, &heap_unclaimed );
	Con_Reportf( "Xash3D GameCube: mem FAIL subsystem=%s size=%s map=%s at=%s:%i total=%s hwm=%s heap_used=%s heap_free=%s unclaimed=%s\n",
		subsystem ? subsystem : "unknown", Q_memprint( size ), gc_mem_map, file, line,
		Q_memprint( Mem_TotalRealSize() ), Q_memprint( gc_mem_hwm ),
		Q_memprint( heap_used ), Q_memprint( heap_free ), Q_memprint( heap_unclaimed ));
	GC_MemReportPoolPressure( subsystem, size );
}

/* Runtime memory arena telemetry */
void GC_MemArena_GetStats( GC_MemArenaStats *stats )
{
	size_t total = Mem_TotalRealSize();
	
	if( !stats )
		return;
	
	stats->total = total;
	stats->hwm = gc_mem_hwm;
	stats->budget = GC_MEMORY_BUDGET_BYTES;
	stats->budget_used = total;
	stats->budget_free = (total < GC_MEMORY_BUDGET_BYTES) ? (GC_MEMORY_BUDGET_BYTES - total) : 0;
	stats->budget_exceeded = (total > GC_MEMORY_BUDGET_BYTES);
}

qboolean GC_MemBudgetCheck( void )
{
	size_t total = Mem_TotalRealSize();
	return (total <= GC_MEMORY_BUDGET_BYTES);
}

void GC_MemBudgetWarn( const char *stage )
{
	size_t total = Mem_TotalRealSize();
	size_t used_percent = (total * 100) / GC_MEMORY_BUDGET_BYTES;
	
	if( total >= GC_MEMORY_CRITICAL_95_PERCENT )
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: mem CRITICAL %d%% budget used (%s/%s) at %s\n",
			used_percent, Q_memprint( total ), Q_memprint( GC_MEMORY_BUDGET_BYTES ), stage );
	}
	else if( total >= GC_MEMORY_WARNING_90_PERCENT )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: mem WARNING %d%% budget used (%s/%s) at %s\n",
			used_percent, Q_memprint( total ), Q_memprint( GC_MEMORY_BUDGET_BYTES ), stage );
	}
	else if( total >= GC_MEMORY_WARNING_80_PERCENT )
	{
		Con_Reportf( "Xash3D GameCube: mem INFO %d%% budget used (%s/%s) at %s\n",
			used_percent, Q_memprint( total ), Q_memprint( GC_MEMORY_BUDGET_BYTES ), stage );
	}
}

qboolean GC_MemBudgetEnforce( size_t requested, const char *subsystem )
{
	size_t total = Mem_TotalRealSize();
	size_t new_total = total + requested;
	
	if( new_total > GC_MEMORY_BUDGET_BYTES )
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: mem BUDGET EXCEEDED requested=%s total=%s new=%s budget=%s subsystem=%s\n",
			Q_memprint( requested ), Q_memprint( total ), Q_memprint( new_total ),
			Q_memprint( GC_MEMORY_BUDGET_BYTES ), subsystem ? subsystem : "unknown" );
		return false;
	}
	
	return true;
}

/* Memory budget telemetry */
size_t GC_MemBudgetTotal( void )
{
	return Mem_TotalRealSize();
}

size_t GC_MemBudgetUsed( void )
{
	return Mem_TotalRealSize();
}

size_t GC_MemBudgetFree( void )
{
	size_t total = Mem_TotalRealSize();
	return (total < GC_MEMORY_BUDGET_BYTES) ? (GC_MEMORY_BUDGET_BYTES - total) : 0;
}

qboolean GC_MemBudgetExceeded( void )
{
	return (Mem_TotalRealSize() > GC_MEMORY_BUDGET_BYTES);
}

/* Entity memory estimation */
size_t GC_EntityEstimateSize( void )
{
	/* Estimate edict_t + typical private data (model strings, etc.)
	 * edict_t is ~2KB on GameCube, private data varies by entity type.
	 * Use a conservative average estimate for budgeting.
	 * Note: This is an estimate based on typical entity sizes.
	 * The actual size depends on the progdefs and entity type. */
	return 2048 + 512; /* ~2.5KB per entity average */
}

/* Map-load memory pressure measurement */
void GC_MapLoadPressureBegin( void )
{
	gc_mapload_pressure_base = Mem_TotalRealSize();
	gc_mapload_pressure_peak = gc_mapload_pressure_base;
	gc_mapload_pressure_delta = 0;
	gc_mapload_pressure_active = true;
}

void GC_MapLoadPressureEnd( void )
{
	gc_mapload_pressure_active = false;
}

void GC_MapLoadPressureSample( const char *stage )
{
	size_t total;
	
	if( !gc_mapload_pressure_active )
		return;
	
	total = Mem_TotalRealSize();
	
	if( total > gc_mapload_pressure_peak )
	{
		gc_mapload_pressure_peak = total;
		gc_mapload_pressure_delta = gc_mapload_pressure_peak - gc_mapload_pressure_base;
		Con_Reportf( "Xash3D GameCube: map-load pressure stage=%s peak=%s delta=%s base=%s\n",
			stage, Q_memprint( gc_mapload_pressure_peak ), Q_memprint( gc_mapload_pressure_delta ),
			Q_memprint( gc_mapload_pressure_base ) );
	}
}

size_t GC_MapLoadPressurePeak( void )
{
	return gc_mapload_pressure_peak;
}

size_t GC_MapLoadPressureDelta( void )
{
	return gc_mapload_pressure_delta;
}

void GC_InitMapLoadBuffer( void )
{
	/* Prepared on demand in GC_PrepareMapLoadBuffer after client/menu trim. */
}

void GC_PrepareMapLoadBuffer( size_t size )
{
	void *buf;

	if( size == 0 )
		size = GC_MAPLOAD_BUFFER_DEFAULT;

	buf = GC_BorrowMapLoadBuffer( size );
	if( buf )
		GC_ReleaseMapLoadBuffer( buf );
}

void GC_PrepareMapLoadBufferForMap( const char *mapname )
{
	char path[MAX_QPATH];
	fs_offset_t filesize;

	if( !mapname || !mapname[0] )
	{
		GC_PrepareMapLoadBuffer( GC_MAPLOAD_BUFFER_DEFAULT );
		return;
	}

	Q_snprintf( path, sizeof( path ), "maps/%s.bsp", mapname );
	filesize = FS_FileSize( path, false );
	if( filesize > 0 )
		GC_PrepareMapLoadBuffer( (size_t)filesize );
	else
		GC_PrepareMapLoadBuffer( GC_MAPLOAD_BUFFER_DEFAULT );
}

void *GC_BorrowMapLoadBuffer( size_t size )
{
	if( size == 0 )
		return NULL;

	if( gc_mapload_buf && gc_mapload_buf_in_use )
		return NULL;

	if( gc_mapload_buf && size > gc_mapload_buf_size )
	{
		if( gc_mapload_buf_in_use )
			return NULL;
		if( !R_GCReleaseMapLoadStaticArena( gc_mapload_buf ))
			free( gc_mapload_buf );
		gc_mapload_buf = NULL;
		gc_mapload_buf_size = 0;
	}

	if( !gc_mapload_buf )
	{
		size_t alloc_size = ( size + 4095u ) & ~4095u;
		size_t static_capacity = 0;

		gc_mapload_buf = (byte *)R_GCBorrowMapLoadStaticArena( alloc_size, &static_capacity );
		if( gc_mapload_buf )
		{
			gc_mapload_buf_size = static_capacity;
			gc_mapload_tail_highwater = 0;
			gc_mapload_buf_in_use = true;
			return gc_mapload_buf;
		}

		gc_mapload_buf = (byte *)malloc( alloc_size );
		if( !gc_mapload_buf )
		{
			/* One more shot after releasing decode scratch; keeps mid-size BSPs
			 * loading when the renderer arena is still a few hundred KB short. */
			Image_GCPurgeDecodeScratch();
			gc_mapload_buf = (byte *)malloc( alloc_size );
		}
		if( !gc_mapload_buf )
		{
			Con_Reportf( S_ERROR "Xash3D GameCube: map-load buffer alloc failed (%s)\n",
				Q_memprint( alloc_size ));
			return NULL;
		}
		gc_mapload_buf_size = alloc_size;
		gc_mapload_tail_highwater = 0;
		Con_Reportf( "Xash3D GameCube: map-load buffer ready %s\n", Q_memprint( alloc_size ));
	}

	/* A released staging buffer starts a fresh sub-arena carve session. */
	gc_mapload_tail_highwater = 0;
	gc_mapload_buf_in_use = true;
	return gc_mapload_buf;
}

qboolean GC_ReleaseMapLoadBuffer( void *ptr )
{
	if( !ptr || ptr != gc_mapload_buf )
		return false;

	gc_mapload_buf_in_use = false;
	return true;
}

qboolean GC_IsMapLoadBuffer( const void *ptr )
{
	return ptr != NULL && ptr == gc_mapload_buf;
}

void *GC_MapLoadBufferTailAlloc( size_t after_offset, size_t need )
{
	size_t off;
	size_t remaining;

	if( !gc_mapload_buf || !gc_mapload_buf_in_use || need == 0 )
		return NULL;

	off = ( after_offset + 31u ) & ~31u;
	if( off < after_offset )
		return NULL;
	if( off + need < off )
		return NULL;
	if( off < gc_mapload_tail_highwater )
	{
		Con_Reportf( "Xash3D GameCube: mem arena tail=OVERLAP after_bytes=%lu offset_bytes=%lu highwater_bytes=%lu\n",
			(unsigned long)after_offset, (unsigned long)off,
			(unsigned long)gc_mapload_tail_highwater );
		return NULL;
	}
	if( off + need > gc_mapload_buf_size )
	{
		Con_Reportf( "Xash3D GameCube: mem arena tail=FAIL after_bytes=%lu need_bytes=%lu capacity_bytes=%lu\n",
			(unsigned long)after_offset, (unsigned long)need, (unsigned long)gc_mapload_buf_size );
		return NULL;
	}

	remaining = gc_mapload_buf_size - ( off + need );
	gc_mapload_tail_highwater = off + need;
	Con_Reportf( "Xash3D GameCube: mem arena tail=PASS after_bytes=%lu carve_bytes=%lu remaining_bytes=%lu capacity_bytes=%lu\n",
		(unsigned long)after_offset, (unsigned long)need, (unsigned long)remaining,
		(unsigned long)gc_mapload_buf_size );

	return gc_mapload_buf + off;
}

void GC_DiscardMapLoadBuffer( void )
{
	if( gc_mapload_buf_in_use )
		return;

	if( gc_mapload_buf )
	{
		if( R_GCReleaseMapLoadStaticArena( gc_mapload_buf ))
		{
			gc_mapload_buf = NULL;
			gc_mapload_buf_size = 0;
			return;
		}

		Con_Reportf( "Xash3D GameCube: map-load buffer discarded %s\n",
			Q_memprint( gc_mapload_buf_size ));
		free( gc_mapload_buf );
		gc_mapload_buf = NULL;
		gc_mapload_buf_size = 0;
	}
}

void GC_BeginMapLoadMemoryOpt( void )
{
	gc_mapload_memopt_depth++;
	gc_mapload_memopt_session = true;
	gc_newgame_bootstrap_memopt = false;
}

void GC_EndMapLoadMemoryOpt( void )
{
	if( gc_mapload_memopt_depth > 0 )
		gc_mapload_memopt_depth--;
}

void GC_ClearMapLoadMemoryOpt( void )
{
	gc_mapload_memopt_depth = 0;
	gc_mapload_memopt_session = false;
	gc_newgame_bootstrap_memopt = false;
}

qboolean GC_MapLoadMemoryOpt( void )
{
	return gc_mapload_memopt_session
		|| gc_mapload_memopt_depth > 0
		|| Sys_CheckParm( "-gcmap" ) != 0
		|| ( Sys_CheckParm( "-gcnewgame" ) != 0 && gc_newgame_bootstrap_memopt );
}
#endif
