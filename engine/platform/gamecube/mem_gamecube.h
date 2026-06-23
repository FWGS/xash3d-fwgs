/*
mem_gamecube.h - GameCube main-memory telemetry
Copyright (C) 2026 xash3d-gc contributors
*/
#pragma once

#if XASH_GAMECUBE

void GC_MemSetMap( const char *mapname );
void GC_MemSample( const char *stage );
void GC_MemFail( const char *subsystem, size_t size, const char *file, int line );

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

#endif
