/*
mem_gamecube.c - GameCube main-memory telemetry
Copyright (C) 2026 xash3d-gc contributors
*/
#include "common.h"

#if XASH_GAMECUBE
#include "mem_gamecube.h"

static char gc_mem_map[MAX_QPATH] = "(none)";
static size_t gc_mem_last;
static size_t gc_mem_hwm;

void GC_MemSetMap( const char *mapname )
{
	if( mapname && mapname[0] )
		Q_strncpy( gc_mem_map, mapname, sizeof( gc_mem_map ));
	else Q_strncpy( gc_mem_map, "(none)", sizeof( gc_mem_map ));
}

void GC_MemSample( const char *stage )
{
	size_t total = Mem_TotalRealSize();
	size_t delta = (total >= gc_mem_last) ? (total - gc_mem_last) : 0;

	if( total > gc_mem_hwm )
		gc_mem_hwm = total;

	gc_mem_last = total;

	Con_Reportf( "Xash3D GameCube: mem stage=%s total=%s delta=%s hwm=%s map=%s\n",
		stage, Q_memprint( total ), Q_memprint( delta ), Q_memprint( gc_mem_hwm ), gc_mem_map );
}

void GC_MemFail( const char *subsystem, size_t size, const char *file, int line )
{
	Con_Reportf( "Xash3D GameCube: mem FAIL subsystem=%s size=%s map=%s at=%s:%i total=%s hwm=%s\n",
		subsystem ? subsystem : "unknown", Q_memprint( size ), gc_mem_map, file, line,
		Q_memprint( Mem_TotalRealSize() ), Q_memprint( gc_mem_hwm ));
}
#endif
