/*
mem_gamecube.h - GameCube main-memory telemetry and map-load staging
Copyright (C) 2026 xash3d-gc contributors
*/
#pragma once

#if XASH_GAMECUBE

void GC_MemSetMap( const char *mapname );
void GC_MemSample( const char *stage );
void GC_MemFail( const char *subsystem, size_t size, const char *file, int line );

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

#endif
