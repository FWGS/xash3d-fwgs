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
void GC_MemFail( const char *subsystem, size_t size, const char *file, int line );

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
void *GC_BorrowMapLoadBuffer( size_t size );
qboolean GC_ReleaseMapLoadBuffer( void *ptr );
void GC_DiscardMapLoadBuffer( void );
qboolean GC_IsMapLoadBuffer( const void *ptr );

/* True during -gcmap smoke loads and retail New Game (gc_playstart) map loads. */
void GC_BeginMapLoadMemoryOpt( void );
void GC_EndMapLoadMemoryOpt( void );
void GC_ClearMapLoadMemoryOpt( void );
qboolean GC_MapLoadMemoryOpt( void );

/* Memory budget telemetry */
size_t GC_MemBudgetTotal( void );
size_t GC_MemBudgetUsed( void );
size_t GC_MemBudgetFree( void );
qboolean GC_MemBudgetExceeded( void );

/* Entity memory estimation */
size_t GC_EntityEstimateSize( void );

#else

static inline void GC_MemSetMap( const char *mapname ) { (void)mapname; }
static inline void GC_MemSample( const char *stage ) { (void)stage; }
static inline void GC_MemFail( const char *subsystem, size_t size, const char *file, int line )
{
	(void)subsystem;
	(void)size;
	(void)file;
	(void)line;
}
static inline void GC_InitMapLoadBuffer( void ) { }
static inline void GC_PrepareMapLoadBuffer( size_t size ) { (void)size; }
static inline void GC_PrepareMapLoadBufferForMap( const char *mapname ) { (void)mapname; }
static inline void *GC_BorrowMapLoadBuffer( size_t size ) { (void)size; return NULL; }
static inline qboolean GC_ReleaseMapLoadBuffer( void *ptr ) { (void)ptr; return false; }
static inline void GC_DiscardMapLoadBuffer( void ) { }
static inline qboolean GC_IsMapLoadBuffer( const void *ptr ) { (void)ptr; return false; }
static inline void GC_BeginMapLoadMemoryOpt( void ) { }
static inline void GC_EndMapLoadMemoryOpt( void ) { }
static inline void GC_ClearMapLoadMemoryOpt( void ) { }
static inline qboolean GC_MapLoadMemoryOpt( void ) { return false; }

/* Map-load memory pressure measurement (stubs) */
static inline void GC_MapLoadPressureBegin( void ) { }
static inline void GC_MapLoadPressureEnd( void ) { }
static inline void GC_MapLoadPressureSample( const char *stage ) { (void)stage; }
static inline size_t GC_MapLoadPressurePeak( void ) { return 0; }
static inline size_t GC_MapLoadPressureDelta( void ) { return 0; }

/* Entity memory estimation (stubs) */
static inline size_t GC_EntityEstimateSize( void ) { return 0; }

#endif
