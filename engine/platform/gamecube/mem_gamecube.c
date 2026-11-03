/*
mem_gamecube.c - GameCube main-memory telemetry and map-load staging
Copyright (C) 2026 xash3d-gc contributors
*/
#include "common.h"
#include "client.h"

#if XASH_GAMECUBE
#include "mem_gamecube.h"
#include <stdlib.h>
#include <malloc.h>
#include <ogc/system.h>
#include <ogc/aram.h>
#include <ogc/cache.h>

void *R_GCBorrowMapLoadStaticArena( size_t size, size_t *capacity );
qboolean R_GCReleaseMapLoadStaticArena( void *ptr );
qboolean SV_Active( void );
qboolean SV_GCPrivateOverflowReserved( void );
void GCube_AppendSwissEvidence( const char *fmt, ... );

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

#define GC_JOB_MAX 64
typedef struct { GC_JobFn fn; void *opaque; GC_JobPriority priority; } gc_job_t;
static gc_job_t gc_jobs[GC_JOB_MAX];
static unsigned gc_job_count;

qboolean GC_JobEnqueue( GC_JobFn fn, void *opaque, GC_JobPriority priority )
{
	unsigned i;
	if( !fn || gc_job_count >= GC_JOB_MAX ) return false;
	for( i = gc_job_count; i > 0 && gc_jobs[i - 1].priority > priority; i-- )
		gc_jobs[i] = gc_jobs[i - 1];
	gc_jobs[i].fn = fn;
	gc_jobs[i].opaque = opaque;
	gc_jobs[i].priority = priority;
	gc_job_count++;
	return true;
}

unsigned GC_JobPending( void ) { return gc_job_count; }

unsigned GC_JobRunBudget( unsigned budget_us )
{
	const double start = Sys_DoubleTime( );
	unsigned ran = 0;
	while( gc_job_count )
	{
		gc_job_t job = gc_jobs[0];
		memmove( gc_jobs, gc_jobs + 1, ( gc_job_count - 1 ) * sizeof( gc_jobs[0] ));
		gc_job_count--;
		job.fn( job.opaque );
		ran++;
		if( budget_us && ( Sys_DoubleTime( ) - start ) * 1000000.0 >= budget_us ) break;
	}
	return ran;
}

#define GC_BSP_BATCH_FACE_MAX 16384
static unsigned short gc_bsp_face_batch[GC_BSP_BATCH_FACE_MAX];
static int gc_bsp_batch_faces;
static unsigned int gc_bsp_batch_count;
static qboolean gc_bsp_batches_valid;
static int gc_bsp_lump_offset[15], gc_bsp_lump_size[15];
static qboolean gc_bsp_lump_hot[15];
static unsigned int gc_bsp_lump_aram[15];
static unsigned int gc_bsp_hot_aram, gc_bsp_hot_bytes;

unsigned int GC_MapFaceBatchKey( int face_index )
{
	if( !gc_bsp_batches_valid || face_index < 0 || face_index >= gc_bsp_batch_faces )
		return 0;
	return gc_bsp_face_batch[face_index];
}

void GC_MapBatchStats( unsigned int *batches, unsigned int *faces )
{
	if( batches ) *batches = gc_bsp_batches_valid ? gc_bsp_batch_count : 0;
	if( faces ) *faces = gc_bsp_batches_valid ? (unsigned int)gc_bsp_batch_faces : 0;
}

#define GC_PREFETCH_MAX_ENTRIES 32
#define GC_PREFETCH_MAX_BYTES (2u * 1024u * 1024u)
#define GC_PREFETCH_CHUNK 32768u
typedef struct { char path[MAX_QPATH]; unsigned int size, aram; qboolean ready; } gc_prefetch_entry_t;
static gc_prefetch_entry_t gc_prefetch[GC_PREFETCH_MAX_ENTRIES];
static unsigned int gc_prefetch_count, gc_prefetch_next, gc_prefetch_used, gc_prefetch_aram;
static unsigned int gc_prefetch_capacity;
static qboolean gc_prefetch_manifest_ready, gc_prefetch_initialized;
static byte gc_prefetch_bounce[GC_PREFETCH_CHUNK] __attribute__(( aligned( 32 )));

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

/* Heap bytes still allocatable: free inside the arena plus unclaimed MEM1. */
size_t GC_MemHeapFreeBytes( void )
{
	size_t used, free_in_heap, unclaimed;

	GC_MemHeapStats( &used, &free_in_heap, &unclaimed );
	return free_in_heap + unclaimed;
}

void GC_MemSample( const char *stage )
{
	size_t total = Mem_TotalRealSize();
	size_t delta = (total >= gc_mem_last) ? (total - gc_mem_last) : 0;

	GC_MemWatch( stage );

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

void GC_MemBreadcrumb( const char *stage )
{
	size_t heap_used, heap_free, heap_unclaimed;

	if( !stage || !stage[0] ) return;
	GC_WatchdogMark( stage );
	GC_MemHeapStats( &heap_used, &heap_free, &heap_unclaimed );
	/* This is deliberately written even when the memory total is unchanged:
	 * the last completed stage is the useful artifact after a hard reset. */
	GCube_AppendSwissEvidence( "breadcrumb stage=%s map=%s total=%u hwm=%u heap_used=%u heap_free=%u unclaimed=%u",
		stage, gc_mem_map, (unsigned)Mem_TotalRealSize(), (unsigned)gc_mem_hwm,
		(unsigned)heap_used, (unsigned)heap_free, (unsigned)heap_unclaimed );
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
	GC_LoadMapMetadata( mapname );

	Q_snprintf( path, sizeof( path ), "maps/%s.bsp", mapname );
	filesize = FS_FileSize( path, false );
	if( filesize > 0 )
		GC_PrepareMapLoadBuffer( (size_t)filesize );
	else
		GC_PrepareMapLoadBuffer( GC_MAPLOAD_BUFFER_DEFAULT );
}

qboolean GC_LoadMapMetadata( const char *mapname )
{
	char path[MAX_QPATH];
	char base[MAX_QPATH];
	/* The metadata is only needed while parsing. Do not reserve 64 KiB of
	 * scarce MEM1 permanently for a buffer that is idle during gameplay. */
	char *text;
	file_t *f;
	int n, rows = 0, hot = 0;
	fs_offset_t text_size;
	qboolean valid = false;
	unsigned file_size = 0;
	memset( gc_bsp_face_batch, 0, sizeof( gc_bsp_face_batch ));
	gc_bsp_batch_faces = 0;
	gc_bsp_batch_count = 0;
	gc_bsp_batches_valid = false;
	memset( gc_bsp_lump_offset, 0, sizeof( gc_bsp_lump_offset ));
	memset( gc_bsp_lump_size, 0, sizeof( gc_bsp_lump_size ));
	memset( gc_bsp_lump_hot, 0, sizeof( gc_bsp_lump_hot ));
	memset( gc_bsp_lump_aram, 0, sizeof( gc_bsp_lump_aram ));
	gc_bsp_hot_aram = gc_bsp_hot_bytes = 0;
	if( !mapname || !mapname[0] ) return false;
	COM_FileBase( mapname, base, sizeof( base ));
	Q_snprintf( path, sizeof( path ), "media/%s.gcmeta", base );
	f = FS_Open( path, "rb", false );
	if( !f ) return false;
	text_size = FS_FileLength( f );
	if( text_size <= 0 || text_size >= 65536 )
	{
		FS_Close( f );
		return false;
	}
	text = malloc( (size_t)text_size + 1 );
	if( !text )
	{
		FS_Close( f );
		return false;
	}
	n = FS_Read( f, text, (size_t)text_size );
	FS_Close( f );
	if( n != text_size ) goto done;
	text[n] = '\0';
	{
		char *cursor = text;
		char *end;
	unsigned version, crc;
	fs_offset_t actual_size;
		if( sscanf( cursor, "GCMETA 1 %u %x %u", &version, &crc, &file_size ) != 3
			|| version != 30 || !crc || !file_size ) goto done;
		actual_size = FS_FileSize( va( "maps/%s.bsp", base ), false );
		if( actual_size <= 0 || (unsigned)actual_size != file_size ) goto done;
		end = strchr( cursor, '\n' );
		if( !end ) goto done;
		cursor = end + 1;
		while( cursor && *cursor )
		{
			int index, offset, size, align;
			char name[32], tier[8];
			end = strchr( cursor, '\n' );
			if( end ) *end = '\0';
			if( !Q_strnicmp( cursor, "BATCHES ", 8 ))
			{
				int batch_count, face_count, batch_no = 0;
				if( sscanf( cursor, "BATCHES %d %d", &batch_count, &face_count ) != 2
					|| batch_count < 0 || batch_count > 4096 || face_count < 0
					|| face_count > GC_BSP_BATCH_FACE_MAX ) goto done;
				cursor = end ? end + 1 : NULL;
				while( cursor && *cursor && batch_no < batch_count )
				{
					int miptex, flags, count, firstface, consumed = 0;
					char *p;
					end = strchr( cursor, '\n' );
					if( end ) *end = '\0';
					if( sscanf( cursor, "BATCH %d %d %d %d %n", &miptex, &flags,
						&count, &firstface, &consumed ) != 4 || count < 0 || firstface < 0
						|| firstface + count > face_count ) goto done;
					p = cursor + consumed;
					while( count-- > 0 )
					{
						char *next;
						long face = strtol( p, &next, 10 );
						if( next == p || face < 0 || face >= GC_BSP_BATCH_FACE_MAX ) goto done;
						gc_bsp_face_batch[face] = (unsigned short)( batch_no + 1 );
						if( face + 1 > gc_bsp_batch_faces ) gc_bsp_batch_faces = (int)face + 1;
						p = next;
						if( *p == ',' ) p++;
					}
					batch_no++;
					if( !end ) break;
					cursor = end + 1;
				}
				if( batch_no != batch_count ) goto done;
				gc_bsp_batch_count = (unsigned int)batch_count;
				gc_bsp_batches_valid = true;
				break;
			}
			if( sscanf( cursor, "%d %31s %7s %d %d %d", &index, name, tier,
				&offset, &size, &align ) != 6 || index != rows || index < 0 || index >= 15
				|| offset < 0 || size < 0 || align != ( offset & 31 )
				|| ( Q_stricmp( tier, "HOT" ) && Q_stricmp( tier, "COLD" )))
				goto done;
			gc_bsp_lump_offset[index] = offset;
			gc_bsp_lump_size[index] = size;
			gc_bsp_lump_hot[index] = !Q_stricmp( tier, "HOT" );
			if( !Q_stricmp( tier, "HOT" )) hot++;
			rows++;
			if( !end ) break;
			cursor = end + 1;
		}
	}
	if( rows != 15 ) goto done;
	Con_Reportf( "Xash3D GameCube: validated BSP metadata %s rows=%d hot=%d size=%u\n",
				path, rows, hot, file_size );
	valid = true;
done:
	free( text );
	if( !valid )
	{
		gc_bsp_batches_valid = false;
		memset( gc_bsp_lump_aram, 0, sizeof( gc_bsp_lump_aram ));
	}
	return valid;
}

void *GC_BorrowMapLoadBuffer( size_t size )
{
	if( size == 0 )
		return NULL;
	/* Entity private objects may still point into this map's arena. */
	if( SV_GCPrivateOverflowReserved() )
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

		if( GC_MemHeapFreeBytes() < alloc_size )
		{
			Con_Reportf( S_ERROR "Xash3D GameCube: map-load buffer rejected by heap budget need=%s free=%s\n",
				Q_memprint( alloc_size ), Q_memprint( GC_MemHeapFreeBytes() ));
			return NULL;
		}
		gc_mapload_buf = (byte *)memalign( 32, alloc_size );
		if( !gc_mapload_buf )
		{
			/* One more shot after releasing decode scratch; keeps mid-size BSPs
			 * loading when the renderer arena is still a few hundred KB short. */
			Image_GCPurgeDecodeScratch();
			if( GC_MemHeapFreeBytes() >= alloc_size )
				gc_mapload_buf = (byte *)memalign( 32, alloc_size );
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

qboolean GC_IsMapLoadBufferAddress( const void *ptr )
{
	const uintptr_t address = (uintptr_t)ptr;
	const uintptr_t base = (uintptr_t)gc_mapload_buf;

	return ptr != NULL && gc_mapload_buf != NULL
		&& address >= base && address - base < gc_mapload_buf_size;
}

size_t GC_MapLoadBufferCapacity( void )
{
	return gc_mapload_buf_size;
}

/* End of the last tail carve: the next carve must start at or above it. */
size_t GC_MapLoadBufferTailHighwater( void )
{
	return gc_mapload_tail_highwater;
}

void *GC_MapLoadBufferTailAlloc( size_t after_offset, size_t need )
{
	size_t off;
	size_t remaining;
	uintptr_t address, aligned, base;

	/* A live entity overflow reservation pins this map's arena after the BSP
	 * staging borrow ends. Its high-water mark also protects all earlier tail
	 * allocations, so remaining tail space is still safe for world arrays. */
	if( !gc_mapload_buf || ( !gc_mapload_buf_in_use && !SV_GCPrivateOverflowReserved() ) || need == 0 )
		return NULL;

	base = (uintptr_t)gc_mapload_buf;
	address = base + after_offset;
	if( address < base || address > UINTPTR_MAX - 31u )
		return NULL;
	aligned = ( address + 31u ) & ~(uintptr_t)31u;
	off = (size_t)( aligned - base );
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
	if( SV_GCPrivateOverflowReserved() )
	{
		Con_Reportf( "Xash3D GameCube: retaining map arena for entity private data\n" );
		return;
	}
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
	/* Jobs may retain pointers into the outgoing map arena.  Drop them before
	 * releasing/resetting map-owned storage; future streaming producers can
	 * safely enqueue work again on the next map. */
	gc_job_count = 0;
	GC_AramMapReset();
	GC_PrefetchResetForMenu();
}

qboolean GC_MapLoadMemoryOpt( void )
{
	return gc_mapload_memopt_session
		|| gc_mapload_memopt_depth > 0
		|| Sys_CheckParm( "-gcmap" ) != 0
		|| ( Sys_CheckParm( "-gcnewgame" ) != 0 && gc_newgame_bootstrap_memopt );
}

/*
==============================================================================

ARAM storage helpers

The 16 MiB of auxiliary RAM is DMA-only. These move arbitrary byte ranges
through a 32-byte aligned MEM1 bounce buffer, so callers (the transition
save bank) never need a MEM1 copy of a whole file.

==============================================================================
*/
#define GC_ARAM_BOUNCE 4096
static u8 gc_aram_bounce[GC_ARAM_BOUNCE] __attribute__(( aligned( 32 )));
static u32 gc_aram_reserve_blocks[8];
static u32 gc_aram_audio_base;
static u32 gc_aram_audio_capacity;
static u32 gc_aram_map_base;
static u32 gc_aram_map_capacity;

static void GC_AramDMA( u32 type, u32 aram, u32 len )
{
	if( type == AR_MRAMTOARAM )
		DCFlushRange( gc_aram_bounce, len );
	else
		DCInvalidateRange( gc_aram_bounce, len );
	while( AR_GetDMAStatus( ))
		;
	AR_StartDMA( type, (u32)gc_aram_bounce, aram, len );
	while( AR_GetDMAStatus( ))
		;
	if( type == AR_ARAMTOMRAM )
		DCInvalidateRange( gc_aram_bounce, len );
}

qboolean GC_StageMapHotLumps( const void *bsp, size_t bsp_size )
{
	unsigned int total = 0, reserve, cursor = 0, base;
	int i;

	if( !bsp || !bsp_size || !gc_bsp_batches_valid ) return false;
	for( i = 0; i < 15; i++ )
	{
		if( !gc_bsp_lump_hot[i] || gc_bsp_lump_size[i] <= 0 ) continue;
		total = ( total + 31u ) & ~31u;
		total += (unsigned int)gc_bsp_lump_size[i];
	}
	/* Reserve headroom on the first map so the same session window can hold
	 * the next tram-chain BSP's slightly larger hot-lump set without asking
	 * the non-growing ARAM allocator for a second block mid-transition. */
	reserve = total + 128u * 1024u;
	if( reserve < total ) return false;
	if( reserve < 1024u * 1024u ) reserve = 1024u * 1024u;
	base = GC_AramMapReserve( reserve );
	if( !base || total > GC_AramMapCapacity() ) return false;
	for( i = 0; i < 15; i++ )
	{
		unsigned int size;
		if( !gc_bsp_lump_hot[i] || gc_bsp_lump_size[i] <= 0 ) continue;
		if( gc_bsp_lump_offset[i] < 0
			|| (size_t)gc_bsp_lump_offset[i] + (size_t)gc_bsp_lump_size[i] > bsp_size )
			return false;
		size = (unsigned int)gc_bsp_lump_size[i];
		GC_AramCopy( base + cursor, (byte *)bsp + gc_bsp_lump_offset[i], size, true );
		gc_bsp_lump_aram[i] = base + cursor;
		cursor = ( cursor + size + 31u ) & ~31u;
	}
	gc_bsp_hot_aram = base;
	gc_bsp_hot_bytes = cursor;
	Con_Reportf( "Xash3D GameCube: staged BSP HOT bytes=%s aram=0x%08x\n",
		Q_memprint( cursor ), base );
	return true;
}

qboolean GC_MapHotLumpRead( int lump, size_t offset, void *dst, size_t size )
{
	if( lump < 0 || lump >= 15 || !dst || !size || !gc_bsp_lump_aram[lump]
		|| !gc_bsp_lump_hot[lump] || offset > (size_t)gc_bsp_lump_size[lump]
		|| size > (size_t)gc_bsp_lump_size[lump] - offset )
		return false;
	GC_AramCopy( gc_bsp_lump_aram[lump] + (unsigned int)offset, dst,
		(unsigned int)size, false );
	return true;
}

qboolean GC_MapHotLumpReadPointer( const void *source, void *dst, size_t size )
{
	uintptr_t p = (uintptr_t)source, base = (uintptr_t)gc_mapload_buf;
	int i;
	if( !source || !dst || !size || !base || p < base || p >= base + gc_mapload_buf_size )
		return false;
	for( i = 0; i < 15; i++ )
	{
		uintptr_t start;
		if( !gc_bsp_lump_hot[i] || !gc_bsp_lump_aram[i] ) continue;
		start = base + (uintptr_t)gc_bsp_lump_offset[i];
		if( p >= start && p - start <= (uintptr_t)gc_bsp_lump_size[i]
			&& size <= (size_t)gc_bsp_lump_size[i] - (size_t)( p - start ))
			return GC_MapHotLumpRead( i, (size_t)( p - start ), dst, size );
	}
	return false;
}

unsigned int GC_AramReserve( unsigned int size )
{
	u32 base;

	if( !AR_CheckInit( ))
		AR_Init( gc_aram_reserve_blocks, 8 );
	base = AR_Alloc(( size + 31u ) & ~31u );
	Con_Reportf( "Xash3D GameCube: ARAM reserve %s at 0x%08x\n", Q_memprint( size ), base );
	return base;
}

unsigned int GC_AramAudioReserve( unsigned int size )
{
	u32 aligned;

	if( gc_aram_audio_base )
		return gc_aram_audio_capacity >= size ? gc_aram_audio_base : 0;
	aligned = ( size + 31u ) & ~31u;
	if( !aligned )
		return 0;
	/* Host-frame reservation can precede the first map allocator request. Keep
	 * ARAM initialization local to this ownership boundary so callers do not
	 * depend on incidental ordering in texture or BSP streaming. */
	if( !AR_CheckInit( ))
		AR_Init( gc_aram_reserve_blocks, 8 );
	gc_aram_audio_base = AR_Alloc( aligned );
	if( !gc_aram_audio_base )
		return 0;
	gc_aram_audio_capacity = aligned;
	Con_Reportf( "Xash3D GameCube: ARAM audio reserve %s at 0x%08x\n",
		Q_memprint( aligned ), gc_aram_audio_base );
	return gc_aram_audio_base;
}

unsigned int GC_AramAudioBase( void )
{
	return gc_aram_audio_base;
}

unsigned int GC_AramAudioCapacity( void )
{
	return gc_aram_audio_capacity;
}

unsigned int GC_AramMapReserve( unsigned int size )
{
	u32 aligned = ( size + 31u ) & ~31u;
	if( !aligned ) return 0;
	if( gc_aram_map_base && aligned <= gc_aram_map_capacity )
		return gc_aram_map_base;
	if( gc_aram_map_base )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: map ARAM window too small need=%s have=%s\n",
			Q_memprint( aligned ), Q_memprint( gc_aram_map_capacity ));
		return 0;
	}
	gc_aram_map_base = GC_AramReserve( aligned );
	if( gc_aram_map_base ) gc_aram_map_capacity = aligned;
	return gc_aram_map_base;
}

void GC_AramMapReset( void )
{
	/* Ownership is retained; callers can overwrite the window for the next
	 * map without asking AR_Alloc for another block. */
}

unsigned int GC_AramMapCapacity( void )
{
	return gc_aram_map_capacity;
}

/* Whole 32-byte aligned blocks, straight to or from the buffer. */
void GC_AramTransfer( unsigned int aram, void *buf, unsigned int len, qboolean write )
{
	if( write )
		DCFlushRange( buf, len );
	else
		DCInvalidateRange( buf, len );
	while( AR_GetDMAStatus( ))
		;
	AR_StartDMA( write ? AR_MRAMTOARAM : AR_ARAMTOMRAM, (u32)buf, aram, len );
	while( AR_GetDMAStatus( ))
		;
	if( !write )
		DCInvalidateRange( buf, len );
}

void GC_AramCopy( unsigned int aram, void *buf, unsigned int len, qboolean write )
{
	byte *p = (byte *)buf;

	while( len > 0 )
	{
		const u32 a0 = aram & ~31u;
		const u32 off = aram - a0;
		const u32 chunk = Q_min( len, GC_ARAM_BOUNCE - off );
		const u32 span = ( off + chunk + 31u ) & ~31u;

		if( write )
		{
			/* Partial 32-byte blocks keep their neighbouring bytes. */
			if( off || ( chunk & 31u ))
				GC_AramDMA( AR_ARAMTOMRAM, a0, span );
			memcpy( gc_aram_bounce + off, p, chunk );
			GC_AramDMA( AR_MRAMTOARAM, a0, span );
		}
		else
		{
			GC_AramDMA( AR_ARAMTOMRAM, a0, span );
			memcpy( p, gc_aram_bounce + off, chunk );
		}
		aram += chunk;
		p += chunk;
		len -= chunk;
	}
}

void GC_PrefetchMenuIdle( void )
{
	file_t *f;
	char manifest[8192];
	int n;
	if( gc_prefetch_manifest_ready || cls.key_dest != key_menu
		|| SV_Active() || Sys_CheckParm( "-gcmap" )) return;
	if( !gc_prefetch_initialized )
	{
		gc_prefetch_initialized = true;
		f = FS_Open( "media/c0a0.prefetch", "rb", false );
		if( !f ) { gc_prefetch_manifest_ready = true; return; }
			n = FS_Read( f, manifest, sizeof( manifest ) - 1 ); FS_Close( f );
		if( n <= 0 ) { gc_prefetch_manifest_ready = true; return; }
		manifest[n] = '\0';
		{
			char *cursor = manifest;
			while( gc_prefetch_count < GC_PREFETCH_MAX_ENTRIES && cursor && *cursor )
			{
				char *end = strchr( cursor, '\n' );
				if( end ) *end = '\0';
				if( cursor[0] != '#' && cursor[0] != '\0'
					&& sscanf( cursor, "%127s %u", gc_prefetch[gc_prefetch_count].path,
						&gc_prefetch[gc_prefetch_count].size ) == 2 ) gc_prefetch_count++;
				if( !end ) break; cursor = end + 1;
			}
		}
		if( !gc_prefetch_count ) { gc_prefetch_manifest_ready = true; return; }
		{
			unsigned int reserve = 0;
			unsigned int i;
			for( i = 0; i < gc_prefetch_count; i++ )
			{
				if( gc_prefetch[i].size <= 512u * 1024u
					&& gc_prefetch[i].size <= GC_PREFETCH_MAX_BYTES - reserve )
					reserve += ( gc_prefetch[i].size + 31u ) & ~31u;
			}
			if( !reserve ) { gc_prefetch_manifest_ready = true; return; }
			if( reserve > GC_PREFETCH_MAX_BYTES ) reserve = GC_PREFETCH_MAX_BYTES;
			if( !gc_prefetch_aram )
			{
				gc_prefetch_aram = GC_AramReserve( reserve );
				if( gc_prefetch_aram ) gc_prefetch_capacity = reserve;
			}
			if( !gc_prefetch_aram ) { gc_prefetch_manifest_ready = true; return; }
		}
	}
	if( gc_prefetch_next >= gc_prefetch_count ) { gc_prefetch_manifest_ready = true; return; }
	{
		gc_prefetch_entry_t *entry = &gc_prefetch[gc_prefetch_next++];
		unsigned int left, offset = 0;
		if( !entry->size || entry->size > 512u * 1024u
			|| gc_prefetch_used > gc_prefetch_capacity
			|| (( entry->size + 31u ) & ~31u ) > gc_prefetch_capacity - gc_prefetch_used ) return;
		f = FS_Open( entry->path, "rb", false ); if( !f ) return;
		entry->aram = gc_prefetch_aram + gc_prefetch_used; left = entry->size;
		while( left )
		{
			unsigned int chunk = left > GC_PREFETCH_CHUNK ? GC_PREFETCH_CHUNK : left;
			if( FS_Read( f, gc_prefetch_bounce, chunk ) != (int)chunk ) break;
			/* File tails are commonly not DMA-sized; AramCopy preserves the
			 * neighbouring bytes in the final 32-byte block. */
			GC_AramCopy( entry->aram + offset, gc_prefetch_bounce, chunk, true );
			offset += chunk; left -= chunk;
		}
		FS_Close( f );
		if( !left )
		{
			entry->ready = true;
			gc_prefetch_used += ( entry->size + 31u ) & ~31u;
		}
	}
}

void GC_PrefetchResetForMenu( void )
{
	/* AR_Free is intentionally not used here: other subsystems may have
	 * allocated a block after the prefetch window. The window is a permanent
	 * session owner and is reset in place, which gives transitions deterministic
	 * reuse without ARAM fragmentation or stale reads. */
	memset( gc_prefetch, 0, sizeof( gc_prefetch ));
	gc_prefetch_count = 0;
	gc_prefetch_next = 0;
	gc_prefetch_used = 0;
	gc_prefetch_manifest_ready = false;
	gc_prefetch_initialized = false;
}

void GC_PrefetchStats( unsigned int *used, unsigned int *capacity, unsigned int *entries )
{
	if( used ) *used = gc_prefetch_used;
	if( capacity ) *capacity = gc_prefetch_capacity;
	if( entries ) *entries = gc_prefetch_next;
}

qboolean GC_PrefetchRead( const char *path, void *buffer, unsigned int size )
{
	unsigned int i;
	if( !path || !buffer || !size ) return false;
	for( i = 0; i < gc_prefetch_count; i++ )
	{
		gc_prefetch_entry_t *entry = &gc_prefetch[i];
		if( entry->ready && entry->size == size && !Q_stricmp( entry->path, path ))
		{
			GC_AramCopy( entry->aram, buffer, size, false );
			return true;
		}
	}
	return false;
}

qboolean GC_PrefetchContains( const char *path, unsigned int size )
{
	unsigned int i;
	if( !path || !size ) return false;
	for( i = 0; i < gc_prefetch_count; i++ )
	{
		const gc_prefetch_entry_t *entry = &gc_prefetch[i];
		if( entry->ready && entry->size == size && !Q_stricmp( entry->path, path ))
			return true;
	}
	return false;
}
#endif
