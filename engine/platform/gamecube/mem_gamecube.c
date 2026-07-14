/*
mem_gamecube.c - GameCube main-memory telemetry and map-load staging
Copyright (C) 2026 xash3d-gc contributors
*/
#include "common.h"

#if XASH_GAMECUBE
#include "mem_gamecube.h"
#include <stdlib.h>

static char gc_mem_map[MAX_QPATH] = "(none)";
static size_t gc_mem_last;
static size_t gc_mem_hwm;

/* Contiguous staging buffer for maps/*.bsp. Borrowed after menu/client trim. */
static byte gc_mapload_static[GC_MAPLOAD_BUFFER_DEFAULT];
static byte *gc_mapload_buf;
static size_t gc_mapload_buf_size;
static qboolean gc_mapload_buf_in_use;
static qboolean gc_mapload_buf_dynamic;
static int gc_mapload_memopt_depth;
static qboolean gc_mapload_memopt_session; /* stays on after playstart until cleared */

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

	/* G72: unchanged totals are noise during map load; keep FAIL path chatty. */
	if( delta == 0 )
		return;

	Con_Reportf( "Xash3D GameCube: mem stage=%s total=%s delta=%s hwm=%s map=%s\n",
		stage, Q_memprint( total ), Q_memprint( delta ), Q_memprint( gc_mem_hwm ), gc_mem_map );
}

void GC_MemFail( const char *subsystem, size_t size, const char *file, int line )
{
	Con_Reportf( "Xash3D GameCube: mem FAIL subsystem=%s size=%s map=%s at=%s:%i total=%s hwm=%s\n",
		subsystem ? subsystem : "unknown", Q_memprint( size ), gc_mem_map, file, line,
		Q_memprint( Mem_TotalRealSize() ), Q_memprint( gc_mem_hwm ));
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
		if( gc_mapload_buf_dynamic )
			free( gc_mapload_buf );
		gc_mapload_buf = NULL;
		gc_mapload_buf_size = 0;
		gc_mapload_buf_dynamic = false;
	}

	if( !gc_mapload_buf )
	{
		size_t alloc_size = ( size + 4095u ) & ~4095u;

		if( alloc_size <= sizeof( gc_mapload_static ))
		{
			gc_mapload_buf = gc_mapload_static;
			gc_mapload_buf_dynamic = false;
			Con_Reportf( "Xash3D GameCube: map-load buffer using static staging %s\n",
				Q_memprint( alloc_size ));
		}
		else
		{
			gc_mapload_buf = (byte *)malloc( alloc_size );
			if( !gc_mapload_buf )
			{
				Con_Reportf( S_ERROR "Xash3D GameCube: map-load buffer alloc failed (%s)\n",
					Q_memprint( alloc_size ));
				return NULL;
			}
			gc_mapload_buf_dynamic = true;
			Con_Reportf( "Xash3D GameCube: map-load buffer ready %s\n", Q_memprint( alloc_size ));
		}
		gc_mapload_buf_size = alloc_size;
	}

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

void GC_DiscardMapLoadBuffer( void )
{
	if( gc_mapload_buf_in_use )
		return;

	if( gc_mapload_buf )
	{
		if( gc_mapload_buf_dynamic )
		{
			Con_Reportf( "Xash3D GameCube: map-load buffer discarded %s\n",
				Q_memprint( gc_mapload_buf_size ));
			free( gc_mapload_buf );
			gc_mapload_buf = NULL;
			gc_mapload_buf_size = 0;
			gc_mapload_buf_dynamic = false;
		}
	}
}

void GC_BeginMapLoadMemoryOpt( void )
{
	gc_mapload_memopt_depth++;
	gc_mapload_memopt_session = true;
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
}

qboolean GC_MapLoadMemoryOpt( void )
{
	return gc_mapload_memopt_session
		|| gc_mapload_memopt_depth > 0
		|| Sys_CheckParm( "-gcmap" ) != 0
		|| Sys_CheckParm( "-gcnewgame" ) != 0;
}
#endif
