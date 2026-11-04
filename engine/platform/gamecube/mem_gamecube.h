/*
mem_gamecube.h - GameCube main-memory telemetry and map-load staging
Copyright (C) 2026 xash3d-gc contributors
*/
#pragma once

#if XASH_GAMECUBE

/* GameCube memory budget: 24 MB main RAM */
#define GC_MEMORY_BUDGET_BYTES (24u * 1024u * 1024u)

/* Memory budget thresholds for warnings */
#define GC_MEMORY_WARNING_80_PERCENT (GC_MEMORY_BUDGET_BYTES * 80 / 100)
#define GC_MEMORY_WARNING_90_PERCENT (GC_MEMORY_BUDGET_BYTES * 90 / 100)
#define GC_MEMORY_CRITICAL_95_PERCENT (GC_MEMORY_BUDGET_BYTES * 95 / 100)

/* Runtime memory arena telemetry */
typedef struct {
    size_t total;           /* Current total memory */
    size_t hwm;             /* High-water mark */
    size_t budget;          /* Memory budget */
    size_t budget_used;     /* Memory used from budget */
    size_t budget_free;     /* Memory free from budget */
    qboolean budget_exceeded; /* Budget exceeded flag */
} GC_MemArenaStats;

void GC_MemSetMap( const char *mapname );
void GC_MemSample( const char *stage );
void GC_MemBreadcrumb( const char *stage );
void GC_MemFail( const char *subsystem, size_t size, const char *file, int line );
void GC_MemReportPoolPressure( const char *subsystem, size_t requested );
void GC_MemPoolReport( const char *stage );

/* Runtime memory arena telemetry */
void GC_MemArena_GetStats( GC_MemArenaStats *stats );
qboolean GC_MemBudgetCheck( void );
void GC_MemBudgetWarn( const char *stage );

/* Memory budget enforcement */
qboolean GC_MemBudgetEnforce( size_t requested, const char *subsystem );

/* Map-load memory pressure measurement */
void GC_MapLoadPressureBegin( void );
void GC_MapLoadPressureEnd( void );
void GC_MapLoadPressureSample( const char *stage );
size_t GC_MapLoadPressurePeak( void );
size_t GC_MapLoadPressureDelta( void );

/* Default contiguous BSP staging size (covers retail c1a1/c2a2-class maps). */
#define GC_MAPLOAD_BUFFER_DEFAULT (3072u * 1024u)
void GC_InitMapLoadBuffer( void );
void GC_PrepareMapLoadBuffer( size_t size );
void GC_PrepareMapLoadBufferForMap( const char *mapname );
qboolean GC_LoadMapMetadata( const char *mapname );
unsigned int GC_MapFaceBatchKey( int face_index );
void GC_MapBatchStats( unsigned int *batches, unsigned int *faces );
void *GC_BorrowMapLoadBuffer( size_t size );
qboolean GC_ReleaseMapLoadBuffer( void *ptr );
void GC_DiscardMapLoadBuffer( void );
qboolean GC_IsMapLoadBuffer( const void *ptr );
qboolean GC_IsMapLoadBufferAddress( const void *ptr );
size_t GC_MapLoadBufferCapacity( void );
/* Unused bytes after a borrowed BSP image inside the static map-load arena. */
void *GC_MapLoadBufferTailAlloc( size_t after_offset, size_t need );
size_t GC_MapLoadBufferTailHighwater( void );
unsigned int GC_AramReserve( unsigned int size );
/* Session-owned audio/DSP budget; established before map streaming. */
unsigned int GC_AramAudioReserve( unsigned int size );
unsigned int GC_AramAudioBase( void );
unsigned int GC_AramAudioCapacity( void );
/* One-shot boot-video window. It is released before menu idle prefetch starts. */
unsigned int GC_AramVideoReserve( unsigned int size );
void GC_AramVideoRelease( void );
/* One map-owned ARAM window, reused across transitions without new blocks. */
unsigned int GC_AramMapReserve( unsigned int size );
void GC_AramMapReset( void );
unsigned int GC_AramMapCapacity( void );
qboolean GC_StageMapHotLumps( const void *bsp, size_t bsp_size );
qboolean GC_MapHotLumpRead( int lump, size_t offset, void *dst, size_t size );
qboolean GC_MapHotLumpReadPointer( const void *source, void *dst, size_t size );
void GC_AramTransfer( unsigned int aram, void *buf, unsigned int len, qboolean write );
void GC_AramCopy( unsigned int aram, void *buf, unsigned int len, qboolean write );
void GC_PrefetchMenuIdle( void );
/* Reuse the session-owned ARAM window without allocating another block. */
void GC_PrefetchResetForMenu( void );
void GC_PrefetchStats( unsigned int *used, unsigned int *capacity, unsigned int *entries );
qboolean GC_PrefetchRead( const char *path, void *buffer, unsigned int size );
qboolean GC_PrefetchContains( const char *path, unsigned int size );

/* True during -gcmap smoke loads and retail New Game (gc_playstart) map loads. */
void GC_BeginMapLoadMemoryOpt( void );
void GC_EndMapLoadMemoryOpt( void );
void GC_ClearMapLoadMemoryOpt( void );
qboolean GC_MapLoadMemoryOpt( void );
size_t GC_MemHeapFreeBytes( void );

/* Memory budget telemetry */
size_t GC_MemBudgetTotal( void );
size_t GC_MemBudgetUsed( void );
size_t GC_MemBudgetFree( void );
qboolean GC_MemBudgetExceeded( void );

/* Entity memory estimation */
size_t GC_EntityEstimateSize( void );

/* Stall watchdog stage marker (perf_gamecube.c). */
void GC_WatchdogMark( const char *stage );
void GC_WatchdogStage( const char *stage );

typedef void (*GC_JobFn)( void *opaque );
typedef enum { GC_JOB_CRITICAL = 0, GC_JOB_STREAM = 1, GC_JOB_BACKGROUND = 2 } GC_JobPriority;
qboolean GC_JobEnqueue( GC_JobFn fn, void *opaque, GC_JobPriority priority );
unsigned GC_JobRunBudget( unsigned budget_us );
unsigned GC_JobPending( void );

#else

static inline void GC_MemSetMap( const char *mapname ) { (void)mapname; }
static inline void GC_MemSample( const char *stage ) { (void)stage; }
static inline void GC_MemBreadcrumb( const char *stage ) { (void)stage; }
static inline void GC_MemFail( const char *subsystem, size_t size, const char *file, int line )
{
	(void)subsystem;
	(void)size;
	(void)file;
	(void)line;
}
static inline void GC_MemReportPoolPressure( const char *subsystem, size_t requested )
{
	(void)subsystem;
	(void)requested;
}
static inline void GC_MemPoolReport( const char *stage ) { (void)stage; }
static inline void GC_InitMapLoadBuffer( void ) { }
static inline void GC_PrepareMapLoadBuffer( size_t size ) { (void)size; }
static inline void GC_PrepareMapLoadBufferForMap( const char *mapname ) { (void)mapname; }
static inline qboolean GC_LoadMapMetadata( const char *mapname ) { (void)mapname; return false; }
static inline unsigned int GC_MapFaceBatchKey( int face_index ) { (void)face_index; return 0; }
static inline qboolean GC_StageMapHotLumps( const void *bsp, size_t bsp_size )
{ (void)bsp; (void)bsp_size; return false; }
static inline qboolean GC_MapHotLumpRead( int lump, size_t offset, void *dst, size_t size )
{ (void)lump; (void)offset; (void)dst; (void)size; return false; }
static inline qboolean GC_MapHotLumpReadPointer( const void *source, void *dst, size_t size )
{ (void)source; (void)dst; (void)size; return false; }
static inline void GC_MapBatchStats( unsigned int *batches, unsigned int *faces )
{ if( batches ) *batches = 0; if( faces ) *faces = 0; }
static inline void *GC_BorrowMapLoadBuffer( size_t size ) { (void)size; return NULL; }
static inline qboolean GC_ReleaseMapLoadBuffer( void *ptr ) { (void)ptr; return false; }
static inline void GC_DiscardMapLoadBuffer( void ) { }
static inline qboolean GC_IsMapLoadBuffer( const void *ptr ) { (void)ptr; return false; }
static inline qboolean GC_IsMapLoadBufferAddress( const void *ptr ) { (void)ptr; return false; }
static inline size_t GC_MapLoadBufferCapacity( void ) { return 0; }
static inline void *GC_MapLoadBufferTailAlloc( size_t after_offset, size_t need )
{
	(void)after_offset;
	(void)need;
	return NULL;
}
static inline void GC_BeginMapLoadMemoryOpt( void ) { }
static inline void GC_EndMapLoadMemoryOpt( void ) { }
static inline void GC_ClearMapLoadMemoryOpt( void ) { }
static inline qboolean GC_MapLoadMemoryOpt( void ) { return false; }
static inline void GC_PrefetchMenuIdle( void ) { }
static inline void GC_PrefetchResetForMenu( void ) { }
static inline void GC_PrefetchStats( unsigned int *used, unsigned int *capacity, unsigned int *entries )
{
	if( used ) *used = 0; if( capacity ) *capacity = 0; if( entries ) *entries = 0;
}
static inline qboolean GC_PrefetchRead( const char *path, void *buffer, unsigned int size )
{ (void)path; (void)buffer; (void)size; return false; }
static inline qboolean GC_PrefetchContains( const char *path, unsigned int size )
{ (void)path; (void)size; return false; }

/* Map-load memory pressure measurement (stubs) */
static inline void GC_MapLoadPressureBegin( void ) { }
static inline void GC_MapLoadPressureEnd( void ) { }
static inline void GC_MapLoadPressureSample( const char *stage ) { (void)stage; }
static inline size_t GC_MapLoadPressurePeak( void ) { return 0; }
static inline size_t GC_MapLoadPressureDelta( void ) { return 0; }

/* Entity memory estimation (stubs) */
static inline size_t GC_EntityEstimateSize( void ) { return 0; }
static inline void GC_WatchdogMark( const char *stage ) { (void)stage; }
static inline void GC_WatchdogStage( const char *stage ) { (void)stage; }
typedef void (*GC_JobFn)( void *opaque );
typedef enum { GC_JOB_CRITICAL = 0, GC_JOB_STREAM = 1, GC_JOB_BACKGROUND = 2 } GC_JobPriority;
static inline qboolean GC_JobEnqueue( GC_JobFn fn, void *opaque, GC_JobPriority priority )
{ (void)fn; (void)opaque; (void)priority; return false; }
static inline unsigned GC_JobRunBudget( unsigned budget_us ) { (void)budget_us; return 0; }
static inline unsigned GC_JobPending( void ) { return 0; }

#endif
