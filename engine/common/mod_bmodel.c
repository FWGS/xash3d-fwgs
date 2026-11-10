/*
mod_bmodel.c - loading & handling world and brushmodels
Copyright (C) 2016 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "common.h"
#include "mod_local.h"
#include "sprite.h"
#include "xash3d_mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "enginefeatures.h"
#include "client.h"
#include "server.h"			// LUMP_ error codes

#if XASH_GAMECUBE
#include <stdlib.h>
#include "gamecube/mem_gamecube.h"
#include "platform/platform.h"
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <malloc.h>

void GC_CaptureNewGamePVS( void );
void GC_CaptureNewGamePVSFromModel( model_t *wmodel );
void GC_CaptureIntroTrainFaces( model_t *wmodel ); /* G277 */
qboolean GC_PrefetchCapFacesWhileEdgesLive( model_t *mod );
void GC_RebakeCapFaceLightmaps( model_t *mod );

static void Mod_GCFreeBspPin( void **ptr );
static byte *Mod_GCPinBspLump( poolhandle_t pool, const byte *src, size_t size );

typedef struct gc_bsp_deferred_s
{
	void  *markfaces;
	void  *leafs;
	void  *nodes;
	void  *clipnodes;
	byte  *lightdata;
	size_t lightdatasize;
} gc_bsp_deferred_t;

static gc_bsp_deferred_t gc_bsp_deferred;
static void *gc_marksurfaces_malloc_block;
static void *gc_leafs_malloc_block;
static qboolean gc_leafs_arena_tail;
static void *gc_surfaces_malloc_block;
static void *gc_texinfo_malloc_block;
static void *gc_nodes_malloc_block;
static qboolean gc_nodes_arena_tail;
static qboolean gc_nodes_static_block;
/* Filled by Mod_GCPreallocWorldNodes before leafs consume contiguous scratch
 * (c2a4d→c2a4e: 294 KiB nodes lost after leafs took 189 KiB, tip FATAL).
 * When both fit, park leafs in the same scratch carve (c4a1e→…→c5a1: nodes
 * alone left no 186 KiB leaf hole — probe 20261106-072643 / 090852). */
static mnode_t *gc_nodes_prealloc;
static qboolean gc_nodes_prealloc_scratch;
static mleaf_t *gc_leafs_prealloc;
/* Surface Tension peaks need ~270 KiB of mnode_t (c2a5e→c2a5f). Scratch is
 * often full of in-place planes + reserved surfaces + leafs; tip/malloc are
 * dry after changelevel. Keep a BSS last resort. */
#define GC_STATIC_NODES_BYTES ( 384u * 1024u )
/* Allocated once at the first map load (Mod_GCEnsureStaticReserves): as BSS
 * this and the reserves below took ~650 KiB of heap away from the startup
 * video and the main menu, which never use them. */
static byte *gc_static_nodes;
static void *gc_clipnodes_malloc_block;
static qboolean gc_clipnodes_pool_block;
#if XASH_GAMECUBE
/* Compact hull 1–3 clipnodes: campaign peak is c5a1 / rocket_frenzy ~32.3k
 * (c0a0c is already 16,698). Grow ONLY this table — dual 32k reserves
 * (+256 KiB BSS) starved the 853 KiB BSP tip on c0a0b (probe 20261104-051425).
 * Hull 0 stays on the smaller reserve (node count << clipnode peak). */
#define GC_STATIC_CLIPNODE_CAPACITY 32768
#define GC_STATIC_HULL0_CAPACITY    16384
static mclipnode16_t gc_static_clipnodes[GC_STATIC_CLIPNODE_CAPACITY]
	__attribute__((aligned( 32 )));
/* Hull 0 as BSS (like clipnodes): heap memalign of even 26 KiB fails under
 * c4a3 smoke fragmentation (free≈50 Kb, probe 20261105-065947) after the
 * durable surfedges tail copy. 128 KiB fixed cost; never tip-fatal. */
static mclipnode16_t gc_static_hull0_storage[GC_STATIC_HULL0_CAPACITY]
	__attribute__((aligned( 32 )));
static mclipnode16_t *gc_static_hull0 = gc_static_hull0_storage;
static qboolean gc_clipnodes_static_block;
static qboolean gc_clipnodes_scratch_block; /* in-place normalized scratch lump */
/* Submodel table is tiny (~64 B each) but tip-fatal / fragmented after denser
 * hops (c1a0→c1a0d 20261104-070402; dual-hop 080408 heap_free=55 Kb still tip
 * FAIL; c1a3d→c1a3a needs ~163 entries). Prefer malloc; 256-entry BSS last
 * (~16 KiB). Never arena tail (surface reserve). */
#define GC_STATIC_SUBMODEL_CAPACITY 256
static dmodel_t *gc_static_submodels;
static void *gc_submodels_malloc_block;
static qboolean gc_submodels_static_block;
static void *gc_entities_malloc_block;
static qboolean gc_entities_arena_tail;
/* Dense Residue maps (c2a4e ~68 KiB) can miss both malloc and tip after a
 * changelevel (heap_free=149 Kb still fragmented). Keep a heap last resort
 * via Mod_GCTakeReserve — true BSS here (128 KiB) tipped c4a3 boot (085734). */
#define GC_STATIC_ENTITIES_BYTES ( 128u * 1024u )
static char *gc_static_entities;
static qboolean gc_entities_static_block;
/* Tip-safe HL1 changelevel: dense dest entity lumps miss fragmented tip
 * (c3a2a≈75 KiB, c4a3≈98 KiB). DoD listen CapFaces needs that tip back
 * (tip-donner OOM when BSS was linked) — valve builds omit LIBBBA, so
 * gate the BSS on that. Call site already requires BYTES > 0. */
#if !defined(XASH_GAMECUBE_HAVE_LIBBBA) || !XASH_GAMECUBE_HAVE_LIBBBA
#define GC_CHANGELEVEL_ENTITIES_FALLBACK_BYTES ( 112u * 1024u )
static char gc_changelevel_entities_fallback_bss[GC_CHANGELEVEL_ENTITIES_FALLBACK_BYTES];
static char *gc_changelevel_entities_fallback = gc_changelevel_entities_fallback_bss;
#else
#define GC_CHANGELEVEL_ENTITIES_FALLBACK_BYTES 0
static char *gc_changelevel_entities_fallback = NULL;
#endif
static qboolean gc_changelevel_entities_fallback_used;
/* Surface Tension c2a5f: tip reports ≥280 KiB free but malloc/memalign of
 * 202 KiB mleaf_t misses (fragmentation) after BSP staging. Hold a contiguous
 * session block when nodes 384 cannot be taken (free≈281 on that hop). */
/* Peak materialized leafs ~202 KiB (c2a5f); keep a little align pad. */
/* dod_donner mleaf_t table is ~242 KiB (probe 114602). */
#define GC_STATIC_LEAFS_BYTES ( 256u * 1024u )
static byte *gc_static_leafs;
static qboolean gc_leafs_static_block;
/* Set when EnsureStaticReserves sees free < nodes 384 — carve mleaf_t from
 * the surface high-slot on this load only (c2a5f). Cold loads with free≥384
 * must not carve: TailAlloc into the slot hung texinfo on c2a5e. */
static qboolean gc_leafs_highslot_carve;

/*
 * Last-resort world-table reserves.  Each is a separate heap block taken at
 * the start of a map load (before BSP staging) and kept for the session; a
 * reserve that does not fit yet is retried on the next map load.  As BSS they
 * charged ~650 KiB to boot, the startup video and the menu, which never use
 * them.  Returns true once every reserve is held.
 */
static void *Mod_GCTakeReserve( void **slot, size_t bytes, const char *name )
{
	if( *slot )
		return *slot;
	*slot = memalign( 32, bytes );
	if( *slot )
		Con_Reportf( "Xash3D GameCube: world reserve %s ready %s\n", name, Q_memprint( bytes ));
	else
		Con_Reportf( "Xash3D GameCube: world reserve %s deferred need=%s free=%s\n",
			name, Q_memprint( bytes ), Q_memprint( GC_MemHeapFreeBytes() ));
	return *slot;
}

qboolean Mod_GCEnsureStaticReserves( void )
{
	/* Submodels first (tiny). Nodes before entities: taking entities (128 KiB)
	 * first left free=380 on c2a4c boot (nodes need 384) and free≈83 on
	 * Residue→QE hops — Mod_LoadNodes then tip-FATAL at 294 KiB. */
	Mod_GCTakeReserve( (void **)&gc_static_submodels, sizeof( dmodel_t ) * GC_STATIC_SUBMODEL_CAPACITY, "submodels" );
	/* hull0 is BSS (gc_static_hull0_storage) — no heap reserve. */
	/* SP BSS edicts (~1 MiB) shrink MEM1. memalign can still pull the 384 KiB
	 * nodes block from the arena and leave tip≈2 KiB — CL_InitLocal then hangs
	 * with no further OSReport (end-triple reprove 20261106-085739). At boot
	 * require tip headroom for client init; during map load only keep a small
	 * floor so c5a1 can still take the session reserve. */
	{
		/* 32 KiB headroom: boot free≈418 clears the gate; map load parks
		 * nodes in BSP scratch after host releases this reserve. */
		const size_t nodes_headroom = 32u * 1024u;

		if( GC_MemHeapFreeBytes() >= GC_STATIC_NODES_BYTES + nodes_headroom )
			Mod_GCTakeReserve( (void **)&gc_static_nodes, GC_STATIC_NODES_BYTES, "nodes" );
		else
			Con_Reportf( "Xash3D GameCube: world reserve nodes deferred need=%s free=%s\n",
				Q_memprint( GC_STATIC_NODES_BYTES ), Q_memprint( GC_MemHeapFreeBytes() ));
	}
	/* Never consolation-seize leafs when nodes deferred (-gcmap boot tip). */
	gc_leafs_highslot_carve = false;
	if( !gc_static_nodes && !gc_static_leafs
		&& GC_MemHeapFreeBytes() < GC_STATIC_NODES_BYTES )
	{
		Con_Reportf( "Xash3D GameCube: world reserve leafs deferred (preserve tip for GameDLLInit) free=%s\n",
			Q_memprint( GC_MemHeapFreeBytes() ));
	}
	/* Hold entities only when nodes are already secured, or tip can still fund
	 * a dense node tree after the 128 KiB take. Skip pre-BSP entities seize on
	 * c2a5e/f — Mod_LoadEntities malloc(exact) first so leaf tip-park still
	 * sees a contiguous hole (128 KiB seize starved leafs 202 on c2a5f). */
	{
		const qboolean st_leafs_hop = sv.name[0]
			&& ( !Q_stricmp( sv.name, "c2a5f" ) || !Q_stricmp( sv.name, "c2a5e" ));

		if( !st_leafs_hop
			&& ( gc_static_nodes || GC_MemHeapFreeBytes() >= GC_STATIC_ENTITIES_BYTES + 300u * 1024u ))
			Mod_GCTakeReserve( (void **)&gc_static_entities, GC_STATIC_ENTITIES_BYTES, "entities" );
		else if( !gc_static_entities )
			Con_Reportf( "Xash3D GameCube: world reserve entities deferred%s free=%s\n",
				st_leafs_hop ? " (ST leafs hop)" : " (preserve tip for nodes)",
				Q_memprint( GC_MemHeapFreeBytes() ));
	}
	return gc_static_submodels && gc_static_hull0 && ( gc_static_nodes || gc_static_entities || gc_static_leafs );
}

/*
 * Eager reserves keep Surface Tension / dense hops from tip-OOMing when
 * scratch is full. Maps that settle nodes/submodels elsewhere (c5a1: nodes
 * in BSP scratch) would otherwise leave ~400 KiB idle and hang InitInput /
 * HUD ZIP at free≈4 KiB (probe 090911). Return unused blocks once we know
 * this map does not need them; EnsureStaticReserves retakes on the next load.
 *
 * Call this immediately before deferred client InitInput — releasing during
 * BSP load let the 384 KiB get fragmented/consumed before CL_LoadProgs
 * (probe 091844: free only rose to 9 KiB and InitInput still hung).
 */
void Mod_GCReleaseUnusedWorldReserves( void )
{
	if( gc_static_nodes && !gc_nodes_static_block )
	{
		free( gc_static_nodes );
		gc_static_nodes = NULL;
		Con_Reportf( "Xash3D GameCube: released unused nodes reserve %s\n",
			Q_memprint( GC_STATIC_NODES_BYTES ));
	}
	if( gc_static_submodels && !gc_submodels_static_block )
	{
		free( gc_static_submodels );
		gc_static_submodels = NULL;
		Con_Reportf( "Xash3D GameCube: released unused submodels reserve %s\n",
			Q_memprint( sizeof( dmodel_t ) * GC_STATIC_SUBMODEL_CAPACITY ));
	}
}

/* Dense source maps (c1a0d) leave tip≈22 KiB into the next hop when the
 * 128 KiB entities reserve stays held and nodes were never taken (deferred).
 * Drop every session heap reserve on changelevel so EnsureStaticReserves can
 * retake in order on the destination (probe 173129 hung at spawn c1a0a). */
void Mod_GCReleaseWorldReservesForChangelevel( void )
{
	if( gc_static_nodes && !gc_nodes_static_block )
	{
		free( gc_static_nodes );
		gc_static_nodes = NULL;
		Con_Reportf( "Xash3D GameCube: changelevel released nodes reserve %s\n",
			Q_memprint( GC_STATIC_NODES_BYTES ));
	}
	if( gc_static_submodels && !gc_submodels_static_block )
	{
		free( gc_static_submodels );
		gc_static_submodels = NULL;
		Con_Reportf( "Xash3D GameCube: changelevel released submodels reserve %s\n",
			Q_memprint( sizeof( dmodel_t ) * GC_STATIC_SUBMODEL_CAPACITY ));
	}
	/* Even if the previous world borrowed this as static_block, purge has
	 * already dropped mod->entities — reclaim the 128 KiB tip block. */
	if( gc_static_entities )
	{
		free( gc_static_entities );
		gc_static_entities = NULL;
		gc_entities_static_block = false;
		Con_Reportf( "Xash3D GameCube: changelevel released entities reserve %s\n",
			Q_memprint( GC_STATIC_ENTITIES_BYTES ));
	}
	/* BSS fallback is not heap — just allow the next dest to reuse it. */
	gc_changelevel_entities_fallback_used = false;
	if( gc_static_leafs && !gc_leafs_static_block )
	{
		free( gc_static_leafs );
		gc_static_leafs = NULL;
		Con_Reportf( "Xash3D GameCube: changelevel released leafs reserve %s\n",
			Q_memprint( GC_STATIC_LEAFS_BYTES ));
	}
	else if( gc_static_leafs )
	{
		/* Previous world used it — drop so Ensure can retake contiguous. */
		free( gc_static_leafs );
		gc_static_leafs = NULL;
		gc_leafs_static_block = false;
		Con_Reportf( "Xash3D GameCube: changelevel released leafs reserve %s\n",
			Q_memprint( GC_STATIC_LEAFS_BYTES ));
	}
}

/* Call after Mod_PurgeForMapLoad on changelevel — tip is contiguous then.
 * c2a5f tip-fragments a 202 KiB leafs malloc (free≈281). Also seize on
 * predecessor c2a5e so the hop releases a contiguous 208 KiB hole (gated
 * c2a5f-only left free≈247 < headroom). Do NOT seize on Lambda/Xen colds
 * (c3a2b tip≈28 when every hop seized). Keep ≥96 KiB after the take. */
void *Mod_GCTakeLeafsReserveForChangelevel( void )
{
	/* Called after Mod_LoadEntities — entities already own tip/static.
	 * Old 96 KiB headroom was for pre-entity seize; post-entity c2a5f has
	 * free≈262 and need leafs 208, so 96 blocked the take and LoadLeafs
	 * tip-OOM'd at 202 KiB. Keep 48 KiB for ent-private overflow. */
	const size_t headroom = 48u * 1024u;
	const qboolean st_leafs_hop = sv.name[0]
		&& ( !Q_stricmp( sv.name, "c2a5f" ) || !Q_stricmp( sv.name, "c2a5e" ));

	if( gc_static_leafs )
		return gc_static_leafs;
	if( !st_leafs_hop )
		return NULL;
	Image_GCPurgeDecodeScratch();
	if( GC_MemHeapFreeBytes() < GC_STATIC_LEAFS_BYTES + headroom )
	{
		Con_Reportf( "Xash3D GameCube: changelevel leafs seize skipped need=%s+48k free=%s map=%s\n",
			Q_memprint( GC_STATIC_LEAFS_BYTES ), Q_memprint( GC_MemHeapFreeBytes() ), sv.name );
		return NULL;
	}
	Mod_GCTakeReserve( (void **)&gc_static_leafs, GC_STATIC_LEAFS_BYTES, "leafs" );
	if( gc_static_leafs )
		Con_Reportf( "Xash3D GameCube: changelevel post-entities seized leafs %s for %s (free now %s)\n",
			Q_memprint( GC_STATIC_LEAFS_BYTES ), sv.name, Q_memprint( GC_MemHeapFreeBytes() ));
	return gc_static_leafs;
}
#endif
static void *gc_planes_malloc_block;
static qboolean gc_planes_arena_tail;
static qboolean gc_retain_bsp_source_buffer;
static qboolean gc_surfedges_source_alias;
static qboolean gc_faces_source_alias;
static dface_t *gc_faces_tail_copy;
/* G283: world msurface_t settled in retained BSP scratch (busy-protected). */
static qboolean gc_world_msurface_retained;
/* The world model whose load claimed the gc_*_malloc_block / scratch state
 * below. Freeing any other brush model (a stale previous level after a
 * changelevel) must not tear the current world down. */
static const model_t *gc_world_blocks_owner;
static byte *gc_bsp_scratch_base;
static size_t gc_bsp_scratch_size;
static size_t gc_bsp_scratch_carve_high;
/* Bytes kept free at the high end of the map-load arena until the world
 * surface table is placed. Converted planes/vertexes/edges/faces must not
 * consume this region: c2a4e/c5a1 need the lighting-through-EOF hole plus
 * the unused tail as one contiguous block. */
static size_t gc_bsp_surface_reserve;
/* Set when the high slot is clamped for a compact (oversized) BSP image —
 * world-tex pack must not eat the medge16 expand window. */
static qboolean gc_bsp_surface_reserve_clamped;
/* CapFaces: live msurface table omitted — offline cf_*.gcp owns Flipper draw. */
static qboolean gc_capfaces_lean_surfaces;

qboolean GC_CapFacesLeanSurfaces( void )
{
	return gc_capfaces_lean_surfaces;
}
/* Compact DoD: edges take the clipnodes+markfaces hole; texinfo (~196 KiB)
 * and leafs (~249 KiB on dod_flash) spill here when BSP holes are spent.
 * Paired with a matching cut to studio BSS so total MEM1 stays flat. */
/* Flash fits in ~456 KiB. Clamped maps (donner) also park edges + surfedges +
 * faces + planes here so the surface high-slot can open (~2 Mb). */
/* Flash historically fit in 456 KiB. Clamped maps need edges + surfedges +
 * faces (~232+214+228 KiB) parked here so the faces range at EOF cannot pin
 * the surface high-slot floor (donner busy [2.04..2.26) Mb). */
/* After faces reclaim: edges+surfedges(~446)+markfaces(31)+marksurfaces(62)
 * +leafs(242) = ~781 KiB (probe 113318). 768 left leafs ~14 KiB short when
 * marksurfaces also spills; scratch-only marksurfaces OOMs (probe 114822). */
/* 800 KiB total: last 32 KiB reserved for CapFaces CMPR bounce so tip-tight
 * maps (dod_donner) still texture CapFaces after listen spawn. */
#define GC_WORLD_ARRAY_SPILL_BYTES (800u * 1024u)
#define GC_WORLD_ARRAY_SPILL_BOUNCE_RESERVE (32u * 1024u)
#define GC_WORLD_ARRAY_SPILL_USABLE \
	( GC_WORLD_ARRAY_SPILL_BYTES - GC_WORLD_ARRAY_SPILL_BOUNCE_RESERVE )
static byte gc_world_array_spill[GC_WORLD_ARRAY_SPILL_BYTES] __attribute__((aligned( 32 )));
static qboolean gc_world_array_spill_bounce_taken;
static size_t gc_world_array_spill_used;
/* Permanent base reservation (e.g. server string arena). Survives ClearRetained. */
static size_t gc_world_array_spill_pin;
static qboolean gc_texinfo_carve_pending;
static file_t *gc_bsp_reload_file;
static char gc_bsp_reload_path[MAX_QPATH];
static dheader_t gc_bsp_reload_header;
static qboolean gc_bsp_reload_header_ok;
/* G154 c5a1: full LUMP_LIGHTING (~1 MiB) will not fit tip or fragmented scratch
 * after the surface high-slot claim. Stage the lump in ARAM and hydrate one
 * face at a time into gc_lm_hydrate_buf for CapFaces/G180 bake.
 * lightofs lives in gc_surf_lightofs[] — mextrasurf.reserved[] is CACHESPOT. */
static byte gc_lm_aram_marker;
static unsigned int gc_lm_aram_base;
static size_t gc_lm_aram_bytes;
/* Session-owned lighting ARAM window — reused across changelevels so each
 * large map does not AR_Alloc another 1 MiB (campaign ARAM exhaustion). */
static unsigned int gc_lm_aram_pool;
static size_t gc_lm_aram_pool_cap;
/* Keep BSS tiny — 96 KiB here fatal'd c4a3 at sv_init 42 KiB (probe 143315).
 * 32 KiB covers 64×64×2 styles; larger faces skip to mid-grey. */
#define GC_LM_HYDRATE_MAX ( 32u * 1024u )
#define GC_LM_ARAM_POOL_MIN ( 1280u * 1024u )
static byte gc_lm_hydrate_buf[GC_LM_HYDRATE_MAX] __attribute__(( aligned( 32 )));
/* Full int — lightofs is a byte offset into LUMP_LIGHTING (up to ~1 MiB).
 * Prefer tip malloc; on fragmentation park the table in a session ARAM pool
 * (c2a4e dest 34 KiB tip-OOM → CapFaces lm_real=0/320 in 164958). Avoid BSS —
 * permanent MEM1 shrink starved changelevel tip (150615–151426). */
#define GC_SURF_LIGHTOFS_ARAM_MIN ( 9216u * sizeof( int ))
static int *gc_surf_lightofs;
static unsigned int gc_surf_lightofs_aram;
static size_t gc_surf_lightofs_aram_cap;
static int gc_surf_lightofs_count;
static model_t *gc_surf_lightofs_model;

static void Mod_GCClearSurfLightofs( void )
{
	if( gc_surf_lightofs )
		free( gc_surf_lightofs );
	gc_surf_lightofs = NULL;
	/* Keep the ARAM pool for the next map — only drop the live binding. */
	gc_surf_lightofs_count = 0;
	gc_surf_lightofs_model = NULL;
}

static qboolean Mod_GCEnsureSurfLightofs( int need )
{
	const size_t bytes = (size_t)need * sizeof( int );
	unsigned int want;
	int *mem1;
	size_t filled;

	if( need <= 0 )
		return false;

	mem1 = (int *)malloc( bytes );
	if( mem1 )
	{
		for( filled = 0; filled < (size_t)need; filled++ )
			mem1[filled] = -1;
		if( gc_surf_lightofs )
			free( gc_surf_lightofs );
		gc_surf_lightofs = mem1;
		gc_surf_lightofs_count = need;
		return true;
	}

	want = (unsigned int)(( bytes + 31u ) & ~31u );
	if( want < GC_SURF_LIGHTOFS_ARAM_MIN )
		want = GC_SURF_LIGHTOFS_ARAM_MIN;
	if( !gc_surf_lightofs_aram || want > gc_surf_lightofs_aram_cap )
	{
		unsigned int aram = GC_AramReserve( want );

		if( !aram )
			return false;
		gc_surf_lightofs_aram = aram;
		gc_surf_lightofs_aram_cap = want;
		Con_Reportf( "Xash3D GameCube: lightofs side-table ARAM %s (%d slots)\n",
			Q_memprint( want ), need );
	}

	/* Seed -1 via the lighting hydrate bounce (32 KiB aligned). */
	memset( gc_lm_hydrate_buf, 0xff, GC_LM_HYDRATE_MAX );
	filled = 0;
	while( filled < bytes )
	{
		const size_t chunk = ( bytes - filled ) > GC_LM_HYDRATE_MAX
			? GC_LM_HYDRATE_MAX : ( bytes - filled );

		GC_AramCopy( gc_surf_lightofs_aram + (unsigned int)filled,
			gc_lm_hydrate_buf, (unsigned int)chunk, true );
		filled += chunk;
	}
	gc_surf_lightofs = NULL;
	gc_surf_lightofs_count = need;
	return true;
}

static void Mod_GCStoreSurfLightofs( int idx, int lightofs )
{
	if( idx < 0 || idx >= gc_surf_lightofs_count )
		return;
	if( gc_surf_lightofs )
	{
		gc_surf_lightofs[idx] = lightofs;
		return;
	}
	if( gc_surf_lightofs_aram )
		GC_AramCopy( gc_surf_lightofs_aram + (unsigned int)idx * sizeof( int ),
			&lightofs, sizeof( lightofs ), true );
}

static int Mod_GCSurfaceLightofs( const msurface_t *surf )
{
	int idx;
	int value = -1;

	if( !surf || !gc_surf_lightofs_model || !gc_surf_lightofs_model->surfaces )
		return -1;
	if( gc_surf_lightofs_count <= 0 )
		return -1;
	idx = (int)( surf - gc_surf_lightofs_model->surfaces );
	if( idx < 0 || idx >= gc_surf_lightofs_count )
		return -1;
	if( gc_surf_lightofs )
		return gc_surf_lightofs[idx];
	if( !gc_surf_lightofs_aram )
		return -1;
	GC_AramCopy( gc_surf_lightofs_aram + (unsigned int)idx * sizeof( int ),
		&value, sizeof( value ), false );
	return value;
}

typedef struct gc_bsp_busy_range_s
{
	size_t start;
	size_t end;
} gc_bsp_busy_range_t;

static qboolean Mod_GCPointerInBuffer( const void *ptr, const byte *base, size_t size );
static void *Mod_GCAllocBspScratch( byte *base, size_t size, const gc_bsp_busy_range_t *busy, size_t busy_count, size_t alloc_size, size_t align );

qboolean Mod_GCIsRetainedBspScratch( const void *buf )
{
	return buf != NULL && buf == gc_bsp_scratch_base;
}

static byte *gc_menu_lm_atlas;
static int gc_menu_ls_count; /* animated faces kept in the scratch, see below */

void Mod_GCClearRetainedBspScratch( void )
{
	gc_menu_lm_atlas = NULL; /* lives in the scratch */
	gc_menu_ls_count = 0;
	gc_bsp_scratch_base = NULL;
	gc_bsp_scratch_size = 0;
	gc_bsp_scratch_carve_high = 0;
	gc_bsp_surface_reserve = 0;
	gc_retain_bsp_source_buffer = false;
	gc_surfedges_source_alias = false;
	gc_faces_source_alias = false;
	gc_faces_tail_copy = NULL;
	gc_world_msurface_retained = false;
	gc_lm_aram_base = 0;
	gc_lm_aram_bytes = 0;
	/* Drop spill-parked string arena, rewind, then re-park at base WHILE
	 * used==0. Waiting until faces reclaim leaves used>0 so Ensure skips
	 * and entity spawn Host_Errors (tip-flash-strbase). */
	{
		extern void SV_GCDropSpillStringArena( void );
		extern qboolean SV_GCEnsureStringArena( void );
		SV_GCDropSpillStringArena();
		gc_world_array_spill_pin = 0;
		gc_world_array_spill_used = 0;
		SV_GCEnsureStringArena();
	}
	/* Bounce carve stays for the session — CapFaces may still hold the ptr. */
	Mod_GCClearSurfLightofs();
}

#if XASH_GAMECUBE
/* Disc LM bake for Flipper CapFaces — New Game and listen/connect world render. */
static qboolean Mod_GCWantFlipperDiscLightmaps( void )
{
	return Sys_CheckParm( "-gcnewgame" ) || Sys_CheckParm( "-gcworldrender" );
}

qboolean Mod_GCAramLightmapActive( void )
{
	return gc_lm_aram_base != 0 && gc_lm_aram_bytes > 0;
}

color24 *Mod_GCHydrateSurfaceLightmap( const msurface_t *surf )
{
	int lightofs;
	int sample_size;
	int smax, tmax;
	int styles = 0;
	int map;
	size_t bytes;

	if( !surf )
		return NULL;
	if( surf->samples )
		return surf->samples;
	if( !gc_lm_aram_base || !surf->info )
		return NULL;

	lightofs = Mod_GCSurfaceLightofs( surf );
	if( lightofs < 0 )
		return NULL;

	sample_size = Mod_SampleSizeForFace( surf );
	if( sample_size < 1 )
		sample_size = 16;
	smax = ( surf->info->lightextents[0] / sample_size ) + 1;
	tmax = ( surf->info->lightextents[1] / sample_size ) + 1;
	if( smax < 1 )
		smax = 1;
	if( tmax < 1 )
		tmax = 1;
	for( map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++ )
		styles++;
	if( styles < 1 )
		styles = 1;

	bytes = (size_t)smax * (size_t)tmax * (size_t)styles * sizeof( color24 );
	if( bytes == 0 || bytes > GC_LM_HYDRATE_MAX )
		return NULL;
	if((size_t)lightofs + bytes > gc_lm_aram_bytes )
		return NULL;

	GC_AramCopy( gc_lm_aram_base + (unsigned int)lightofs, gc_lm_hydrate_buf,
		(unsigned int)bytes, false );
	return (color24 *)gc_lm_hydrate_buf;
}

void Mod_GCReleaseTempNewGameLightmaps( model_t *mod )
{
	int i;
	const qboolean had_aram = ( gc_lm_aram_base != 0 );

	if( !mod || !mod->surfaces )
		return;
	if( !gc_lm_aram_base && !mod->lightdata )
		return;

	for( i = 0; i < mod->numsurfaces; i++ )
		mod->surfaces[i].samples = NULL;
	mod->lightdata = NULL;
	Con_Reportf( "Xash3D GameCube: G154 released disc lightmap after face bake%s\n",
		had_aram ? " [ARAM]" : "" );
	/* Drop the ARAM bind here — Mod_GCFreeBspPin(lightdata) is skipped while
	 * CapFaces is deferred, otherwise hydrate sees base=0 (c1a2b lm_real=0/320). */
	gc_lm_aram_base = 0;
	gc_lm_aram_bytes = 0;
	Mod_GCClearSurfLightofs();
}
#endif /* XASH_GAMECUBE */

static qboolean Mod_GCPointerInScratchArena( const void *ptr )
{
	/* Include the high-end carve reserved for deferred nodes/clipnodes. */
	size_t full_size;

	if( !gc_bsp_scratch_base )
		return false;

	full_size = gc_bsp_scratch_size + gc_bsp_scratch_carve_high;
	return Mod_GCPointerInBuffer( ptr, gc_bsp_scratch_base, full_size );
}

#if XASH_GAMECUBE
static size_t Mod_GCSurfaceBlockBytes( size_t numsurfaces )
{
	if( numsurfaces == 0 )
		return 0;
	return numsurfaces * ( sizeof( msurface_t ) + sizeof( mextrasurf_t ));
}

static size_t Mod_GCPreSurfaceCapacity( void )
{
	const size_t cap = GC_MapLoadBufferCapacity();

	if( gc_bsp_surface_reserve == 0 || cap <= gc_bsp_surface_reserve )
		return cap;
	return cap - gc_bsp_surface_reserve;
}

/* Tail carves that would land in the reserved surface slot must fail so the
 * surface table can occupy lighting-through-EOF plus unused tail as one hole.
 * Never start below the BSP image: a zero highwater would otherwise place
 * faces/texinfo at offset 0 on top of in-place planes (c1a1f/c4a3 hang). */
static void *Mod_GCPreSurfaceTailAlloc( size_t after, size_t need )
{
	const size_t usable = Mod_GCPreSurfaceCapacity();
	const size_t floor = gc_bsp_scratch_size + gc_bsp_scratch_carve_high;

	if( after < floor )
		after = floor;
	if( need == 0 || after >= usable || need > usable - after )
		return NULL;
	return GC_MapLoadBufferTailAlloc( after, need );
}
#endif

static void Mod_GCFreeBspPin( void **ptr )
{
	if( !ptr || !*ptr )
		return;

	if( *ptr == &gc_lm_aram_marker )
	{
		gc_lm_aram_base = 0;
		gc_lm_aram_bytes = 0;
		*ptr = NULL;
		return;
	}

	if( Mod_GCPointerInScratchArena( *ptr ))
	{
		*ptr = NULL;
		return;
	}

	/* Spill BSS is not malloc — freeing it Guru's (dod_donner markfaces). */
	if( (const byte *)*ptr >= gc_world_array_spill
		&& (const byte *)*ptr < gc_world_array_spill + GC_WORLD_ARRAY_SPILL_BYTES )
	{
		*ptr = NULL;
		return;
	}

	free( *ptr );
	*ptr = NULL;
}
#endif
#include "swaplib.h"
#include "ref_common.h"
#if defined( HAVE_OPENMP )
#include <omp.h>
#endif // HAVE_OPENMP

#define MIPTEX_CUSTOM_PALETTE_SIZE_BYTES ( sizeof( int16_t ) + 768 )

typedef struct leaflist_s
{
	int      count;
	int      maxcount;
	qboolean overflowed;
	int      *list;
	vec3_t   mins, maxs;
	int      topnode; // for overflows where each leaf can't be stored individually
} leaflist_t;

typedef struct
{
	// generic lumps
	dmodel_t   *submodels;
	size_t     numsubmodels;

	dvertex_t  *vertexes;
	size_t     numvertexes;

	dplane_t   *planes;
	size_t     numplanes;

	union
	{
		dnode_t   *nodes;
		dnode32_t *nodes32;
	};
	size_t numnodes;

	union
	{
		dleaf_t   *leafs;
		dleaf32_t *leafs32;
	};
	size_t numleafs;

	union
	{
		dclipnode_t   *clipnodes;
		dclipnode32_t *clipnodes32;
	};
	size_t numclipnodes;

	dtexinfo_t *texinfo;
	size_t     numtexinfo;

	union
	{
		dmarkface_t   *markfaces;
		dmarkface32_t *markfaces32;
	};
	size_t nummarkfaces;

	dsurfedge_t *surfedges;
	size_t      numsurfedges;

	union
	{
		dedge_t   *edges;
		dedge32_t *edges32;
	};
	size_t numedges;

	union
	{
		dface_t   *surfaces;
		dface32_t *surfaces32;
	};
	size_t numsurfaces;

	dfaceinfo_t *faceinfo;
	size_t      numfaceinfo;

	// array lumps
	byte   *visdata;
	size_t visdatasize;

	byte   *lightdata;
	size_t lightdatasize;

	byte   *deluxdata;
	size_t deluxdatasize;

	byte   *shadowdata;
	size_t shadowdatasize;

	byte   *entdata;
	size_t entdatasize;

	byte   *rgblightdata;
	size_t rgblightdatasize;

	// lumps that required personal handler
	dmiptexlump_t *textures;
	size_t        texdatasize;

	// intermediate arrays (pointers will lost after loading, but keep the data)
	color24       *deluxedata_out; // deluxemap data pointer
	byte          *shadowdata_out; // occlusion data pointer
	dclipnode32_t *clipnodes_out;  // temporary 32-bit array to hold clipnodes

	// misc stuff
	int       lightmap_samples; // samples per lightmap (1 or 3)
	int       version;          // model version
	qboolean  isworld;
	qboolean  isbsp30ext;

} dbspmodel_t;
#if XASH_GAMECUBE
static void *Mod_GCAllocWorldScratch( model_t *mod, dbspmodel_t *bmod, const gc_bsp_busy_range_t *busy, size_t busy_count, size_t alloc_size, size_t align );
static size_t Mod_GCCollectAllWorldScratchBusy( model_t *mod, dbspmodel_t *bmod, gc_bsp_busy_range_t *busy, size_t cap );
static void Mod_GCRefreshClampedSurfaceSlot( model_t *mod, dbspmodel_t *bmod );
#endif
static void *Mod_GCWorldArrayAlloc( model_t *mod, dbspmodel_t *bmod, size_t bytes, const char *what );

#if XASH_GAMECUBE
/* Compact (no-lighting) DoD BSPs have no deferred-LM scratch hole. Drop
 * lumps EnsureBspLump / soft-skip can restore so edges (~186 KiB) and
 * texinfo (~196 KiB) land in BSP holes instead of the surface high-slot. */
static void Mod_GCReleasePreEdgeScratchLumps( dbspmodel_t *bmod )
{
	size_t freed = 0;

	if( !GC_MapLoadMemoryOpt() || !bmod || !bmod->isworld || !gc_bsp_scratch_base )
		return;

	if( bmod->visdata && Mod_GCPointerInBuffer( bmod->visdata, gc_bsp_scratch_base, gc_bsp_scratch_size ))
	{
		freed += bmod->visdatasize;
		Mod_GCFreeBspPin( (void **)&bmod->visdata );
		bmod->visdatasize = 0;
	}
	if( bmod->version == QBSP2_VERSION )
	{
		if( bmod->clipnodes32 && Mod_GCPointerInBuffer( bmod->clipnodes32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
		{
			freed += (size_t)bmod->numclipnodes * sizeof( *bmod->clipnodes32 );
			Mod_GCFreeBspPin( (void **)&bmod->clipnodes32 );
		}
	}
	else if( bmod->clipnodes && Mod_GCPointerInBuffer( bmod->clipnodes, gc_bsp_scratch_base, gc_bsp_scratch_size ))
	{
		freed += (size_t)bmod->numclipnodes * sizeof( *bmod->clipnodes );
		Mod_GCFreeBspPin( (void **)&bmod->clipnodes );
	}
	/* marksurfaces sits immediately after clipnodes in HL BSPs — free both
	 * so edges (~186 KiB) get one contiguous hole (clip alone is ~181 KiB).
	 * Also free on clamped maps: nodes need that hole; markfaces reload into
	 * spill after surfaces (probe 112813). */
	if( bmod->version == QBSP2_VERSION )
	{
		if( bmod->markfaces32 && Mod_GCPointerInBuffer( bmod->markfaces32, gc_bsp_scratch_base, gc_bsp_scratch_size ))
		{
			freed += (size_t)bmod->nummarkfaces * sizeof( *bmod->markfaces32 );
			Mod_GCFreeBspPin( (void **)&bmod->markfaces32 );
		}
	}
	else if( bmod->markfaces && Mod_GCPointerInBuffer( bmod->markfaces, gc_bsp_scratch_base, gc_bsp_scratch_size ))
	{
		freed += (size_t)bmod->nummarkfaces * sizeof( *bmod->markfaces );
		Mod_GCFreeBspPin( (void **)&bmod->markfaces );
	}
	if( bmod->entdata && Mod_GCPointerInBuffer( bmod->entdata, gc_bsp_scratch_base, gc_bsp_scratch_size ))
	{
		freed += bmod->entdatasize;
		Mod_GCFreeBspPin( (void **)&bmod->entdata );
		bmod->entdatasize = 0;
	}
	if( freed )
		Con_Reportf( "Xash3D GameCube: pre-edge scratch reclaim %s (vis/clipnodes/markfaces/ents)\n",
			Q_memprint( freed ));
}

/* During BSP load use the full 800 KiB. CapFaces CMPR borrow only reserves
 * the high 32 KiB once taken — locking it earlier starved dod_flash leafs
 * (tip-flash-regress 130709: spill left 157 < mleaf 248). */
static size_t Mod_GCWorldArraySpillCap( void )
{
	return gc_world_array_spill_bounce_taken
		? GC_WORLD_ARRAY_SPILL_USABLE
		: GC_WORLD_ARRAY_SPILL_BYTES;
}

static void *Mod_GCWorldArraySpillAlloc( size_t bytes, const char *what )
{
	size_t aligned = ALIGN( bytes, 32 );
	size_t cap = Mod_GCWorldArraySpillCap();
	byte *out;

	if( aligned > cap - gc_world_array_spill_used )
		return NULL;
	out = gc_world_array_spill + gc_world_array_spill_used;
	gc_world_array_spill_used += aligned;
	Con_Reportf( "Xash3D GameCube: world %s using array spill %s (spill used %s)\n",
		what, Q_memprint( bytes ), Q_memprint( gc_world_array_spill_used ));
	return out;
}

void *Mod_GCWorldArraySpillTryAlloc( size_t bytes, const char *what )
{
	return Mod_GCWorldArraySpillAlloc( bytes, what );
}

size_t Mod_GCWorldArraySpillUsed( void )
{
	return gc_world_array_spill_used;
}

/* Pin through a spill allocation so ClearRetained cannot rewind over it. */
void Mod_GCWorldArraySpillPinBase( const void *ptr, size_t bytes )
{
	size_t end;
	size_t off;

	if( !ptr || bytes == 0 )
		return;
	if((const byte *)ptr < gc_world_array_spill
		|| (const byte *)ptr >= gc_world_array_spill + GC_WORLD_ARRAY_SPILL_USABLE )
		return;
	off = (size_t)((const byte *)ptr - gc_world_array_spill );
	end = ALIGN( off + bytes, 32 );
	if( end > GC_WORLD_ARRAY_SPILL_USABLE )
		end = GC_WORLD_ARRAY_SPILL_USABLE;
	if( end > gc_world_array_spill_pin )
		gc_world_array_spill_pin = end;
	if( gc_world_array_spill_used < gc_world_array_spill_pin )
		gc_world_array_spill_used = gc_world_array_spill_pin;
	Con_Reportf( "Xash3D GameCube: world spill pin through %s (pin %s)\n",
		Q_memprint( end ), Q_memprint( gc_world_array_spill_pin ));
}

/* CapFaces CMPR bounce: high-end carve after map tables settle. */
byte *Mod_GCWorldArraySpillBorrow( size_t bytes, const char *what )
{
	size_t aligned = ALIGN( bytes, 32 );

	if( aligned == 0 || aligned > GC_WORLD_ARRAY_SPILL_BOUNCE_RESERVE )
		return NULL;
	if( gc_world_array_spill_bounce_taken )
		return gc_world_array_spill + GC_WORLD_ARRAY_SPILL_USABLE;
	/* Refuse if load already ate into the high 32 KiB. */
	if( gc_world_array_spill_used > GC_WORLD_ARRAY_SPILL_USABLE )
		return NULL;
	gc_world_array_spill_bounce_taken = true;
	Con_Reportf( "Xash3D GameCube: world spill borrow %s %s (reserve %s)\n",
		what, Q_memprint( aligned ), Q_memprint( GC_WORLD_ARRAY_SPILL_BOUNCE_RESERVE ));
	return gc_world_array_spill + GC_WORLD_ARRAY_SPILL_USABLE;
}

/* After Mod_LoadSurfaces the dface_t spill copy is dead. Rewind the spill tip
 * so markfaces/marksurfaces can reuse it without growing BSS (dod_donner). */
static void Mod_GCReclaimSpillFaces( model_t *mod, dbspmodel_t *bmod )
{
	byte *p;
	size_t off, bytes;

	(void)mod;
	if( !gc_faces_tail_copy || !bmod || bmod->numsurfaces <= 0 )
		return;
	p = (byte *)gc_faces_tail_copy;
	if( p < gc_world_array_spill
		|| p >= gc_world_array_spill + GC_WORLD_ARRAY_SPILL_BYTES )
		return;
	off = (size_t)( p - gc_world_array_spill );
	bytes = ALIGN( (size_t)bmod->numsurfaces * sizeof( dface_t ), 32 );
	if( off + bytes > gc_world_array_spill_used )
		return;
	/* Faces must be the spill tip (edges+surfedges then faces). */
	if( off + bytes != gc_world_array_spill_used )
		return;
	if( bmod->surfaces == gc_faces_tail_copy )
		bmod->surfaces = NULL;
	gc_faces_tail_copy = NULL;
	gc_world_array_spill_used = off;
	Con_Reportf( "Xash3D GameCube: reclaimed faces spill %s (spill used %s)\n",
		Q_memprint( bytes ), Q_memprint( gc_world_array_spill_used ));
	/* Tip memalign(96K) fails at boot; park Server Strings in the hole
	 * faces just freed so CapFaces LM rebake + entity spawn can't Host_Error
	 * on SV_AllocString (tip-flash-lmrebake magenta fatal). */
	{
		extern qboolean SV_GCEnsureStringArena( void );
		SV_GCEnsureStringArena();
	}
}

/* After LoadMarkSurfaces the raw dmarkface spill copy is dead. If marksurfaces
 * sit immediately after it, slide them down and free 31 KiB for leafs (needed
 * once the CMPR bounce reserves the spill high 32 KiB). */
static void Mod_GCReclaimSpillMarkfaces( model_t *mod, dbspmodel_t *bmod )
{
	byte *mf;
	size_t mf_off, mf_bytes, ms_bytes;

	if( !mod || !bmod || bmod->nummarkfaces <= 0 )
		return;
	if( bmod->version == QBSP2_VERSION )
		return; /* 32-bit markfaces not on the dod_donner spill path */
	if( !bmod->markfaces )
		return;
	mf = (byte *)bmod->markfaces;
	if( mf < gc_world_array_spill
		|| mf >= gc_world_array_spill + GC_WORLD_ARRAY_SPILL_USABLE )
		return;
	mf_off = (size_t)( mf - gc_world_array_spill );
	mf_bytes = ALIGN( (size_t)bmod->nummarkfaces * sizeof( dmarkface_t ), 32 );
	ms_bytes = ALIGN( (size_t)mod->nummarksurfaces * sizeof( *mod->marksurfaces ), 32 );
	if( !mod->marksurfaces )
		return;
	if((byte *)mod->marksurfaces != mf + mf_bytes )
		return; /* marksurfaces not packed against markfaces tip */
	if( mf_off + mf_bytes + ms_bytes != gc_world_array_spill_used )
		return;
	memmove( mf, mod->marksurfaces, ms_bytes );
	mod->marksurfaces = (msurface_t **)mf;
	bmod->markfaces = NULL;
	gc_world_array_spill_used = mf_off + ms_bytes;
	Con_Reportf( "Xash3D GameCube: reclaimed markfaces spill %s (spill used %s)\n",
		Q_memprint( mf_bytes ), Q_memprint( gc_world_array_spill_used ));
}

static void Mod_GCFreeGcmapPreSurfaceLumps( dbspmodel_t *bmod )
{
	if( !GC_MapLoadMemoryOpt() || !bmod->isworld || !gc_bsp_scratch_base )
		return;

	Mod_GCFreeBspPin( (void **)&bmod->entdata );
	Mod_GCFreeBspPin( (void **)&bmod->planes );
	Mod_GCFreeBspPin( (void **)&bmod->vertexes );
	Mod_GCFreeBspPin( (void **)&bmod->submodels );
	/* Keep aliased surfedges live until CapFaces EDGE bake (mod->surfedges
	 * may still point here). Nulling only the bmod pin is safe for copies;
	 * for aliases, skip so CollectAll busy-protects the range. */
	if( !gc_surfedges_source_alias )
		Mod_GCFreeBspPin( (void **)&bmod->surfedges );
	if( bmod->version == QBSP2_VERSION )
		Mod_GCFreeBspPin( (void **)&bmod->edges32 );
	else
		Mod_GCFreeBspPin( (void **)&bmod->edges );
	Mod_GCFreeBspPin( (void **)&bmod->texinfo );
	if( bmod->visdata )
		Mod_GCFreeBspPin( (void **)&bmod->visdata );
	Con_Reportf( "Xash3D GameCube: freed pre-surface BSP scratch lumps\n" );
}

/*
 * DoD maps with more faces than dod_flash (e.g. dod_donner 11699) need a ~2.05 Mb
 * surface block, but the early high-slot clamp runs while the BSP image still
 * spans ~2.04 Mb — leaving only ~1.43 Mb. After pre-surface lump frees, relocate
 * aliased faces out of the image, shrink the scratch high-water, and reopen the
 * full surface slot so CapFaces can load without Mem_Calloc FATAL.
 */
static void Mod_GCRefreshClampedSurfaceSlot( model_t *mod, dbspmodel_t *bmod )
{
	const size_t surf_bytes = Mod_GCSurfaceBlockBytes( bmod->numsurfaces );
	const size_t need = surf_bytes ? ALIGN( surf_bytes, 32 ) + 32 : 0;
	gc_bsp_busy_range_t busy[64];
	size_t busy_count, i, busy_end = 0;
	size_t cap, floor, hw;

	if( !GC_MapLoadMemoryOpt() || !bmod->isworld || !need )
		return;
	/* CapFaces lean never materializes the live surface block. */
	if( gc_capfaces_lean_surfaces )
		return;
	if( !gc_bsp_scratch_base || !GC_IsMapLoadBuffer( gc_bsp_scratch_base ))
		return;
	if( !gc_bsp_surface_reserve_clamped || gc_bsp_surface_reserve >= need )
		return;

	/* dod_donner: texinfo in scratch pins busy_end into the surface window.
	 * Rescue texinfo BEFORE faces so faces land at the spill tip and
	 * Mod_GCReclaimSpillFaces can rewind after Mod_LoadSurfaces.
	 * If faces already occupy spill tip (clamped early spill), do NOT park
	 * texinfo on top — that blocks reclaim and starves leafs on dod_flash
	 * (tip-flash-fix12: spill 561 after texinfo-rescue, leafs miss). */
	if( !gc_faces_tail_copy && mod->texinfo
		&& Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base,
			gc_bsp_scratch_size + gc_bsp_scratch_carve_high ))
	{
		const size_t tex_bytes = mod->numtexinfo * sizeof( mtexinfo_t );
		void *copy = Mod_GCWorldArraySpillAlloc( tex_bytes, "texinfo-rescue" );

		if( copy )
		{
			memcpy( copy, mod->texinfo, tex_bytes );
			mod->texinfo = (mtexinfo_t *)copy;
			Con_Reportf( "Xash3D GameCube: rescued texinfo from map arena %s (surface-slot refresh)\n",
				Q_memprint( tex_bytes ));
		}
	}
	else if( gc_faces_tail_copy && mod->texinfo
		&& Mod_GCPointerInBuffer( mod->texinfo, gc_bsp_scratch_base,
			gc_bsp_scratch_size + gc_bsp_scratch_carve_high ))
	{
		Con_Reportf( "Xash3D GameCube: texinfo rescue skipped; faces hold spill tip for reclaim\n" );
	}
	/* Pull faces out of the BSP image — they usually sit near EOF and pin the
	 * scratch high-water above the surface slot. Spill has room for DoD face
	 * lumps (~230 KiB); heap is typically exhausted here. */
	if( bmod->version != QBSP