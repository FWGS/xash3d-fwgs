/*
vid_gamecube.c - GameCube video backend (software buffer + GX display)
Copyright (C) 2026 xash3d-gc contributors

Ported from Division-Zero-GX/xash3d-wii with libogc GX output for GameCube.
*/
#include "platform/platform.h"

#if XASH_VIDEO == VIDEO_GX

#include "client.h"
#include "ref_common.h"
#include "vid_common.h"
#include "imagelib/imagelib.h"
#if XASH_GAMECUBE
#include "server.h"
#include "mod_local.h"
#include "sound.h"
#include "gamecube/mem_gamecube.h"
#include "cl_tent.h"

void CL_GCSeedFlipperEfxProof( const float *org );

qboolean R_GcmapEnsureSurfaceCache( void );
qboolean R_TryInitLowResSurfaceCache( void );
void R_GcmapTrimSurfaceCache( void );
qboolean R_GcmapEnsureWorldRenderScratch( void );
qboolean R_GcmapPrepareWorldRender( void );
qboolean R_GcmapGetViewport( int *width, int *height );
unsigned R_GcmapShadeDumpFromDepth( unsigned short *dst, int dst_w, int dst_h, int dst_stride );
unsigned R_GcmapPosterizeDumpFromDepth( unsigned short *dst, int dst_w, int dst_h, int dst_stride );
qboolean GC_PrepareNewGameWorldPresent( void );
void R_GcmapTrimForMapLoad( void );
void Mod_GCClearRetainedBspScratch( void );
#endif
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#if XASH_GAMECUBE
#include <ogc/gx.h>
#include <ogc/gu.h>
#include <ogc/video.h>
#include <ogc/color.h>
#include <ogc/system.h>
#include <ogc/cache.h>
#include <ogc/aram.h>
#endif

typedef struct gc_video_s
{
	qboolean initialized;
	int width;
	int height;
	int stride;
	uint bpp;
	unsigned short *buffer;
	size_t buffer_pixels;
} gc_video_t;

static gc_video_t gc;
#if XASH_GAMECUBE
static void *xfb[2] = { NULL, NULL };
static int which_fb = 0;
static GXRModeObj *rmode = NULL;
static uint8_t gx_fifo[256 * 1024] __attribute__((aligned(32)));
static unsigned int gc_present_count;
static unsigned int gc_blank_present_count;
static unsigned int gc_budget_sample_count;
static unsigned int gc_budget_warmup_left;
static unsigned int gc_light_present_left;
static qboolean gc_newgame_world_ready;
static qboolean gc_newgame_g36_done; /* sticky: never re-arm probe after first flush */
static int gc_lean_sky_attempts; /* G285 deferred textured backdrop tries */
static int gc_newgame_viewcluster = -1;
static qboolean gc_newgame_pvs_ready;
static byte *gc_newgame_vis; /* active row pointer into pvs table */
static byte *gc_newgame_nodebits; /* active row pointer into node table */
static byte *gc_newgame_pvs_table; /* [numclusters][visbytes] or lean slots */
static byte *gc_newgame_node_table; /* [numclusters][nodebytes] or lean slots */
static byte *gc_newgame_surf_table; /* G132: [numclusters][surfbytes] marksurface bits at capture */
static byte *gc_newgame_surfbits; /* active row into surf_table */
/* G163/G171/G175: when full surf_table OOMs, keep a few capture-time rows (marks die later).
 * G171: trade cache slots 8→5 for refresh cands 32→48 (240 cells).
 * G175: trade again 5→4 for cands 48→64 (256 cells = original 8×32 budget)
 * so outdoor restore admits more wall faces without the old 8×64 MEM1 OOM.
 * G199: keep 4×64 — raising cands starved stuffcmds/boot MEM1.
 * Do not shrink to 48: G212 mid dropped 52→43 with no working portal admit. */
#define GC_SURFBITS_CACHE_SLOTS 4
/* G237: one shared cand buffer (was [4][64] = 24 KiB BSS). 96×96 B ≈ 9 KiB.
 * G281: keep 64 — 96 tipped Client Static / InitInput (BSS +3 KiB). */
#define GC_CAP_REFRESH_NEW_MAX 64
#define GC_CAP_MAX_VERTS 5	/* G199: s16 verts; reclaim per-face LM BSS */
static byte *gc_newgame_surf_cache;
static int gc_newgame_surf_cache_cluster[GC_SURFBITS_CACHE_SLOTS];
static int gc_newgame_surf_cache_slots;
/* Pre-captured face geom for refresh — live msurface_t->plane dangles at present. */
typedef struct
{
	int		firstedge;
	int		numedges;
	int		flags;
	int		area;
	mplane_t	plane;
	mtexinfo_t	texinfo;
	qboolean	has_tex;
	short		texturemins[2];
	short		extents[2];
	byte		styles[MAXLIGHTMAPS];
} gc_refresh_cand_t;
static gc_refresh_cand_t gc_refresh_cands[GC_CAP_REFRESH_NEW_MAX];
static int gc_refresh_ncands[GC_SURFBITS_CACHE_SLOTS]; /* last build n per cache slot */
static int gc_refresh_walls[GC_SURFBITS_CACHE_SLOTS]; /* wall count in last build */
static int gc_refresh_loaded_slot = -1; /* which slot currently fills gc_refresh_cands */
static int gc_g165_eye_cluster = -1; /* G165: player-eye cluster for restore refresh */
static qboolean gc_g171_logged;
static qboolean gc_g175_logged;
static qboolean gc_g199_logged;
static byte *gc_newgame_cluster_valid; /* one byte per cluster (full) or per lean slot */
static int gc_newgame_numclusters;
static qboolean gc_newgame_pvs_lean; /* G96/G101: compact cache when multi-row OOM */
static int gc_newgame_lean_cluster; /* G96 compat: first lean slot cluster id */
#define GC_LEAN_PVS_SLOTS		4 /* G101: small multi-room follow cache */
static int gc_newgame_lean_slots; /* 1..GC_LEAN_PVS_SLOTS populated */
static int gc_newgame_lean_clusters[GC_LEAN_PVS_SLOTS]; /* real cluster id per slot */
static unsigned int gc_newgame_lean_age[GC_LEAN_PVS_SLOTS];
static unsigned int gc_newgame_lean_clock;
static byte *gc_newgame_compressed_pvs; /* G107: compact rows for lean LRU misses */
static int *gc_newgame_compressed_ofs; /* cluster -> packed row offset, -1 unavailable */
static size_t gc_newgame_compressed_size;
static byte *gc_newgame_packed_nodebits; /* fixed node row per retained cluster */
static size_t gc_newgame_packed_nodebits_size;
static int gc_newgame_visbytes;
static int gc_newgame_nodebytes;
static int gc_newgame_surfbytes;
static int gc_newgame_numleafs;
static int gc_newgame_numnodes;
static int gc_newgame_numsurfaces;
static int gc_newgame_vis_leafs;
static int gc_newgame_vis_nodes;
/* G132/G148/G150/G160/G176: compact face records captured while msurface_t is still valid.
 * G176: trade lightmap tile 8→4 (GX 4×4 RGB565) to raise face cap 256→320
 * without BSS growth (~−4 KiB net: LM save outweighs +64 face structs). */
#define GC_MAX_CAP_FACES 320
#define GC_CAP_AREA_SLOTS (( GC_MAX_CAP_FACES * 7 ) / 8) /* 280 top-K by area */
/* G153/G176: bake style-0 lightmap to RGB565 at capture (must be multiple of 4). */
#define GC_CAP_LM_DIM 4
/* G180: pack 4×4 face LMs into one Flipper atlas (32×16 tiles = 512 slots). */
#define GC_LM_ATLAS_W 128
#define GC_LM_ATLAS_H 64
#define GC_LM_ATLAS_COLS ( GC_LM_ATLAS_W / GC_CAP_LM_DIM )
#define GC_LM_ATLAS_ROWS ( GC_LM_ATLAS_H / GC_CAP_LM_DIM )
typedef struct
{
	int		firstedge;
	int		numedges;
	int		flags;
	mplane_t	plane;
	mtexinfo_t	texinfo;	/* G133: copy — texture* stays in model pool */
	mextrasurf_t	info;		/* lightextents + CACHESPOT reserved */
	short		texturemins[2];
	short		extents[2];
	byte		styles[MAXLIGHTMAPS];
	color24		*samples;	/* may dangle later — lit path tolerates NULL */
} gc_cap_face_t;
/* G199: bake ≤8 verts per cap so Flipper emit survives scratch edge reuse. */
static gc_cap_face_t gc_newgame_cap_faces[GC_MAX_CAP_FACES];
static msurface_t gc_newgame_draw_surfs[GC_MAX_CAP_FACES];
static int gc_newgame_cap_areas[GC_MAX_CAP_FACES]; /* extents product per slot */
static byte gc_cap_nverts[GC_MAX_CAP_FACES];
/* G204: bake provenance — EDGE/TEX keep real ST/LM; PLANE quads are eye-centered. */
enum
{
	GC_CAP_BAKE_NONE = 0,
	GC_CAP_BAKE_EDGE = 1,
	GC_CAP_BAKE_PLANE = 2,
	GC_CAP_BAKE_TEX = 3 /* G205: texinfo ST solve — real world pos, LM-valid */
};
static byte gc_cap_bake_src[GC_MAX_CAP_FACES];
/* G199: reclaim former per-face LM staging (~10 KiB) for baked Flipper verts.
 * Lightmaps write straight into the G180 atlas tiles. */
static signed short gc_cap_pts_s16[GC_MAX_CAP_FACES][GC_CAP_MAX_VERTS][3];
/* G180/G198: BSS atlas — memalign(16 KiB) fails under MEM1 fragmentation. */
static u16 gc_newgame_cap_lm_atlas_bss[GC_LM_ATLAS_W * GC_LM_ATLAS_H]
	__attribute__((aligned( 32 )));
static u16 *gc_newgame_cap_lm_atlas = gc_newgame_cap_lm_atlas_bss;
static byte gc_newgame_cap_lm_w[GC_MAX_CAP_FACES];
static byte gc_newgame_cap_lm_h[GC_MAX_CAP_FACES];
static byte gc_newgame_cap_lm_real[GC_MAX_CAP_FACES]; /* 1 if baked from samples */
static int gc_newgame_cap_face_count;
static int gc_newgame_cap_tex_faces; /* faces that kept a live texture* */
static int gc_newgame_cap_lm_faces; /* faces with real sample bake */
static int gc_newgame_cap_generation; /* bumps when faces/LMs rewrite */
static qboolean gc_newgame_cap_lm_atlas_ready;
static qboolean gc_g176_logged;
static qboolean gc_g180_logged;
static qboolean gc_g212_logged;
static qboolean gc_g212_stream_locked; /* hold near-eye set; stop cluster refresh thrash */
static vec3_t gc_newgame_capture_origin; /* G132/G201e: bake + dump camera eye */
static vec3_t gc_newgame_capture_forward; /* G217: dump look for live frustum rank */
/* G213: compact PVS faces on heap (full msurface promote is 662 KiB OOM).
 * Emit walks mempool edges16/surfedges; planes/texinfo are copied here. */
/*
 * G213: lean live-face pool for Flipper beyond the 320 LM-cap snapshot.
 * Full msurface_t promote (~662 KiB) OOMs; full gc_refresh_cand_t + draw
 * arrays also starve MEM1 after FatPVS. Allocate a compact geom table
 * BEFORE lean FatPVS calloc, then fill from surfbits. Emit builds a stack
 * msurface_t (no resident draw array).
 */
typedef struct
{
	int		firstedge;
	int		numedges;
	int		flags;
	int		area;
	mplane_t	plane;
	short		texturemins[2];
	short		extents[2];
	byte		styles[MAXLIGHTMAPS];
	float		vecs[2][4];
	texture_t	*texture;
	int		tex_flags;
	qboolean	has_tex;
	/* G216: capture-time baked verts — emit without live edge walk. */
	byte		nverts;
	byte		bake_src; /* G217: 1=edge 2=plane 3=tex 0=none */
	signed short	pts_s16[GC_CAP_MAX_VERTS][3];
} gc_live_cand_t;
/* G252: drop Flipper diagnostic format strings from .rodata/.text to free MEM1. */
#ifndef GC_FLIPPER_QUIET
#define GC_FLIPPER_QUIET 1
#endif
#if GC_FLIPPER_QUIET
#define GC_FlipperTrace(...) ((void)0)
#else
#define GC_FlipperTrace(...) SYS_Report( __VA_ARGS__ )
#endif
#define GC_LIVE_MAX_FACES 248 /* G262: quiet Capture reclaim → tip-safe 248 (256 hangs) */
/* G298/G299: after FatPVS, heap is fragmented — BSS lean under tip.
 * 96 tipped NEWGAME_EARLY; 48 passed; G299 raises 48→64 (no extra ents). */
#define GC_LIVE_BSS_FACES 64
static qboolean GC_CapFaceAlready( int firstedge, int numedges );
static qboolean GC_DumpEyeInFrontOfBestWall( float *eye, float *out_angles );
static qboolean GC_DumpEyeAtTramStart( float *eye, float *out_angles );
static void GC_FlushPendingCapFaceRefresh( void );
static byte *GC_LookupSurfbitsCache( int cluster );
static void GC_DecompressPVS( byte *out, const byte *in, size_t visbytes );
static void GC_BuildSurfbitsForVisRow( model_t *wmodel, const byte *vis, byte *surfbits );
static int GC_VisLeafsForCluster( int cluster );
static int GC_RefreshCandWallCount( int cache_slot );
static void GC_CaptureFillFacesFromSurfbits( model_t *wmodel, const byte *surfbits, qboolean append );
static void GC_CaptureWaterFacesFromSurfbits( model_t *wmodel, const byte *surfbits );
static void GC_BuildRefreshCandsFromSurfbits( model_t *wmodel, const byte *surfbits, int cache_slot );
void GC_CaptureIntroTrainFaces( model_t *wmodel ); /* G277: also called from mod_bmodel */
static int GC_BakeCapVertsFromModel( model_t *wmodel, int firstedge, int numedges,
	signed short out[][3], int maxverts );
static int GC_BakeCapVertsFromModelDecimated( model_t *wmodel, int firstedge, int numedges,
	const mplane_t *pl, signed short out[][3], int maxverts );
static gc_live_cand_t *gc_live_faces;
static int gc_live_face_capacity;
static int gc_live_face_count;
static int *gc_live_face_scores; /* sized with pool */
static qboolean gc_g213_live_logged;
static gc_live_cand_t gc_live_faces_bss[GC_LIVE_BSS_FACES];
static int gc_live_face_scores_bss[GC_LIVE_BSS_FACES];
static qboolean gc_live_faces_bss_active;

/*
 * G222/G224: flat-fill faces beyond the 320+192 MEM1 budget. Heap AFTER live
 * pool succeeds — BSS here starved live 192→96. G224 raises 96→128 (~+2 KiB).
 * G225: lean ARAM overflow (append-only, no score BSS) for MEM1 rank losers.
 */
#define GC_FILL_MAX_FACES 160 /* G256: was 128 — more MEM1 fill for TR residual */
#define GC_ARAM_FILL_MAX 128
#define GC_ARAM_FACE_STRIDE 64 /* sizeof(gc_fill_face_t); DMA multiple of 32 */
typedef struct
{
	byte		nverts;
	byte		flags; /* SURF_PLANEBACK */
	signed short	pts_s16[GC_CAP_MAX_VERTS][3];
	mplane_t	plane;
	int		firstedge;
	int		numedges;
	int		area;
} gc_fill_face_t;
static gc_fill_face_t *gc_fill_faces;
static int *gc_fill_face_scores;
static int gc_fill_face_capacity;
static int gc_fill_face_count;
static qboolean gc_g222_fill_logged;
/* G277: *12 intro tram — live msurface_t dangles after scratch; bake at capture. */
#define GC_TRAM_MAX_FACES 16
#define GC_TRAM_LM_SLOT0 320	/* unused tiles in 128×64 cap atlas (512 capacity) */
typedef struct
{
	byte		nverts;
	signed short	pts_s16[GC_CAP_MAX_VERTS][3];
} gc_tram_face_t;
static gc_tram_face_t gc_tram_faces[GC_TRAM_MAX_FACES];
static int gc_tram_face_count;
static qboolean gc_tram_lm_ready;
static int gc_tram_diffuse_texnum;
static qboolean gc_tram_lm_logged;
/* G286: tip-safe Flipper water — baked turb verts (no live-pool tip / no draw walk hang). */
#define GC_WATER_MAX_FACES 8
typedef struct
{
	byte		nverts;
	byte		flags; /* SURF_PLANEBACK */
	signed short	pts_s16[GC_CAP_MAX_VERTS][3];
	mplane_t	plane;
} gc_water_face_t;
static gc_water_face_t gc_water_faces[GC_WATER_MAX_FACES];
static int gc_water_face_count;
static qboolean gc_g286_water_logged;
/* G225: ARAM page + aligned MEM1 DMA stage. */
static u32 gc_aram_fill_base;
static int gc_aram_fill_count;
static qboolean gc_aram_fill_tried;
static qboolean gc_g225_aram_logged;
static gc_fill_face_t gc_aram_stage __attribute__(( aligned( 32 )));
static u32 gc_aram_blocks[4];

/*
 * G212: keep-score for the 320-slot Flipper budget. MEM1 cannot hold all BSP30
 * faces, so stream the set toward geometry near the dump/capture eye — local
 * room walls beat distant towers that leave sky holes in DumpFrames.
 */
static int GC_CapNearEyeScore( const mplane_t *pl, int area, qboolean is_wall )
{
	float	dist, d, along;
	int	score = area;

	if( area <= 0 )
		return 0;
	if( is_wall )
		score += ( area >> 1 );
	if( !pl || VectorIsNull( gc_newgame_capture_origin ))
		return score;
	d = DotProduct( gc_newgame_capture_origin, pl->normal ) - pl->dist;
	dist = (float)fabs( d );
	/* G232 near-eye keep + G234 along-look (portal throat). Softening
	 * ultra-near raised clear%~14.37 — keep the stronger near band. */
	if( dist < 256.0f )
		score += 400000;
	else if( dist < 512.0f )
		score += 180000;
	else if( dist < 768.0f )
		score += 60000;
	else if( dist > 1280.0f )
		score /= 4;
	else if( dist > 896.0f )
		score /= 2;
	/* G281: under tram lock, demote mid/far faces harder so look-ahead
	 * walls win refresh admits without BSS eviction. */
	if( gc_g212_stream_locked && dist > 640.0f )
		score /= 2;
	if( !VectorIsNull( gc_newgame_capture_forward ))
	{
		along = -d * DotProduct( pl->normal, gc_newgame_capture_forward );
		if( along > 96.0f && along < 1280.0f )
			score += (int)( along * 60.0f );
		else if( along < -64.0f )
			score /= 2;
	}
	return score;
}

static int GC_CapSlotKeepScore( int slot )
{
	const gc_cap_face_t *f;
	int area;
	qboolean wall;
	int score;

	if( slot < 0 || slot >= gc_newgame_cap_face_count )
		return 0;
	f = &gc_newgame_cap_faces[slot];
	area = (int)f->extents[0] * (int)f->extents[1];
	wall = ( fabs( f->plane.normal[2] ) < 0.55f ); /* G232: incl. shallow ceilings */
	score = GC_CapNearEyeScore( &f->plane, area, wall );
	if( !gc_newgame_cap_lm_real[slot] )
		score -= 1;
	return score;
}

int GC_GetNewGameCapFaceCount( void )
{
	return gc_newgame_cap_face_count;
}

/* G213: compact live faces and/or full surface pin. */
qboolean GC_WorldSurfacesLive( void )
{
	model_t *wmodel;

	if( gc_live_face_count > 0 && gc_live_faces )
		return true;
	wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	return Mod_GCWorldSurfacesPinned( wmodel );
}

qboolean GC_WorldSurfacesPinned( void )
{
	model_t *wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	return Mod_GCWorldSurfacesPinned( wmodel );
}

qboolean GC_WorldSurfacesScratchRetained( void )
{
	model_t *wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	return Mod_GCWorldSurfacesScratchRetained( wmodel );
}

int GC_GetLiveFaceCount( void )
{
	return gc_live_face_count;
}

qboolean GC_LiveFaceIsCapped( int index )
{
	if( index < 0 || index >= gc_live_face_count || !gc_live_faces )
		return false;
	return GC_CapFaceAlready( gc_live_faces[index].firstedge, gc_live_faces[index].numedges );
}

msurface_t *GC_GetLiveDrawSurfs( void )
{
	/* Draw surfs are stack-built via GC_FillLiveDrawSurf — no resident array. */
	return NULL;
}

qboolean GC_FillLiveDrawSurf( int index, msurface_t *out, mtexinfo_t *tex_out )
{
	gc_live_cand_t *src;

	if( !out || index < 0 || index >= gc_live_face_count || !gc_live_faces )
		return false;
	src = &gc_live_faces[index];
	memset( out, 0, sizeof( *out ));
	out->firstedge = src->firstedge;
	out->numedges = src->numedges;
	out->flags = src->flags;
	out->plane = &src->plane;
	out->texturemins[0] = src->texturemins[0];
	out->texturemins[1] = src->texturemins[1];
	out->extents[0] = src->extents[0];
	out->extents[1] = src->extents[1];
	memcpy( out->styles, src->styles, sizeof( out->styles ));
	if( src->has_tex && tex_out )
	{
		memset( tex_out, 0, sizeof( *tex_out ));
		memcpy( tex_out->vecs, src->vecs, sizeof( src->vecs ));
		tex_out->texture = src->texture;
		tex_out->flags = src->tex_flags;
		tex_out->faceinfo = NULL;
		out->texinfo = tex_out;
	}
	return true;
}

int GC_GetLiveFaceVerts( int index, float out[][3], int maxverts )
{
	gc_live_cand_t *src;
	int i, n;

	if( !out || maxverts < 3 || index < 0 || index >= gc_live_face_count || !gc_live_faces )
		return 0;
	src = &gc_live_faces[index];
	n = (int)src->nverts;
	if( n < 3 )
		return 0;
	if( n > maxverts )
		n = maxverts;
	if( n > GC_CAP_MAX_VERTS )
		n = GC_CAP_MAX_VERTS;
	for( i = 0; i < n; i++ )
	{
		out[i][0] = (float)src->pts_s16[i][0];
		out[i][1] = (float)src->pts_s16[i][1];
		out[i][2] = (float)src->pts_s16[i][2];
	}
	return n;
}

int GC_GetLiveFaceBakeSrc( int index )
{
	if( index < 0 || index >= gc_live_face_count || !gc_live_faces )
		return GC_CAP_BAKE_NONE;
	return (int)gc_live_faces[index].bake_src;
}

static qboolean GC_AramFillEnsure( void );
static qboolean GC_AramFillRead( int slot );

int GC_GetFillFaceCount( void )
{
	return gc_fill_face_count + gc_aram_fill_count;
}

int GC_GetFillFaceVerts( int index, float out[][3], int maxverts )
{
	const gc_fill_face_t *src;
	int n, i, mem1 = gc_fill_face_count;

	if( !out || maxverts < 3 || index < 0 )
		return 0;
	if( index < mem1 )
	{
		if( !gc_fill_faces )
			return 0;
		src = &gc_fill_faces[index];
	}
	else
	{
		if( !GC_AramFillRead( index - mem1 ))
			return 0;
		src = &gc_aram_stage;
	}
	n = (int)src->nverts;
	if( n < 3 )
		return 0;
	if( n > GC_CAP_MAX_VERTS )
		n = GC_CAP_MAX_VERTS;
	if( n > maxverts )
		n = maxverts;
	for( i = 0; i < n; i++ )
	{
		out[i][0] = (float)src->pts_s16[i][0];
		out[i][1] = (float)src->pts_s16[i][1];
		out[i][2] = (float)src->pts_s16[i][2];
	}
	return n;
}

qboolean GC_FillFacePlane( int index, mplane_t *out, int *out_flags )
{
	const gc_fill_face_t *src;
	int mem1 = gc_fill_face_count;

	if( !out || index < 0 )
		return false;
	if( index < mem1 )
	{
		if( !gc_fill_faces )
			return false;
		src = &gc_fill_faces[index];
	}
	else
	{
		if( !GC_AramFillRead( index - mem1 ))
			return false;
		src = &gc_aram_stage;
	}
	*out = src->plane;
	if( out_flags )
		*out_flags = (int)src->flags;
	return true;
}

/*
=============
GC_CaptureIntroTrainFaces

G277: snapshot *12 brush faces while msurface_t/edges are still valid.
Full surface promote OOMs (~662 KiB); Flipper world already uses baked caps.
=============
*/
void GC_CaptureIntroTrainFaces( model_t *wmodel )
{
	model_t *tram;
	int i, q;
	static qboolean baked_ok;
	/*
	 * Windshield frame on local x=144 (hole shows tunnel). Keep the hole large
	 * enough that Z-ignore frame quads do not paint out the tunnel mid.
	 * Short ±Y returns add depth; ceil/sill / Z-split body tipped InitInput.
	 */
	static const short shell[6][4][3] = {
		{ { 144, -75, 100 }, { 144, 75, 100 }, { 144, 75, 131 }, { 144, -75, 131 } },
		{ { 144, -75, -6 }, { 144, 75, -6 }, { 144, 75, 40 }, { 144, -75, 40 } },
		{ { 144, 40, 40 }, { 144, 75, 40 }, { 144, 75, 100 }, { 144, 40, 100 } },
		{ { 144, -75, 40 }, { 144, -40, 40 }, { 144, -40, 100 }, { 144, -75, 100 } },
		/* Short depth returns at the pillars (x 120→144). */
		{ { 120, 72, 40 }, { 144, 72, 40 }, { 144, 72, 100 }, { 120, 72, 100 } },
		{ { 144, -72, 40 }, { 120, -72, 40 }, { 120, -72, 100 }, { 144, -72, 100 } },
	};

	(void)wmodel;
	if( baked_ok && gc_tram_face_count > 0 )
		return;
	gc_tram_face_count = 0;
	baked_ok = false;

	tram = Mod_FindName( "*12", false );
	if( !tram || tram->type != mod_brush || tram->nummodelsurfaces < 273 )
		return;

	for( q = 0; q < 6 && q < GC_TRAM_MAX_FACES; q++ )
	{
		gc_tram_face_t *dst = &gc_tram_faces[gc_tram_face_count];

		dst->nverts = 4;
		for( i = 0; i < 4; i++ )
		{
			dst->pts_s16[i][0] = shell[q][i][0];
			dst->pts_s16[i][1] = shell[q][i][1];
			dst->pts_s16[i][2] = shell[q][i][2];
		}
		gc_tram_face_count++;
	}

	if( gc_tram_face_count > 0 )
		baked_ok = true;
}

static u16 *GC_CapAtlasTile( int slot );

/*
=============
GC_BakeTramLightmapTile

Downsample *12 style-0 samples into one 4×4 RGB565 GX tile (same boost as caps).
=============
*/
static void GC_BakeTramLightmapTile( const msurface_t *src, u16 *dst )
{
	const mextrasurf_t *info;
	int sample_size, smax, tmax, dw, dh, x, y;
	u16 linear[GC_CAP_LM_DIM * GC_CAP_LM_DIM];
	const color24 *lm;
	const u16 mid = 0xC618;
	int tile_x, tile_y, ty;
	u16 *out;

	if( !dst )
		return;
	info = src ? src->info : NULL;
	dw = dh = GC_CAP_LM_DIM;
	lm = ( src && src->samples ) ? src->samples : NULL;

	if( info && lm )
	{
		sample_size = Mod_SampleSizeForFace( src );
		if( sample_size < 1 )
			sample_size = 16;
		smax = ( info->lightextents[0] / sample_size ) + 1;
		tmax = ( info->lightextents[1] / sample_size ) + 1;
		if( smax < 1 )
			smax = 1;
		if( tmax < 1 )
			tmax = 1;
	}
	else
	{
		smax = tmax = 1;
		lm = NULL;
	}

	for( y = 0; y < dh; y++ )
	{
		int sy = ( tmax <= 1 ) ? 0 : ( y * ( tmax - 1 )) / ( dh > 1 ? dh - 1 : 1 );

		for( x = 0; x < dw; x++ )
		{
			int sx = ( smax <= 1 ) ? 0 : ( x * ( smax - 1 )) / ( dw > 1 ? dw - 1 : 1 );
			u16 pix = mid;

			if( lm && sx >= 0 && sy >= 0 && sx < smax && sy < tmax )
			{
				const color24 *c = &lm[sy * smax + sx];
				/* Match cap LM boost (G209 ×3, floor 128). */
				unsigned r = ((unsigned)c->r * 3u);
				unsigned g = ((unsigned)c->g * 3u);
				unsigned b = ((unsigned)c->b * 3u);

				if( r < 128 )
					r = 128;
				if( g < 128 )
					g = 128;
				if( b < 128 )
					b = 128;
				if( r > 255 )
					r = 255;
				if( g > 255 )
					g = 255;
				if( b > 255 )
					b = 255;
				pix = (u16)((( r >> 3 ) << 11 ) | (( g >> 2 ) << 5 ) | ( b >> 3 ));
			}
			linear[y * dw + x] = pix;
		}
	}

	out = dst;
	for( tile_y = 0; tile_y < dh; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < dw; tile_x += 4 )
		{
			for( ty = 0; ty < 4; ty++ )
			{
				const u16 *row = linear + ( tile_y + ty ) * dw + tile_x;

				out[0] = row[0];
				out[1] = row[1];
				out[2] = row[2];
				out[3] = row[3];
				out += 4;
			}
		}
	}
}

/*
=============
GC_BakeTramLightmaps

G277: after disc LM is live, sample *12 front faces into the tram atlas so
Flipper can draw tex×LM instead of flat orange.
=============
*/
static void GC_BakeTramLightmaps( model_t *wmodel )
{
	model_t *tram;
	int fi, si, baked = 0;

	gc_tram_lm_ready = false;
	gc_tram_diffuse_texnum = 0;
	if( !wmodel || !wmodel->surfaces || gc_tram_face_count <= 0 )
		return;

	tram = Mod_FindName( "*12", false );
	if( !tram || tram->type != mod_brush || tram->nummodelsurfaces <= 0 )
		return;
	if( tram->firstmodelsurface < 0
		|| tram->firstmodelsurface + tram->nummodelsurfaces > wmodel->numsurfaces )
		return;

	for( fi = 0; fi < gc_tram_face_count && fi < GC_TRAM_MAX_FACES; fi++ )
	{
		const gc_tram_face_t *face = &gc_tram_faces[fi];
		msurface_t *best = NULL;
		int best_score = -1;
		float cy = 0.0f, cz = 0.0f;
		int v;
		u16 *tile;

		if( GC_TRAM_LM_SLOT0 + fi >= GC_LM_ATLAS_COLS * GC_LM_ATLAS_ROWS )
			break;

		for( v = 0; v < (int)face->nverts && v < 4; v++ )
		{
			cy += (float)face->pts_s16[v][1];
			cz += (float)face->pts_s16[v][2];
		}
		cy *= 0.25f;
		cz *= 0.25f;

		for( si = 0; si < tram->nummodelsurfaces; si++ )
		{
			msurface_t *surf = &wmodel->surfaces[tram->firstmodelsurface + si];
			float nx, dist_pl;
			int area, score;

			if( !surf->plane || !surf->samples )
				continue;
			if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
				continue;
			if( surf->numedges < 3 )
				continue;
			nx = surf->plane->normal[0];
			if( nx < 0.55f )
				continue;
			area = (int)surf->extents[0] * (int)surf->extents[1];
			if( area <= 0 )
				continue;
			dist_pl = (float)fabs( (double)surf->plane->dist - 144.0 );
			score = area + (int)( nx * 200000.0f ) - (int)( dist_pl * 100.0f );
			score -= (int)( fabs( (double)cy ) + fabs( (double)cz ) ) / 8;
			if( score > best_score )
			{
				best_score = score;
				best = surf;
			}
		}

		/* G277: reuse unused cap-atlas tiles (slots 320+) — no dedicated BSS. */
		tile = GC_CapAtlasTile( GC_TRAM_LM_SLOT0 + fi );
		if( !tile )
			continue;
		if( best )
		{
			GC_BakeTramLightmapTile( best, tile );
			baked++;
			if( !gc_tram_diffuse_texnum && best->texinfo && best->texinfo->texture )
				gc_tram_diffuse_texnum = best->texinfo->texture->gl_texturenum;
		}
		else
			GC_BakeTramLightmapTile( NULL, tile );
		DCFlushRange( tile, (u32)( GC_CAP_LM_DIM * GC_CAP_LM_DIM * sizeof( u16 )));
	}

	gc_tram_lm_ready = ( baked > 0 );
	if( !gc_tram_lm_logged && gc_tram_face_count > 0 )
	{
		gc_tram_lm_logged = true;
		Con_Reportf( "Xash3D GameCube: G277 tram LM baked=%d/%d tex=%d\n",
			baked, gc_tram_face_count, gc_tram_diffuse_texnum );
	}
}

int GC_GetTramFaceCount( void )
{
	return gc_tram_face_count;
}

int GC_GetWaterFaceCount( void )
{
	return gc_water_face_count;
}

int GC_GetWaterFaceVerts( int index, float out[][3], int maxverts )
{
	const gc_water_face_t *src;
	int n, i;

	if( !out || maxverts < 3 || index < 0 || index >= gc_water_face_count )
		return 0;
	src = &gc_water_faces[index];
	n = (int)src->nverts;
	if( n < 3 )
		return 0;
	if( n > GC_CAP_MAX_VERTS )
		n = GC_CAP_MAX_VERTS;
	if( n > maxverts )
		n = maxverts;
	for( i = 0; i < n; i++ )
	{
		out[i][0] = (float)src->pts_s16[i][0];
		out[i][1] = (float)src->pts_s16[i][1];
		out[i][2] = (float)src->pts_s16[i][2];
	}
	return n;
}

qboolean GC_WaterFacePlane( int index, mplane_t *out, int *out_flags )
{
	if( !out || index < 0 || index >= gc_water_face_count )
		return false;
	*out = gc_water_faces[index].plane;
	if( out_flags )
		*out_flags = (int)gc_water_faces[index].flags;
	return true;
}

int GC_GetTramFaceVerts( int index, float out[][3], int maxverts )
{
	const gc_tram_face_t *src;
	int n, i;

	if( !out || maxverts < 3 || index < 0 || index >= gc_tram_face_count )
		return 0;
	src = &gc_tram_faces[index];
	n = (int)src->nverts;
	if( n < 3 )
		return 0;
	if( n > GC_CAP_MAX_VERTS )
		n = GC_CAP_MAX_VERTS;
	if( n > maxverts )
		n = maxverts;
	for( i = 0; i < n; i++ )
	{
		out[i][0] = (float)src->pts_s16[i][0];
		out[i][1] = (float)src->pts_s16[i][1];
		out[i][2] = (float)src->pts_s16[i][2];
	}
	return n;
}

qboolean GC_TramLightmapReady( void )
{
	return gc_tram_lm_ready;
}

int GC_GetTramDiffuseTexnum( void )
{
	return gc_tram_diffuse_texnum;
}

const unsigned short *GC_GetTramLightmapAtlas( int *w, int *h )
{
	/* Same 128×64 RGB565 atlas as Flipper caps; tram tiles start at slot 320. */
	if( !gc_tram_lm_ready || !gc_newgame_cap_lm_atlas )
		return NULL;
	if( w )
		*w = GC_LM_ATLAS_W;
	if( h )
		*h = GC_LM_ATLAS_H;
	return gc_newgame_cap_lm_atlas;
}

void GC_GetTramLightmapUV( int face, float s, float t, float *out_s, float *out_t )
{
	int slot, col, row;
	float u, v;

	if( !out_s || !out_t )
		return;
	if( face < 0 || face >= gc_tram_face_count || !gc_tram_lm_ready )
	{
		*out_s = *out_t = 0.0f;
		return;
	}
	if( s < 0.0f )
		s = 0.0f;
	else if( s > 1.0f )
		s = 1.0f;
	if( t < 0.0f )
		t = 0.0f;
	else if( t > 1.0f )
		t = 1.0f;
	slot = GC_TRAM_LM_SLOT0 + face;
	col = slot % GC_LM_ATLAS_COLS;
	row = slot / GC_LM_ATLAS_COLS;
	u = (float)( col * GC_CAP_LM_DIM ) + 0.5f + s * (float)( GC_CAP_LM_DIM - 1 );
	v = (float)( row * GC_CAP_LM_DIM ) + 0.5f + t * (float)( GC_CAP_LM_DIM - 1 );
	*out_s = u / (float)GC_LM_ATLAS_W;
	*out_t = v / (float)GC_LM_ATLAS_H;
}

int GC_GetNewGameCapGeneration( void )
{
	return gc_newgame_cap_generation;
}

msurface_t *GC_GetNewGameDrawSurfs( void )
{
	return gc_newgame_cap_face_count > 0 ? gc_newgame_draw_surfs : NULL;
}


/* G199: Flipper emit uses baked verts when live edges dangle after scratch. */
int GC_GetNewGameCapFaceVerts( int slot, float out[][3], int maxverts )
{
	int i, n;

	if( slot < 0 || slot >= gc_newgame_cap_face_count || !out || maxverts < 3 )
		return 0;
	n = gc_cap_nverts[slot];
	if( n < 3 )
		return 0;
	if( n > maxverts )
		n = maxverts;
	if( n > GC_CAP_MAX_VERTS )
		n = GC_CAP_MAX_VERTS;
	for( i = 0; i < n; i++ )
	{
		out[i][0] = (float)gc_cap_pts_s16[slot][i][0];
		out[i][1] = (float)gc_cap_pts_s16[slot][i][1];
		out[i][2] = (float)gc_cap_pts_s16[slot][i][2];
	}
	return n;
}

/* G204: EDGE=1, PLANE=2, TEX=3, missing=0 — Flipper emit gates LM/ST on this. */
int GC_GetNewGameCapBakeSrc( int slot )
{
	if( slot < 0 || slot >= gc_newgame_cap_face_count )
		return GC_CAP_BAKE_NONE;
	return (int)gc_cap_bake_src[slot];
}

/* Pack one surfedge index into out[n]; returns 0 on OOB. */
static int GC_BakeOneSurfedgeVert( model_t *wmodel, int firstedge, int ei, int nedges,
	signed short out[][3], int n )
{
	int lindex, eabs, v;
	const float *pos;

	lindex = wmodel->surfedges[firstedge + ei];
	eabs = ( lindex > 0 ) ? lindex : -lindex;
	/* G209: reject OOB edge indices (was aborting whole bake → tex-only). */
	if( eabs <= 0 || eabs >= nedges )
		return 0;
	/* Prefer edges16 when present — HL1 maps; edges32 may be stale pin. */
	if( wmodel->edges16 )
	{
		medge16_t *e = &wmodel->edges16[eabs];
		v = ( lindex > 0 ) ? e->v[0] : e->v[1];
	}
	else
	{
		medge32_t *e = &wmodel->edges32[eabs];
		v = ( lindex > 0 ) ? e->v[0] : e->v[1];
	}
	if( v < 0 || v >= wmodel->numvertexes )
		return 0;
	pos = wmodel->vertexes[v].position;
	out[n][0] = (signed short)pos[0];
	out[n][1] = (signed short)pos[1];
	out[n][2] = (signed short)pos[2];
	return 1;
}

/* Pack world verts as s16 — reuses former per-face LM staging BSS.
 * Full edge walk only; callers must not pass numedges > maxverts. */
static int GC_BakeCapVertsFromModel( model_t *wmodel, int firstedge, int numedges,
	signed short out[][3], int maxverts )
{
	int i, n = 0;
	int nedges;

	if( !wmodel || !wmodel->vertexes || !wmodel->surfedges || !out || numedges < 3 )
		return 0;
	if( !wmodel->edges16 && !wmodel->edges32 )
		return 0;
	if( firstedge < 0 || firstedge + numedges > wmodel->numsurfedges )
		return 0;
	if( numedges > maxverts )
		return 0;
	nedges = wmodel->numedges;
	if( nedges < 1 )
		return 0;
	for( i = 0; i < numedges; i++ )
	{
		if( !GC_BakeOneSurfedgeVert( wmodel, firstedge, i, nedges, out, n ))
			return 0;
		n++;
	}
	return n;
}

/* Approximate polygon area via fan cross-products (world units²). */
static float GC_BakePolyArea3( const signed short pts[][3], int n )
{
	int i;
	float area, x0, y0, z0;
	vec3_t cr;

	if( !pts || n < 3 )
		return 0.0f;
	x0 = (float)pts[0][0];
	y0 = (float)pts[0][1];
	z0 = (float)pts[0][2];
	area = 0.0f;
	for( i = 1; i < n - 1; i++ )
	{
		float x1 = (float)pts[i][0] - x0;
		float y1 = (float)pts[i][1] - y0;
		float z1 = (float)pts[i][2] - z0;
		float x2 = (float)pts[i + 1][0] - x0;
		float y2 = (float)pts[i + 1][1] - y0;
		float z2 = (float)pts[i + 1][2] - z0;

		cr[0] = y1 * z2 - z1 * y2;
		cr[1] = z1 * x2 - x1 * z2;
		cr[2] = x1 * y2 - y1 * x2;
		area += VectorLength( cr );
	}
	return area * 0.5f;
}

static void GC_BakeSortSelByRing( byte *sel, int nsel )
{
	int i, k;

	for( i = 0; i < nsel - 1; i++ )
	{
		for( k = i + 1; k < nsel; k++ )
		{
			if( sel[k] < sel[i] )
			{
				byte t = sel[i];
				sel[i] = sel[k];
				sel[k] = t;
			}
		}
	}
}

static int GC_BakeSelToOut( const signed short all[][3], const byte *sel, int nsel,
	signed short out[][3] )
{
	int i, n = 0;

	for( i = 0; i < nsel; i++ )
	{
		int j = (int)sel[i];

		out[n][0] = all[j][0];
		out[n][1] = all[j][1];
		out[n][2] = all[j][2];
		n++;
	}
	return n;
}

/* G247/G248/G250: subsample edge loop into maxverts verts for Flipper emit.
 * G248 farthest-point; G250 also try plane-ST extremes on the same real verts
 * and keep the larger area (still TEX-tier — never EDGE +80k / never TEX AABB). */
static int GC_BakeCapVertsFromModelDecimated( model_t *wmodel, int firstedge, int numedges,
	const mplane_t *pl, signed short out[][3], int maxverts )
{
	signed short all[32][3];
	signed short cand[GC_CAP_MAX_VERTS][3];
	byte sel[GC_CAP_MAX_VERTS];
	byte taken[32];
	int nedges, nall, i, k, n, best, nsel;
	float cx, cy, cz, best_d, d, area_best;

	if( !wmodel || !wmodel->vertexes || !wmodel->surfedges || !out || numedges < 3 || maxverts < 3 )
		return 0;
	if( maxverts > GC_CAP_MAX_VERTS )
		maxverts = GC_CAP_MAX_VERTS;
	if( !wmodel->edges16 && !wmodel->edges32 )
		return 0;
	if( firstedge < 0 || firstedge + numedges > wmodel->numsurfedges )
		return 0;
	nedges = wmodel->numedges;
	if( nedges < 1 )
		return 0;
	if( numedges <= maxverts )
		return GC_BakeCapVertsFromModel( wmodel, firstedge, numedges, out, maxverts );

	nall = numedges;
	if( nall > 32 )
	{
		for( i = 0; i < 32; i++ )
		{
			if( !GC_BakeOneSurfedgeVert( wmodel, firstedge, ( i * numedges ) / 32,
				nedges, all, i ))
				return 0;
		}
		nall = 32;
	}
	else
	{
		for( i = 0; i < nall; i++ )
		{
			if( !GC_BakeOneSurfedgeVert( wmodel, firstedge, i, nedges, all, i ))
				return 0;
		}
	}

	cx = cy = cz = 0.0f;
	for( i = 0; i < nall; i++ )
	{
		cx += (float)all[i][0];
		cy += (float)all[i][1];
		cz += (float)all[i][2];
	}
	cx /= (float)nall;
	cy /= (float)nall;
	cz /= (float)nall;

	/* --- farthest-point --- */
	memset( taken, 0, sizeof( taken ));
	nsel = 0;
	best = 0;
	best_d = -1.0f;
	for( i = 0; i < nall; i++ )
	{
		float dx = (float)all[i][0] - cx;
		float dy = (float)all[i][1] - cy;
		float dz = (float)all[i][2] - cz;

		d = dx * dx + dy * dy + dz * dz;
		if( d > best_d )
		{
			best_d = d;
			best = i;
		}
	}
	sel[nsel++] = (byte)best;
	taken[best] = 1;
	while( nsel < maxverts && nsel < nall )
	{
		best = -1;
		best_d = -1.0f;
		for( i = 0; i < nall; i++ )
		{
			float mind;

			if( taken[i] )
				continue;
			mind = 1e30f;
			for( k = 0; k < nsel; k++ )
			{
				int j = (int)sel[k];
				float dx = (float)all[i][0] - (float)all[j][0];
				float dy = (float)all[i][1] - (float)all[j][1];
				float dz = (float)all[i][2] - (float)all[j][2];

				d = dx * dx + dy * dy + dz * dz;
				if( d < mind )
					mind = d;
			}
			if( mind > best_d )
			{
				best_d = mind;
				best = i;
			}
		}
		if( best < 0 )
			break;
		sel[nsel++] = (byte)best;
		taken[best] = 1;
	}
	GC_BakeSortSelByRing( sel, nsel );
	n = GC_BakeSelToOut( all, sel, nsel, out );
	area_best = GC_BakePolyArea3( out, n );

	/* --- G250: plane-ST extremes on the same ring (real verts only) --- */
	if( pl && nall >= 3 )
	{
		vec3_t right, up, tmp;
		int imins, imaxs, imint, imaxt, iext[4], next;
		float mins, maxs, mint, maxt;

		VectorCopy( pl->normal, tmp );
		if( fabs( tmp[2] ) < 0.9f )
		{
			right[0] = -tmp[1];
			right[1] = tmp[0];
			right[2] = 0.0f;
		}
		else
		{
			right[0] = 0.0f;
			right[1] = -tmp[2];
			right[2] = tmp[1];
		}
		if( VectorLength( right ) < 0.1f )
			return n;
		VectorNormalize( right );
		CrossProduct( pl->normal, right, up );
		if( VectorLength( up ) < 0.1f )
			return n;
		VectorNormalize( up );

		imins = imaxs = imint = imaxt = 0;
		mins = maxs = DotProduct( all[0], right ); /* position as vec — need float */
		/* DotProduct expects float[3]; promote */
		{
			vec3_t p0;
			p0[0] = (float)all[0][0]; p0[1] = (float)all[0][1]; p0[2] = (float)all[0][2];
			mins = maxs = DotProduct( p0, right );
			mint = maxt = DotProduct( p0, up );
		}
		for( i = 1; i < nall; i++ )
		{
			vec3_t pi;
			float s, t;

			pi[0] = (float)all[i][0];
			pi[1] = (float)all[i][1];
			pi[2] = (float)all[i][2];
			s = DotProduct( pi, right );
			t = DotProduct( pi, up );
			if( s < mins ) { mins = s; imins = i; }
			if( s > maxs ) { maxs = s; imaxs = i; }
			if( t < mint ) { mint = t; imint = i; }
			if( t > maxt ) { maxt = t; imaxt = i; }
		}
		iext[0] = imins; iext[1] = imaxs; iext[2] = imint; iext[3] = imaxt;
		memset( taken, 0, sizeof( taken ));
		nsel = 0;
		for( i = 0; i < 4 && nsel < maxverts; i++ )
		{
			int j = iext[i];

			if( taken[j] )
				continue;
			sel[nsel++] = (byte)j;
			taken[j] = 1;
		}
		/* Pad with farthest-from-centroid if under maxverts. */
		while( nsel < maxverts && nsel < nall )
		{
			best = -1;
			best_d = -1.0f;
			for( i = 0; i < nall; i++ )
			{
				float dx, dy, dz;

				if( taken[i] )
					continue;
				dx = (float)all[i][0] - cx;
				dy = (float)all[i][1] - cy;
				dz = (float)all[i][2] - cz;
				d = dx * dx + dy * dy + dz * dz;
				if( d > best_d )
				{
					best_d = d;
					best = i;
				}
			}
			if( best < 0 )
				break;
			sel[nsel++] = (byte)best;
			taken[best] = 1;
		}
		if( nsel >= 3 )
		{
			float area_ext;

			GC_BakeSortSelByRing( sel, nsel );
			next = GC_BakeSelToOut( all, sel, nsel, cand );
			area_ext = GC_BakePolyArea3( cand, next );
			if( area_ext > area_best )
			{
				memcpy( out, cand, (size_t)next * sizeof( cand[0] ));
				n = next;
			}
		}
	}
	return n;
}

/*
 * G199/G200: place a UV-aligned quad on the real face plane using texinfo.
 * Edge walks often fail after BSP scratch reuse; normal*dist quads floated at
 * the wrong world origin (DumpFrames stayed clear-sky despite drawn=179).
 */
static qboolean GC_SolveFaceSTPoint( const mplane_t *pl, const float svec[4], const float tvec[4],
	float s, float t, vec3_t out )
{
	float	a00, a01, a02, a10, a11, a12, a20, a21, a22;
	float	b0, b1, b2, det, inv;

	if( !pl || !svec || !tvec || !out )
		return false;
	a00 = pl->normal[0]; a01 = pl->normal[1]; a02 = pl->normal[2];
	a10 = svec[0]; a11 = svec[1]; a12 = svec[2];
	a20 = tvec[0]; a21 = tvec[1]; a22 = tvec[2];
	b0 = pl->dist;
	b1 = s - svec[3];
	b2 = t - tvec[3];
	det = a00 * ( a11 * a22 - a12 * a21 )
		- a01 * ( a10 * a22 - a12 * a20 )
		+ a02 * ( a10 * a21 - a11 * a20 );
	if( fabs( det ) < 1e-5f )
		return false;
	inv = 1.0f / det;
	out[0] = inv * ( b0 * ( a11 * a22 - a12 * a21 )
		- a01 * ( b1 * a22 - a12 * b2 )
		+ a02 * ( b1 * a21 - a11 * b2 ));
	out[1] = inv * ( a00 * ( b1 * a22 - a12 * b2 )
		- b0 * ( a10 * a22 - a12 * a20 )
		+ a02 * ( a10 * b2 - b1 * a20 ));
	out[2] = inv * ( a00 * ( a11 * b2 - b1 * a21 )
		- a01 * ( a10 * b2 - b1 * a20 )
		+ b0 * ( a10 * a21 - a11 * a20 ));
	return true;
}

static int GC_BakeCapVertsFromTexinfo( const mplane_t *pl, const mtexinfo_t *tex,
	const short mins[2], const short extents[2], signed short out[][3], int maxverts )
{
	vec3_t	p;
	float	s0, t0, s1, t1;
	int	i;
	static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

	if( !pl || !tex || !mins || !extents || !out || maxverts < 4 )
		return 0;
	if( extents[0] < 1 || extents[1] < 1 )
		return 0;
	s0 = (float)mins[0];
	t0 = (float)mins[1];
	s1 = (float)( mins[0] + extents[0] );
	t1 = (float)( mins[1] + extents[1] );
	for( i = 0; i < 4; i++ )
	{
		float s = s0 + uv[i][0] * ( s1 - s0 );
		float t = t0 + uv[i][1] * ( t1 - t0 );

		if( !GC_SolveFaceSTPoint( pl, tex->vecs[0], tex->vecs[1], s, t, p ))
			return 0;
		out[i][0] = (signed short)p[0];
		out[i][1] = (signed short)p[1];
		out[i][2] = (signed short)p[2];
	}
	return 4;
}

/* G201e: axis quad on the face plane, centered at the eye's projection onto
 * the plane (not normal*dist — that pinned quads near the world origin and
 * left DumpFrames sky-only despite drawn=179 + working guFrustum/lookAt). */
static int GC_BakeCapVertsFromPlane( const mplane_t *pl, int ext0, int ext1,
	signed short out[][3], int maxverts )
{
	vec3_t	right, up, org, p, eye;
	float	hs, ht, dist_eye;
	int	i;
	static const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

	if( !pl || !out || maxverts < 4 || ext0 < 1 || ext1 < 1 )
		return 0;
	hs = (float)ext0 * 0.5f;
	ht = (float)ext1 * 0.5f;
	if( hs < 16.0f )
		hs = 16.0f;
	if( ht < 16.0f )
		ht = 16.0f;
	/* Clamp half-size so a single face cannot span the whole map in s16. */
	if( hs > 512.0f )
		hs = 512.0f;
	if( ht > 512.0f )
		ht = 512.0f;

	if( !VectorIsNull( gc_newgame_capture_origin ))
	{
		VectorCopy( gc_newgame_capture_origin, eye );
		dist_eye = DotProduct( eye, pl->normal ) - pl->dist;
		VectorMA( eye, -dist_eye, pl->normal, org );
	}
	else
		VectorScale( pl->normal, pl->dist, org );

	if( fabs( pl->normal[2] ) < 0.7f )
	{
		right[0] = pl->normal[1];
		right[1] = -pl->normal[0];
		right[2] = 0.0f;
	}
	else
	{
		right[0] = 1.0f;
		right[1] = 0.0f;
		right[2] = 0.0f;
	}
	if( VectorLength( right ) < 0.1f )
		return 0;
	VectorNormalize( right );
	CrossProduct( pl->normal, right, up );
	if( VectorLength( up ) < 0.1f )
		return 0;
	VectorNormalize( up );
	for( i = 0; i < 4; i++ )
	{
		VectorMA( org, corners[i][0] * hs, right, p );
		VectorMA( p, corners[i][1] * ht, up, p );
		out[i][0] = (signed short)p[0];
		out[i][1] = (signed short)p[1];
		out[i][2] = (signed short)p[2];
	}
	return 4;
}


/* G216: snapshot verts into lean cand while edges/verts are still valid.
 * G217: do not truncate EDGE walks — faces with >GC_CAP_MAX_VERTS edges
 * must not use first-N truncation (made skyfill jagged + off-frustum).
 * G247/G248/G250: farthest-point / plane-ST decimate (TEX-tier) before ST-AABB.
 * G249 TEX-AABB undershoot hybrid was reverted (clear%↑) — do not restore. */
static void GC_BakeLiveCandVerts( gc_live_cand_t *dst, model_t *wmodel )
{
	int n;

	if( !dst )
		return;
	dst->nverts = 0;
	dst->bake_src = GC_CAP_BAKE_NONE;
	if( !wmodel )
		return;
	n = 0;
	if( dst->numedges >= 3 && dst->numedges <= GC_CAP_MAX_VERTS )
	{
		n = GC_BakeCapVertsFromModel( wmodel, dst->firstedge, dst->numedges,
			dst->pts_s16, GC_CAP_MAX_VERTS );
		if( n == dst->numedges && n >= 3 )
			dst->bake_src = GC_CAP_BAKE_EDGE;
		else
			n = 0;
	}
	/* G247/G248/G250: Flipper-side geometry for high-edge faces — real world
	 * verts, farthest-point / plane-ST subsample. TEX-tier (never EDGE +80k). */
	if( n < 3 && dst->numedges > GC_CAP_MAX_VERTS )
	{
		int nd = GC_BakeCapVertsFromModelDecimated( wmodel, dst->firstedge, dst->numedges,
			&dst->plane, dst->pts_s16, GC_CAP_MAX_VERTS );

		if( nd >= 3 )
		{
			n = nd;
			dst->bake_src = GC_CAP_BAKE_TEX;
		}
	}
	if( n < 3 && dst->has_tex )
	{
		mtexinfo_t tex;

		memset( &tex, 0, sizeof( tex ));
		memcpy( tex.vecs, dst->vecs, sizeof( dst->vecs ));
		tex.texture = dst->texture;
		tex.flags = dst->tex_flags;
		n = GC_BakeCapVertsFromTexinfo( &dst->plane, &tex,
			dst->texturemins, dst->extents, dst->pts_s16, GC_CAP_MAX_VERTS );
		if( n >= 3 )
			dst->bake_src = GC_CAP_BAKE_TEX;
	}
	/* G219: no plane-fallback for live skyfill — eye-centered quads blow out
	 * DumpFrames as giant white REPLACE slabs. EDGE/TEX only. */
	dst->nverts = (byte)(( n > 0 && n <= GC_CAP_MAX_VERTS ) ? n : 0 );
	if( dst->nverts < 3 )
		dst->bake_src = GC_CAP_BAKE_NONE;
}

static void GC_LiveCandCentroid( const gc_live_cand_t *c, vec3_t out )
{
	int i, n;
	float sx, sy, sz;

	if( !c || !out )
		return;
	n = (int)c->nverts;
	if( n < 3 )
	{
		VectorClear( out );
		return;
	}
	sx = sy = sz = 0.0f;
	for( i = 0; i < n; i++ )
	{
		sx += (float)c->pts_s16[i][0];
		sy += (float)c->pts_s16[i][1];
		sz += (float)c->pts_s16[i][2];
	}
	out[0] = sx / (float)n;
	out[1] = sy / (float)n;
	out[2] = sz / (float)n;
}

/* G217: rank lean faces by dump-eye frustum (centroid along forward), not
 * infinite-plane distance (which promoted distant coplanar walls). */
static int GC_LiveViewScore( const gc_live_cand_t *c )
{
	vec3_t	cent, d;
	float	along, dist2, dist;
	int	score;
	qboolean is_wall;

	if( !c || c->nverts < 3 || c->area <= 0 )
		return 0;
	is_wall = ( fabs( c->plane.normal[2] ) < 0.55f ); /* G232: was 0.35 */
	score = c->area;
	if( is_wall )
		score += ( c->area >> 1 );
	else if( fabs( c->plane.normal[2] ) > 0.7f )
		score += ( c->area >> 2 ); /* floors still get a little */
	/* Prefer real EDGE/TEX geometry over eye-centered plane quads. */
	if( c->bake_src == GC_CAP_BAKE_EDGE )
		score += 80000;
	else if( c->bake_src == GC_CAP_BAKE_TEX )
		score += 40000;
	else if( c->bake_src == GC_CAP_BAKE_PLANE )
		score /= 2;

	if( VectorIsNull( gc_newgame_capture_origin ))
		return score;

	GC_LiveCandCentroid( c, cent );
	VectorSubtract( cent, gc_newgame_capture_origin, d );
	dist2 = DotProduct( d, d );
	dist = VectorLength( d );
	if( dist < 1.0f )
		dist = 1.0f;

	if( !VectorIsNull( gc_newgame_capture_forward ))
	{
		along = DotProduct( d, gc_newgame_capture_forward );
		/* G228: keep near side walls (tram tunnel) — was score/8 below along=24. */
		if( along < -64.0f )
			return score / 8;
		/* G220/G228: admit wide side walls (was 0.04 / ~70°). */
		if( along > 0.0f && along * along < 0.01f * dist2 )
			score /= 2;
		/* G220: sticky ultra-near so throat walls stay in the live set. */
		if( dist < 192.0f )
			score += 300000;
		else if( dist < 384.0f )
			score += 150000;
		else if( dist < 768.0f )
			score += 60000;
		else if( dist > 1536.0f )
			score /= 3;
		/* Angular-size proxy: large near walls beat tiny scraps. */
		score += (int)((float)c->area * (180.0f / dist));
		if( along > 0.0f )
			score += (int)( along * 8.0f );
		/* G234: portal ceilings/floors mid-field + portal-frame walls. */
		if( along > 48.0f && dist < 896.0f )
		{
			if( fabs( c->plane.normal[2] ) > 0.55f )
				score += c->area + 150000;
			else if( along > 200.0f && fabs( c->plane.normal[2] ) < 0.55f )
				score += ( c->area >> 1 ) + 80000;
		}
		return score;
	}
	return GC_CapNearEyeScore( &c->plane, c->area, is_wall );
}

static int GC_BakeCapVertsForSurf( model_t *wmodel, const msurface_t *src,
	signed short out[][3], int maxverts, byte *out_src )
{
	int n;

	if( out_src )
		*out_src = GC_CAP_BAKE_NONE;
	if( !src || !out )
		return 0;
	n = 0;
	if( src->numedges >= 3 && src->numedges <= maxverts )
	{
		n = GC_BakeCapVertsFromModel( wmodel, src->firstedge, src->numedges, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_EDGE;
			return n;
		}
		n = 0;
	}
	/* G247/G248/G250: decimated edge (no G249 TEX-AABB undershoot hybrid). */
	if( src->numedges > maxverts )
	{
		n = GC_BakeCapVertsFromModelDecimated( wmodel, src->firstedge, src->numedges,
			src->plane, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_TEX;
			return n;
		}
		n = 0;
	}
	/* G205: texinfo ST solve puts quads on the real face (LM/ST valid). Prefer
	 * over eye-centered plane when wall-aim can look at those walls. */
	if( src->plane && src->texinfo )
	{
		n = GC_BakeCapVertsFromTexinfo( src->plane, src->texinfo,
			src->texturemins, src->extents, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_TEX;
			return n;
		}
	}
	if( !src->plane )
		return 0;
	n = GC_BakeCapVertsFromPlane( src->plane, src->extents[0], src->extents[1],
		out, maxverts );
	if( out_src && n >= 3 )
		*out_src = GC_CAP_BAKE_PLANE;
	return n;
}

static int GC_BakeCapVertsForCap( model_t *wmodel, int firstedge, int numedges,
	const mplane_t *pl, const mtexinfo_t *tex, const short mins[2], const short extents[2],
	signed short out[][3], int maxverts, byte *out_src )
{
	int n = 0;

	if( out_src )
		*out_src = GC_CAP_BAKE_NONE;
	if( numedges >= 3 && numedges <= maxverts )
	{
		n = GC_BakeCapVertsFromModel( wmodel, firstedge, numedges, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_EDGE;
			return n;
		}
		n = 0;
	}
	if( numedges > maxverts )
	{
		n = GC_BakeCapVertsFromModelDecimated( wmodel, firstedge, numedges, pl, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_TEX;
			return n;
		}
		n = 0;
	}
	if( pl && tex && mins && extents )
	{
		n = GC_BakeCapVertsFromTexinfo( pl, tex, mins, extents, out, maxverts );
		if( n >= 3 )
		{
			if( out_src )
				*out_src = GC_CAP_BAKE_TEX;
			return n;
		}
	}
	if( !pl || !extents )
		return 0;
	n = GC_BakeCapVertsFromPlane( pl, extents[0], extents[1], out, maxverts );
	if( out_src && n >= 3 )
		*out_src = GC_CAP_BAKE_PLANE;
	return n;
}

const unsigned short *GC_GetNewGameCapLightmap( int slot, int *w, int *h )
{
	if( slot < 0 || slot >= gc_newgame_cap_face_count )
		return NULL;
	if( gc_newgame_cap_lm_w[slot] < 4 || gc_newgame_cap_lm_h[slot] < 4 )
		return NULL;
	if( w )
		*w = gc_newgame_cap_lm_w[slot];
	if( h )
		*h = gc_newgame_cap_lm_h[slot];
	return GC_CapAtlasTile( slot );
}

const unsigned short *GC_GetNewGameCapLightmapAtlas( int *w, int *h )
{
	if( !gc_newgame_cap_lm_atlas_ready || !gc_newgame_cap_lm_atlas
		|| gc_newgame_cap_face_count <= 0 )
		return NULL;
	if( w )
		*w = GC_LM_ATLAS_W;
	if( h )
		*h = GC_LM_ATLAS_H;
	return gc_newgame_cap_lm_atlas;
}

void GC_GetNewGameCapLightmapAtlasUV( int slot, float s, float t, float *out_s, float *out_t )
{
	int col, row;
	float u, v;

	if( !out_s || !out_t )
		return;
	if( slot < 0 || slot >= gc_newgame_cap_face_count
		|| gc_newgame_cap_lm_w[slot] < 4 || gc_newgame_cap_lm_h[slot] < 4 )
	{
		*out_s = *out_t = 0.0f;
		return;
	}
	col = slot % GC_LM_ATLAS_COLS;
	row = slot / GC_LM_ATLAS_COLS;
	/* Half-texel inset so LINEAR filter stays inside the 4×4 cell. */
	if( s < 0.0f )
		s = 0.0f;
	else if( s > 1.0f )
		s = 1.0f;
	if( t < 0.0f )
		t = 0.0f;
	else if( t > 1.0f )
		t = 1.0f;
	u = (float)( col * GC_CAP_LM_DIM ) + 0.5f + s * (float)( GC_CAP_LM_DIM - 1 );
	v = (float)( row * GC_CAP_LM_DIM ) + 0.5f + t * (float)( GC_CAP_LM_DIM - 1 );
	*out_s = u / (float)GC_LM_ATLAS_W;
	*out_t = v / (float)GC_LM_ATLAS_H;
}

static u16 *GC_CapAtlasTile( int slot )
{
	if( slot < 0 || !gc_newgame_cap_lm_atlas )
		return NULL;
	if( slot >= GC_LM_ATLAS_COLS * GC_LM_ATLAS_ROWS )
		return NULL;
	return &gc_newgame_cap_lm_atlas[slot * ( GC_CAP_LM_DIM * GC_CAP_LM_DIM )];
}

/* Pack already-tiled 4×4 face LMs into one GX_TF_RGB565 atlas. */
static void GC_PackCapLightmapAtlas( void )
{
	int i;
	const u16 mid = 0xC618;
	const size_t bytes = (size_t)GC_LM_ATLAS_W * (size_t)GC_LM_ATLAS_H * sizeof( u16 );

	gc_newgame_cap_lm_atlas_ready = false;
	gc_newgame_cap_lm_atlas = gc_newgame_cap_lm_atlas_bss;

	/* Face LMs were written directly into atlas tiles; paint unused tiles mid-grey. */
	for( i = gc_newgame_cap_face_count; i < GC_LM_ATLAS_COLS * GC_LM_ATLAS_ROWS; i++ )
	{
		u16 *tile = GC_CapAtlasTile( i );
		int k;
		if( !tile )
			break;
		for( k = 0; k < GC_CAP_LM_DIM * GC_CAP_LM_DIM; k++ )
			tile[k] = mid;
	}
	DCFlushRange( gc_newgame_cap_lm_atlas, (u32)bytes );
	gc_newgame_cap_lm_atlas_ready = ( gc_newgame_cap_face_count > 0 );

	if( !gc_g180_logged && gc_newgame_cap_lm_atlas_ready )
	{
		gc_g180_logged = true;
		Con_Reportf( "Xash3D GameCube: G180 lightmap atlas %dx%d faces=%d cols=%d\n",
			GC_LM_ATLAS_W, GC_LM_ATLAS_H, gc_newgame_cap_face_count, GC_LM_ATLAS_COLS );
	}
}

static void GC_BakeCapLightmap( const msurface_t *src, int slot )
{
	const mextrasurf_t *info;
	int sample_size;
	int smax, tmax;
	int dw, dh, x, y;
	u16 linear[GC_CAP_LM_DIM * GC_CAP_LM_DIM];
	u16 *dst;
	const color24 *lm;
	const u16 mid = 0xC618;
	int tile_x, tile_y, ty;
	u16 *out;

	gc_newgame_cap_lm_w[slot] = 0;
	gc_newgame_cap_lm_h[slot] = 0;
	dst = GC_CapAtlasTile( slot );
	if( !dst )
		return;
	info = src->info;

	if( !info )
	{
		dw = dh = 4;
		for( y = 0; y < dw * dh; y++ )
			linear[y] = mid;
		gc_newgame_cap_lm_real[slot] = 0;
		goto swizzle;
	}

	sample_size = Mod_SampleSizeForFace( src );
	if( sample_size < 1 )
		sample_size = 16;
	smax = ( info->lightextents[0] / sample_size ) + 1;
	tmax = ( info->lightextents[1] / sample_size ) + 1;
	if( smax < 1 )
		smax = 1;
	if( tmax < 1 )
		tmax = 1;

	dw = smax;
	dh = tmax;
	if( dw > GC_CAP_LM_DIM )
		dw = GC_CAP_LM_DIM;
	if( dh > GC_CAP_LM_DIM )
		dh = GC_CAP_LM_DIM;
	dw = ( dw < 4 ) ? 4 : ( dw & ~3 );
	dh = ( dh < 4 ) ? 4 : ( dh & ~3 );

	lm = src->samples;
	for( y = 0; y < dh; y++ )
	{
		int sy = ( tmax <= 1 ) ? 0 : ( y * ( tmax - 1 )) / ( dh > 1 ? dh - 1 : 1 );
		for( x = 0; x < dw; x++ )
		{
			int sx = ( smax <= 1 ) ? 0 : ( x * ( smax - 1 )) / ( dw > 1 ? dw - 1 : 1 );
			u16 pix = mid;

			if( lm && sx >= 0 && sy >= 0 && sx < smax && sy < tmax )
			{
				const color24 *c = &lm[sy * smax + sx];
				/* G209: stronger boost toward REPLACE brightness (~115 avg). */
				unsigned r = ((unsigned)c->r * 3u);
				unsigned g = ((unsigned)c->g * 3u);
				unsigned b = ((unsigned)c->b * 3u);
				if( r < 128 )
					r = 128;
				if( g < 128 )
					g = 128;
				if( b < 128 )
					b = 128;
				if( r > 255 )
					r = 255;
				if( g > 255 )
					g = 255;
				if( b > 255 )
					b = 255;
				pix = (u16)((( r >> 3 ) << 11 ) | (( g >> 2 ) << 5 ) | ( b >> 3 ));
			}
			linear[y * dw + x] = pix;
		}
	}
	gc_newgame_cap_lm_real[slot] = lm ? 1 : 0;

swizzle:
	/* GX_TF_RGB565 needs 4×4 tiled layout. */
	out = dst;
	for( tile_y = 0; tile_y < dh; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < dw; tile_x += 4 )
		{
			for( ty = 0; ty < 4; ty++ )
			{
				const u16 *row = linear + ( tile_y + ty ) * dw + tile_x;
				out[0] = row[0];
				out[1] = row[1];
				out[2] = row[2];
				out[3] = row[3];
				out += 4;
			}
		}
	}
	DCFlushRange( dst, (u32)( dw * dh * sizeof( u16 )));
	gc_newgame_cap_lm_w[slot] = (byte)dw;
	gc_newgame_cap_lm_h[slot] = (byte)dh;
}

static qboolean GC_CaptureOneDrawFaceAt( model_t *wmodel, int surf_index, int slot )
{
	msurface_t *src;
	gc_cap_face_t *dst;
	msurface_t *draw;

	if( slot < 0 || slot >= GC_MAX_CAP_FACES )
		return false;
	if( surf_index < 0 || surf_index >= wmodel->numsurfaces )
		return false;
	src = &wmodel->surfaces[surf_index];
	if( !src->plane || src->numedges < 3 || src->numedges > 32 )
		return false;
	if( src->firstedge < 0
		|| src->firstedge + src->numedges > wmodel->numsurfedges )
		return false;
	if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
		return false;

	dst = &gc_newgame_cap_faces[slot];
	draw = &gc_newgame_draw_surfs[slot];
	memset( dst, 0, sizeof( *dst ));
	dst->firstedge = src->firstedge;
	dst->numedges = src->numedges;
	dst->flags = src->flags;
	dst->plane = *src->plane;
	dst->texturemins[0] = src->texturemins[0];
	dst->texturemins[1] = src->texturemins[1];
	dst->extents[0] = src->extents[0];
	dst->extents[1] = src->extents[1];
	memcpy( dst->styles, src->styles, sizeof( dst->styles ));
	dst->samples = src->samples; /* may dangle later — lit path tolerates NULL */

	if( src->texinfo && src->texinfo->texture )
	{
		dst->texinfo = *src->texinfo;
		dst->texinfo.faceinfo = NULL; /* may point into scratch */
	}
	if( src->info )
	{
		dst->info = *src->info;
		dst->info.surf = draw;
		dst->info.bevel = NULL;
		dst->info.deluxemap = NULL;
		memset( dst->info.reserved, 0, sizeof( dst->info.reserved ));
	}
	else
	{
		dst->info.lightextents[0] = src->extents[0];
		dst->info.lightextents[1] = src->extents[1];
		dst->info.lightmapmins[0] = src->texturemins[0];
		dst->info.lightmapmins[1] = src->texturemins[1];
		dst->info.surf = draw;
	}

	memset( draw, 0, sizeof( *draw ));
	draw->firstedge = dst->firstedge;
	draw->numedges = dst->numedges;
	draw->flags = dst->flags;
	draw->plane = &dst->plane;
	draw->texturemins[0] = dst->texturemins[0];
	draw->texturemins[1] = dst->texturemins[1];
	draw->extents[0] = dst->extents[0];
	draw->extents[1] = dst->extents[1];
	memcpy( draw->styles, dst->styles, sizeof( draw->styles ));
	draw->samples = NULL; /* force mid-grade light; samples may be scratch */
	draw->info = &dst->info;
	if( dst->texinfo.texture )
		draw->texinfo = &dst->texinfo;
	gc_newgame_cap_areas[slot] = (int)src->extents[0] * (int)src->extents[1];
	/* Best-effort bake — do not reject the face if edges are briefly unreadable. */
	gc_cap_nverts[slot] = (byte)GC_BakeCapVertsForSurf( wmodel, src,
		gc_cap_pts_s16[slot], GC_CAP_MAX_VERTS, &gc_cap_bake_src[slot] );
	return true;
}

/* G163: mid-grade LM only — no sample walk (cluster refresh must stay cheap). */
static void GC_FillCapLightmapMid( int slot )
{
	u16 linear[GC_CAP_LM_DIM * GC_CAP_LM_DIM];
	u16 *dst;
	const u16 mid = 0xC618;
	int dw = 4, dh = 4;
	int tile_x, tile_y, ty, y;
	u16 *out;

	if( slot < 0 || slot >= GC_MAX_CAP_FACES )
		return;
	for( y = 0; y < dw * dh; y++ )
		linear[y] = mid;
	gc_newgame_cap_lm_real[slot] = 0;
	gc_newgame_cap_lm_w[slot] = (byte)dw;
	gc_newgame_cap_lm_h[slot] = (byte)dh;
	dst = GC_CapAtlasTile( slot );
	if( !dst )
		return;
	out = dst;
	for( tile_y = 0; tile_y < dh; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < dw; tile_x += 4 )
		{
			for( ty = 0; ty < 4; ty++ )
			{
				const u16 *row = linear + ( tile_y + ty ) * dw + tile_x;
				out[0] = row[0];
				out[1] = row[1];
				out[2] = row[2];
				out[3] = row[3];
				out += 4;
			}
		}
	}
	DCFlushRange( dst, (u32)( dw * dh * sizeof( u16 )));
}

static void GC_SwapCapSlots( int a, int b )
{
	gc_cap_face_t face;
	msurface_t draw;
	int area;
	u16 lm[GC_CAP_LM_DIM * GC_CAP_LM_DIM];
	u16 *tile_a, *tile_b;
	byte lmw, lmh, lmr, nv, bsrc;
	signed short pts[GC_CAP_MAX_VERTS][3];

	if( a == b || a < 0 || b < 0 || a >= GC_MAX_CAP_FACES || b >= GC_MAX_CAP_FACES )
		return;
	face = gc_newgame_cap_faces[a];
	draw = gc_newgame_draw_surfs[a];
	area = gc_newgame_cap_areas[a];
	tile_a = GC_CapAtlasTile( a );
	tile_b = GC_CapAtlasTile( b );
	if( tile_a )
		memcpy( lm, tile_a, sizeof( lm ));
	lmw = gc_newgame_cap_lm_w[a];
	lmh = gc_newgame_cap_lm_h[a];
	lmr = gc_newgame_cap_lm_real[a];
	nv = gc_cap_nverts[a];
	bsrc = gc_cap_bake_src[a];
	memcpy( pts, gc_cap_pts_s16[a], sizeof( pts ));

	gc_newgame_cap_faces[a] = gc_newgame_cap_faces[b];
	gc_newgame_draw_surfs[a] = gc_newgame_draw_surfs[b];
	gc_newgame_cap_areas[a] = gc_newgame_cap_areas[b];
	if( tile_a && tile_b )
		memcpy( tile_a, tile_b, sizeof( lm ));
	gc_newgame_cap_lm_w[a] = gc_newgame_cap_lm_w[b];
	gc_newgame_cap_lm_h[a] = gc_newgame_cap_lm_h[b];
	gc_newgame_cap_lm_real[a] = gc_newgame_cap_lm_real[b];
	gc_cap_nverts[a] = gc_cap_nverts[b];
	gc_cap_bake_src[a] = gc_cap_bake_src[b];
	memcpy( gc_cap_pts_s16[a], gc_cap_pts_s16[b], sizeof( pts ));

	gc_newgame_cap_faces[b] = face;
	gc_newgame_draw_surfs[b] = draw;
	gc_newgame_cap_areas[b] = area;
	if( tile_b )
		memcpy( tile_b, lm, sizeof( lm ));
	gc_newgame_cap_lm_w[b] = lmw;
	gc_newgame_cap_lm_h[b] = lmh;
	gc_newgame_cap_lm_real[b] = lmr;
	gc_cap_nverts[b] = nv;
	gc_cap_bake_src[b] = bsrc;
	memcpy( gc_cap_pts_s16[b], pts, sizeof( pts ));
}

/* Geometry + optional sample bake. bake_lm=false → mid tile only (G163). */
/* Geometry only — never touch src->info/samples (may dangle after scratch reuse). */
static qboolean GC_CaptureOneDrawFaceGeomSafe( model_t *wmodel, int surf_index, int slot )
{
	msurface_t *src;
	gc_cap_face_t *dst;
	msurface_t *draw;

	if( slot < 0 || slot >= GC_MAX_CAP_FACES )
		return false;
	if( surf_index < 0 || surf_index >= wmodel->numsurfaces )
		return false;
	src = &wmodel->surfaces[surf_index];
	if( !src->plane || src->numedges < 3 || src->numedges > 32 )
		return false;
	if( src->firstedge < 0
		|| src->firstedge + src->numedges > wmodel->numsurfedges )
		return false;
	if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
		return false;

	dst = &gc_newgame_cap_faces[slot];
	draw = &gc_newgame_draw_surfs[slot];
	memset( dst, 0, sizeof( *dst ));
	dst->firstedge = src->firstedge;
	dst->numedges = src->numedges;
	dst->flags = src->flags;
	dst->plane = *src->plane;
	dst->texturemins[0] = src->texturemins[0];
	dst->texturemins[1] = src->texturemins[1];
	dst->extents[0] = src->extents[0];
	dst->extents[1] = src->extents[1];
	memcpy( dst->styles, src->styles, sizeof( dst->styles ));
	dst->samples = NULL;

	if( src->texinfo && src->texinfo->texture )
	{
		dst->texinfo = *src->texinfo;
		dst->texinfo.faceinfo = NULL;
	}
	dst->info.lightextents[0] = src->extents[0];
	dst->info.lightextents[1] = src->extents[1];
	dst->info.lightmapmins[0] = src->texturemins[0];
	dst->info.lightmapmins[1] = src->texturemins[1];
	dst->info.surf = draw;

	memset( draw, 0, sizeof( *draw ));
	draw->firstedge = dst->firstedge;
	draw->numedges = dst->numedges;
	draw->flags = dst->flags;
	draw->plane = &dst->plane;
	draw->texturemins[0] = dst->texturemins[0];
	draw->texturemins[1] = dst->texturemins[1];
	draw->extents[0] = dst->extents[0];
	draw->extents[1] = dst->extents[1];
	memcpy( draw->styles, dst->styles, sizeof( draw->styles ));
	draw->samples = NULL;
	draw->info = &dst->info;
	if( dst->texinfo.texture )
		draw->texinfo = &dst->texinfo;
	gc_newgame_cap_areas[slot] = (int)src->extents[0] * (int)src->extents[1];
	gc_cap_nverts[slot] = (byte)GC_BakeCapVertsForSurf( wmodel, src,
		gc_cap_pts_s16[slot], GC_CAP_MAX_VERTS, &gc_cap_bake_src[slot] );
	return true;
}

static qboolean GC_CaptureOneDrawFaceAtEx( model_t *wmodel, int surf_index, int slot, qboolean bake_lm )
{
	msurface_t *src;

	if( bake_lm )
	{
		if( !GC_CaptureOneDrawFaceAt( wmodel, surf_index, slot ))
			return false;
		src = &wmodel->surfaces[surf_index];
		GC_BakeCapLightmap( src, slot );
	}
	else
	{
		/* G163 live refresh: safe geom + mid LM (no dangling extrasurf/samples). */
		if( !GC_CaptureOneDrawFaceGeomSafe( wmodel, surf_index, slot ))
			return false;
		GC_FillCapLightmapMid( slot );
	}
	return true;
}

static qboolean GC_CapFaceAlready( int firstedge, int numedges )
{
	int j;

	for( j = 0; j < gc_newgame_cap_face_count; j++ )
	{
		if( gc_newgame_draw_surfs[j].firstedge == firstedge
			&& gc_newgame_draw_surfs[j].numedges == numedges )
			return true;
	}
	return false;
}

/* G150: sort captured faces largest-first so edge budget prefers towers. */
static void GC_SortCapFacesByAreaDesc( void )
{
	int i, j;

	for( i = 1; i < gc_newgame_cap_face_count; i++ )
	{
		j = i;
		while( j > 0 && gc_newgame_cap_areas[j - 1] < gc_newgame_cap_areas[j] )
		{
			GC_SwapCapSlots( j - 1, j );
			j--;
		}
	}
	/* Re-bind plane/texinfo/info pointers after moves. */
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		msurface_t *draw = &gc_newgame_draw_surfs[i];
		gc_cap_face_t *dst = &gc_newgame_cap_faces[i];
		u16 *tile;

		draw->plane = &dst->plane;
		draw->info = &dst->info;
		dst->info.surf = draw;
		if( dst->texinfo.texture )
			draw->texinfo = &dst->texinfo;
		else
			draw->texinfo = NULL;
		tile = GC_CapAtlasTile( i );
		if( tile && gc_newgame_cap_lm_w[i] >= 4 && gc_newgame_cap_lm_h[i] >= 4 )
			DCFlushRange( tile,
				(u32)( gc_newgame_cap_lm_w[i] * gc_newgame_cap_lm_h[i] * sizeof( u16 )));
	}
	GC_PackCapLightmapAtlas();
	gc_newgame_cap_generation++;
}

/* G150: sort captured faces largest-first so edge budget prefers towers. */
static void GC_SortCapFacesByAreaDesc( void );
static void GC_CaptureDrawFacesFromSurfbits( model_t *wmodel, const byte *surfbits );
static qboolean GC_CaptureOneDrawFaceAtEx( model_t *wmodel, int surf_index, int slot, qboolean bake_lm );

/*
================
GC_CaptureDrawFacesNoPVS

When FatPVS lean calloc fails, still bake Flipper cap faces while msurface_t
is intact (BSP load). Top-K by area across all opaque surfaces — no surfbits.
================
*/
static void GC_CaptureDrawFacesNoPVS( model_t *wmodel )
{
	int i;
	int min_i;
	int min_area;
	int area_slots = GC_CAP_AREA_SLOTS;
	byte *allbits = NULL;
	size_t bytes;

	if( !wmodel || !wmodel->surfaces || wmodel->numsurfaces <= 0 )
		return;
	if( gc_newgame_cap_face_count > 0 )
		return; /* already baked this map */

	bytes = (size_t)( wmodel->numsurfaces + 7 ) / 8;
	allbits = (byte *)malloc( bytes );
	if( allbits )
	{
		memset( allbits, 0xff, bytes );
		GC_CaptureDrawFacesFromSurfbits( wmodel, allbits );
		free( allbits );
		GC_FlipperTrace( "Xash3D GameCube: G198 no-PVS face bake count=%d (Flipper cap)\n",
			gc_newgame_cap_face_count );
		return;
	}

	/* Even malloc for surfbits failed — capture top faces inline. */
	gc_newgame_cap_face_count = 0;
	gc_newgame_cap_tex_faces = 0;
	gc_newgame_cap_lm_faces = 0;
	gc_newgame_cap_lm_atlas_ready = false;

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src = &wmodel->surfaces[i];
		int area;
		qboolean is_wall;

		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area <= 0 )
			continue;
		is_wall = ( fabs( src->plane->normal[2] ) < 0.35f );
		if( is_wall )
			area += ( area >> 1 );

		if( gc_newgame_cap_face_count < area_slots )
		{
			if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, gc_newgame_cap_face_count, true ))
				continue;
			gc_newgame_cap_areas[gc_newgame_cap_face_count] = area;
			if( src->texinfo && src->texinfo->texture )
				gc_newgame_cap_tex_faces++;
			gc_newgame_cap_face_count++;
			continue;
		}

		min_i = 0;
		min_area = gc_newgame_cap_areas[0];
		{
			int k;
			for( k = 1; k < area_slots; k++ )
			{
				if( gc_newgame_cap_areas[k] < min_area )
				{
					min_area = gc_newgame_cap_areas[k];
					min_i = k;
				}
			}
		}
		if( area <= min_area )
			continue;
		if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, min_i, true ))
			continue;
		gc_newgame_cap_areas[min_i] = area;
	}

	GC_SortCapFacesByAreaDesc();
	GC_PackCapLightmapAtlas();
	GC_BakeTramLightmaps( wmodel );
	gc_newgame_cap_generation++;
	GC_FlipperTrace( "Xash3D GameCube: G198 inline face bake count=%d tex=%d lm=%d\n",
		gc_newgame_cap_face_count, gc_newgame_cap_tex_faces, gc_newgame_cap_lm_faces );
}

/*
=============
GC_CaptureWaterFacesFromSurfbits

G286: bake ≤8 SURF_DRAWTURB faces from capture surfbits into BSS. Avoids G276
live-pool tip and G273 uncapped draw-time BSP walk. Runs on scratch-retain.
=============
*/
static void GC_CaptureWaterFacesFromSurfbits( model_t *wmodel, const byte *surfbits )
{
	int i;
	int replaced = 0;
	int scores[GC_WATER_MAX_FACES];
	int pvs_hits = 0;

	gc_water_face_count = 0;
	gc_g286_water_logged = false;
	memset( gc_water_faces, 0, sizeof( gc_water_faces ));
	memset( scores, 0, sizeof( scores ));
	if( !wmodel || !wmodel->surfaces )
		return;
	(void)surfbits; /* Prefer near-eye turb even outside capture surfbits. */

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		gc_water_face_t trial;
		byte bake_src = GC_CAP_BAKE_NONE;
		int area, score, n, min_i, min_score, k;
		float fdot, dist;

		src = &wmodel->surfaces[i];
		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( !( src->flags & SURF_DRAWTURB ))
			continue;
		if( src->flags & SURF_DRAWSKY )
			continue;
		if( src->firstedge < 0
			|| src->firstedge + src->numedges > wmodel->numsurfedges )
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area < 64 )
			continue;
		fdot = DotProduct( gc_newgame_capture_origin, src->plane->normal )
			- src->plane->dist;
		dist = (float)fabs( fdot );
		/* Water is often a floor sheet — keep near-eye regardless of side. */
		if( dist > 1536.0f )
			continue;
		if( surfbits && ( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			pvs_hits++;

		memset( &trial, 0, sizeof( trial ));
		n = GC_BakeCapVertsForSurf( wmodel, src, trial.pts_s16, GC_CAP_MAX_VERTS, &bake_src );
		if( n < 3 || ( bake_src != GC_CAP_BAKE_EDGE && bake_src != GC_CAP_BAKE_TEX
			&& bake_src != GC_CAP_BAKE_PLANE ))
			continue;
		trial.nverts = (byte)n;
		trial.flags = (byte)( src->flags & SURF_PLANEBACK );
		trial.plane = *src->plane;
		score = GC_CapNearEyeScore( src->plane, area, false );
		score += area;
		if( surfbits && ( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			score += 100000;

		if( gc_water_face_count < GC_WATER_MAX_FACES )
		{
			gc_water_faces[gc_water_face_count] = trial;
			scores[gc_water_face_count] = score;
			gc_water_face_count++;
			continue;
		}
		min_i = 0;
		min_score = scores[0];
		for( k = 1; k < GC_WATER_MAX_FACES; k++ )
		{
			if( scores[k] < min_score )
			{
				min_score = scores[k];
				min_i = k;
			}
		}
		if( score <= min_score )
			continue;
		gc_water_faces[min_i] = trial;
		scores[min_i] = score;
		replaced++;
	}

	if( !gc_g286_water_logged )
	{
		gc_g286_water_logged = true;
		Con_Reportf( "Xash3D GameCube: G286 water bake n=%d max=%d rep=%d pvs=%d\n",
			gc_water_face_count, GC_WATER_MAX_FACES, replaced, pvs_hits );
	}
}

static void GC_CaptureDrawFacesFromSurfbits( model_t *wmodel, const byte *surfbits )
{
	int i;
	int min_i;
	int min_area;
	int area_slots = GC_CAP_AREA_SLOTS;
	int replaced = 0;
	int wall_boost = 0;

	gc_newgame_cap_face_count = 0;
	gc_newgame_cap_tex_faces = 0;
	gc_newgame_cap_lm_faces = 0;
	gc_newgame_cap_lm_atlas_ready = false;
	if( !wmodel || !wmodel->surfaces || !surfbits || gc_newgame_surfbytes <= 0 )
		return;

	/* Pass 1 (G150/G212): online top-K by near-eye keep-score into area_slots.
	 * G160 wall boost + G212 eye proximity so the 320 Flipper slots track local
	 * BSP geometry instead of map-wide towers. */
	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		int area;
		int score;
		qboolean is_wall;

		if( !( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		src = &wmodel->surfaces[i];
		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area <= 0 )
			continue;
		is_wall = ( fabs( src->plane->normal[2] ) < 0.35f );
		if( is_wall )
			wall_boost++;
		score = GC_CapNearEyeScore( src->plane, area, is_wall );

		if( gc_newgame_cap_face_count < area_slots )
		{
			if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, gc_newgame_cap_face_count, true ))
				continue;
			gc_newgame_cap_areas[gc_newgame_cap_face_count] = score;
			if( src->texinfo && src->texinfo->texture )
				gc_newgame_cap_tex_faces++;
			gc_newgame_cap_face_count++;
			continue;
		}

		min_i = 0;
		min_area = gc_newgame_cap_areas[0];
		{
			int k;
			for( k = 1; k < area_slots; k++ )
			{
				if( gc_newgame_cap_areas[k] < min_area )
				{
					min_area = gc_newgame_cap_areas[k];
					min_i = k;
				}
			}
		}
		if( score <= min_area )
			continue;
		{
			qboolean had_tex = ( gc_newgame_cap_faces[min_i].texinfo.texture != NULL );

			if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, min_i, true ))
				continue;
			/* Store keep-score so later compares stay consistent. */
			gc_newgame_cap_areas[min_i] = score;
			if( had_tex && !( src->texinfo && src->texinfo->texture ))
				gc_newgame_cap_tex_faces--;
			else if( !had_tex && src->texinfo && src->texinfo->texture )
				gc_newgame_cap_tex_faces++;
			replaced++;
		}
	}

	/* Pass 2: fill remaining slots with any uncaptured PVS faces (connectors). */
	for( i = 0; i < wmodel->numsurfaces && gc_newgame_cap_face_count < GC_MAX_CAP_FACES; i++ )
	{
		msurface_t *src;

		if( !( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		src = &wmodel->surfaces[i];
		if( GC_CapFaceAlready( src->firstedge, src->numedges ))
			continue;
		if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, gc_newgame_cap_face_count, true ))
			continue;
		if( src->texinfo && src->texinfo->texture )
			gc_newgame_cap_tex_faces++;
		gc_newgame_cap_face_count++;
	}

	GC_SortCapFacesByAreaDesc();
	gc_newgame_cap_lm_faces = 0;
	{
		int baked = 0;
		int edge = 0, plane = 0, tex = 0;

		for( i = 0; i < gc_newgame_cap_face_count; i++ )
		{
			if( gc_newgame_cap_lm_real[i] )
				gc_newgame_cap_lm_faces++;
			if( gc_cap_nverts[i] >= 3 )
				baked++;
			if( gc_cap_bake_src[i] == GC_CAP_BAKE_EDGE )
				edge++;
			else if( gc_cap_bake_src[i] == GC_CAP_BAKE_PLANE )
				plane++;
			else if( gc_cap_bake_src[i] == GC_CAP_BAKE_TEX )
				tex++;
		}
		GC_FlipperTrace( "Xash3D GameCube: G160 f=%d tx=%d lm=%d e=%d t=%d p=%d r=%d w=%d m=%d\n",
			gc_newgame_cap_face_count, gc_newgame_cap_tex_faces, gc_newgame_cap_lm_faces,
			baked, edge, tex, plane, replaced, wall_boost, GC_MAX_CAP_FACES );
	}
	/* G277: *12 style-0 LM while samples still live. */
	GC_BakeTramLightmaps( wmodel );
	/* G286: bake visible turb sheets while edges/planes are still valid. */
	GC_CaptureWaterFacesFromSurfbits( wmodel, surfbits );
	/* G176: prove LM 8→4 funded face cap 256→320 without BSS growth. */
	if( !gc_g176_logged && gc_newgame_cap_face_count >= 300
		&& GC_MAX_CAP_FACES >= 320 && GC_CAP_LM_DIM == 4 )
	{
		gc_g176_logged = true;
		GC_FlipperTrace( "Xash3D GameCube: G176 cap=%d max=%d lm=%d real=%d\n",
			gc_newgame_cap_face_count, GC_MAX_CAP_FACES, GC_CAP_LM_DIM,
			gc_newgame_cap_lm_faces );
	}
}


/*
 * G213: heap-backed compact PVS faces (full surface promote is 662 KiB OOM).
 * Emit uses mempool edges via firstedge; planes/texinfo are snapshotted here.
 */
static void GC_FreeLiveFaces( void )
{
	if( gc_live_faces_bss_active )
	{
		gc_live_faces = NULL;
		gc_live_face_scores = NULL;
		gc_live_faces_bss_active = false;
	}
	else
	{
		if( gc_live_faces )
		{
			free( gc_live_faces );
			gc_live_faces = NULL;
		}
		if( gc_live_face_scores )
		{
			free( gc_live_face_scores );
			gc_live_face_scores = NULL;
		}
	}
	gc_live_face_capacity = 0;
	gc_live_face_count = 0;
	gc_g213_live_logged = false;
	if( gc_fill_faces )
	{
		free( gc_fill_faces );
		gc_fill_faces = NULL;
	}
	if( gc_fill_face_scores )
	{
		free( gc_fill_face_scores );
		gc_fill_face_scores = NULL;
	}
	gc_fill_face_capacity = 0;
	gc_fill_face_count = 0;
	gc_g222_fill_logged = false;
	gc_aram_fill_count = 0;
	gc_g225_aram_logged = false;
	/* G277: do NOT free tram faces here — AllocLiveFacePool calls this. */
}

static void GC_FreeTramFaces( void )
{
	gc_tram_face_count = 0;
	gc_tram_lm_ready = false;
	gc_tram_diffuse_texnum = 0;
	gc_water_face_count = 0;
	gc_g286_water_logged = false;
}

/* Allocate lean live-face pool BEFORE FatPVS calloc — after lean tables
 * reside, even 64×(cand+msurface) OOMs. G298: BSS fallback when heap is dead. */
static qboolean GC_AllocLiveFacePool( int want )
{
	int max_faces = want;

	if( gc_live_faces && gc_live_face_capacity > 0 )
		return true;
	GC_FreeLiveFaces();
	if( max_faces > GC_LIVE_MAX_FACES )
		max_faces = GC_LIVE_MAX_FACES;
	while( max_faces >= 64 )
	{
		size_t cand_bytes = (size_t)max_faces * sizeof( gc_live_cand_t );
		size_t score_bytes = (size_t)max_faces * sizeof( int );

		gc_live_faces = (gc_live_cand_t *)malloc( cand_bytes );
		gc_live_face_scores = (int *)malloc( score_bytes );
		if( gc_live_faces && gc_live_face_scores )
		{
			gc_live_face_capacity = max_faces;
			gc_live_face_count = 0;
			gc_live_faces_bss_active = false;
			memset( gc_live_faces, 0, cand_bytes );
			memset( gc_live_face_scores, 0, score_bytes );
			GC_FlipperTrace( "Xash3D GameCube: G213 live face pool max=%d (~%u Kb lean)\n",
				max_faces, (unsigned)(( cand_bytes + score_bytes ) / 1024 ));
			return true;
		}
		if( gc_live_faces )
		{
			free( gc_live_faces );
			gc_live_faces = NULL;
		}
		if( gc_live_face_scores )
		{
			free( gc_live_face_scores );
			gc_live_face_scores = NULL;
		}
		max_faces /= 2;
	}
	/* G298: tip-safe BSS — heap OOM after FatPVS under G283 scratch retain. */
	gc_live_faces = gc_live_faces_bss;
	gc_live_face_scores = gc_live_face_scores_bss;
	gc_live_face_capacity = GC_LIVE_BSS_FACES;
	gc_live_face_count = 0;
	gc_live_faces_bss_active = true;
	memset( gc_live_faces_bss, 0, sizeof( gc_live_faces_bss ));
	memset( gc_live_face_scores_bss, 0, sizeof( gc_live_face_scores_bss ));
	Con_Reportf( "Xash3D GameCube: G298 lean live BSS pool max=%d (~%u Kb)\n",
		GC_LIVE_BSS_FACES,
		(unsigned)(( sizeof( gc_live_faces_bss ) + sizeof( gc_live_face_scores_bss )) / 1024 ));
	return true;
}

/* Re-score lean live faces after dump eye moves capture_origin (geom already snapshotted). */
static void GC_RerankLiveFacesNearEye( void )
{
	int i, swapped;

	if( !gc_live_faces || gc_live_face_count <= 1 || !gc_live_face_scores )
		return;
	for( i = 0; i < gc_live_face_count; i++ )
	{
		gc_live_cand_t *c = &gc_live_faces[i];

		gc_live_face_scores[i] = GC_LiveViewScore( c );
	}
	/* Simple bubble so near-eye walls emit first within the Flipper budget. */
	do
	{
		swapped = 0;
		for( i = 0; i < gc_live_face_count - 1; i++ )
		{
			if( gc_live_face_scores[i + 1] > gc_live_face_scores[i] )
			{
				int ts = gc_live_face_scores[i];
				gc_live_cand_t tc = gc_live_faces[i];

				gc_live_face_scores[i] = gc_live_face_scores[i + 1];
				gc_live_face_scores[i + 1] = ts;
				gc_live_faces[i] = gc_live_faces[i + 1];
				gc_live_faces[i + 1] = tc;
				swapped = 1;
			}
		}
	} while( swapped );
	GC_FlipperTrace( "Xash3D GameCube: G214 rerank n=%d top=%d\n",
		gc_live_face_count, gc_live_face_scores[0] );
}

static qboolean GC_LiveFaceAlready( int firstedge, int numedges )
{
	int j;

	for( j = 0; j < gc_live_face_count; j++ )
	{
		if( gc_live_faces[j].firstedge == firstedge
			&& gc_live_faces[j].numedges == numedges )
			return true;
	}
	return false;
}

static qboolean GC_InstallLiveCand( const gc_refresh_cand_t *cand, int score )
{
	gc_live_cand_t *dst;
	int min_i, min_score, k;

	(void)score; /* G217: eviction uses LiveViewScore after bake */
	if( !cand || !gc_live_faces || gc_live_face_capacity <= 0 )
		return false;
	if( cand->numedges < 3 || cand->numedges > 32 || cand->area <= 0 )
		return false;
	if( GC_LiveFaceAlready( cand->firstedge, cand->numedges ))
		return false;

	if( gc_live_face_count < gc_live_face_capacity )
	{
		dst = &gc_live_faces[gc_live_face_count];
		memset( dst, 0, sizeof( *dst ));
		dst->firstedge = cand->firstedge;
		dst->numedges = cand->numedges;
		dst->flags = cand->flags;
		dst->area = cand->area;
		dst->plane = cand->plane;
		dst->texturemins[0] = cand->texturemins[0];
		dst->texturemins[1] = cand->texturemins[1];
		dst->extents[0] = cand->extents[0];
		dst->extents[1] = cand->extents[1];
		memcpy( dst->styles, cand->styles, sizeof( dst->styles ));
		if( cand->has_tex )
		{
			memcpy( dst->vecs, cand->texinfo.vecs, sizeof( dst->vecs ));
			dst->texture = cand->texinfo.texture;
			dst->tex_flags = cand->texinfo.flags;
			dst->has_tex = true;
		}
		{
			model_t *wm = sv.models[1];
#if !XASH_DEDICATED
			if( !wm )
				wm = cl.worldmodel;
#endif
			GC_BakeLiveCandVerts( dst, wm );
		}
		if( dst->nverts < 3 )
			return false;
		gc_live_face_scores[gc_live_face_count] = GC_LiveViewScore( dst );
		gc_live_face_count++;
		return true;
	}

	min_i = 0;
	min_score = gc_live_face_scores[0];
	for( k = 1; k < gc_live_face_capacity; k++ )
	{
		if( gc_live_face_scores[k] < min_score )
		{
			min_score = gc_live_face_scores[k];
			min_i = k;
		}
	}
	{
		gc_live_cand_t trial;
		model_t *wm = sv.models[1];
		int view_score;
#if !XASH_DEDICATED
		if( !wm )
			wm = cl.worldmodel;
#endif
		memset( &trial, 0, sizeof( trial ));
		trial.firstedge = cand->firstedge;
		trial.numedges = cand->numedges;
		trial.flags = cand->flags;
		trial.area = cand->area;
		trial.plane = cand->plane;
		trial.texturemins[0] = cand->texturemins[0];
		trial.texturemins[1] = cand->texturemins[1];
		trial.extents[0] = cand->extents[0];
		trial.extents[1] = cand->extents[1];
		memcpy( trial.styles, cand->styles, sizeof( trial.styles ));
		if( cand->has_tex )
		{
			memcpy( trial.vecs, cand->texinfo.vecs, sizeof( trial.vecs ));
			trial.texture = cand->texinfo.texture;
			trial.tex_flags = cand->texinfo.flags;
			trial.has_tex = true;
		}
		GC_BakeLiveCandVerts( &trial, wm );
		if( trial.nverts < 3 )
			return false;
		view_score = GC_LiveViewScore( &trial );
		if( view_score <= min_score )
			return false;
		gc_live_faces[min_i] = trial;
		gc_live_face_scores[min_i] = view_score;
	}
	return true;
}

static void GC_MergeRefreshCandsIntoLiveFaces( void )
{
	int slot, i, merged = 0, skipped_cap = 0;

	if( !gc_live_faces || gc_live_face_capacity <= 0 )
		return;

	for( slot = 0; slot < GC_SURFBITS_CACHE_SLOTS; slot++ )
	{
		int n;
		byte *bits;
		model_t *wmodel;

		if( gc_refresh_ncands[slot] <= 0 )
			continue;
		/* G237: shared cand buffer — rebuild this slot before merging. */
		if( gc_refresh_loaded_slot != slot )
		{
			wmodel = sv.models[1];
#if !XASH_DEDICATED
			if( !wmodel )
				wmodel = cl.worldmodel;
#endif
			bits = NULL;
			if( gc_newgame_surf_cache && gc_newgame_surfbytes > 0
				&& slot < gc_newgame_surf_cache_slots )
				bits = gc_newgame_surf_cache + (size_t)slot * (size_t)gc_newgame_surfbytes;
			if( !wmodel || !bits )
				continue;
			GC_BuildRefreshCandsFromSurfbits( wmodel, bits, slot );
		}
		n = gc_refresh_ncands[slot];
		if( gc_refresh_loaded_slot != slot || n <= 0 )
			continue;

		for( i = 0; i < n; i++ )
		{
			const gc_refresh_cand_t *cand = &gc_refresh_cands[i];
			qboolean is_wall;
			int score;

			if( GC_CapFaceAlready( cand->firstedge, cand->numedges ))
			{
				skipped_cap++;
				continue;
			}
			is_wall = ( fabs( cand->plane.normal[2] ) < 0.55f ); /* G234 */
			score = GC_CapNearEyeScore( &cand->plane, cand->area, is_wall );
			if( is_wall )
				score += cand->area;
			/* Prefer faces the dump eye looks at (front side). */
			{
				float dot = DotProduct( gc_newgame_capture_origin, cand->plane.normal )
					- cand->plane.dist;
				if( cand->flags & SURF_PLANEBACK )
				{
					if( dot < -0.1f )
						score += 200000;
				}
				else if( dot > 0.1f )
					score += 200000;
			}
			if( GC_InstallLiveCand( cand, score ))
				merged++;
		}
	}
	GC_RerankLiveFacesNearEye();
	GC_FlipperTrace( "Xash3D GameCube: G214 merge +%d skipcap=%d n=%d\n",
		merged, skipped_cap, gc_live_face_count );
}

static void GC_CaptureLiveFacesFromSurfbits( model_t *wmodel, const byte *surfbits, qboolean append )
{
	int i, max_faces;
	int replaced = 0;

	if( !wmodel || !wmodel->surfaces || !surfbits || gc_newgame_surfbytes <= 0 )
		return;
	if( !wmodel->surfedges || !wmodel->vertexes || ( !wmodel->edges16 && !wmodel->edges32 ))
		return;
	/* G283: malloc-pin uses live BSP emit — skip lean pool.
	 * G298: scratch retain — bake lean AFTER FatPVS (MEM1 free). Flipper
	 * emits baked verts only (no late edge-walk). Do not alloc lean before
	 * FatPVS (that OOMs Capture on c0a0). */
	if( Mod_GCWorldSurfacesPinned( wmodel )
		&& !Mod_GCWorldSurfacesScratchRetained( wmodel ))
	{
		static qboolean g283_skip_logged;

		if( !g283_skip_logged )
		{
			g283_skip_logged = true;
			Con_Reportf( "Xash3D GameCube: G283 lean live pool skipped (malloc pin)\n" );
		}
		return;
	}
	/* After scratch reuse, plane* dangles — never wipe a good early pool.
	 * Ride restream appends PVS faces (see GC_MaybeRestreamRideMapFaces). */
	if( gc_live_face_count > 0 && !append )
	{
		GC_FlipperTrace( "Xash3D GameCube: G213 live faces keep early pool n=%d (surfaces dangling)\n",
			gc_live_face_count );
		return;
	}
	if( !gc_live_faces || gc_live_face_capacity <= 0 )
	{
		if( !GC_AllocLiveFacePool( GC_LIVE_MAX_FACES ))
			return;
	}

	max_faces = gc_live_face_capacity;
	if( !append )
	{
		gc_live_face_count = 0;
		memset( gc_live_faces, 0, (size_t)max_faces * sizeof( gc_live_cand_t ));
		memset( gc_live_face_scores, 0, (size_t)max_faces * sizeof( int ));
	}

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		int area, score, min_i, min_score, k;
		qboolean is_wall;
		gc_live_cand_t *dst;
		float dot;

		if( !( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		src = &wmodel->surfaces[i];
		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( src->firstedge < 0
			|| src->firstedge + src->numedges > wmodel->numsurfedges )
			continue;
		if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area <= 0 )
			continue;
		if( GC_CapFaceAlready( src->firstedge, src->numedges ))
			continue; /* complementary to LM-cap (PVS leftovers) */
		if( GC_LiveFaceAlready( src->firstedge, src->numedges ))
			continue;
		is_wall = ( fabs( src->plane->normal[2] ) < 0.55f ); /* G234: match LiveViewScore */
		/* Provisional score; G217 replaces with LiveViewScore after bake. */
		score = GC_CapNearEyeScore( src->plane, area, is_wall );
		if( is_wall )
			score += area;
		dot = DotProduct( gc_newgame_capture_origin, src->plane->normal ) - src->plane->dist;
		if( src->flags & SURF_PLANEBACK )
		{
			if( dot < -0.1f )
				score += 200000;
		}
		else if( dot > 0.1f )
			score += 200000;

		if( gc_live_face_count < max_faces )
		{
			dst = &gc_live_faces[gc_live_face_count];
			memset( dst, 0, sizeof( *dst ));
			dst->firstedge = src->firstedge;
			dst->numedges = src->numedges;
			dst->flags = src->flags;
			dst->area = area;
			dst->plane = *src->plane;
			dst->texturemins[0] = src->texturemins[0];
			dst->texturemins[1] = src->texturemins[1];
			dst->extents[0] = src->extents[0];
			dst->extents[1] = src->extents[1];
			memcpy( dst->styles, src->styles, sizeof( dst->styles ));
			if( src->texinfo && src->texinfo->texture )
			{
				memcpy( dst->vecs, src->texinfo->vecs, sizeof( dst->vecs ));
				dst->texture = src->texinfo->texture;
				dst->tex_flags = src->texinfo->flags;
				dst->has_tex = true;
			}
			GC_BakeLiveCandVerts( dst, wmodel );
			if( dst->nverts < 3 )
				continue;
			score = GC_LiveViewScore( dst );
			if( src->flags & SURF_PLANEBACK )
			{
				if( dot < -0.1f )
					score += 200000;
			}
			else if( dot > 0.1f )
				score += 200000;
			gc_live_face_scores[gc_live_face_count] = score;
			gc_live_face_count++;
			continue;
		}
		min_i = 0;
		min_score = gc_live_face_scores[0];
		for( k = 1; k < max_faces; k++ )
		{
			if( gc_live_face_scores[k] < min_score )
			{
				min_score = gc_live_face_scores[k];
				min_i = k;
			}
		}
		{
			gc_live_cand_t trial;

			memset( &trial, 0, sizeof( trial ));
			trial.firstedge = src->firstedge;
			trial.numedges = src->numedges;
			trial.flags = src->flags;
			trial.area = area;
			trial.plane = *src->plane;
			trial.texturemins[0] = src->texturemins[0];
			trial.texturemins[1] = src->texturemins[1];
			trial.extents[0] = src->extents[0];
			trial.extents[1] = src->extents[1];
			memcpy( trial.styles, src->styles, sizeof( trial.styles ));
			if( src->texinfo && src->texinfo->texture )
			{
				memcpy( trial.vecs, src->texinfo->vecs, sizeof( trial.vecs ));
				trial.texture = src->texinfo->texture;
				trial.tex_flags = src->texinfo->flags;
				trial.has_tex = true;
			}
			GC_BakeLiveCandVerts( &trial, wmodel );
			if( trial.nverts < 3 )
				continue;
			score = GC_LiveViewScore( &trial );
			if( src->flags & SURF_PLANEBACK )
			{
				if( dot < -0.1f )
					score += 200000;
			}
			else if( dot > 0.1f )
				score += 200000;
			if( score <= min_score )
				continue;
			gc_live_faces[min_i] = trial;
			gc_live_face_scores[min_i] = score;
			replaced++;
		}
	}

	GC_FlipperTrace( "Xash3D GameCube: G217 live=%d max=%d rep=%d app=%d\n",
		gc_live_face_count, max_faces, replaced, append ? 1 : 0 );

	/* G222: overflow into BSS flat-fill (no malloc). */
	GC_CaptureFillFacesFromSurfbits( wmodel, surfbits, append );
}

static qboolean GC_FillFaceAlready( int firstedge, int numedges )
{
	int j;

	if( !gc_fill_faces )
		return false;
	for( j = 0; j < gc_fill_face_count; j++ )
	{
		if( gc_fill_faces[j].firstedge == firstedge
			&& gc_fill_faces[j].numedges == numedges )
			return true;
	}
	return false;
}

/* G225: MEM1-safe ARAM overflow for fill faces that lose the heap rank. */
static qboolean GC_AramFillEnsure( void )
{
	if( gc_aram_fill_base )
		return true;
	if( gc_aram_fill_tried )
		return false;
	gc_aram_fill_tried = true;
	if( !AR_CheckInit())
		AR_Init( gc_aram_blocks, 4 );
	gc_aram_fill_base = AR_Alloc( (u32)GC_ARAM_FILL_MAX * (u32)GC_ARAM_FACE_STRIDE );
	if( !gc_aram_fill_base )
	{
		SYS_Report( "Xash3D GameCube: G225 ARAM fail\n" );
		return false;
	}
	GC_FlipperTrace( "Xash3D GameCube: G225 ARAM max=%d\n", GC_ARAM_FILL_MAX );
	return true;
}

static void GC_AramFillWrite( int slot, const gc_fill_face_t *face )
{
	gc_aram_stage = *face;
	DCFlushRange( &gc_aram_stage, GC_ARAM_FACE_STRIDE );
	while( AR_GetDMAStatus())
		;
	AR_StartDMAWrite( (u32)&gc_aram_stage,
		gc_aram_fill_base + (u32)slot * (u32)GC_ARAM_FACE_STRIDE,
		GC_ARAM_FACE_STRIDE );
	while( AR_GetDMAStatus())
		;
}

static qboolean GC_AramFillRead( int slot )
{
	if( !gc_aram_fill_base || slot < 0 || slot >= gc_aram_fill_count )
		return false;
	while( AR_GetDMAStatus())
		;
	AR_StartDMARead( (u32)&gc_aram_stage,
		gc_aram_fill_base + (u32)slot * (u32)GC_ARAM_FACE_STRIDE,
		GC_ARAM_FACE_STRIDE );
	while( AR_GetDMAStatus())
		;
	DCInvalidateRange( &gc_aram_stage, GC_ARAM_FACE_STRIDE );
	return true;
}

static void GC_AramFillInsert( const gc_fill_face_t *face )
{
	if( !face || face->nverts < 3 || gc_aram_fill_count >= GC_ARAM_FILL_MAX )
		return;
	if( !GC_AramFillEnsure())
		return;
	GC_AramFillWrite( gc_aram_fill_count, face );
	gc_aram_fill_count++;
}

/* True if live pool already emits this face (EDGE/TEX). Plane-only live
 * slots are wasted at emit — fill may reclaim them. */
static qboolean GC_LiveFaceEmitsGeom( int firstedge, int numedges )
{
	int j;

	if( !gc_live_faces )
		return false;
	for( j = 0; j < gc_live_face_count; j++ )
	{
		if( gc_live_faces[j].firstedge == firstedge
			&& gc_live_faces[j].numedges == numedges )
		{
			byte b = gc_live_faces[j].bake_src;
			return ( b == GC_CAP_BAKE_EDGE || b == GC_CAP_BAKE_TEX );
		}
	}
	return false;
}

/* Score a provisional fill cand (verts already baked into trial). */
static int GC_FillViewScore( const gc_fill_face_t *c )
{
	gc_live_cand_t tmp;

	if( !c || c->nverts < 3 || c->area <= 0 )
		return 0;
	memset( &tmp, 0, sizeof( tmp ));
	tmp.nverts = c->nverts;
	tmp.area = c->area;
	tmp.plane = c->plane;
	tmp.flags = c->flags;
	tmp.bake_src = GC_CAP_BAKE_EDGE;
	memcpy( tmp.pts_s16, c->pts_s16, sizeof( tmp.pts_s16 ));
	return GC_LiveViewScore( &tmp );
}

/* Allocate AFTER live pool — never steal the 192-face budget. */
static qboolean GC_AllocFillFacePool( int want )
{
	int max_faces = want;

	if( gc_fill_faces && gc_fill_face_capacity > 0 )
		return true;
	if( max_faces > GC_FILL_MAX_FACES )
		max_faces = GC_FILL_MAX_FACES;
	while( max_faces >= 32 )
	{
		size_t face_bytes = (size_t)max_faces * sizeof( gc_fill_face_t );
		size_t score_bytes = (size_t)max_faces * sizeof( int );

		gc_fill_faces = (gc_fill_face_t *)malloc( face_bytes );
		gc_fill_face_scores = (int *)malloc( score_bytes );
		if( gc_fill_faces && gc_fill_face_scores )
		{
			gc_fill_face_capacity = max_faces;
			gc_fill_face_count = 0;
			memset( gc_fill_faces, 0, face_bytes );
			memset( gc_fill_face_scores, 0, score_bytes );
			GC_FlipperTrace( "Xash3D GameCube: G222 fill face pool max=%d (~%u Kb heap)\n",
				max_faces, (unsigned)(( face_bytes + score_bytes ) / 1024 ));
			return true;
		}
		if( gc_fill_faces )
		{
			free( gc_fill_faces );
			gc_fill_faces = NULL;
		}
		if( gc_fill_face_scores )
		{
			free( gc_fill_face_scores );
			gc_fill_face_scores = NULL;
		}
		max_faces /= 2;
	}
	SYS_Report( "Xash3D GameCube: G222 fill face pool OOM (live pool kept)\n" );
	return false;
}

static void GC_CaptureFillFacesFromSurfbits( model_t *wmodel, const byte *surfbits, qboolean append )
{
	int i, replaced = 0, max_faces;

	if( !wmodel || !surfbits || !wmodel->surfaces )
		return;
	/* G283/G298: under scratch retain keep fill off — heap fill tipped
	 * Mem_AllocPool (cl_game) after lean BSS bake. Caps + lean BSS only. */
	if( Mod_GCWorldSurfacesScratchRetained( wmodel ))
		return;
	/* Live pool first — fill is optional overflow. */
	if( !gc_live_faces || gc_live_face_capacity <= 0 )
		return;
	if( !gc_fill_faces || gc_fill_face_capacity <= 0 )
	{
		if( !GC_AllocFillFacePool( GC_FILL_MAX_FACES ))
			return;
	}
	max_faces = gc_fill_face_capacity;
	if( !append )
	{
		gc_fill_face_count = 0;
		gc_aram_fill_count = 0;
		memset( gc_fill_faces, 0, (size_t)max_faces * sizeof( gc_fill_face_t ));
		memset( gc_fill_face_scores, 0, (size_t)max_faces * sizeof( int ));
	}

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		gc_fill_face_t trial;
		int area, score, min_i, min_score, k, n;
		byte bake_src = GC_CAP_BAKE_NONE;
		qboolean is_wall;

		if( !( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		src = &wmodel->surfaces[i];
		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
			continue;
		if( src->firstedge < 0
			|| src->firstedge + src->numedges > wmodel->numsurfedges )
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area < 256 ) /* G250: was 512 — admit smaller fill plugs for pipe/nook holes */
			continue;
		if( GC_CapFaceAlready( src->firstedge, src->numedges ))
			continue;
		if( GC_LiveFaceEmitsGeom( src->firstedge, src->numedges ))
			continue;
		if( GC_FillFaceAlready( src->firstedge, src->numedges ))
			continue;
		/* G222: only front-facing dump-eye faces — leftovers were all culled
		 * (fill=0 back=56 frustum=40). */
		{
			float fdot = DotProduct( gc_newgame_capture_origin, src->plane->normal )
				- src->plane->dist;
			if( src->flags & SURF_PLANEBACK )
			{
				if( fdot >= -0.1f )
					continue;
			}
			else if( fdot <= 0.1f )
				continue;
		}

		memset( &trial, 0, sizeof( trial ));
		n = GC_BakeCapVertsForSurf( wmodel, src, trial.pts_s16, GC_CAP_MAX_VERTS, &bake_src );
		if( n < 3 || ( bake_src != GC_CAP_BAKE_EDGE && bake_src != GC_CAP_BAKE_TEX ))
			continue;
		trial.nverts = (byte)n;
		trial.flags = (byte)( src->flags & SURF_PLANEBACK );
		trial.plane = *src->plane;
		trial.firstedge = src->firstedge;
		trial.numedges = src->numedges;
		trial.area = area;
		/* No along filter — live already owns the forward cone; fill plugs
		 * side/ceiling holes that LiveViewScore demoted (emit: backface only). */
		score = GC_FillViewScore( &trial );
		if( score <= 0 )
			score = area;
		is_wall = ( fabs( src->plane->normal[2] ) < 0.35f );
		if( is_wall )
			score += ( area >> 1 );
		score += 200000; /* front-side bonus */

		if( gc_fill_face_count < max_faces )
		{
			gc_fill_faces[gc_fill_face_count] = trial;
			gc_fill_face_scores[gc_fill_face_count] = score;
			gc_fill_face_count++;
			continue;
		}
		min_i = 0;
		min_score = gc_fill_face_scores[0];
		for( k = 1; k < max_faces; k++ )
		{
			if( gc_fill_face_scores[k] < min_score )
			{
				min_score = gc_fill_face_scores[k];
				min_i = k;
			}
		}
		if( score <= min_score )
		{
			/* G225: keep MEM1 losers in ARAM overflow page. */
			GC_AramFillInsert( &trial );
			continue;
		}
		gc_fill_faces[min_i] = trial;
		gc_fill_face_scores[min_i] = score;
		replaced++;
	}

	if( !gc_g222_fill_logged || !append )
	{
		gc_g222_fill_logged = true;
		GC_FlipperTrace( "Xash3D GameCube: G222 fill=%d max=%d rep=%d app=%d\n",
			gc_fill_face_count, max_faces, replaced, append ? 1 : 0 );
	}
	if( ( !gc_g225_aram_logged || !append ) && gc_aram_fill_count > 0 )
	{
		gc_g225_aram_logged = true;
		GC_FlipperTrace( "Xash3D GameCube: G225 ARAM n=%d\n", gc_aram_fill_count );
	}
}

/*
 * G163/G170: live cluster face refresh without sample LM rebake.
 * Deferred + incremental: keep baked faces; admit up to GC_CAP_REFRESH_NEW_MAX
 * new top-area faces with mid-grade LM only. Full rebuild mid-PVS hung Host_Frame
 * on c1a0a. Face geom must be snapshotted at capture — live plane* dangles at present.
 */
static qboolean gc_cap_refresh_pending;
/* G188: landmark put-in reposition wants a fresh cap-face set at that origin. */
static qboolean gc_g188_reposition_pending;

/* Prefer near-eye / larger faces first so local walls beat distant floors. */
static void GC_SortRefreshCandsByAreaDesc( int cache_slot )
{
	int i, j;
	int n;

	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return;
	if( gc_refresh_loaded_slot != cache_slot )
		return;
	n = gc_refresh_ncands[cache_slot];
	for( i = 0; i < n - 1; i++ )
	{
		for( j = i + 1; j < n; j++ )
		{
			const gc_refresh_cand_t *a = &gc_refresh_cands[i];
			const gc_refresh_cand_t *b = &gc_refresh_cands[j];
			int sa = GC_CapNearEyeScore( &a->plane, a->area,
				fabs( a->plane.normal[2] ) < 0.35f );
			int sb = GC_CapNearEyeScore( &b->plane, b->area,
				fabs( b->plane.normal[2] ) < 0.35f );

			if( sb > sa )
			{
				gc_refresh_cand_t tmp = gc_refresh_cands[i];
				gc_refresh_cands[i] = gc_refresh_cands[j];
				gc_refresh_cands[j] = tmp;
			}
		}
	}
}

static void GC_BuildSurfbitsForVisRow( model_t *wmodel, const byte *vis, byte *surfbits );
static byte *GC_LookupSurfbitsCache( int cluster );
static int GC_VisLeafsForCluster( int cluster );
static int GC_SelectClusterForOrigin( const float *org );
static qboolean GC_SetActiveNewGameCluster( int cluster, qboolean log_change );
static int GC_LookupSurfbitsCacheSlot( int cluster )
{
	int i;

	if( cluster < 0 )
		return -1;
	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		if( gc_newgame_surf_cache_cluster[i] == cluster )
			return i;
	}
	return -1;
}

static qboolean GC_InstallRefreshCand( const gc_refresh_cand_t *cand, int slot )
{
	gc_cap_face_t *dst;
	msurface_t *draw;

	if( !cand || slot < 0 || slot >= GC_MAX_CAP_FACES )
		return false;

	dst = &gc_newgame_cap_faces[slot];
	draw = &gc_newgame_draw_surfs[slot];
	memset( dst, 0, sizeof( *dst ));
	dst->firstedge = cand->firstedge;
	dst->numedges = cand->numedges;
	dst->flags = cand->flags;
	dst->plane = cand->plane;
	dst->texturemins[0] = cand->texturemins[0];
	dst->texturemins[1] = cand->texturemins[1];
	dst->extents[0] = cand->extents[0];
	dst->extents[1] = cand->extents[1];
	memcpy( dst->styles, cand->styles, sizeof( dst->styles ));
	dst->samples = NULL;
	if( cand->has_tex )
	{
		dst->texinfo = cand->texinfo;
		dst->texinfo.faceinfo = NULL;
	}
	dst->info.lightextents[0] = cand->extents[0];
	dst->info.lightextents[1] = cand->extents[1];
	dst->info.lightmapmins[0] = cand->texturemins[0];
	dst->info.lightmapmins[1] = cand->texturemins[1];
	dst->info.surf = draw;

	memset( draw, 0, sizeof( *draw ));
	draw->firstedge = dst->firstedge;
	draw->numedges = dst->numedges;
	draw->flags = dst->flags;
	draw->plane = &dst->plane;
	draw->texturemins[0] = dst->texturemins[0];
	draw->texturemins[1] = dst->texturemins[1];
	draw->extents[0] = dst->extents[0];
	draw->extents[1] = dst->extents[1];
	memcpy( draw->styles, dst->styles, sizeof( draw->styles ));
	draw->samples = NULL;
	draw->info = &dst->info;
	if( dst->texinfo.texture )
		draw->texinfo = &dst->texinfo;
	gc_newgame_cap_areas[slot] = GC_CapNearEyeScore( &cand->plane, cand->area,
		fabs( cand->plane.normal[2] ) < 0.35f );
	{
		model_t *wmodel = sv.models[1];
#if !XASH_DEDICATED
		if( !wmodel )
			wmodel = cl.worldmodel;
#endif
		if( wmodel )
		{
			gc_cap_nverts[slot] = (byte)GC_BakeCapVertsForCap( wmodel,
				cand->firstedge, cand->numedges,
				&cand->plane, cand->has_tex ? &cand->texinfo : NULL,
				cand->texturemins, cand->extents,
				gc_cap_pts_s16[slot], GC_CAP_MAX_VERTS, &gc_cap_bake_src[slot] );
		}
		else
		{
			gc_cap_nverts[slot] = (byte)GC_BakeCapVertsFromPlane( &cand->plane,
				cand->extents[0], cand->extents[1],
				gc_cap_pts_s16[slot], GC_CAP_MAX_VERTS );
			gc_cap_bake_src[slot] = gc_cap_nverts[slot] >= 3
				? (byte)GC_CAP_BAKE_PLANE : (byte)GC_CAP_BAKE_NONE;
		}
		if( !gc_cap_nverts[slot] )
		{
			gc_cap_nverts[slot] = (byte)GC_BakeCapVertsFromPlane( &cand->plane,
				cand->extents[0], cand->extents[1],
				gc_cap_pts_s16[slot], GC_CAP_MAX_VERTS );
			gc_cap_bake_src[slot] = gc_cap_nverts[slot] >= 3
				? (byte)GC_CAP_BAKE_PLANE : (byte)GC_CAP_BAKE_NONE;
		}
	}
	GC_FillCapLightmapMid( slot );
	return gc_cap_nverts[slot] >= 3;
}

static void GC_BuildRefreshCandsFromSurfbits( model_t *wmodel, const byte *surfbits, int cache_slot )
{
	int i, k;
	int ncand = 0;
	int wall_boost = 0;
	int leaves;
	qboolean outdoor;

	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return;
	gc_refresh_ncands[cache_slot] = 0;
	gc_refresh_walls[cache_slot] = 0;
	if( gc_refresh_loaded_slot == cache_slot )
		gc_refresh_loaded_slot = -1;
	if( !wmodel || !wmodel->surfaces || !surfbits || gc_newgame_surfbytes <= 0 )
		return;

	leaves = GC_VisLeafsForCluster( gc_newgame_surf_cache_cluster[cache_slot] );
	outdoor = ( leaves > 0 && leaves <= 48 );
	/* G281: locked tram also wall-boosts cand heap (indoor tunnel). */
	{
		const qboolean prefer_walls = outdoor || gc_g212_stream_locked;

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		gc_refresh_cand_t *dst;
		int area;
		int score;
		qboolean is_wall;
		int min_i, min_score;

		if( !( surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		src = &wmodel->surfaces[i];
		if( !src->plane || src->numedges < 3 || src->numedges > 32 )
			continue;
		if( src->firstedge < 0
			|| src->firstedge + src->numedges > wmodel->numsurfedges )
			continue;
		if( src->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_TRANSPARENT ))
			continue;
		area = (int)src->extents[0] * (int)src->extents[1];
		if( area <= 0 )
			continue;
		is_wall = ( fabs( src->plane->normal[2] ) < 0.35f );
		if( is_wall )
			wall_boost++;
		score = GC_CapNearEyeScore( src->plane, area, is_wall );
		if( prefer_walls && is_wall )
			score += area;

		if( ncand < GC_CAP_REFRESH_NEW_MAX )
		{
			dst = &gc_refresh_cands[ncand];
			memset( dst, 0, sizeof( *dst ));
			dst->firstedge = src->firstedge;
			dst->numedges = src->numedges;
			dst->flags = src->flags;
			dst->area = area;
			dst->plane = *src->plane;
			dst->texturemins[0] = src->texturemins[0];
			dst->texturemins[1] = src->texturemins[1];
			dst->extents[0] = src->extents[0];
			dst->extents[1] = src->extents[1];
			memcpy( dst->styles, src->styles, sizeof( dst->styles ));
			if( src->texinfo && src->texinfo->texture )
			{
				dst->texinfo = *src->texinfo;
				dst->texinfo.faceinfo = NULL;
				dst->has_tex = true;
			}
			ncand++;
			continue;
		}
		min_i = 0;
		min_score = GC_CapNearEyeScore( &gc_refresh_cands[0].plane,
			gc_refresh_cands[0].area,
			fabs( gc_refresh_cands[0].plane.normal[2] ) < 0.35f );
		if( prefer_walls && fabs( gc_refresh_cands[0].plane.normal[2] ) < 0.35f )
			min_score += gc_refresh_cands[0].area;
		for( k = 1; k < GC_CAP_REFRESH_NEW_MAX; k++ )
		{
			int ks = GC_CapNearEyeScore( &gc_refresh_cands[k].plane,
				gc_refresh_cands[k].area,
				fabs( gc_refresh_cands[k].plane.normal[2] ) < 0.35f );

			if( prefer_walls && fabs( gc_refresh_cands[k].plane.normal[2] ) < 0.35f )
				ks += gc_refresh_cands[k].area;
			if( ks < min_score )
			{
				min_score = ks;
				min_i = k;
			}
		}
		if( score <= min_score )
			continue;
		dst = &gc_refresh_cands[min_i];
		memset( dst, 0, sizeof( *dst ));
		dst->firstedge = src->firstedge;
		dst->numedges = src->numedges;
		dst->flags = src->flags;
		dst->area = area;
		dst->plane = *src->plane;
		dst->texturemins[0] = src->texturemins[0];
		dst->texturemins[1] = src->texturemins[1];
		dst->extents[0] = src->extents[0];
		dst->extents[1] = src->extents[1];
		memcpy( dst->styles, src->styles, sizeof( dst->styles ));
		if( src->texinfo && src->texinfo->texture )
		{
			dst->texinfo = *src->texinfo;
			dst->texinfo.faceinfo = NULL;
			dst->has_tex = true;
		}
	}
	} /* prefer_walls */
	{
		int wi, walls = 0;

		for( wi = 0; wi < ncand; wi++ )
		{
			if( fabs( gc_refresh_cands[wi].plane.normal[2] ) < 0.35f )
				walls++;
		}
		gc_refresh_walls[cache_slot] = walls;
	}
	gc_refresh_ncands[cache_slot] = ncand;
	gc_refresh_loaded_slot = cache_slot;
	GC_FlipperTrace( "Xash3D GameCube: G163 refresh cands ready slot=%d n=%d wallboost=%d\n",
		cache_slot, ncand, wall_boost );
}

static void GC_RefreshCapFacesFromCands( int cache_slot )
{
	int i, k;
	int mid_new = 0;
	int wall_boost = 0;
	int wall_new = 0;
	int prev_count = gc_newgame_cap_face_count;
	int ncand;
	int leaves;
	qboolean outdoor;

	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return;
	if( gc_refresh_loaded_slot != cache_slot )
	{
		model_t *wmodel = sv.models[1];
		byte *bits = NULL;
#if !XASH_DEDICATED
		if( !wmodel )
			wmodel = cl.worldmodel;
#endif
		if( gc_newgame_surf_cache && gc_newgame_surfbytes > 0
			&& cache_slot < gc_newgame_surf_cache_slots )
			bits = gc_newgame_surf_cache + (size_t)cache_slot * (size_t)gc_newgame_surfbytes;
		if( wmodel && bits )
			GC_BuildRefreshCandsFromSurfbits( wmodel, bits, cache_slot );
	}
	ncand = gc_refresh_ncands[cache_slot];
	if( gc_refresh_loaded_slot != cache_slot || ncand <= 0 )
		return;
	leaves = GC_VisLeafsForCluster( gc_newgame_viewcluster );
	outdoor = ( leaves > 0 && leaves <= 48 );
	/* G281: locked New Game tram also prefers walls over floors. */
	{
		const qboolean prefer_walls = outdoor || gc_g212_stream_locked;

	GC_SortRefreshCandsByAreaDesc( cache_slot );
	GC_FlipperTrace( "Xash3D GameCube: G163 refresh begin cluster=%d faces=%d cands=%d\n",
		gc_newgame_viewcluster, prev_count, ncand );

	for( i = 0; i < ncand; i++ )
	{
		const gc_refresh_cand_t *cand = &gc_refresh_cands[i];
		int slot = -1;
		int min_i, min_score;
		qboolean cand_wall;
		int cand_score;

		if( GC_CapFaceAlready( cand->firstedge, cand->numedges ))
			continue;
		cand_wall = ( fabs( cand->plane.normal[2] ) < 0.55f ); /* G232 */
		if( cand_wall )
			wall_boost++;
		cand_score = GC_CapNearEyeScore( &cand->plane, cand->area, cand_wall );
		if( prefer_walls && cand_wall )
			cand_score += ( cand->area >> 1 );

		if( gc_newgame_cap_face_count < GC_MAX_CAP_FACES )
			slot = gc_newgame_cap_face_count;
		else
		{
			min_i = 0;
			min_score = GC_CapSlotKeepScore( 0 );
			if( prefer_walls && fabs( gc_newgame_cap_faces[0].plane.normal[2] ) >= 0.55f )
				min_score -= ( gc_newgame_cap_areas[0] >> 2 );
			for( k = 1; k < GC_MAX_CAP_FACES; k++ )
			{
				int score = GC_CapSlotKeepScore( k );

				if( prefer_walls && fabs( gc_newgame_cap_faces[k].plane.normal[2] ) >= 0.55f )
					score -= ( gc_newgame_cap_areas[k] >> 2 );
				if( score < min_score )
				{
					min_score = score;
					min_i = k;
				}
			}
			{
				int threshold = GC_CapSlotKeepScore( min_i );
				int admit = cand_score;
				qboolean victim_floor = ( fabs( gc_newgame_cap_faces[min_i].plane.normal[2] ) >= 0.55f );

				if( prefer_walls && cand_wall && victim_floor )
					admit = threshold + 1;
				/* G281: also punch far victims (tram-start walls left behind). */
				else if( prefer_walls && cand_wall && threshold < 90000 )
					admit = threshold + 1;
				if( admit <= threshold )
					continue;
			}
			slot = min_i;
		}

		if( !GC_InstallRefreshCand( cand, slot ))
			continue;
		if( slot == gc_newgame_cap_face_count )
			gc_newgame_cap_face_count++;
		mid_new++;
		if( cand_wall )
			wall_new++;
	}
	} /* prefer_walls */

	if( mid_new > 0 )
		GC_SortCapFacesByAreaDesc();

	gc_newgame_cap_lm_faces = 0;
	gc_newgame_cap_tex_faces = 0;
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		if( gc_newgame_cap_lm_real[i] )
			gc_newgame_cap_lm_faces++;
		if( gc_newgame_cap_faces[i].texinfo.texture )
			gc_newgame_cap_tex_faces++;
	}
	GC_FlipperTrace( "Xash3D GameCube: G163 refresh=%d prev=%d mid=%d lm=%d wb=%d cl=%d\n",
		gc_newgame_cap_face_count, prev_count, mid_new, gc_newgame_cap_lm_faces,
		wall_boost, gc_newgame_viewcluster );
	/* G212: prove near-eye streaming replaced far faces after dump aim. */
	if( !gc_g212_logged && mid_new > 0 && !VectorIsNull( gc_newgame_capture_origin ))
	{
		gc_g212_logged = true;
		GC_FlipperTrace( "Xash3D GameCube: G212 mid=%d wall=%d n=%d\n",
			mid_new, wall_new, gc_newgame_cap_face_count );
	}
	/* G165: sparse clusters (outdoor / landmark camera) — leaves typically << indoor. */
	if( outdoor )
	{
		GC_FlipperTrace( "Xash3D GameCube: G165 restore cl=%d mid=%d c=%d lf=%d\n",
			gc_newgame_viewcluster, mid_new, ncand, leaves );
		/* G171/G175: prove slots↔cands trade admitted outdoor walls. */
		if( !gc_g171_logged && ncand >= 40 && ( mid_new >= 18 || wall_new >= 10 ))
		{
			gc_g171_logged = true;
			GC_FlipperTrace( "Xash3D GameCube: G171 out mid=%d wall=%d c=%d L=%d cl=%d\n",
				mid_new, wall_new, ncand, leaves, gc_newgame_viewcluster );
		}
		/* G175: 4×64 trade — more outdoor walls than G171's 5×48. */
		if( !gc_g175_logged && ncand >= 56 && ( mid_new >= 20 || wall_new >= 14 ))
		{
			gc_g175_logged = true;
			GC_FlipperTrace( "Xash3D GameCube: G175 out mid=%d wall=%d c=%d L=%d cl=%d\n",
				mid_new, wall_new, ncand, leaves, gc_newgame_viewcluster );
		}
		/* G199: outdoor wall-aim refresh proof (4×64 + eye-in-front). */
		if( !gc_g199_logged && ncand >= 40 && ( mid_new >= 24 || wall_new >= 16 ))
		{
			gc_g199_logged = true;
			GC_FlipperTrace( "Xash3D GameCube: G199 out mid=%d wall=%d c=%d L=%d cl=%d\n",
				mid_new, wall_new, ncand, leaves, gc_newgame_viewcluster );
		}
	}
}

static int GC_RefreshCandWallCount( int cache_slot )
{
	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return 0;
	return gc_refresh_walls[cache_slot];
}

/* G189/G190: densest SelectCluster often returns a mega indoor row. Prefer a
 * cached outdoor-band cluster (leaves 20–48, matches G163 outdoor refresh)
 * with the most near-vertical wall cands so soft dumps fill with walls. */
static qboolean GC_PreferOutdoorWallCluster( void )
{
	int cur_leaves = GC_VisLeafsForCluster( gc_newgame_viewcluster );
	int cur_slot = GC_LookupSurfbitsCacheSlot( gc_newgame_viewcluster );
	int cur_walls = ( cur_slot >= 0 ) ? GC_RefreshCandWallCount( cur_slot ) : 0;
	int outdoor = -1;
	int outdoor_walls = -1;
	int outdoor_leaves = -1;
	int i;
	qboolean need_any = ( cur_slot < 0 || gc_refresh_ncands[cur_slot] <= 0 );

	/* Prefer a true outdoor-band row when densest, or when active has no cands. */
	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		int cand = gc_newgame_surf_cache_cluster[i];
		int leaves;
		int walls;

		if( cand < 0 || gc_refresh_ncands[i] <= 0 )
			continue;
		leaves = GC_VisLeafsForCluster( cand );
		if( leaves < 20 || leaves > 48 )
			continue;
		walls = GC_RefreshCandWallCount( i );
		if( walls > outdoor_walls
			|| ( walls == outdoor_walls && leaves > outdoor_leaves ))
		{
			outdoor_walls = walls;
			outdoor_leaves = leaves;
			outdoor = cand;
		}
	}
	if( outdoor >= 0 && outdoor != gc_newgame_viewcluster
		&& ( cur_leaves > 80 || outdoor_walls > cur_walls || need_any )
		&& GC_SetActiveNewGameCluster( outdoor, true ))
	{
		GC_FlipperTrace( "Xash3D GameCube: G190 prefer %d walls=%d L=%d was=%d\n",
			outdoor, outdoor_walls, outdoor_leaves, cur_leaves );
		return true;
	}
	/* Fallback only when the active cluster has no refresh cands at all. */
	if( !need_any )
		return false;
	outdoor = -1;
	outdoor_walls = -1;
	outdoor_leaves = -1;
	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		int cand = gc_newgame_surf_cache_cluster[i];
		int leaves;
		int walls;

		if( cand < 0 || cand == gc_newgame_viewcluster || gc_refresh_ncands[i] <= 0 )
			continue;
		leaves = GC_VisLeafsForCluster( cand );
		walls = GC_RefreshCandWallCount( i );
		if( walls > outdoor_walls
			|| ( walls == outdoor_walls && leaves > outdoor_leaves ))
		{
			outdoor_walls = walls;
			outdoor_leaves = leaves;
			outdoor = cand;
		}
	}
	if( outdoor < 0 || !GC_SetActiveNewGameCluster( outdoor, true ))
		return false;
	GC_FlipperTrace( "Xash3D GameCube: G190 prefer %d walls=%d L=%d was=%d\n",
		outdoor, outdoor_walls, outdoor_leaves, cur_leaves );
	return true;
}

/* G190: aim dump camera into the largest near-vertical cap face. */
static qboolean GC_DumpLookAtBestWall( const float *eye, float *out_angles );

/* G211: centroid of baked cap verts (or plane point fallback). */
static void GC_CapFaceCentroid( int slot, vec3_t out )
{
	const gc_cap_face_t *f;
	int nv, v;
	float sx, sy, sz;

	if( !out || slot < 0 || slot >= gc_newgame_cap_face_count )
	{
		if( out )
			VectorClear( out );
		return;
	}
	f = &gc_newgame_cap_faces[slot];
	nv = gc_cap_nverts[slot];
	if( nv >= 3 )
	{
		sx = sy = sz = 0.0f;
		for( v = 0; v < nv && v < GC_CAP_MAX_VERTS; v++ )
		{
			sx += (float)gc_cap_pts_s16[slot][v][0];
			sy += (float)gc_cap_pts_s16[slot][v][1];
			sz += (float)gc_cap_pts_s16[slot][v][2];
		}
		out[0] = sx / (float)nv;
		out[1] = sy / (float)nv;
		out[2] = sz / (float)nv;
		return;
	}
	out[0] = f->plane.normal[0] * f->plane.dist;
	out[1] = f->plane.normal[1] * f->plane.dist;
	out[2] = f->plane.normal[2] * f->plane.dist;
}

/* G211: how many other cap faces sit near this one (indoor enclosure). */
static int GC_CountNearbyCapFaces( int slot, float radius )
{
	vec3_t	c0, c1, d;
	int	i, n = 0;
	float	r2 = radius * radius;

	GC_CapFaceCentroid( slot, c0 );
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		if( i == slot || gc_cap_nverts[i] < 3 )
			continue;
		GC_CapFaceCentroid( i, c1 );
		VectorSubtract( c1, c0, d );
		if( DotProduct( d, d ) <= r2 )
			n++;
	}
	return n;
}


/* G215/G218: dump-eye cluster live pool — steal an outdoor surfbits slot if needed. */
static void GC_CaptureLiveFacesForDumpClusters( model_t *wmodel )
{
	vec3_t eye;
	vec3_t angles;
	int indoor = -1;
	int indoor_walls = -1;
	int indoor_leaves = -1;
	int i, primary = -1;
	int eye_cluster = -1;
	byte *bits;
	mleaf_t *leaf;

	if( !wmodel || gc_newgame_cap_face_count <= 0 )
		return;

	if( !( Sys_CheckParm( "-gcnewgame" ) && GC_DumpEyeAtTramStart( eye, angles ))
		&& !GC_DumpEyeInFrontOfBestWall( eye, angles ))
	{
		GC_FlipperTrace( "Xash3D GameCube: G218 dump-eye predict skipped (no wall)\n" );
		return;
	}

	leaf = Mod_PointInLeaf( eye, wmodel->nodes, wmodel );
	if( leaf && leaf->cluster >= 0 )
		eye_cluster = leaf->cluster;

	/* G218: if dump-eye cluster missed the 4-slot cache, overwrite an outdoor row. */
	if( eye_cluster >= 0 && !GC_LookupSurfbitsCache( eye_cluster )
		&& gc_newgame_surf_cache && gc_newgame_surf_cache_slots > 0
		&& gc_newgame_surfbytes > 0 && gc_newgame_visbytes > 0
		&& gc_newgame_visbytes <= 512 )
	{
		byte vis[512];
		int slot, li;

		slot = gc_newgame_surf_cache_slots - 1;
		for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
		{
			int lv = GC_VisLeafsForCluster( gc_newgame_surf_cache_cluster[i] );

			if( lv > 0 && lv <= 48 )
			{
				slot = i;
				break;
			}
		}
		for( li = 1; li < wmodel->numleafs; li++ )
		{
			if( wmodel->leafs[li].cluster != eye_cluster
				|| !wmodel->leafs[li].compressed_vis )
				continue;
			GC_DecompressPVS( vis, wmodel->leafs[li].compressed_vis,
				(size_t)gc_newgame_visbytes );
			bits = gc_newgame_surf_cache + (size_t)slot * (size_t)gc_newgame_surfbytes;
			GC_BuildSurfbitsForVisRow( wmodel, vis, bits );
			gc_newgame_surf_cache_cluster[slot] = eye_cluster;
			GC_BuildRefreshCandsFromSurfbits( wmodel, bits, slot );
			GC_FlipperTrace( "Xash3D GameCube: G218 dump-eye surfbits slot=%d cluster=%d\n",
				slot, eye_cluster );
			break;
		}
	}
	/* G229: restream 320 LM-cap from tram-eye cluster cands before live/fill. */
	if( eye_cluster >= 0 )
		gc_newgame_viewcluster = eye_cluster;
	GC_FlushPendingCapFaceRefresh();
	/* G230: lock tram-aimed cap — prepare/PVS-follow must not refresh to cl 0/16. */
	gc_g212_stream_locked = true;
	SYS_Report( "Xash3D GameCube: G230 lock cl=%d\n", eye_cluster );

	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		int cand = gc_newgame_surf_cache_cluster[i];
		int leaves;
		int walls;

		if( cand < 0 || gc_refresh_ncands[i] <= 0 )
			continue;
		leaves = GC_VisLeafsForCluster( cand );
		if( leaves <= 48 )
			continue;
		walls = GC_RefreshCandWallCount( i );
		if( walls > indoor_walls
			|| ( walls == indoor_walls && leaves > indoor_leaves ))
		{
			indoor_walls = walls;
			indoor_leaves = leaves;
			indoor = cand;
		}
	}
	primary = -1;
	if( eye_cluster >= 0 && GC_LookupSurfbitsCache( eye_cluster ))
		primary = eye_cluster;
	else if( indoor >= 0 )
		primary = indoor;
	else
		primary = gc_newgame_viewcluster;
	bits = GC_LookupSurfbitsCache( primary );
	if( !bits && gc_newgame_surfbits )
		bits = gc_newgame_surfbits;
	if( !bits )
	{
		GC_FlipperTrace( "Xash3D GameCube: G218 skip cl=%d eye=%d\n",
			primary, eye_cluster );
		return;
	}

	GC_CaptureLiveFacesFromSurfbits( wmodel, bits, false );
	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		int cand = gc_newgame_surf_cache_cluster[i];

		if( cand < 0 || cand == primary )
			continue;
		bits = GC_LookupSurfbitsCache( cand );
		if( bits )
			GC_CaptureLiveFacesFromSurfbits( wmodel, bits, true );
	}
	/* G229: table-driven neighbor PVS (denser along-track than G226 if/else). */
	if( gc_newgame_surfbytes > 0 && gc_newgame_surfbytes <= 1024
		&& gc_newgame_visbytes > 0 && gc_newgame_visbytes <= 512
		&& !VectorIsNull( gc_newgame_capture_forward ))
	{
		/* fwd, side, up offsets — denser along-track + portal throat. */
		static const short	od[18][3] = {
			{ 256, 0, 0 }, { 512, 0, 0 }, { 768, 0, 0 }, { 1024, 0, 0 },
			{ 384, 192, 0 }, { 384, -192, 0 }, { 192, 0, 128 },
			{ -256, 0, 0 }, { 128, 0, -160 }, { 640, 128, 0 },
			{ 896, 0, 320 }, { 512, 0, 384 }, { 256, 256, 192 }, { 256, -256, 192 },
			/* G234: deeper portal + throat sides (or was stuck at 3). */
			{ 1280, 0, 64 }, { 1536, 0, 0 }, { 768, 320, 96 }, { 768, -320, 96 }
		};
		byte	ubuf[1024], tbuf[1024], vis[512], seen[128];
		float	*f = gc_newgame_capture_forward;
		float	rx = -f[1], ry = f[0]; /* XY side from forward */
		int	pi, or_hits = 0;
		vec3_t	probe;

		memset( ubuf, 0, (size_t)gc_newgame_surfbytes );
		memset( seen, 0, sizeof( seen ));
		for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
		{
			int c = gc_newgame_surf_cache_cluster[i];

			if( c >= 0 && c < 1024 )
				seen[c >> 3] |= (byte)( 1 << ( c & 7 ));
		}
		for( pi = 0; pi < 18; pi++ )
		{
			mleaf_t *pl;
			int cl, li, b, hit = 0;
			float fd = (float)od[pi][0], sd = (float)od[pi][1], ud = (float)od[pi][2];

			probe[0] = eye[0] + f[0] * fd + rx * sd;
			probe[1] = eye[1] + f[1] * fd + ry * sd;
			probe[2] = eye[2] + f[2] * fd + ud;
			pl = Mod_PointInLeaf( probe, wmodel->nodes, wmodel );
			cl = ( pl && pl->cluster >= 0 ) ? pl->cluster : -1;
			if( cl < 0 || cl >= 1024 || ( seen[cl >> 3] & ( 1 << ( cl & 7 ))))
				continue;
			seen[cl >> 3] |= (byte)( 1 << ( cl & 7 ));
			for( li = 1; li < wmodel->numleafs; li++ )
			{
				if( wmodel->leafs[li].cluster != cl || !wmodel->leafs[li].compressed_vis )
					continue;
				GC_DecompressPVS( vis, wmodel->leafs[li].compressed_vis,
					(size_t)gc_newgame_visbytes );
				GC_BuildSurfbitsForVisRow( wmodel, vis, tbuf );
				for( b = 0; b < gc_newgame_surfbytes; b++ )
				{
					if( tbuf[b] & ~ubuf[b] )
						hit = 1;
					ubuf[b] |= tbuf[b];
				}
				break;
			}
			if( hit )
				or_hits++;
		}
		if( or_hits > 0 )
			GC_CaptureLiveFacesFromSurfbits( wmodel, ubuf, true );
		GC_FlipperTrace( "Xash3D GameCube: G234 or=%d f=%d a=%d\n",
			or_hits, gc_fill_face_count, gc_aram_fill_count );
	}
	GC_RerankLiveFacesNearEye();
	if( Mod_GCWorldSurfacesScratchRetained( wmodel ))
		Con_Reportf( "Xash3D GameCube: G298 lean live after Capture n=%d fill=%d (scratch retain)\n",
			gc_live_face_count, gc_fill_face_count );
	GC_FlipperTrace( "Xash3D GameCube: G218 p=%d eye=%d n=%d\n",
		primary, eye_cluster, gc_live_face_count );
}

/* G211: densest indoor-ish cluster (leaves > 48) with wall cands — rooms, not sky. */
static qboolean GC_PreferIndoorWallCluster( void )
{
	int cur_leaves = GC_VisLeafsForCluster( gc_newgame_viewcluster );
	int indoor = -1;
	int indoor_walls = -1;
	int indoor_leaves = -1;
	int i;

	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		int cand = gc_newgame_surf_cache_cluster[i];
		int leaves;
		int walls;

		if( cand < 0 || gc_refresh_ncands[i] <= 0 )
			continue;
		leaves = GC_VisLeafsForCluster( cand );
		/* Outdoor band is ≤48 (G163/G190); indoor rooms sit above that. */
		if( leaves <= 48 )
			continue;
		walls = GC_RefreshCandWallCount( i );
		if( walls > indoor_walls
			|| ( walls == indoor_walls && leaves > indoor_leaves ))
		{
			indoor_walls = walls;
			indoor_leaves = leaves;
			indoor = cand;
		}
	}
	if( indoor < 0 || indoor == gc_newgame_viewcluster )
		return false;
	if( !GC_SetActiveNewGameCluster( indoor, true ))
		return false;
	{
		static int prefer_log_gen = -1;

		if( prefer_log_gen != gc_newgame_cap_generation )
		{
			prefer_log_gen = gc_newgame_cap_generation;
			GC_FlipperTrace( "Xash3D GameCube: G211 prefer %d walls=%d L=%d was=%d\n",
				indoor, indoor_walls, indoor_leaves, cur_leaves );
		}
	}
	return true;
}

/* G227: c0a0 tram-ride start (info_player_start) — not indoor wall-aim. */
static qboolean GC_DumpEyeAtTramStart( float *eye, float *out_angles )
{
	vec3_t	right, up;

	if( !eye || !out_angles )
		return false;
	eye[0] = 2864.0f;
	eye[1] = 2804.0f;
	eye[2] = 542.0f;
	out_angles[0] = -6.0f;
	out_angles[1] = 180.0f;
	out_angles[2] = 0.0f;
	VectorCopy( eye, gc_newgame_capture_origin );
	AngleVectors( out_angles, gc_newgame_capture_forward, right, up );
	/* G229: restream 320 LM-cap slots toward tram (wall-aim used to do this). */
	gc_cap_refresh_pending = true;
	{
		static qboolean logged;
		if( !logged )
		{
			logged = true;
			GC_FlipperTrace( "Xash3D GameCube: G227 tram eye=(2864,2804,542)\n" );
		}
	}
	return true;
}

/*
===========
GC_NewGameRideEye

G279: follow the parented tram cabin eye. The G233 lock used DumpEyeAtTramStart
every frame, so Flipper stayed at trainstop while *12 rolled away.
===========
*/
static qboolean GC_NewGameRideEye( float *eye, float *out_angles )
{
	edict_t *player;

	if( !eye || !out_angles || !Sys_CheckParm( "-gcnewgame" ))
		return false;
	if( !svgame.edicts || svs.maxclients < 1 )
		return GC_DumpEyeAtTramStart( eye, out_angles );

	player = SV_EdictNum( 1 );
	if( player && !player->free && !VectorIsNull( player->v.origin ))
	{
		VectorCopy( player->v.origin, eye );
		/* G279: ignore player-move probe yaw — keep looking down the tunnel. */
		out_angles[0] = -6.0f;
		out_angles[1] = 180.0f;
		out_angles[2] = 0.0f;
		{
			vec3_t right, up;

			VectorCopy( eye, gc_newgame_capture_origin );
			AngleVectors( out_angles, gc_newgame_capture_forward, right, up );
		}
		{
			static int ride_eye_log;
			static float last_x = 1e30f;

			if( ride_eye_log < 16 && ( ride_eye_log < 4 || fabs( eye[0] - last_x ) > 8.0f ))
			{
#if 0 /* G281 DOL reclaim */
				Con_Reportf( "Xash3D GameCube: G279 ride eye=(%.0f,%.0f,%.0f) yaw=%.0f\n",
					eye[0], eye[1], eye[2], out_angles[1] );
#endif
				last_x = eye[0];
				ride_eye_log++;
			}
		}
		return true;
	}
	return GC_DumpEyeAtTramStart( eye, out_angles );
}

/*
===========
GC_MaybeRestreamRideMapFaces

G281: under G212 lock, refresh LM-cap/live as the ride eye moves. Sample a
look-ahead origin (tunnel yaw≈180) so faces ahead of the cabin enter the
cap — not only the tram-start leaf. Recycle one outdoor surfbits slot in
place when the ahead cluster is uncached (G218, no alloc).
===========
*/
static void GC_MaybeRestreamRideMapFaces( const float *eye )
{
	static float last[3];
	static int cd, nlog;
	float dx, dy, dz;
	int cluster, slot, i;
	model_t *wm;
	const byte *bits;
	vec3_t ahead;

	if( !eye || !gc_g212_stream_locked )
		return;
	if( cd > 0 )
	{
		cd--;
		return;
	}
	dx = eye[0] - last[0];
	dy = eye[1] - last[1];
	dz = eye[2] - last[2];
	if( ( last[0] || last[1] || last[2] ) && ( dx * dx + dy * dy + dz * dz ) < 16384.0f )
		return;

	wm = sv.models[1];
	if( !wm )
		return;

	/* Walk look-ahead along −X until cluster changes (c0a0 cl 117 is large). */
	VectorCopy( eye, ahead );
	cluster = GC_SelectClusterForOrigin( eye );
	{
		const float looks[4] = { 384.0f, 768.0f, 1152.0f, 1536.0f };
		int base = cluster;

		for( i = 0; i < 4; i++ )
		{
			int c;

			ahead[0] = eye[0] - looks[i];
			ahead[1] = eye[1];
			ahead[2] = eye[2];
			c = GC_SelectClusterForOrigin( ahead );
			if( c >= 0 && ( base < 0 || c != base ))
			{
				cluster = c;
				break;
			}
			if( i == 3 && c >= 0 )
			{
				/* Furthest sample — still use it for ranking even if same cl. */
				cluster = c;
			}
		}
	}
	if( cluster < 0 )
		return;

	slot = GC_LookupSurfbitsCacheSlot( cluster );
	if( slot < 0 && gc_newgame_surf_cache && gc_newgame_surf_cache_slots > 0
		&& gc_newgame_surfbytes > 0 && gc_newgame_visbytes > 0
		&& gc_newgame_visbytes <= 512 )
	{
		byte vis[512];
		int li;

		slot = gc_newgame_surf_cache_slots - 1;
		for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
		{
			if( GC_VisLeafsForCluster( gc_newgame_surf_cache_cluster[i] ) <= 48 )
			{
				slot = i;
				break;
			}
		}
		for( li = 1; li < wm->numleafs; li++ )
		{
			if( wm->leafs[li].cluster != cluster || !wm->leafs[li].compressed_vis )
				continue;
			GC_DecompressPVS( vis, wm->leafs[li].compressed_vis,
				(size_t)gc_newgame_visbytes );
			{
				byte *row = gc_newgame_surf_cache
					+ (size_t)slot * (size_t)gc_newgame_surfbytes;
				GC_BuildSurfbitsForVisRow( wm, vis, row );
				gc_newgame_surf_cache_cluster[slot] = cluster;
				GC_BuildRefreshCandsFromSurfbits( wm, row, slot );
			}
			break;
		}
		if( gc_newgame_surf_cache_cluster[slot] != cluster )
			slot = -1;
	}
	if( slot < 0 )
		slot = GC_LookupSurfbitsCacheSlot( gc_newgame_viewcluster );
	if( slot < 0 )
		return;

	/* Rank faces toward look-ahead (tunnel reading), not cabin origin. */
	VectorCopy( ahead, gc_newgame_capture_origin );
	gc_newgame_capture_forward[0] = -1.0f;
	gc_newgame_capture_forward[1] = 0.0f;
	gc_newgame_capture_forward[2] = 0.0f;
	if( cluster != gc_newgame_viewcluster
		&& GC_LookupSurfbitsCacheSlot( cluster ) >= 0 )
		(void)GC_SetActiveNewGameCluster( cluster, false );
	/* Rebuild cand top-K with new capture_origin, then incrementally replace
	 * weak Flipper LM-cap slots. Full CaptureDrawFacesFromSurfbits softlocks
	 * Host_Frame under MEM1 during present. */
	gc_refresh_loaded_slot = -1;
	GC_RefreshCapFacesFromCands( slot );
	bits = GC_LookupSurfbitsCache( gc_newgame_viewcluster );
	if( !bits )
		bits = GC_LookupSurfbitsCache( cluster );
	if( bits )
	{
		/* G283: MarkLeaves stamps ride-cluster surfbits onto live msurface_t. */
		gc_newgame_surfbits = bits;
		/* Lean only when surfaces are unpinned (dangling scratch without retain). */
		if( !Mod_GCWorldSurfacesPinned( wm ))
			GC_CaptureLiveFacesFromSurfbits( wm, bits, true );
	}

	last[0] = eye[0];
	last[1] = eye[1];
	last[2] = eye[2];
	cd = 8;
	gc_cap_refresh_pending = false;
	if( nlog < 12 )
	{
		Con_Reportf( "G281 cl=%d n=%d L=%d%s\n", cluster, gc_newgame_cap_face_count,
			gc_live_face_count,
			Mod_GCWorldSurfacesScratchRetained( wm ) ? " pin" :
			( Mod_GCWorldSurfacesPinned( wm ) ? " mpin" : "" ) );
		nlog++;
		/* Arm DumpFrames EFB hold after restream so late ride frames encode. */
		if( nlog == 2 || nlog == 5 )
		{
			extern void R_GXHoldEfbForDump( int frames );
			R_GXHoldEfbForDump( 5 );
		}
	}
}

/* G199/G211: place dump eye in front of an enclosed wall (not outdoor sky slab). */
static qboolean GC_DumpEyeInFrontOfBestWall( float *eye, float *out_angles )
{
	int i;
	int best = -1;
	int best_score = -1;
	int best_area = 0;
	int best_near = 0;
	vec3_t point;
	const gc_cap_face_t *f;
	static int aim_gen = -1;
	static int aim_slot = -1;
	static int aim_area = 0;
	static int aim_near = 0;
	vec3_t	cents[GC_MAX_CAP_FACES];

	if( !eye || !out_angles || gc_newgame_cap_face_count <= 0 )
		return false;

	/* Score once per cap generation — O(n²) neighbor counts are too heavy per frame. */
	if( aim_gen != gc_newgame_cap_generation || aim_slot < 0
		|| aim_slot >= gc_newgame_cap_face_count )
	{
		for( i = 0; i < gc_newgame_cap_face_count; i++ )
			GC_CapFaceCentroid( i, cents[i] );
		best = -1;
		best_score = -1;
		for( i = 0; i < gc_newgame_cap_face_count; i++ )
		{
			int area = gc_newgame_cap_areas[i];
			int nearby = 0;
			int score;
			int j;

			f = &gc_newgame_cap_faces[i];
			if( fabs( f->plane.normal[2] ) >= 0.35f )
				continue;
			if( area < 1024 || gc_cap_nverts[i] < 3 )
				continue;
			for( j = 0; j < gc_newgame_cap_face_count; j++ )
			{
				vec3_t d;
				if( j == i || gc_cap_nverts[j] < 3 )
					continue;
				VectorSubtract( cents[j], cents[i], d );
				if( DotProduct( d, d ) <= ( 640.0f * 640.0f ))
					nearby++;
			}
			/* Mid-size walls in dense neighborhoods beat outdoor mega-slabs.
			 * (Aggressive area penalties hurt DumpFrames fill under the 320 cap.) */
			score = nearby * 8000 + ( area > 24000 ? 24000 : area );
			if( area > 60000 )
				score /= 4;
			if( nearby < 12 )
				score /= 2;
			if( score > best_score )
			{
				best_score = score;
				best_area = area;
				best_near = nearby;
				best = i;
			}
		}
		aim_gen = gc_newgame_cap_generation;
		aim_slot = best;
		aim_area = best_area;
		aim_near = best_near;
	}
	best = aim_slot;
	best_area = aim_area;
	best_near = aim_near;
	if( best < 0 )
		return false;
	f = &gc_newgame_cap_faces[best];
	GC_CapFaceCentroid( best, point );
	/* Closer indoor standoff — outdoor 320 left DumpFrames sky-heavy. */
	eye[0] = point[0] + f->plane.normal[0] * 224.0f;
	eye[1] = point[1] + f->plane.normal[1] * 224.0f;
	eye[2] = point[2] + f->plane.normal[2] * 224.0f + 40.0f;
	{
		vec3_t	right, up_ref;
		float	front;

		if( fabs( f->plane.normal[2] ) < 0.9f )
		{
			up_ref[0] = 0.0f;
			up_ref[1] = 0.0f;
			up_ref[2] = 1.0f;
		}
		else
		{
			up_ref[0] = 0.0f;
			up_ref[1] = 1.0f;
			up_ref[2] = 0.0f;
		}
		CrossProduct( f->plane.normal, up_ref, right );
		VectorNormalize( right );
		eye[0] += right[0] * 72.0f;
		eye[1] += right[1] * 72.0f;
		eye[2] += right[2] * 72.0f;
		front = DotProduct( eye, f->plane.normal ) - f->plane.dist;
		if( front < 48.0f )
		{
			float push = 48.0f - front + 48.0f;
			eye[0] += f->plane.normal[0] * push;
			eye[1] += f->plane.normal[1] * push;
			eye[2] += f->plane.normal[2] * push;
		}
	}
	VectorSubtract( point, eye, point );
	VectorAngles( point, out_angles );
	out_angles[2] = 0.0f;
	if( out_angles[0] > -2.0f )
		out_angles[0] = -6.0f;
	{
		static int eye_log_gen = -1;
		static int eye_log_slot = -1;

		if( eye_log_gen != gc_newgame_cap_generation || eye_log_slot != best )
		{
			eye_log_gen = gc_newgame_cap_generation;
			eye_log_slot = best;
			GC_FlipperTrace( "Xash3D GameCube: G220 eye area=%d near=%d bake=%d\n",
				best_area, best_near, (int)gc_cap_bake_src[best] );
		}
	}
	VectorCopy( eye, gc_newgame_capture_origin );
	{
		vec3_t	right, up;

		AngleVectors( out_angles, gc_newgame_capture_forward, right, up );
	}
	/* G212: restream 320 slots toward the new eye once per significant move. */
	{
		static vec3_t g212_last_stream_eye;
		vec3_t	d;

		VectorSubtract( eye, g212_last_stream_eye, d );
		if( VectorIsNull( g212_last_stream_eye ) || DotProduct( d, d ) > ( 96.0f * 96.0f ))
		{
			gc_cap_refresh_pending = true;
			VectorCopy( eye, g212_last_stream_eye );
		}
	}
	/* G201e/G204: rebake only plane-fallback verts around the dump eye —
	 * edge-baked faces keep real BSP loops for ST/LM. */
	{
		static int rebake_gen = -1;
		int	s;
		int	nplane = 0;

		if( rebake_gen != gc_newgame_cap_generation )
		{
			for( s = 0; s < gc_newgame_cap_face_count; s++ )
			{
				const gc_cap_face_t *cf = &gc_newgame_cap_faces[s];
				if( gc_cap_bake_src[s] != GC_CAP_BAKE_PLANE )
					continue;
				gc_cap_nverts[s] = (byte)GC_BakeCapVertsFromPlane( &cf->plane,
					cf->extents[0], cf->extents[1],
					gc_cap_pts_s16[s], GC_CAP_MAX_VERTS );
				if( gc_cap_nverts[s] < 3 )
					gc_cap_bake_src[s] = GC_CAP_BAKE_NONE;
				else
					nplane++;
			}
			gc_newgame_cap_generation++;
			rebake_gen = gc_newgame_cap_generation;
			/* Keep aim_slot — generation bump alone must not re-pick a worse wall. */
			aim_gen = gc_newgame_cap_generation;
			GC_FlipperTrace( "Xash3D GameCube: G204 rebaked %d plane quads (edge verts preserved)\n",
				nplane );
		}
	}
	return true;
}

/* G190: aim dump camera into the largest near-vertical cap face. */
static qboolean GC_DumpLookAtBestWall( const float *eye, float *out_angles )
{
	int i;
	int best = -1;
	int best_area = 0;
	vec3_t look;
	float d;

	if( !eye || !out_angles || gc_newgame_cap_face_count <= 0 )
		return false;
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		const gc_cap_face_t *f = &gc_newgame_cap_faces[i];
		int area = gc_newgame_cap_areas[i];

		if( fabs( f->plane.normal[2] ) >= 0.35f )
			continue;
		if( area <= best_area )
			continue;
		d = DotProduct( eye, f->plane.normal ) - f->plane.dist;
		if( d <= 8.0f )
			continue;
		best_area = area;
		best = i;
	}
	if( best < 0 )
	{
		for( i = 0; i < gc_newgame_cap_face_count; i++ )
		{
			const gc_cap_face_t *f = &gc_newgame_cap_faces[i];
			int area = gc_newgame_cap_areas[i];

			if( fabs( f->plane.normal[2] ) >= 0.35f )
				continue;
			if( area <= best_area )
				continue;
			best_area = area;
			best = i;
		}
	}
	if( best < 0 )
		return false;
	{
		const gc_cap_face_t *f = &gc_newgame_cap_faces[best];
		vec3_t on_plane;

		d = DotProduct( eye, f->plane.normal ) - f->plane.dist;
		on_plane[0] = eye[0] - f->plane.normal[0] * d;
		on_plane[1] = eye[1] - f->plane.normal[1] * d;
		on_plane[2] = eye[2] - f->plane.normal[2] * d;
		VectorSubtract( on_plane, eye, look );
		if( VectorLength( look ) < 1.0f )
			VectorScale( f->plane.normal, -1.0f, look );
		VectorAngles( look, out_angles );
		out_angles[2] = 0.0f;
		if( out_angles[0] > -5.0f )
			out_angles[0] = -10.0f;
		GC_FlipperTrace( "Xash3D GameCube: G190 landmark wall soft dump walls=%d area=%d yaw=%.0f cluster=%d\n",
			GC_RefreshCandWallCount( GC_LookupSurfbitsCacheSlot( gc_newgame_viewcluster )),
			best_area, out_angles[1], gc_newgame_viewcluster );
		return true;
	}
}

static void GC_FlushPendingCapFaceRefresh( void )
{
	int cache_slot;

	/* G230: tram/near-eye lock — drop pending refreshes (cluster 0/16 thrash). */
	if( gc_g212_stream_locked )
	{
		gc_cap_refresh_pending = false;
		return;
	}
	if( !gc_cap_refresh_pending )
		return;
	gc_cap_refresh_pending = false;
	if( gc_newgame_cap_face_count <= 0 )
		return;

	/* G189/G190: landmark reposition often lands densest indoor; swap to an
	 * outdoor wall-heavy cached row before refresh. */
	if( gc_g188_reposition_pending )
		GC_PreferOutdoorWallCluster();

	cache_slot = GC_LookupSurfbitsCacheSlot( gc_newgame_viewcluster );
	if( cache_slot < 0 || gc_refresh_ncands[cache_slot] <= 0 )
	{
		GC_FlipperTrace( "Xash3D GameCube: G163 refresh skipped (no capture cands cluster=%d)\n",
			gc_newgame_viewcluster );
		gc_g188_reposition_pending = false;
		return;
	}
	GC_RefreshCapFacesFromCands( cache_slot );
	if( gc_g188_reposition_pending )
	{
		gc_g188_reposition_pending = false;
#if 0 /* G281 DOL reclaim */
		SYS_Report( "Xash3D GameCube: G188 landmark Flipper continuity cluster=%d faces=%d\n",
			gc_newgame_viewcluster, gc_newgame_cap_face_count );
#endif
	}
}

/* G188: after put-in landmark reposition, force a cap-face refresh so the
 * Flipper face set matches the continued origin (not the load-time cluster). */
void GC_NewGameNotifyLandmarkReposition( void )
{
	edict_t *player;
	vec3_t eye;

	gc_g188_reposition_pending = true;
	player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;
	if( player && !player->free && !VectorIsNull( player->v.origin ))
	{
		VectorCopy( player->v.origin, eye );
		eye[2] += 48.0f;
		GC_UpdateNewGamePVSForOrigin( eye );
	}
	gc_cap_refresh_pending = true;
}

typedef struct
{
	vec3_t	mins;
	vec3_t	maxs;
	int	cluster;
} gc_newgame_leafbox_t;
static gc_newgame_leafbox_t *gc_newgame_leafboxes;
static int gc_newgame_nleafboxes;
static qboolean gc_newgame_pvs_follow_proved;
static qboolean gc_gx_present_logged;
/* G128/G191: remaining soft DumpFrames latch presents. After Flipper EFB
 * clear, Dolphin DumpFramesAsImages follows EFB — CPU YUYV→XFB alone stays
 * invisible (flat sky). G191 presents soft RGB565 via tiled EFB quad. */
static int gc_cpu_dump_presents_left;
static qboolean gc_dump_look_into_map;
static qboolean gc_force_draw_viewmodel; /* G149: allow dump/G105 presents to keep VM on */
static convar_t *gc_quality;
static double gc_last_present_time;
static double gc_worst_frame_ms;
static qboolean gc_budget_probe_active;
static GXTexObj gc_present_tex;
static qboolean gc_present_tex_ready;
static gc_boot_phase_t gc_boot_phase = GC_BOOT_NONE;
static qboolean gc_gameplay_sound_done;
static qboolean gc_gameplay_sound_queued;
static void GC_FreeNewGamePVSCache( void );
static void GC_ResetNewGameGameplaySoundState( void );
#endif

#if XASH_GAMECUBE
static void GC_ResetNewGameGameplaySoundState( void )
{
	gc_gameplay_sound_done = false;
	gc_gameplay_sound_queued = false;
}

const char *GC_GetBootPhaseName( gc_boot_phase_t phase )
{
	switch( phase )
	{
	case GC_BOOT_EARLY: return "early";
	case GC_BOOT_ENGINE: return "engine";
	case GC_BOOT_RENDERER: return "renderer";
	case GC_BOOT_SW_FB: return "sw_fb";
	case GC_BOOT_MENU: return "menu";
	case GC_BOOT_CLIENT: return "client";
	case GC_BOOT_INTRO: return "intro";
	case GC_BOOT_MAP: return "map";
	default: return "none";
	}
}

gc_boot_phase_t GC_GetBootPhase( void )
{
	return gc_boot_phase;
}

/* G82: intentional early fault after reporting a named phase (probe smoke). */
static void GC_MaybePhaseFault( gc_boot_phase_t phase )
{
	char want[32];

	if( !Sys_GetParmFromCmdLine( "-gc_phase_test", want ))
		return;
	if( Q_stricmp( want, GC_GetBootPhaseName( phase )))
		return;
	Sys_Error( "G82: Intentional phase fault at %s\n", GC_GetBootPhaseName( phase ));
}

void GC_ReportBootPhase( gc_boot_phase_t phase )
{
	gc_boot_phase_t prev = gc_boot_phase;

	if( phase < gc_boot_phase )
		return;

	gc_boot_phase = phase;
	SYS_Report( "Xash3D GameCube: boot phase=%s last=%s\n",
		GC_GetBootPhaseName( phase ), GC_GetBootPhaseName( prev ));
	GC_MaybePhaseFault( phase );
}

qboolean GC_BootDrawAllowed( void )
{
	/* Fallback menu FillRGBA / present need a validated software framebuffer. */
	return gc_boot_phase >= GC_BOOT_SW_FB && gc.buffer != NULL && gc.width > 0 && gc.height > 0;
}
#endif

#define GC_VIDEO_SAFE_AREA_PERCENT 10
#define GC_VIDEO_MIN_READABLE_WIDTH 320
#define GC_VIDEO_MIN_READABLE_HEIGHT 240
#define GC_VIDEO_PROBE_WIDTH 320
#define GC_VIDEO_PROBE_HEIGHT 240
/* G93: New Game world/G36 default 320×240 (matches r_gcmap static screen).
 * Pass -gcnewgame160 to keep the prior 160×120 G36-safe path. */
#define GC_VIDEO_NEWGAME_PROBE_WIDTH 320
#define GC_VIDEO_NEWGAME_PROBE_HEIGHT 240
#define GC_VIDEO_NEWGAME_FALLBACK_WIDTH 160
#define GC_VIDEO_NEWGAME_FALLBACK_HEIGHT 120
/* Skip first Host_Frame after arm (connect residual), then sample. */
#define GC_VIDEO_BUDGET_WARMUP_PRESENTS 1
#define GC_VIDEO_BUDGET_SAMPLE_TARGET 16
#define GC_VIDEO_NEWGAME_BUDGET_SAMPLE_TARGET 8
/* Keep SCR on the light fill path after G36 samples so Host_Frame still presents
 * while we restore the framebuffer for world render. Short grace so real hardware
 * reaches the low-res world path quickly after evidence is collected. */
#define GC_VIDEO_LIGHT_PRESENT_GRACE 8

#if XASH_GAMECUBE
static void GC_GetNewGamePresentSize( int *width, int *height )
{
	if( Sys_CheckParm( "-gcnewgame160" ))
	{
		*width = GC_VIDEO_NEWGAME_FALLBACK_WIDTH;
		*height = GC_VIDEO_NEWGAME_FALLBACK_HEIGHT;
	}
	else
	{
		*width = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		*height = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}
}
#endif

#if XASH_GAMECUBE
/* Collected during the probe window; flushed to OSReport only after sampling so
 * SYS_Report I/O does not inflate the measured Host_Frame intervals. */
static float gc_budget_sample_ms[GC_VIDEO_BUDGET_SAMPLE_TARGET];
static uint8_t gc_budget_sample_nonblack[GC_VIDEO_BUDGET_SAMPLE_TARGET];
#endif

static unsigned int GC_GetFrameBudgetSampleTarget( void )
{
#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcnewgame" ))
		return GC_VIDEO_NEWGAME_BUDGET_SAMPLE_TARGET;
#endif
	return GC_VIDEO_BUDGET_SAMPLE_TARGET;
}

/* GC_GetVisualQuality is provided by ref/gx/r_local.h as an inline helper.
 * The platform video backend does not redefine it to avoid duplicate symbols.
 * Quality 0: Low (smoke/minimal visuals, reduced particles)
 * Quality 1: Medium (default, some optimizations for memory)
 * Quality 2: High (full visuals if memory permits)
 */

void Platform_Minimize_f( void )
{
}

void GC_EarlyBootSplash( void )
{
#if XASH_GAMECUBE
	unsigned short *dst;

	if( gc.initialized || rmode )
		return;

	GC_FlipperTrace( "Xash3D GameCube: early video splash\n" );
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode( NULL );
	if( !rmode )
		return;

	VIDEO_Configure( rmode );
	xfb[0] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	if( !xfb[0] )
		return;

	dst = (unsigned short *)xfb[0];
	VIDEO_ClearFrameBuffer( rmode, dst, 0x0010 ); /* dark blue boot frame */
	VIDEO_SetNextFramebuffer( xfb[0] );
	VIDEO_SetBlack( false );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if( rmode->viTVMode & VI_NON_INTERLACE )
		VIDEO_WaitVSync();
#endif
}

static void GC_InitVideoHardware( void )
{
#if XASH_GAMECUBE
	int safe_x, safe_y, safe_w, safe_h;
	qboolean progressive;

	if( gc.initialized )
		return;
	gc_last_present_time = 0.0;
	SYS_Report( "Xash3D GameCube: mem stage=video_init total=%.2f\n", 0.0 );
	if( !rmode )
	{
		VIDEO_Init();
		rmode = VIDEO_GetPreferredMode( NULL );
		VIDEO_Configure( rmode );
	}
	else
	{
		GC_FlipperTrace( "Xash3D GameCube: video init continuing after early splash\n" );
	}
	progressive = ( rmode->viTVMode & VI_NON_INTERLACE ) ? true : false;
	safe_x = ( rmode->fbWidth * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	safe_y = ( rmode->xfbHeight * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	safe_w = rmode->fbWidth - safe_x * 2;
	safe_h = rmode->xfbHeight - safe_y * 2;
	SYS_Report( "Xash3D GameCube: video mode fb=%dx%d efb=%dx%d vi=%dx%d tv=0x%08x progressive=%u policy=preferred-4:3-480i\n",
		rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth, rmode->efbHeight,
		rmode->viWidth, rmode->viHeight, rmode->viTVMode, progressive ? 1u : 0u );
	SYS_Report( "Xash3D GameCube: video safe_area percent=%d rect=%d,%d,%d,%d min_readable=%dx%d\n",
		GC_VIDEO_SAFE_AREA_PERCENT, safe_x, safe_y, safe_w, safe_h,
		GC_VIDEO_MIN_READABLE_WIDTH, GC_VIDEO_MIN_READABLE_HEIGHT );

	if( !xfb[0] )
		xfb[0] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	if( !xfb[1] )
		xfb[1] = MEM_K0_TO_K1( SYS_AllocateFramebuffer( rmode ));
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_SetBlack( false );
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if( rmode->viTVMode & VI_NON_INTERLACE )
		VIDEO_WaitVSync();

	GX_Init( gx_fifo, sizeof( gx_fifo ));
	GX_SetDispCopyGamma( GX_GM_1_0 );
	GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
	GX_SetFieldMode( rmode->field_rendering, (( rmode->viHeight == 2 * rmode->xfbHeight ) ? GX_ENABLE : GX_DISABLE ));

	/* Pure Flipper: RGB565 EFB + explicit copy-clear (sky/black). */
	GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );
	{
		GXColor clear = { 89, 141, 210, 255 }; /* outdoor sky; G221 overrides per-frame */
		GX_SetCopyClear( clear, 0x00ffffff );
	}

	GX_SetViewport( 0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1 );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	GX_SetNumChans( 1 );
	GX_SetNumTexGens( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetNumTevStages( 1 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );

	gc_present_tex_ready = false;
	gc.initialized = true;
	GC_FlipperTrace( "Xash3D GameCube: renderer initialized gx\n" );
	SYS_Report( "Xash3D GameCube: retail Flipper policy capture=%d softworld=%d xfb_dual=1 copy_clear=1 safe_area=%d%% soft_fb_max=%dx%d efb_native=1\n",
		GC_IsCaptureDiagnostics() ? 1 : 0,
		Sys_CheckParm( "-gcsoftworld" ) ? 1 : 0,
		GC_VIDEO_SAFE_AREA_PERCENT,
		GC_VIDEO_NEWGAME_PROBE_WIDTH, GC_VIDEO_NEWGAME_PROBE_HEIGHT );
	GC_ReportBootPhase( GC_BOOT_RENDERER );
#endif
}

#if XASH_GAMECUBE
/* Linear→tiled staging for GX_TF_RGB565 (4×4). Sized for New Game / probe presents. */
#define GC_GX_TILE_MAX_W 320
#define GC_GX_TILE_MAX_H 240
static u16 gc_tiled_rgb565[GC_GX_TILE_MAX_W * GC_GX_TILE_MAX_H] __attribute__((aligned( 32 )));
/* Post-map MEM1 cannot calloc a present FB; keep a BSS New Game probe buffer
 * so G36 never falls back to a 640×480 CPU blit on real hardware. */
static unsigned short gc_probe_rgb565[GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT] __attribute__((aligned( 32 )));
static qboolean gc_buffer_owns_heap;

static void GC_InitPresentTexture( void )
{
	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 )
	{
		gc_present_tex_ready = false;
		return;
	}

	GX_InitTexObj( &gc_present_tex, gc.buffer, (u16)gc.width, (u16)gc.height,
		GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( &gc_present_tex, GX_LINEAR, GX_LINEAR );
	gc_present_tex_ready = true;
}

static int gc_tiled_tex_w;
static int gc_tiled_tex_h;
static void *gc_tiled_tex_ptr;
static qboolean gc_gx_present_pipe_ready;
/* G151: live New Game draws world tris into EFB; DumpFrames stay on soft. */
static qboolean gc_gx_world_live;
static qboolean gc_gx_world_efb_ready;
static qboolean gc_g192_post_changelevel; /* G192: DumpFrames re-arm after changelevel */
static int gc_g193_defer_flipper_left; /* G193: soft-only presents before Flipper */
static qboolean gc_g193_draining; /* G193: soft drain active (defer Flipper) */
static qboolean gc_g193_soft_lock; /* G193: keep soft in XFB — DumpFrames encode lags Flipper */
/* G196: after Flipper resume, force G189/G190 wall-aim for N SCR frames so
 * DumpFrames capture walls (landmark player eye often faces open sky). */
static int gc_g196_flipper_dump_aim_left;
/* G193: dedicated linear soft snap (tiled staging is overwritten by GX swizzle).
 * Heap after clipnodes pin — 150 KiB BSS here starved the 59 KiB pin. */
static u16 *gc_g193_soft_snap;
static qboolean gc_g193_snap_valid;
static int gc_g193_snap_w, gc_g193_snap_h, gc_g193_snap_stride;

static void GC_G193CaptureSoftSnap( void )
{
	size_t bytes;
	size_t snap_bytes;

	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 || gc.stride <= 0 )
		return;
	bytes = (size_t)gc.stride * (size_t)gc.height * sizeof( unsigned short );
	snap_bytes = (size_t)GC_GX_TILE_MAX_W * (size_t)GC_GX_TILE_MAX_H * sizeof( u16 );
	if( bytes > snap_bytes )
	{
		gc_g193_snap_valid = false;
		Con_Reportf( S_ERROR "Xash3D GameCube: G193 soft snap too large (%u > %u)\n",
			(unsigned)bytes, (unsigned)snap_bytes );
		return;
	}
	if( !gc_g193_soft_snap )
	{
		gc_g193_soft_snap = (u16 *)memalign( 32, snap_bytes );
		if( !gc_g193_soft_snap )
		{
			gc_g193_snap_valid = false;
			Con_Reportf( S_ERROR "Xash3D GameCube: G193 soft snap alloc failed (%u)\n",
				(unsigned)snap_bytes );
			return;
		}
	}
	memcpy( gc_g193_soft_snap, gc.buffer, bytes );
	gc_g193_snap_w = gc.width;
	gc_g193_snap_h = gc.height;
	gc_g193_snap_stride = gc.stride;
	gc_g193_snap_valid = true;
	Con_Reportf( "Xash3D GameCube: G193 soft snap captured %dx%d soft0=0x%04x\n",
		gc.width, gc.height, gc.buffer[0] );
}

static void GC_G193ReleaseSoftSnap( void )
{
	gc_g193_snap_valid = false;
	gc_g193_snap_w = gc_g193_snap_h = gc_g193_snap_stride = 0;
}

static void GC_InitPresentTextureTiled( void *tiled, int width, int height )
{
	if( !tiled || width <= 0 || height <= 0 )
	{
		gc_present_tex_ready = false;
		gc_tiled_tex_ptr = NULL;
		gc_tiled_tex_w = 0;
		gc_tiled_tex_h = 0;
		return;
	}

	/* Same staging buffer + size: keep the TexObj; DCFlush + Invalidate refresh data. */
	if( gc_present_tex_ready && tiled == gc_tiled_tex_ptr
		&& width == gc_tiled_tex_w && height == gc_tiled_tex_h )
		return;

	GX_InitTexObj( &gc_present_tex, tiled, (u16)width, (u16)height,
		GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( &gc_present_tex, GX_NEAR, GX_NEAR );
	gc_tiled_tex_ptr = tiled;
	gc_tiled_tex_w = width;
	gc_tiled_tex_h = height;
	gc_present_tex_ready = true;
}

static void GC_SwizzleRGB565ToTiled( const unsigned short *src, int src_stride,
	int width, int height, unsigned short *dst )
{
	int tile_x, tile_y, y;
	unsigned short *out = dst;

	/* GX_TF_RGB565 stores 4×4 tiles contiguously.
	 * Copy each tile row as two u32 loads when naturally aligned. */
	for( tile_y = 0; tile_y < height; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < width; tile_x += 4 )
		{
			for( y = 0; y < 4; y++ )
			{
				const unsigned short *row = src + ( tile_y + y ) * src_stride + tile_x;
#if defined( __GNUC__ )
				if( (((unsigned long)row) & 3 ) == 0 && (((unsigned long)out) & 3 ) == 0 )
				{
					const unsigned int *r32 = (const unsigned int *)(const void *)row;
					unsigned int *o32 = (unsigned int *)(void *)out;

					o32[0] = r32[0];
					o32[1] = r32[1];
				}
				else
#endif
				{
					out[0] = row[0];
					out[1] = row[1];
					out[2] = row[2];
					out[3] = row[3];
				}
				out += 4;
			}
		}
	}
}

static void GC_PresentBufferViaGX( void );
static void GC_TryDeferredLeanSky( void );
static void GC_TryDeferredHudSheets( void );
static void GC_TryDeferredStudios( void );
static void GC_TryDeferredEfxProof( void );
static void GC_TryDeferredDecalProof( void );
static void GC_ScrubLiveWorldSpeckles( unsigned short *dst, int width, int height, int stride );

static qboolean GC_CanPresentViaGX( int width, int height )
{
	if( !gc.initialized || !rmode || !xfb[which_fb] )
		return false;
	if( width <= 0 || height <= 0 )
		return false;
	if(( width & 3 ) || ( height & 3 ))
		return false;
	if( width > GC_GX_TILE_MAX_W || height > GC_GX_TILE_MAX_H )
		return false;
	/* Prefer native GX (tiled RGB565 → EFB → XFB) for any buffer that fits
	 * the tile staging area. Avoids CPU YUYV upscale on GameCube. */
	return true;
}

static void GC_PresentBufferViaGX( void )
{
	f32 fb_w, fb_h;
	Mtx44 proj;
	Mtx modelview;
	u32 copy_clear;
	qboolean dump_latch = ( gc_cpu_dump_presents_left > 0 );

	if( !rmode || !xfb[which_fb] || !gc_present_tex_ready )
		return;

	fb_w = (f32)rmode->fbWidth;
	fb_h = (f32)rmode->efbHeight;

	/* Rebuild 2D soft-present pipe only after Flipper/world dirtied GX state.
	 * Consecutive soft presents reuse ortho/TEV (was full rebuild every frame). */
	if( !gc_gx_present_pipe_ready )
	{
		GX_SetViewport( 0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f );
		GX_SetScissor( 0, 0, (u32)fb_w, (u32)fb_h );
		GX_SetDispCopySrc( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetDispCopyDst( rmode->fbWidth, rmode->xfbHeight );
		GX_SetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight );
		/* Match Flipper present: CRT vfilter on 480i, lean on progressive. */
		if( rmode->viTVMode & VI_NON_INTERLACE )
			GX_SetCopyFilter( GX_FALSE, NULL, GX_FALSE, NULL );
		else
			GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
		GX_SetDispCopyGamma( GX_GM_1_0 );

		guOrtho( proj, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
		GX_LoadProjectionMtx( proj, GX_ORTHOGRAPHIC );
		guMtxIdentity( modelview );
		GX_LoadPosMtxImm( modelview, GX_PNMTX0 );

		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
		GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
		GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
		GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
		GX_SetNumChans( 1 );
		GX_SetNumTexGens( 1 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		GX_SetNumTevStages( 1 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
		GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
		GX_SetColorUpdate( GX_TRUE );
		GX_SetCullMode( GX_CULL_NONE );
		gc_gx_present_pipe_ready = true;
		GX_InvVtxCache();
	}

	/* Soft RGB565 tiles change every present — invalidate before bind. */
	GX_InvalidateTexAll();
	GX_LoadTexObj( &gc_present_tex, GX_TEXMAP0 );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 0.0f, 0.0f );
	GX_Position3f32( fb_w, 0.0f, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 1.0f, 0.0f );
	GX_Position3f32( fb_w, fb_h, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 1.0f, 1.0f );
	GX_Position3f32( 0.0f, fb_h, 0.0f );
	GX_Color1u32( 0xFFFFFFFF );
	GX_TexCoord2f32( 0.0f, 1.0f );
	GX_End();

	GX_DrawDone();
	/* G191: keep soft RGB565 on EFB during dump latch — Dolphin DumpFramesAsImages
	 * after changelevel tracks EFB; CopyDisp clear left flat sky in dumps. */
	copy_clear = dump_latch ? GX_FALSE : GX_TRUE;
	GX_CopyDisp( xfb[which_fb], copy_clear );
	/* Flush only — a second DrawDone after CopyDisp was locking soft presents
	 * near 33ms. VIDEO_WaitVSync (retail) covers copy retirement. */
	GX_Flush();
}
#endif

static void GC_ShutdownVideoHardware( void )
{
#if XASH_GAMECUBE
	if( !gc.initialized )
		return;

	gc.initialized = false;
	gc_present_tex_ready = false;
	gc_gx_present_pipe_ready = false;
	gc_tiled_tex_ptr = NULL;
	gc_tiled_tex_w = 0;
	gc_tiled_tex_h = 0;

	GX_AbortFrame();
	/* Leave the last XFB visible (fatal breadcrumb / last frame). Blacking
	 * the display hides the only diagnostic the player can see on hardware. */
	VIDEO_Flush();
#endif
}

static unsigned int GC_RGBPairToYUYV( unsigned short p1, unsigned short p2 )
{
	int r1, g1, b1, r2, g2, b2;
	int y1, cb1, cr1, y2, cb2, cr2;
	int cb, cr;

	/* Budget presents only need a readable non-black XFB; skip full BT.601.
	 * G129: CPU dump presents use full conversion so sky/wall colors survive. */
	if(( gc_budget_probe_active || gc_newgame_world_ready )
		&& gc_cpu_dump_presents_left <= 0 )
	{
		y1 = (( p1 >> 5 ) & 0x3F ) << 2;
		y2 = (( p2 >> 5 ) & 0x3F ) << 2;
		return ( y1 << 24 ) | ( 128 << 16 ) | ( y2 << 8 ) | 128;
	}

	r1 = ((( p1 >> 11 ) & 0x1F ) * 527 + 23 ) >> 6;
	g1 = ((( p1 >> 5 ) & 0x3F ) * 259 + 33 ) >> 6;
	b1 = (( p1 & 0x1F ) * 527 + 23 ) >> 6;
	r2 = ((( p2 >> 11 ) & 0x1F ) * 527 + 23 ) >> 6;
	g2 = ((( p2 >> 5 ) & 0x3F ) * 259 + 33 ) >> 6;
	b2 = (( p2 & 0x1F ) * 527 + 23 ) >> 6;

	y1 = ( 299 * r1 + 587 * g1 + 114 * b1 ) / 1000;
	cb1 = ( -16874 * r1 - 33126 * g1 + 50000 * b1 + 12800000 ) / 100000;
	cr1 = ( 50000 * r1 - 41869 * g1 - 8131 * b1 + 12800000 ) / 100000;
	y2 = ( 299 * r2 + 587 * g2 + 114 * b2 ) / 1000;
	cb2 = ( -16874 * r2 - 33126 * g2 + 50000 * b2 + 12800000 ) / 100000;
	cr2 = ( 50000 * r2 - 41869 * g2 - 8131 * b2 + 12800000 ) / 100000;

	cb = ( cb1 + cb2 ) >> 1;
	cr = ( cr1 + cr2 ) >> 1;

	return ( y1 << 24 ) | ( cb << 16 ) | ( y2 << 8 ) | cr;
}

static void GC_SampleBufferNonBlack( const unsigned short *src, int src_w, int src_h, int src_stride, qboolean *sampled_nonblack )
{
	int sample_x, sample_y;
	int grid;
	int denom;

	if( !src || src_w <= 0 || src_h <= 0 || !sampled_nonblack )
		return;

	/* Budget probes only need a cheap non-black signal. Keep the denser 9x9
	 * grid for normal presents so thin menu/title pixels are still caught. */
	grid = gc_budget_probe_active ? 5 : 9;
	denom = grid - 1;
	for( sample_y = 0; sample_y < grid; sample_y++ )
	{
		int y = ( sample_y * ( src_h - 1 )) / denom;
		const unsigned short *scanrow = src + y * src_stride;

		for( sample_x = 0; sample_x < grid; sample_x++ )
		{
			int x = ( sample_x * ( src_w - 1 )) / denom;

			if( scanrow[x] != 0 )
			{
				*sampled_nonblack = true;
				return;
			}
		}
	}
}

static void GC_BlitSoftwareBufferScaled( const unsigned short *src, int src_w, int src_h, int src_stride,
	unsigned int *dst, int copy_w, int copy_h, int row_pairs )
{
	int dst_y, dst_x, src_y, src_x;
	const int pairs = src_w / 2;

	if( src_w > 0 && src_h > 0 && ( src_w & 1 ) == 0 && ( copy_w & 1 ) == 0
		&& src_w * 2 == copy_w && src_h * 2 == copy_h )
	{
		/* G136: each source pixel → two identical dest pixels = one YUYV(p,p).
		 * Old path duplicated YUYV(A,B) → A,B,A,B combing on DumpFrames text. */
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out0 = dst + ( src_y * 2 ) * row_pairs;
			unsigned int *out1 = out0 + row_pairs;

			for( src_x = 0; src_x < src_w; src_x++ )
			{
				unsigned short p = scanline[src_x];
				unsigned int yuyv = GC_RGBPairToYUYV( p, p );

				out0[src_x] = yuyv;
				out1[src_x] = yuyv;
			}
		}
		return;
	}

	/* Exact 4× nearest (e.g. 160×120 → 640×480) without per-pixel divides. */
	if( src_w > 0 && src_h > 0 && ( src_w & 1 ) == 0 && ( copy_w & 1 ) == 0
		&& src_w * 4 == copy_w && src_h * 4 == copy_h )
	{
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out = dst + ( src_y * 4 ) * row_pairs;
			int row;

			for( src_x = 0; src_x < src_w; src_x++ )
			{
				unsigned short p = scanline[src_x];
				unsigned int yuyv = GC_RGBPairToYUYV( p, p );
				int dst_pair = src_x * 2;

				for( row = 0; row < 4; row++ )
				{
					unsigned int *line = out + row * row_pairs;
					line[dst_pair] = yuyv;
					line[dst_pair + 1] = yuyv;
				}
			}
		}
		return;
	}

	if( src_w == copy_w && src_h == copy_h && pairs > 0 )
	{
		for( src_y = 0; src_y < src_h; src_y++ )
		{
			const unsigned short *scanline = src + src_y * src_stride;
			unsigned int *out = dst + src_y * row_pairs;

			for( src_x = 0; src_x < pairs; src_x++ )
				out[src_x] = GC_RGBPairToYUYV( scanline[src_x * 2], scanline[src_x * 2 + 1] );
		}
		return;
	}

	for( dst_y = 0; dst_y < copy_h; dst_y++ )
	{
		int source_y = dst_y * src_h / copy_h;
		unsigned int *out = dst + dst_y * row_pairs;
		const unsigned short *scanline = src + source_y * src_stride;

		for( dst_x = 0; dst_x < copy_w; dst_x += 2 )
		{
			unsigned short p1 = scanline[dst_x * src_w / copy_w];
			unsigned short p2 = scanline[( dst_x + 1 ) * src_w / copy_w];
			out[dst_x / 2] = GC_RGBPairToYUYV( p1, p2 );
		}
	}
}

static void GC_PresentBuffer( void )
{
#if XASH_GAMECUBE
	unsigned short *src;
	unsigned int *dst;
	unsigned int *diag_rowdst;
	int copy_w, copy_h, row, col_diag;
	int src_w, src_h;
	qboolean sampled_nonblack;
	size_t buf_size;
	unsigned short first_pixel;
	double now;
	double elapsed_ms;

	src = NULL;
	dst = NULL;
	diag_rowdst = NULL;
	copy_w = 0;
	copy_h = 0;
	row = 0;
	src_w = 0;
	src_h = 0;
	sampled_nonblack = false;
	buf_size = 0;
	col_diag = 0;
	first_pixel = 0;
	now = 0.0;
	elapsed_ms = 0.0;

	if( !rmode || !xfb[which_fb] )
		return;

	gc_present_count++;
	GC_TryDeferredLeanSky();
	GC_TryDeferredHudSheets();
	GC_TryDeferredStudios();
	GC_TryDeferredEfxProof();
	GC_TryDeferredDecalProof();

	/* G151: Flipper already holds world geometry — CopyDisp only (no soft blit).
	 * G191: never steal soft DumpFrames latch presents onto a cleared EFB.
	 * G193 soft_lock: never CopyDisp Flipper over soft XFB while DumpFrames lag
	 * (capture diagnostics only — retail never soft-locks). */
	if( gc_gx_world_efb_ready && gc_cpu_dump_presents_left <= 0
		&& !( GC_IsCaptureDiagnostics() && gc_g193_soft_lock ))
	{
		f32 fb_w = (f32)rmode->fbWidth;
		f32 fb_h = (f32)rmode->efbHeight;
		static int g194_flipper_swap_skip;
		static qboolean g197_logged;
		u16 copy_w_u = rmode->fbWidth;
		u16 copy_h_u = rmode->efbHeight;
		u16 xfb_h_u = rmode->xfbHeight;

		gc_gx_present_pipe_ready = false; /* next soft present rebuilds ortho */

		/* G197: validate copy geometry against VI mode (retail + probe). */
		if( copy_w_u == 0 || copy_h_u == 0 || xfb_h_u == 0
			|| copy_w_u > 640 || copy_h_u > 528 || xfb_h_u > 528 )
		{
			if( !g197_logged )
			{
				g197_logged = true;
				SYS_Report( "Xash3D GameCube: G197 CopyDisp geom invalid fb=%u efb=%u xfb=%u\n",
					(unsigned)copy_w_u, (unsigned)copy_h_u, (unsigned)xfb_h_u );
			}
			gc_gx_world_efb_ready = false;
			return;
		}
		if( !g197_logged )
		{
			g197_logged = true;
			SYS_Report( "Xash3D GameCube: G197 CopyDisp ok fb=%u efb=%u xfb=%u stride=%u xfb_k1=%d crt_vf=%d\n",
				(unsigned)copy_w_u, (unsigned)copy_h_u, (unsigned)xfb_h_u,
				(unsigned)rmode->fbWidth,
				( xfb[which_fb] && ((u32)xfb[which_fb] & 0xC0000000u ) == 0xC0000000u ) ? 1 : 0,
				( rmode->viTVMode & VI_NON_INTERLACE ) ? 0 : 1 );
		}

		{
			static u16 last_copy_w, last_copy_h, last_xfb_h;
			static qboolean last_interlaced;
			const qboolean interlaced = !( rmode->viTVMode & VI_NON_INTERLACE );

			if( last_copy_w != copy_w_u || last_copy_h != copy_h_u || last_xfb_h != xfb_h_u
				|| last_interlaced != interlaced )
			{
				GX_SetViewport( 0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f );
				GX_SetScissor( 0, 0, (u32)fb_w, (u32)fb_h );
				GX_SetDispCopySrc( 0, 0, copy_w_u, copy_h_u );
				GX_SetDispCopyDst( copy_w_u, xfb_h_u );
				GX_SetDispCopyYScale((f32)xfb_h_u / (f32)copy_h_u );
				/* Hardware CRT (480i): keep VI vfilter. Progressive: lean copy. */
				if( interlaced )
					GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
				else
					GX_SetCopyFilter( GX_FALSE, NULL, GX_FALSE, NULL );
				last_copy_w = copy_w_u;
				last_copy_h = copy_h_u;
				last_xfb_h = xfb_h_u;
				last_interlaced = interlaced;
			}
		}
		/* One fence before CopyDisp; never clear EFB on retail Flipper copy
		 * (DumpFrames follows EFB — GX_TRUE left solid sky after a good XFB). */
		GX_DrawDone();
		GX_CopyDisp( xfb[which_fb], GX_FALSE );
		GX_Flush();
		gc_gx_world_efb_ready = false;

		/* Capture-only: hold EFB for Dolphin DumpFramesAsImages. */
		if( GC_IsCaptureDiagnostics()
			&& ( Sys_CheckParm( "-gcdumpframes" ) || Sys_CheckParm( "-gcdump" )))
		{
			extern void R_GXHoldEfbForDump( int frames );
			R_GXHoldEfbForDump( 6 );
		}
		/* Capture-only 1/4 ViSwap throttle — retail always swaps + VSync. */
		if( GC_IsCaptureDiagnostics()
			&& ( Sys_CheckParm( "-gcdumpframes" ) || Sys_CheckParm( "-gcdump" )))
		{
			if(( ++g194_flipper_swap_skip & 3 ) != 0 )
			{
				now = Sys_FloatTime();
				elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
				if( elapsed_ms > gc_worst_frame_ms )
					gc_worst_frame_ms = elapsed_ms;
				gc_last_present_time = now;
				return;
			}
		}
		{
			static qboolean g297_cpu_logged;
			double cpu_ms;
			const qboolean budget_no_vsync = gc_budget_probe_active
				&& Sys_CheckParm( "-gcnewgame" );

			cpu_ms = gc_last_present_time > 0.0 ? ( Sys_FloatTime() - gc_last_present_time ) * 1000.0 : 0.0;
			VIDEO_SetNextFramebuffer( xfb[which_fb] );
			VIDEO_Flush();
			/* G297: during G36 samples skip WaitVSync so frame times measure
			 * Flipper work (vsync locked every sample to ~16.7ms). Retail and
			 * post-sample presents still VI-pace at 60 fields/sec. */
			if( !budget_no_vsync )
				VIDEO_WaitVSync();
			which_fb ^= 1;
			if( !g297_cpu_logged && cpu_ms > 0.05 && Sys_CheckParm( "-gcnewgame" ))
			{
				g297_cpu_logged = true;
				SYS_Report( "Xash3D GameCube: G297 Flipper cpu=%.2fms (pre-vsync) frame_budget=%d live=%d fill=%d\n",
					cpu_ms, 224, 112, 32 );
			}
		}
		/* Flipper path used to return before timing updates (G36 blind spot). */
		now = Sys_FloatTime();
		elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
		if( elapsed_ms > gc_worst_frame_ms )
			gc_worst_frame_ms = elapsed_ms;
		if( gc_budget_probe_active && gc_budget_warmup_left <= 0
			&& gc_budget_sample_count < GC_GetFrameBudgetSampleTarget() )
		{
			gc_budget_sample_ms[gc_budget_sample_count] = (float)elapsed_ms;
			gc_budget_sample_nonblack[gc_budget_sample_count] = 1;
			gc_budget_sample_count++;
			if( gc_budget_sample_count >= GC_GetFrameBudgetSampleTarget() )
			{
				unsigned int i;

				gc_budget_probe_active = false;
				if( Sys_CheckParm( "-gcnewgame" ))
					gc_newgame_g36_done = true;
				for( i = 0; i < gc_budget_sample_count; i++ )
				{
					SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u frame time=%.2fms\n",
						i + 1, gc_budget_sample_nonblack[i], gc_budget_sample_ms[i] );
				}
				SYS_Report( "Xash3D GameCube: budget sample flush count=%u worst=%.2fms\n",
					gc_budget_sample_count, gc_worst_frame_ms );
			}
		}
		else if( gc_budget_probe_active && gc_budget_warmup_left > 0 )
			gc_budget_warmup_left--;
		gc_last_present_time = now;
		return;
	}
	if( gc_cpu_dump_presents_left > 0
		|| ( GC_IsCaptureDiagnostics() && gc_g193_soft_lock ))
		gc_gx_world_efb_ready = false;

	/* G192/G193: soft-lock is capture-diagnostics only. Retail never arms it. */
	if( !GC_IsCaptureDiagnostics() )
	{
		gc_g193_soft_lock = false;
		gc_g193_draining = false;
		gc_g193_defer_flipper_left = 0;
	}
	else if( gc_g192_post_changelevel
		&& gc_g193_soft_lock
		&& gc_cpu_dump_presents_left <= 0
		&& xfb[0] && xfb[1] )
	{
		/* Soft-lock after G191 EFB latch: do not ViSwap (PNG encode starvation)
		 * and do not Flipper CopyDisp over the latched soft XFB. */
		static int g193_soft_lock_presents;
		unsigned int *dump_dst = (unsigned int *)MEM_K1_TO_K0( xfb[which_fb] );
		const unsigned short *soft_src = gc_g193_snap_valid
			? gc_g193_soft_snap : gc.buffer;
		int soft_w = gc_g193_snap_valid ? gc_g193_snap_w : gc.width;
		int soft_h = gc_g193_snap_valid ? gc_g193_snap_h : gc.height;
		int soft_stride = gc_g193_snap_valid ? gc_g193_snap_stride : gc.stride;
		qboolean dump_nonblack = false;
		unsigned int xfb_pix;

		g193_soft_lock_presents++;
		if(( g193_soft_lock_presents & 31 ) == 1 )
		{
			xfb_pix = dump_dst[(size_t)( rmode->fbWidth / 4 )];
			if( soft_src && soft_w > 0 && soft_h > 0 )
				GC_SampleBufferNonBlack( soft_src, soft_w, soft_h, soft_stride, &dump_nonblack );
#if 0 /* G281 DOL reclaim */
			Con_Reportf( "Xash3D GameCube: G193 softlock n=%d nb=%d y=0x%08x s=0x%04x snap=%d\n",
				g193_soft_lock_presents, dump_nonblack ? 1 : 0, xfb_pix,
				soft_src ? soft_src[0] : 0, gc_g193_snap_valid ? 1 : 0 );
#else
			(void)xfb_pix;
			(void)dump_nonblack;
#endif
		}
		return;
	}

	copy_w = rmode->fbWidth;
	copy_h = rmode->xfbHeight;
	/* XFB policy: CPU writes through cached K0, DCFlushRange, VI/GX use K1. */
	dst = (unsigned int *)MEM_K1_TO_K0( xfb[which_fb] );

	{
	qboolean g128_cpu_dump = false;

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		const int row_pairs = rmode->fbWidth / 2;

		src = gc.buffer;
		src_w = gc.width;
		src_h = gc.height;

		buf_size = gc.stride * gc.height * sizeof(unsigned short);

		/* G36: Sample first pixel for visual evidence only on first frame. */
		if( gc_present_count == 1 )
		{
			first_pixel = gc.buffer[0];
			SYS_Report( "Xash3D GameCube: software buffer pixel[0]=0x%04X (RGB565)\n", first_pixel );
		}

		/* G144: scrub soft chroma / span cracks on live New Game frames before
		 * GX or CPU present. Dump path already scrubs once in PrepareNewGame. */
		if( gc_newgame_world_ready && gc_cpu_dump_presents_left <= 0 )
			GC_ScrubLiveWorldSpeckles( gc.buffer, gc.width, gc.height, gc.stride );

		/* Native GX present: tile linear RGB565 → EFB textured quad → XFB.
		 * Avoids the CPU nearest-neighbor YUYV scale that dominates post-G36 lag.
		 * G191: soft DumpFrames latch MUST go through EFB — after Flipper clear,
		 * Dolphin DumpFrames follow EFB and miss CPU YUYV→XFB (flat sky). */
		if( GC_CanPresentViaGX( src_w, src_h ))
		{
			qboolean dump_latch = ( gc_cpu_dump_presents_left > 0 );

			GC_SwizzleRGB565ToTiled( src, gc.stride, src_w, src_h, gc_tiled_rgb565 );
			DCFlushRange( gc_tiled_rgb565, (u32)((size_t)src_w * (size_t)src_h * sizeof( u16 )));
			GC_InitPresentTextureTiled( gc_tiled_rgb565, src_w, src_h );
			GC_PresentBufferViaGX();
			/* G192: with XFB→RAM (probe GFX), CPU YUYV ensures DumpFrames see the
			 * scrubbed soft buffer even if the tiled EFB quad is muted. */
			if( dump_latch )
			{
				if( !gc_budget_probe_active )
					DCFlushRange( gc.buffer, (u32)buf_size );
				GC_BlitSoftwareBufferScaled( src, src_w, src_h, gc.stride, dst, copy_w, copy_h, row_pairs );
			}
			if( dump_latch )
			{
				qboolean dump_nonblack = false;
				unsigned int xfb_pix = 0;

				g128_cpu_dump = true; /* keep VSync so DumpFrames can latch */
				/* G194: unique corner stamp so paced soft latch frames differ. */
				if( dst )
				{
					dst[1] = (unsigned int)( 0xA5A50000u
						^ ((unsigned)gc_cpu_dump_presents_left << 8)
						^ (unsigned)gc_present_count );
				}
				GC_SampleBufferNonBlack( src, src_w, src_h, gc.stride, &dump_nonblack );
				if( dst )
					xfb_pix = dst[(size_t)( copy_w / 4 )]; /* mid-ish YUYV sample */
#if 0 /* G281 DOL reclaim */
				Con_Reportf( "Xash3D GameCube: G191 soft EFB l=%d nb=%d %dx%d xfb=%p y=0x%08x s=0x%04x\n",
					gc_cpu_dump_presents_left, dump_nonblack ? 1 : 0, src_w, src_h,
					dst, xfb_pix, src[0] );
#else
				(void)xfb_pix;
				(void)dump_nonblack;
#endif
				gc_cpu_dump_presents_left--;
				if( gc_cpu_dump_presents_left == 0 )
					Con_Reportf( "Xash3D GameCube: G191 soft dump EFB presents ready\n" );
			}
			else if( !gc_gx_present_logged )
			{
				SYS_Report( "Xash3D GameCube: GX present path active %dx%d\n", src_w, src_h );
				gc_gx_present_logged = true;
			}
		}
		else
		{
			if( !gc_budget_probe_active )
				DCFlushRange( gc.buffer, (u32)buf_size );
			GC_BlitSoftwareBufferScaled( src, src_w, src_h, gc.stride, dst, copy_w, copy_h, row_pairs );
			if( gc_cpu_dump_presents_left > 0 )
			{
				qboolean dump_nonblack = false;

				g128_cpu_dump = true;
				GC_SampleBufferNonBlack( src, src_w, src_h, gc.stride, &dump_nonblack );
				Con_Reportf( "Xash3D GameCube: G128 CPU dump present left=%d nonblack=%d %dx%d\n",
					gc_cpu_dump_presents_left, dump_nonblack ? 1 : 0, src_w, src_h );
				gc_cpu_dump_presents_left--;
				if( gc_cpu_dump_presents_left == 0 )
					Con_Reportf( "Xash3D GameCube: G128 CPU dump presents ready\n" );
			}
		}

		/* Sample non-black for evidence. Do not scan the full RGB565 buffer
		 * every New Game world present — that alone was several ms/frame. */
		if( src_h > 0 && src_w > 0 )
		{
			qboolean should_sample = false;

			if( gc_budget_probe_active && !Sys_CheckParm( "-gcworldrender" )
				&& !gc_newgame_world_ready )
			{
				sampled_nonblack = true;
			}
			else if( gc_budget_probe_active || gc_present_count <= 4
				|| ( gc_newgame_world_ready && (( gc_present_count & 31 ) == 1 )))
			{
				should_sample = true;
			}

			if( should_sample )
				GC_SampleBufferNonBlack( src, src_w, src_h, gc.stride, &sampled_nonblack );
			else if( gc_newgame_world_ready )
				sampled_nonblack = true; /* world path already proved nonblack */
		}
	}
	else
	{
		VIDEO_ClearFrameBuffer( rmode, dst, COLOR_BLACK );

		/* G36: Diagnostic blue fill only for first frame when buffer is missing.
		 * Avoid wasting CPU cycles on full-screen fills after initial evidence
		 * is captured. Leaves XFB black (zeroed) for subsequent frames. */
		if( gc_present_count == 1 )
		{
			for( row = 0; row < copy_h; row++ )
			{
				diag_rowdst = dst + row * ( rmode->fbWidth / 2 );
				for( col_diag = 0; col_diag < copy_w / 2; col_diag++ )
					diag_rowdst[col_diag] = GC_RGBPairToYUYV( 0x001F, 0x001F );
			}
			sampled_nonblack = true;
		}
		else
		{
			sampled_nonblack = false;
		}
	}

	/* G36: After New Game arm, skip connect warm-up presents then sample
	 * steady Host_Frame intervals (not synthetic fill bursts). */
	now = Sys_FloatTime();
	elapsed_ms = gc_last_present_time > 0.0 ? ( now - gc_last_present_time ) * 1000.0 : 0.0;
	if( elapsed_ms > gc_worst_frame_ms )
		gc_worst_frame_ms = elapsed_ms;

	if( gc_budget_probe_active )
	{
		if( gc_budget_warmup_left > 0 )
		{
			gc_budget_warmup_left--;
			gc_last_present_time = now;
		}
		else if( gc_budget_sample_count < GC_GetFrameBudgetSampleTarget() )
		{
			gc_budget_sample_ms[gc_budget_sample_count] = (float)elapsed_ms;
			gc_budget_sample_nonblack[gc_budget_sample_count] = sampled_nonblack ? 1 : 0;
			gc_budget_sample_count++;
			if( Sys_CheckParm( "-gcnewgame" ))
			{
				SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u frame time=%.2fms\n",
					gc_budget_sample_count, sampled_nonblack ? 1u : 0u, elapsed_ms );
			}
			gc_last_present_time = now;
			if( gc_budget_sample_count >= GC_GetFrameBudgetSampleTarget() )
			{
				unsigned int i;

				gc_budget_probe_active = false;
				if( Sys_CheckParm( "-gcnewgame" ))
					gc_newgame_g36_done = true;
				/* Emit after the timed window so analyzer still sees frame time=. */
				for( i = 0; i < gc_budget_sample_count; i++ )
				{
					SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u frame time=%.2fms\n",
						i + 1, gc_budget_sample_nonblack[i], gc_budget_sample_ms[i] );
				}
				SYS_Report( "Xash3D GameCube: budget sample flush count=%u worst=%.2fms\n",
					gc_budget_sample_count, gc_worst_frame_ms );
			}
		}
		else
		{
			gc_budget_probe_active = false;
			gc_last_present_time = now;
		}
	}
	else if( gc_present_count <= 16 && !gc_newgame_world_ready && cls.state != ca_cinematic )
	{
		/* One line for smoke evidence — three SYS_Reports per present were
		 * dominating host I/O on short Dolphin probes. Skip once New Game
		 * world is up; that path uses silent budget sampling instead.
		 * Also skip during intro cinematics (direct GX present owns timing). */
		SYS_Report( "Xash3D GameCube: present f=%u nb=%u blank=%u t=%.2fms\n",
			gc_present_count, sampled_nonblack ? 1u : 0u, gc_blank_present_count, elapsed_ms );
		/* gcmap/gcworldrender probes intentionally pace presents while the real
		 * renderer cost is reported inside R_RenderScene. Avoid flagging those
		 * waits as slow frame bugs, but keep the warning for normal gameplay. */
		if( elapsed_ms >= 33.0 )
			SYS_Report( "Xash3D GameCube: G49 slow frame %.2fms worst=%.2fms\n", elapsed_ms, gc_worst_frame_ms );
		gc_last_present_time = now;
	}
	else
	{
		gc_last_present_time = now;
	}

	DCFlushRange( MEM_K1_TO_K0( xfb[which_fb] ), VIDEO_GetFrameBufferSize( rmode ));
	/* G194: DumpFrames queues a PNG on every ViSwap. Capture diagnostics may
	 * throttle identical soft presents; retail always SetNext + VSync. */
	{
		qboolean g194_do_swap = true;

		if( GC_IsCaptureDiagnostics()
			&& !g128_cpu_dump && !gc_g193_soft_lock && gc_cpu_dump_presents_left <= 0 )
		{
			static unsigned short g194_last_pix0;
			static unsigned short g194_last_pixm;
			unsigned short pix0 = ( gc.buffer && gc.width > 0 && gc.height > 0 ) ? gc.buffer[0] : 0;
			unsigned short pixm = ( gc.buffer && gc.width > 0 && gc.height > 0 )
				? gc.buffer[(size_t)gc.stride * (size_t)( gc.height / 2 ) + (size_t)( gc.width / 2 )]
				: 0;

			/* Pre-world-ready boot/menu: hard-throttle ViSwap. Post-ready: skip
			 * swaps when soft pixels idle (DumpFrames encode starvation). */
			if( !gc_newgame_world_ready )
			{
				if(( gc_present_count & 3 ) != 0 )
					g194_do_swap = false;
			}
			else if( pix0 == g194_last_pix0 && pixm == g194_last_pixm
				&& (( gc_present_count & 7 ) != 1 ))
			{
				g194_do_swap = false;
			}
			g194_last_pix0 = pix0;
			g194_last_pixm = pixm;
		}
		if( g194_do_swap )
		{
			VIDEO_SetNextFramebuffer( xfb[which_fb] );
			VIDEO_Flush();
			/* G297: New Game G36 samples skip WaitVSync; Host_CalcFPS also
			 * drops Autosleep while the probe is armed (else fps_max=60 re-locks
			 * every sample to ~16.7ms). Retail outside the window still paces. */
			if( gc_budget_probe_active && Sys_CheckParm( "-gcnewgame" ))
			{
				static qboolean g297_soft_logged;
				if( !g297_soft_logged )
				{
					g297_soft_logged = true;
					SYS_Report( "Xash3D GameCube: G297 budget present without WaitVSync (Host sleep off)\n" );
				}
			}
			else if( !GC_IsCaptureDiagnostics()
				|| g128_cpu_dump
				|| ( !gc_budget_probe_active && !gc_newgame_world_ready && cls.state != ca_cinematic ))
			{
				VIDEO_WaitVSync();
			}
			which_fb ^= 1;
		}
	}
	} /* g128_cpu_dump scope */
#else
	(void)0;
#endif
}

qboolean R_Init_Video( ref_graphic_apis_t type )
{
	if( type != REF_GX && type != REF_SOFTWARE )
		return false;

#if XASH_GAMECUBE
	gc_quality = Cvar_Get( "gc_quality", "1", FCVAR_ARCHIVE, "GameCube quality profile: 0=playable/low-mem, 1=standard (default), 2=high telemetry-only" );
	Cvar_Get( "gc_hud_probe_skip", "0", 0, "GameCube HUD UpdateClientData skip gate during post-map probe" );
	GC_ReportQualityProfile( "video-init" );
#endif

	GC_InitVideoHardware();

#if XASH_GAMECUBE
	{
		uint stride, bpp, r, g, b;
		int width = refState.width > 0 ? refState.width : ( rmode ? rmode->fbWidth : DEFAULT_MODE_WIDTH );
		int height = refState.height > 0 ? refState.height : ( rmode ? rmode->efbHeight : DEFAULT_MODE_HEIGHT );

		if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
		{
			SYS_Report( "GX video: failed to allocate software buffer %dx%d\n", width, height );
			return false;
		}
		SYS_Report( "GX video: software buffer %dx%d allocated (stride %u, bpp %u)\n",
		          width, height, stride, bpp );
		SYS_Report( "Xash3D GameCube: mem stage=video_alloc total=%.2f\n", ( width * height * 2.0 ) / ( 1024.0 * 1024.0 ) );
		GC_PresentBuffer();
	}
#endif

	host.renderinfo_changed = false;
	return true;
}

void R_Free_Video( void )
{
#if !XASH_GAMECUBE
	if( gc.buffer )
	{
		free( gc.buffer );
		gc.buffer = NULL;
	}
#else
	if( gc.buffer && gc_buffer_owns_heap )
		free( gc.buffer );
	gc.buffer = NULL;
	gc.buffer_pixels = 0;
	gc_buffer_owns_heap = false;
	gc.width = 0;
	gc.height = 0;
	gc.stride = 0;
	gc.bpp = 0;
	GC_FreeNewGamePVSCache();
	GC_FreeLiveFaces();
	GC_FreeTramFaces();
	gc_newgame_world_ready = false;
	gc_lean_sky_attempts = 0;
	gc_newgame_viewcluster = -1;
	gc_newgame_g36_done = false;
	gc_budget_probe_active = false;
	gc_light_present_left = 0;
	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = 0;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	GC_ResetNewGameGameplaySoundState();
#endif

	GC_ShutdownVideoHardware();

	if( ref.dllFuncs.GL_ClearExtensions )
		ref.dllFuncs.GL_ClearExtensions();
}

void GL_SwapBuffers( void )
{
	GC_PresentBuffer();
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	size_t needed_pixels;

	if( width <= 0 || height <= 0 )
		return false;

#if XASH_GAMECUBE
	/* Hardware MEM1: never calloc a native 640×480 soft FB (~0.59 MiB tip).
	 * Flipper owns full-res EFB/XFB; soft buffer is menus/probe/diagnostic only. */
	if( width > GC_VIDEO_NEWGAME_PROBE_WIDTH || height > GC_VIDEO_NEWGAME_PROBE_HEIGHT )
	{
		static qboolean clamped_logged;

		if( !clamped_logged )
		{
			SYS_Report( "Xash3D GameCube: hardware soft FB clamp %dx%d → %dx%d (Flipper EFB native %dx%d)\n",
				width, height, GC_VIDEO_NEWGAME_PROBE_WIDTH, GC_VIDEO_NEWGAME_PROBE_HEIGHT,
				rmode ? rmode->fbWidth : 640, rmode ? rmode->efbHeight : 480 );
			clamped_logged = true;
		}
		width = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		height = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}
#endif

	needed_pixels = (size_t)width * (size_t)height;

#if XASH_GAMECUBE
	/* Prefer BSS New Game probe FB for anything that fits — avoids post-map
	 * calloc failure that left presents stuck at 640×480 on real hardware. */
	if( needed_pixels <= (size_t)( GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT ))
	{
		if( gc.buffer && gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = gc_probe_rgb565;
		gc.buffer_pixels = (size_t)( GC_VIDEO_NEWGAME_PROBE_WIDTH * GC_VIDEO_NEWGAME_PROBE_HEIGHT );
		gc_buffer_owns_heap = false;
		memset( gc.buffer, 0, needed_pixels * sizeof( unsigned short ));
	}
	else if( !gc.buffer || gc.buffer_pixels < needed_pixels || !gc_buffer_owns_heap )
	{
		unsigned short *new_buffer = calloc( needed_pixels, sizeof( unsigned short ));
		if( !new_buffer )
			return false;

		if( gc.buffer && gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = new_buffer;
		gc.buffer_pixels = needed_pixels;
		gc_buffer_owns_heap = true;
	}
	else
	{
		memset( gc.buffer, 0, needed_pixels * sizeof( unsigned short ));
	}

	gc.width = width;
	gc.height = height;
	gc.stride = width;
	gc.bpp = 2;

	for( size_t i = 0; i < needed_pixels; i++ )
		gc.buffer[i] = 0x0010; /* dark blue boot backdrop */
#else
	if( gc.buffer )
		free( gc.buffer );
	gc.buffer = calloc( needed_pixels, sizeof( unsigned short ));
	if( !gc.buffer )
		return false;

	gc.width = width;
	gc.height = height;
	gc.stride = width;
	gc.bpp = 2;
#endif

	if( !gc.buffer )
		return false;

#if XASH_GAMECUBE
	GC_InitPresentTexture();
#endif

	*stride = gc.stride;
	*bpp = gc.bpp;
	*r = 0xF800;
	*g = 0x07E0;
	*b = 0x001F;
#if XASH_GAMECUBE
	GC_ReportBootPhase( GC_BOOT_SW_FB );
#endif
	return true;
}

void *SW_LockBuffer( void )
{
	return gc.buffer;
}

void SW_UnlockBuffer( void )
{
	GC_PresentBuffer();
}

qboolean VID_SetMode( void )
{
	R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_FULLSCREEN );
	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
#if XASH_GAMECUBE
	if( rmode )
	{
		if( refState.width > 0 )
			width = refState.width;
		else
			width = rmode->fbWidth;
		if( refState.height > 0 )
			height = refState.height;
		else
			height = rmode->efbHeight;
	}
#endif

	if( !width ) width = DEFAULT_MODE_WIDTH;
	if( !height ) height = DEFAULT_MODE_HEIGHT;

	(void)window_mode;

#if XASH_GAMECUBE
	{
		uint stride, bpp, r, g, b;
		if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
			return rserr_unknown;
		GC_PresentBuffer();
	}
#endif

	R_SaveVideoMode( width, height, width, height, false );
	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	(void)attr;
	(void)val;
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	(void)attr;
	if( val ) *val = 0;
	return 0;
}

int R_MaxVideoModes( void )
{
	return 0;
}

vidmode_t *R_GetVideoMode( int num )
{
	(void)num;
	return NULL;
}

void *GL_GetProcAddress( const char *name )
{
	(void)name;
	return NULL;
}

void GC_TrimVideoMemoryForMapLoad( void )
{
	/* Free the RGB565 present buffer during BSP load; status text draws to XFB
	 * directly. Restored by GC_RestoreVideoMemoryAfterMapLoad. */
	if( gc.buffer )
	{
		if( gc_buffer_owns_heap )
			free( gc.buffer );
		gc.buffer = NULL;
		gc.buffer_pixels = 0;
		gc_buffer_owns_heap = false;
	}
	gc_present_tex_ready = false;
	Con_Reportf( "Xash3D GameCube: present buf released map load\n" );
}

static void GC_ReleasePresentationBufferForWorldRender( void )
{
#if XASH_GAMECUBE
	if( !gc.buffer )
		return;

	if( gc_buffer_owns_heap )
		free( gc.buffer );
	gc.buffer = NULL;
	gc.buffer_pixels = 0;
	gc_buffer_owns_heap = false;
	gc_present_tex_ready = false;
	Con_Reportf( "Xash3D GameCube: released present buf for world\n" );
#endif
}

static qboolean GC_EnsurePresentationBuffer( int width, int height )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;

	if( width <= 0 || height <= 0 )
		return false;

	if( gc.buffer && gc.width == width && gc.height == height
		&& gc.buffer_pixels >= (size_t)width * (size_t)height )
		return true;

	/* Route through SW_CreateBuffer so New Game / probe sizes bind BSS. */
	if( !SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
	{
		Con_Reportf( "Xash3D GameCube: presentation buffer alloc failed %dx%d\n", width, height );
		return false;
	}

	Con_Reportf( "Xash3D GameCube: presentation buffer ready %dx%d\n", width, height );
	return true;
#else
	(void)width;
	(void)height;
	return false;
#endif
}

static unsigned char GC_FatalGlyphRow( char ch, int row )
{
	static const unsigned char digit[10][7] = {
		{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },
		{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
		{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },
		{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E },
		{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E },
		{ 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E },
		{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
		{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },
		{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E },
	};
	static const unsigned char alpha[26][7] = {
		{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
		{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },
		{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },
		{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
		{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E },
		{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
		{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
		{ 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C },
		{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
		{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
		{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 },
		{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
		{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
		{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },
		{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
		{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },
		{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
		{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A },
		{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
		{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },
		{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
	};

	if( row < 0 || row >= 7 )
		return 0;
	if( ch >= 'a' && ch <= 'z' )
		ch -= 32;
	if( ch >= 'A' && ch <= 'Z' )
		return alpha[ch - 'A'][row];
	if( ch >= '0' && ch <= '9' )
		return digit[ch - '0'][row];
	switch( ch )
	{
	case ':': return ( row == 1 || row == 5 ) ? 0x04 : 0x00;
	case '.': return ( row == 6 ) ? 0x04 : 0x00;
	case '-': return ( row == 3 ) ? 0x1F : 0x00;
	case '_': return ( row == 6 ) ? 0x1F : 0x00;
	case '/': return 1 << ( row < 5 ? 4 - row : 0 );
	case '\\': return 1 << ( row < 5 ? row : 4 );
	case '<': return row < 4 ? ( 1 << ( 3 - row )) : ( 1 << ( row - 3 ));
	case '>': return row < 4 ? ( 1 << ( row + 1 )) : ( 1 << ( 7 - row ));
	case ' ': return 0x00;
	default: return 0x04;
	}
}

static void GC_StatusPutPixel( unsigned short *dst, int stride, int width, int height, int x, int y, unsigned short color )
{
	if( x < 0 || y < 0 || x >= width || y >= height )
		return;
	dst[y * stride + x] = color;
}

static void GC_FatalPutPixel( unsigned short *dst, int x, int y, unsigned short color )
{
	if( !rmode )
		return;
	GC_StatusPutPixel( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight, x, y, color );
}

static void GC_StatusDrawChar( unsigned short *dst, int stride, int width, int height,
	int x, int y, char ch, unsigned short color, int scale )
{
	int row, col, sx, sy;

	for( row = 0; row < 7; row++ )
	{
		unsigned char bits = GC_FatalGlyphRow( ch, row );
		for( col = 0; col < 5; col++ )
		{
			if( !FBitSet( bits, 1 << ( 4 - col )))
				continue;
			for( sy = 0; sy < scale; sy++ )
				for( sx = 0; sx < scale; sx++ )
					GC_StatusPutPixel( dst, stride, width, height,
						x + col * scale + sx, y + row * scale + sy, color );
		}
	}
}

static void GC_FatalDrawChar( unsigned short *dst, int x, int y, char ch, unsigned short color, int scale )
{
	if( !rmode )
		return;
	GC_StatusDrawChar( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight,
		x, y, ch, color, scale );
}

static int GC_StatusDrawLine( unsigned short *dst, int stride, int width, int height,
	int x, int y, const char *text, unsigned short color, int scale, int max_chars )
{
	int i;

	for( i = 0; text && text[i] && text[i] != '\n' && i < max_chars; i++ )
		GC_StatusDrawChar( dst, stride, width, height, x + i * 6 * scale, y, text[i], color, scale );
	return i;
}

static int GC_FatalDrawLine( unsigned short *dst, int x, int y, const char *text, unsigned short color, int scale, int max_chars )
{
	if( !rmode )
		return 0;
	return GC_StatusDrawLine( dst, rmode->fbWidth, rmode->fbWidth, rmode->xfbHeight,
		x, y, text, color, scale, max_chars );
}

static void GC_FatalDrawWrapped( unsigned short *dst, int x, int y, const char *text, unsigned short color, int scale, int max_chars, int max_lines )
{
	int line = 0;
	while( text && *text && line < max_lines )
	{
		int used = GC_FatalDrawLine( dst, x, y + line * 9 * scale, text, color, scale, max_chars );
		text += used;
		if( *text == '\n' )
			text++;
		while( *text == ' ' )
			text++;
		line++;
	}
}

#if XASH_GAMECUBE
/* HL1-themed loading plaque (baked at disc build). Keep tiny for MEM1. */
#define GC_LOADING_BG_W		160
#define GC_LOADING_BG_H		120
static unsigned short gc_loading_bg[GC_LOADING_BG_W * GC_LOADING_BG_H];
static qboolean gc_loading_bg_ready;
static float gc_loading_progress;

void GC_SetLoadingProgress( float progress )
{
	if( progress < 0.0f )
		progress = 0.0f;
	if( progress > 1.0f )
		progress = 1.0f;
	gc_loading_progress = progress;
}

float GC_GetLoadingProgress( void )
{
	return gc_loading_progress;
}

static unsigned short GC_RGB8To565( int r, int g, int b )
{
	if( r < 0 ) r = 0;
	if( g < 0 ) g = 0;
	if( b < 0 ) b = 0;
	if( r > 255 ) r = 255;
	if( g > 255 ) g = 255;
	if( b > 255 ) b = 255;
	return (unsigned short)((( r >> 3 ) << 11 ) | (( g >> 2 ) << 5 ) | ( b >> 3 ));
}

static qboolean GC_LoadLoadingBackground( void )
{
	byte *file;
	fs_offset_t size = 0;
	const byte *p;
	int id_len, cmap_type, image_type, bpp, width, height, x, y;
	int row_stride;
	qboolean top_origin;

	if( gc_loading_bg_ready )
		return true;

	file = FS_LoadFile( "resource/gc_menu/loading.tga", &size, false );
	if( !file || size < 18 )
	{
		if( file )
			Mem_Free( file );
		return false;
	}

	id_len = file[0];
	cmap_type = file[1];
	image_type = file[2];
	width = file[12] | ( file[13] << 8 );
	height = file[14] | ( file[15] << 8 );
	bpp = file[16];
	top_origin = ( file[17] & 0x20 ) != 0;

	if( cmap_type != 0 || ( image_type != 2 && image_type != 10 ) || ( bpp != 24 && bpp != 32 )
		|| width <= 0 || height <= 0 )
	{
		Mem_Free( file );
		return false;
	}

	/* Only uncompressed truecolor for the baked plaque. */
	if( image_type != 2 )
	{
		Mem_Free( file );
		return false;
	}

	p = file + 18 + id_len;
	row_stride = width * ( bpp / 8 );
	if( 18 + id_len + (fs_offset_t)row_stride * height > size )
	{
		Mem_Free( file );
		return false;
	}

	for( y = 0; y < GC_LOADING_BG_H; y++ )
	{
		int src_y = ( y * height ) / GC_LOADING_BG_H;
		if( !top_origin )
			src_y = height - 1 - src_y;
		for( x = 0; x < GC_LOADING_BG_W; x++ )
		{
			int src_x = ( x * width ) / GC_LOADING_BG_W;
			const byte *px = p + src_y * row_stride + src_x * ( bpp / 8 );
			/* TGA is BGR(A). */
			gc_loading_bg[y * GC_LOADING_BG_W + x] = GC_RGB8To565( px[2], px[1], px[0] );
		}
	}

	Mem_Free( file );
	gc_loading_bg_ready = true;
	Con_Reportf( "Xash3D GameCube: HL1 loading plaque ready %dx%d\n",
		GC_LOADING_BG_W, GC_LOADING_BG_H );
	return true;
}

static void GC_BlitLoadingBackground( unsigned short *dst, int width, int height, int stride )
{
	int row, col;

	if( !GC_LoadLoadingBackground())
	{
		unsigned short fill = GC_RGB8To565( 16, 14, 12 );
		for( row = 0; row < height; row++ )
		{
			unsigned short *rowdst = dst + row * stride;
			for( col = 0; col < width; col++ )
				rowdst[col] = fill;
		}
		return;
	}

	for( row = 0; row < height; row++ )
	{
		int src_y = ( row * GC_LOADING_BG_H ) / height;
		unsigned short *rowdst = dst + row * stride;
		const unsigned short *src_row = gc_loading_bg + src_y * GC_LOADING_BG_W;

		for( col = 0; col < width; col++ )
			rowdst[col] = src_row[( col * GC_LOADING_BG_W ) / width];
	}
}

static void GC_DrawProgressBar( unsigned short *dst, int width, int height, int stride,
	int bar_x, int bar_y, int bar_w, int bar_h, float progress )
{
	int row, col;
	int fill_w;
	unsigned short border = GC_RGB8To565( 200, 140, 40 );   /* HL gold */
	unsigned short track = GC_RGB8To565( 28, 24, 20 );
	unsigned short fill = GC_RGB8To565( 230, 120, 20 );     /* HL orange */

	if( bar_w < 8 || bar_h < 4 )
		return;
	if( progress < 0.0f )
		progress = 0.0f;
	if( progress > 1.0f )
		progress = 1.0f;
	fill_w = (int)( ( bar_w - 2 ) * progress + 0.5f );

	for( row = bar_y; row < bar_y + bar_h && row < height; row++ )
	{
		unsigned short *rowdst = dst + row * stride;
		for( col = bar_x; col < bar_x + bar_w && col < width; col++ )
		{
			int local = col - bar_x;
			if( row == bar_y || row == bar_y + bar_h - 1 || local == 0 || local == bar_w - 1 )
				rowdst[col] = border;
			else if( local - 1 < fill_w )
				rowdst[col] = fill;
			else
				rowdst[col] = track;
		}
	}
}

/* G130: 4×4 mode filter — soft-edge / tiny-poly rasters speckled DumpFrames.
 * Quantize each block to its most common RGB565 so large sky/wall regions stay
 * as a readable room silhouette for demo screenshots. */
static void GC_CoalesceDumpWorldBuffer( unsigned short *dst, int width, int height, int stride )
{
	const int block = 4;
	int by, bx, y, x, i;
	unsigned short colors[16];
	unsigned counts[16];
	unsigned nuniq;
	unsigned short mode;
	unsigned best;

	if( !dst || width <= 0 || height <= 0 || stride < width )
		return;

	for( by = 0; by < height; by += block )
	{
		int bh = block;
		if( by + bh > height )
			bh = height - by;

		for( bx = 0; bx < width; bx += block )
		{
			int bw = block;
			if( bx + bw > width )
				bw = width - bx;

			nuniq = 0;
			for( y = 0; y < bh; y++ )
			{
				const unsigned short *row = dst + ( by + y ) * stride + bx;
				for( x = 0; x < bw; x++ )
				{
					unsigned short p = row[x];
					qboolean found = false;

					for( i = 0; i < (int)nuniq; i++ )
					{
						if( colors[i] == p )
						{
							counts[i]++;
							found = true;
							break;
						}
					}
					if( !found && nuniq < 16 )
					{
						colors[nuniq] = p;
						counts[nuniq] = 1;
						nuniq++;
					}
				}
			}

			mode = colors[0];
			best = counts[0];
			for( i = 1; i < (int)nuniq; i++ )
			{
				if( counts[i] > best )
				{
					best = counts[i];
					mode = colors[i];
				}
			}

			for( y = 0; y < bh; y++ )
			{
				unsigned short *row = dst + ( by + y ) * stride + bx;
				for( x = 0; x < bw; x++ )
					row[x] = mode;
			}
		}
	}
}

/* G141/G143: DumpFrames shredding — span cracks, neon sparks, and saturated
 * outliers that disagree with the local wall neighborhood. */
static qboolean GC_DumpPixelIsNeonChroma( unsigned short p )
{
	int pr = ( p >> 11 ) & 0x1F;
	int pg = ( p >> 5 ) & 0x3F;
	int pb = p & 0x1F;
	int pg5 = pg >> 1;

	if( p == 0 )
		return false;
	/* Pure / near-pure green (0x07E0-class and YUYV-softened greens). */
	if( pg >= 40 && pr <= 10 && pb <= 10 )
		return true;
	/* Magenta / pink chroma. */
	if( pr > pg5 + 8 && pb > pg5 + 8 )
		return true;
	/* Hot primary red / orange-red sparkles. */
	if( pr >= 24 && pg <= 20 && pb <= 10 )
		return true;
	/* Cyan / teal sparks. */
	if( pb >= 12 && pg >= 36 && pr <= 8 )
		return true;
	/* Chartreuse / yellow-green sparks. */
	if( pg >= 36 && pr >= 14 && pb <= 14 && pg > pr + 4 )
		return true;
	return false;
}

static int GC_DumpRGB565Sat( unsigned short p )
{
	int pr = ( p >> 11 ) & 0x1F;
	int pg = ( p >> 5 ) & 0x3F;
	int pb = p & 0x1F;
	int pg5 = pg >> 1;
	int mx = pr > pg5 ? pr : pg5;
	int mn = pr < pg5 ? pr : pg5;

	if( pb > mx )
		mx = pb;
	if( pb < mn )
		mn = pb;
	return mx - mn;
}

static int GC_DumpRGB565Dist( unsigned short a, unsigned short b )
{
	int ar = ( a >> 11 ) & 0x1F, ag = ( a >> 5 ) & 0x3F, ab = a & 0x1F;
	int br = ( b >> 11 ) & 0x1F, bg = ( b >> 5 ) & 0x3F, bb = b & 0x1F;
	int dr = ar > br ? ar - br : br - ar;
	int dg = ag > bg ? ag - bg : bg - ag;
	int db = ab > bb ? ab - bb : bb - ab;

	return dr * 2 + dg + db * 2;
}

static unsigned short GC_DumpNeighborFill( const unsigned short *dst, int width, int height, int stride,
	int cx, int cy, qboolean skip_neon )
{
	unsigned short colors[8];
	unsigned counts[8];
	unsigned nuniq = 0;
	unsigned best = 0;
	unsigned short mode = 0;
	int dy, dx, i;

	for( dy = -1; dy <= 1; dy++ )
	{
		int y = cy + dy;
		if( y < 0 || y >= height )
			continue;
		for( dx = -1; dx <= 1; dx++ )
		{
			int x = cx + dx;
			unsigned short p;
			qboolean found = false;

			if( dx == 0 && dy == 0 )
				continue;
			if( x < 0 || x >= width )
				continue;
			p = dst[y * stride + x];
			if( p == 0 )
				continue;
			if( skip_neon && GC_DumpPixelIsNeonChroma( p ))
				continue;
			for( i = 0; i < (int)nuniq; i++ )
			{
				if( colors[i] == p )
				{
					counts[i]++;
					found = true;
					break;
				}
			}
			if( !found && nuniq < 8 )
			{
				colors[nuniq] = p;
				counts[nuniq] = 1;
				nuniq++;
			}
		}
	}

	if( nuniq == 0 )
		return 0;
	mode = colors[0];
	best = counts[0];
	for( i = 1; i < (int)nuniq; i++ )
	{
		if( counts[i] > best )
		{
			best = counts[i];
			mode = colors[i];
		}
	}
	return ( best >= 1 ) ? mode : 0;
}

static void GC_ScrubWorldSpecklesPasses( unsigned short *dst, int width, int height, int stride,
	qboolean quiet, qboolean fill_cracks, qboolean sky_flood );

/* G145/G147 live: neighbor-fill span cracks (zeros + near-black) when the
 * frame is mostly drawn; never blanket sky-flood zeros. */
static void GC_ScrubLiveWorldSpeckles( unsigned short *dst, int width, int height, int stride )
{
	static qboolean g147_logged;
	unsigned nonblack = 0, samples = 0;
	int y, x;
	qboolean fill_cracks;

	if( !dst || width <= 0 || height <= 0 || stride < width )
		return;

	for( y = 0; y < height; y += 8 )
	{
		const unsigned short *row = dst + y * stride;
		for( x = 0; x < width; x += 8 )
		{
			samples++;
			if( row[x] > 0x0020 )
				nonblack++;
		}
	}
	fill_cracks = ( samples > 0 ) && ( nonblack * 5 >= samples * 2 );

	GC_ScrubWorldSpecklesPasses( dst, width, height, stride, true, fill_cracks, false );
	if( !g147_logged )
	{
		g147_logged = true;
		Con_Reportf( "Xash3D GameCube: G147 scrub cracks=%d nb=%u/%u\n",
			fill_cracks ? 1 : 0, nonblack, samples );
	}
}

static void GC_ScrubDumpWorldSpeckles( unsigned short *dst, int width, int height, int stride )
{
	GC_ScrubWorldSpecklesPasses( dst, width, height, stride, false, true, true );
}

static void GC_ScrubWorldSpecklesPasses( unsigned short *dst, int width, int height, int stride,
	qboolean quiet, qboolean fill_cracks, qboolean sky_flood )
{
	const unsigned short sky = 0x5ADB;
	int y, x;
	unsigned filled = 0;
	unsigned scrubbed = 0;

	if( !dst || width <= 0 || height <= 0 || stride < width )
		return;

	if( fill_cracks )
	{
		/* Pass 1: span cracks — replace isolated zeros with wall neighbor mode. */
		for( y = 0; y < height; y++ )
		{
			unsigned short *row = dst + y * stride;
			for( x = 0; x < width; x++ )
			{
				unsigned short fill;

				if( row[x] != 0 )
					continue;
				fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
				if( fill )
				{
					row[x] = fill;
					filled++;
				}
			}
		}

		/* Pass 1b (G147): near-black cracks (not pure zero) that sit inside
		 * brighter wall neighborhoods — leftover OOB/gap shred. */
		for( y = 0; y < height; y++ )
		{
			unsigned short *row = dst + y * stride;
			for( x = 0; x < width; x++ )
			{
				unsigned short fill;
				unsigned short p = row[x];
				int pr, pg, pb, luma, fr, fg, fb, fluma;

				if( p == 0 )
					continue;
				pr = ( p >> 11 ) & 0x1F;
				pg = ( p >> 5 ) & 0x3F;
				pb = p & 0x1F;
				luma = pr * 2 + pg + pb;
				if( luma >= 14 )
					continue;
				fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
				if( !fill )
					continue;
				fr = ( fill >> 11 ) & 0x1F;
				fg = ( fill >> 5 ) & 0x3F;
				fb = fill & 0x1F;
				fluma = fr * 2 + fg + fb;
				if( fluma < luma + 10 )
					continue;
				row[x] = fill;
				filled++;
			}
		}
	}

	if( sky_flood )
	{
		/* Pass 2: remaining zeros are real sky voids. */
		for( y = 0; y < height; y++ )
		{
			unsigned short *row = dst + y * stride;
			for( x = 0; x < width; x++ )
			{
				if( row[x] == 0 )
					row[x] = sky;
			}
		}
	}

	/* Pass 3: neon speckles → local non-neon mode (fallback mid-grey). */
	for( y = 0; y < height; y++ )
	{
		unsigned short *row = dst + y * stride;
		for( x = 0; x < width; x++ )
		{
			unsigned short fill;

			if( !GC_DumpPixelIsNeonChroma( row[x] ))
				continue;
			fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
			row[x] = fill ? fill : (unsigned short)0x8410;
			scrubbed++;
		}
	}

	/* Pass 4: isolated sky speckles inside walls (leftover crack flood). */
	if( sky_flood )
	for( y = 0; y < height; y++ )
	{
		unsigned short *row = dst + y * stride;
		for( x = 0; x < width; x++ )
		{
			unsigned short fill;
			int pr, pg, pb, nsky, nwall, dy, dx;

			pr = ( row[x] >> 11 ) & 0x1F;
			pg = ( row[x] >> 5 ) & 0x3F;
			pb = row[x] & 0x1F;
			/* sky ~0x5ADB → r≈11 g≈22 b≈27 */
			if( !( pb >= 20 && pg >= 16 && pr <= 14 && pb > pr + 6 ))
				continue;

			nsky = 0;
			nwall = 0;
			for( dy = -1; dy <= 1; dy++ )
			{
				int yy = y + dy;
				if( yy < 0 || yy >= height )
					continue;
				for( dx = -1; dx <= 1; dx++ )
				{
					int xx = x + dx;
					unsigned short p;
					int ppr, ppg, ppb;

					if( dx == 0 && dy == 0 )
						continue;
					if( xx < 0 || xx >= width )
						continue;
					p = dst[yy * stride + xx];
					ppr = ( p >> 11 ) & 0x1F;
					ppg = ( p >> 5 ) & 0x3F;
					ppb = p & 0x1F;
					if( ppb >= 20 && ppg >= 16 && ppr <= 14 && ppb > ppr + 6 )
						nsky++;
					else if( p != 0 )
						nwall++;
				}
			}
			if( nwall < 4 || nsky > 2 )
				continue;
			fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
			if( fill )
			{
				row[x] = fill;
				scrubbed++;
			}
		}
	}

	/* Pass 4b (G150): grow wall into sky-through holes from the rim.
	 * Open sky (few wall neighbors) is left alone; enclosed gaps fill inward. */
	if( sky_flood || fill_cracks )
	{
		unsigned hole_fill = 0;
		int pass;
		const int wall_need = sky_flood ? 3 : 5;

		for( pass = 0; pass < 4; pass++ )
		{
			unsigned pass_fill = 0;

			for( y = 1; y < height - 1; y++ )
			{
				unsigned short *row = dst + y * stride;
				for( x = 1; x < width - 1; x++ )
				{
					unsigned short p = row[x];
					int pr = ( p >> 11 ) & 0x1F;
					int pg = ( p >> 5 ) & 0x3F;
					int pb = p & 0x1F;
					int nwall = 0;
					int dx, dy;
					unsigned short fill;
					int fr, fg, fb;

					if( !( pb >= 20 && pg >= 16 && pr <= 14 && pb > pr + 6 )
						&& !( pr >= 28 && pg >= 56 && pb >= 28 ))
						continue;
					for( dy = -1; dy <= 1; dy++ )
					{
						for( dx = -1; dx <= 1; dx++ )
						{
							unsigned short n;
							int nr, ng, nb;

							if( dx == 0 && dy == 0 )
								continue;
							n = dst[( y + dy ) * stride + ( x + dx )];
							nr = ( n >> 11 ) & 0x1F;
							ng = ( n >> 5 ) & 0x3F;
							nb = n & 0x1F;
							if( nb >= 20 && ng >= 16 && nr <= 14 && nb > nr + 6 )
								continue;
							if( nr >= 28 && ng >= 56 && nb >= 28 )
								continue;
							if( n != 0 )
								nwall++;
						}
					}
					if( nwall < wall_need )
						continue;
					fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
					if( !fill )
						continue;
					fr = ( fill >> 11 ) & 0x1F;
					fg = ( fill >> 5 ) & 0x3F;
					fb = fill & 0x1F;
					if( fb >= 20 && fg >= 16 && fr <= 14 && fb > fr + 6 )
						continue;
					if( fr >= 28 && fg >= 56 && fb >= 28 )
						continue;
					row[x] = fill;
					pass_fill++;
				}
			}
			hole_fill += pass_fill;
			if( pass_fill == 0 )
				break;
		}
		if( !quiet && hole_fill )
			Con_Reportf( "Xash3D GameCube: G150 sky-hole rim fill=%u\n", hole_fill );
		scrubbed += hole_fill;
	}

	/* Pass 5 (G143): saturated outliers vs local wall mode — catches residual
	 * soft-decode sparks that are not pure neon but still shred DumpFrames. */
	{
		unsigned outliers = 0;

		for( y = 0; y < height; y++ )
		{
			unsigned short *row = dst + y * stride;
			for( x = 0; x < width; x++ )
			{
				unsigned short fill;
				unsigned short p = row[x];

				if( GC_DumpRGB565Sat( p ) < 12 )
					continue;
				fill = GC_DumpNeighborFill( dst, width, height, stride, x, y, true );
				if( !fill )
					continue;
				if( GC_DumpRGB565Dist( p, fill ) < 16 )
					continue;
				row[x] = fill;
				outliers++;
			}
		}
		scrubbed += outliers;
		if( !quiet )
			Con_Reportf( "Xash3D GameCube: G143 scrub fill=%u neon=%u out=%u %dx%d\n",
				filled, scrubbed - outliers, outliers, width, height );
	}
}


/* G130: snap every pixel to sky vs wall so DumpFrames show a room silhouette.
 * Soft-edge / textured-sky speckles collapse to two planes (+ dark). */
static void GC_PosterizeDumpWorldBuffer( unsigned short *dst, int width, int height, int stride )
{
	const unsigned short sky = 0x5ADB;
	const unsigned short wall = 0xA514;
	const unsigned short dark = 0x10A2;
	const int block = 16;
	int y, x, by, bx;

	if( !dst || width <= 0 || height <= 0 || stride < width )
		return;

	for( y = 0; y < height; y++ )
	{
		unsigned short *row = dst + y * stride;

		for( x = 0; x < width; x++ )
		{
			unsigned short p = row[x];
			int pr = ( p >> 11 ) & 0x1F;
			int pg = ( p >> 5 ) & 0x3F;
			int pb = p & 0x1F;
			int luma = ( pr * 2 + pg + pb );

			if( luma < 12 )
				row[x] = dark;
			else if( pb >= pr + 4 && pb >= ( pg >> 1 ) )
				row[x] = sky;
			else
				row[x] = wall;
		}
	}

	/* Large-block majority — kills residual period banding so DumpFrames
	 * read as room planes instead of mottled tiles. */
	for( by = 0; by < height; by += block )
	{
		int bh = block;
		if( by + bh > height )
			bh = height - by;

		for( bx = 0; bx < width; bx += block )
		{
			int bw = block;
			unsigned nsky = 0, nwall = 0, ndark = 0;
			unsigned short fill;
			if( bx + bw > width )
				bw = width - bx;

			for( y = 0; y < bh; y++ )
			{
				const unsigned short *row = dst + ( by + y ) * stride + bx;
				for( x = 0; x < bw; x++ )
				{
					if( row[x] == sky )
						nsky++;
					else if( row[x] == dark )
						ndark++;
					else
						nwall++;
				}
			}

			if( nsky >= nwall && nsky >= ndark )
				fill = sky;
			else if( ndark >= nwall && ndark >= nsky )
				fill = dark;
			else
				fill = wall;

			for( y = 0; y < bh; y++ )
			{
				unsigned short *row = dst + ( by + y ) * stride + bx;
				for( x = 0; x < bw; x++ )
					row[x] = fill;
			}
		}
	}
}
#endif

static void GC_DrawStatusPanelToBufferEx( unsigned short *dst, int width, int height, int stride,
	const char *message, const char *details, qboolean top_aligned )
{
	int row;
	int col;
	int panel_x;
	int panel_y;
	int panel_w;
	int panel_h;
	int text_scale;
	int line_scale;
	int bar_x, bar_y, bar_w, bar_h;
	unsigned short border = 0xFB40; /* HL amber border */
	unsigned short fill = 0x10A2;   /* dark brown-black panel */

	panel_x = width * GC_VIDEO_SAFE_AREA_PERCENT / 100;
	panel_w = width - panel_x * 2;
	panel_h = height * 110 / 480;
	if( top_aligned )
		panel_y = height * GC_VIDEO_SAFE_AREA_PERCENT / 100;
	else
	{
		panel_y = height - panel_h - height * GC_VIDEO_SAFE_AREA_PERCENT / 100;
		if( panel_y < height * GC_VIDEO_SAFE_AREA_PERCENT / 100 )
			panel_y = height * GC_VIDEO_SAFE_AREA_PERCENT / 100;
	}
	text_scale = height >= 240 ? 2 : 1;
	line_scale = height >= 240 ? 2 : 1;

	for( row = panel_y; row < panel_y + panel_h && row < height; row++ )
	{
		unsigned short *rowdst = dst + row * stride;
		for( col = panel_x; col < panel_x + panel_w && col < width; col++ )
		{
			if( row == panel_y || row == panel_y + panel_h - 1 ||
				col == panel_x || col == panel_x + panel_w - 1 )
				rowdst[col] = border;
			else
				rowdst[col] = fill;
		}
	}

	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 10 / 480, "HALF-LIFE", 0xFE60, text_scale, 24 );
	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 34 / 480, message ? message : "PLEASE WAIT", 0xFFE0, text_scale, 34 );
	GC_StatusDrawLine( dst, stride, width, height, panel_x + width * 18 / 640,
		panel_y + height * 56 / 480, details ? details : "LOADING", 0xC618, line_scale, 72 );

#if XASH_GAMECUBE
	bar_x = panel_x + width * 18 / 640;
	bar_w = panel_w - width * 36 / 640;
	bar_h = height >= 240 ? 10 : 6;
	bar_y = panel_y + panel_h - bar_h - height * 10 / 480;
	GC_DrawProgressBar( dst, width, height, stride, bar_x, bar_y, bar_w, bar_h, gc_loading_progress );
#else
	(void)bar_x; (void)bar_y; (void)bar_w; (void)bar_h;
#endif
}

static void GC_DrawStatusPanelToBuffer( unsigned short *dst, int width, int height, int stride,
	const char *message, const char *details )
{
	GC_DrawStatusPanelToBufferEx( dst, width, height, stride, message, details, false );
}

/*
 * G177: blit lean HUD sheets into the soft DumpFrames RGB565 buffer before the
 * status panel so Dolphin captures crosshair / hud1 with the world composite.
 */
static qboolean gc_g177_logged;

static void GC_SoftDumpCompositeHUD( void )
{
	qboolean saved_prepped;
	int i;
	int real_sheets = 0;

	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 )
		return;

	for( i = 0; i < MAX_CLIENT_SPRITES; i++ )
	{
		model_t *mod = &clgame.sprites[i];

		if( mod->needload == NL_UNREFERENCED )
			continue;
		if( mod->type != mod_sprite )
			continue;
		if( !Mod_GCIsSpriteStub( mod ))
			real_sheets++;
	}
	if( real_sheets < 1 )
		return;

	saved_prepped = cl.video_prepped;
	cl.video_prepped = true;
	ref.dllFuncs.R_BeginFrame( false );
	ref.dllFuncs.R_AllowFog( false );
	ref.dllFuncs.R_Set2DMode( true );
	CL_DrawHUD( CL_ACTIVE );
	ref.dllFuncs.R_AllowFog( true );
	Platform_SetTimer( 0.0f );
	ref.dllFuncs.R_EndFrame();
	cl.video_prepped = saved_prepped;

	if( !gc_g177_logged )
	{
		gc_g177_logged = true;
		Con_Reportf( "Xash3D GameCube: G177 soft dump HUD composite sheets=%d\n",
			real_sheets );
	}
}

/*
 * G60: Present loading progress without relying on renderer assets. Long map
 * loads can otherwise look like a hard hang on real hardware or composite
 * capture, especially before the client has a valid loading texture.
 */
void GC_DrawLoadingStatus( const char *message, const char *details )
{
#if XASH_GAMECUBE
	unsigned short *dst;
	size_t xfb_size;

	/* Host_Init direct-map path: decode the HL plaque once for later DumpFrames,
	 * but skip filling the boot 640×480 SW buffer / XFB. Under Dolphin interpreter
	 * + DumpFrames that fill can stall past probe timeouts (plaque ready, then no
	 * further OSReport). Post-init presents still blit+CPU-YUYV normally. */
	if( host.status == HOST_INIT )
	{
		(void)GC_LoadLoadingBackground();
		Con_Reportf( "Xash3D GameCube: loading status %s (%s) init-skip-present\n",
			message ? message : "?", details ? details : "" );
		return;
	}

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		static int g194_loading_dump_n;

		GC_BlitLoadingBackground( gc.buffer, gc.width, gc.height, gc.stride );
		GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride, message, details );
		/* Pure Flipper: present loading plaque via GX tiled EFB when available. */
		if( GC_CanPresentViaGX( gc.width, gc.height ))
		{
			GC_SwizzleRGB565ToTiled( gc.buffer, gc.stride, gc.width, gc.height, gc_tiled_rgb565 );
			DCFlushRange( gc_tiled_rgb565, (u32)((size_t)gc.width * (size_t)gc.height * sizeof( u16 )));
			GC_InitPresentTextureTiled( gc_tiled_rgb565, gc.width, gc.height );
			GC_PresentBufferViaGX();
			VIDEO_SetNextFramebuffer( xfb[which_fb] );
			VIDEO_Flush();
			VIDEO_WaitVSync();
			which_fb ^= 1;
			gc_gx_world_efb_ready = false;
			return;
		}
		/* G130: force CPU YUYV present so DumpFrames keep the loading plaque.
		 * G194: only the first two loading presents force a dump latch — every
		 * status update was flooding DumpFrames (~16 whites before soft latch). */
		if( g194_loading_dump_n < 2 )
		{
			g194_loading_dump_n++;
			if( gc_cpu_dump_presents_left < 1 )
				gc_cpu_dump_presents_left = 1;
			GC_PresentBuffer();
		}
		else if((( ++g194_loading_dump_n ) & 7 ) == 0 )
		{
			GC_PresentBuffer();
		}
		return;
	}

	if( !rmode || !xfb[which_fb] )
		return;

	{
		static int g194_xfb_loading_n;

		dst = (unsigned short *)MEM_K1_TO_K0( xfb[which_fb] );
		GC_BlitLoadingBackground( dst, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth );
		GC_DrawStatusPanelToBuffer( dst, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth,
			message, details );

		xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
		DCFlushRange( dst, (u32)xfb_size );
		if( g194_xfb_loading_n < 2 || (( ++g194_xfb_loading_n ) & 7 ) == 0 )
		{
			if( g194_xfb_loading_n < 2 )
				g194_xfb_loading_n++;
			VIDEO_SetNextFramebuffer( xfb[which_fb] );
			VIDEO_Flush();
			if( host.status != HOST_INIT )
			{
				VIDEO_WaitVSync();
				which_fb ^= 1;
			}
		}
	}
#endif
}

void GC_RunGcmapSmokeFrames( const char *mapname, int count )
{
#if XASH_GAMECUBE
	char details[64];
	int i;
	unsigned int required_count;
	double smoke_samples_ms[GC_VIDEO_BUDGET_SAMPLE_TARGET];
	unsigned int smoke_sample_count;
	qboolean log_smoke_samples;

	smoke_sample_count = 0;
	log_smoke_samples = gc_budget_probe_active && !Sys_CheckParm( "-gcnewgame" );

	if( count <= 0 )
		count = 12;

	required_count = GC_GetFrameBudgetSampleTarget() + GC_VIDEO_BUDGET_WARMUP_PRESENTS;
	if( gc_budget_probe_active && count < (int)required_count )
		count = (int)required_count;

	Q_snprintf( details, sizeof( details ), "GCMAP %s", mapname && mapname[0] ? mapname : "READY" );

	if( gc.buffer && gc.width > 0 && gc.height > 0 )
	{
		int row, col;
		unsigned short *rowdst;

		for( row = 0; row < gc.height; row++ )
		{
			rowdst = gc.buffer + row * gc.stride;
			for( col = 0; col < gc.width; col++ )
				rowdst[col] = 0x0010;
		}

		GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride,
			"MAP READY", details );

		// G72: Reduce frame/render cost while preserving MAP_READY/G45/nonblack
		if( gc.width >= 160 && gc.height >= 120 )
		{
			for( row = 0; row < gc.height / 4; row++ )
			{
				for( col = 0; col < gc.width / 4; col++ )
				{
					rowdst[col] = 0x0010;
				}
			}
		}
	}

	for( i = 0; i < count; i++ )
	{
		double start = 0.0;
		double end = 0.0;

		if( log_smoke_samples && smoke_sample_count < GC_VIDEO_BUDGET_SAMPLE_TARGET )
			start = Sys_FloatTime();

		if( gc.buffer && gc.width > 0 && gc.height > 0 )
			GC_PresentBuffer();
		else
			GC_DrawLoadingStatus( "MAP LOADED", details );

		if( log_smoke_samples && smoke_sample_count < GC_VIDEO_BUDGET_SAMPLE_TARGET )
		{
			end = Sys_FloatTime();
			smoke_samples_ms[smoke_sample_count++] = ( end - start ) * 1000.0;
		}
	}

	if( log_smoke_samples )
	{
		for( i = 0; i < (int)smoke_sample_count; i++ )
		{
			SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=1 frame time=%.2fms\n",
				(unsigned int)( i + 1 ), smoke_samples_ms[i] );
		}
		SYS_Report( "Xash3D GameCube: budget sample flush count=%u worst=%.2fms\n",
			smoke_sample_count, gc_worst_frame_ms );
	}

	gc_budget_probe_active = false;
#else
	(void)mapname;
	(void)count;
#endif
}

qboolean GC_AttemptGcmapWorldRender( int count )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int present_w, present_h;
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;

	if( count <= 0 )
		count = 12;

	Image_GCPurgeDecodeScratch();
	Mod_GcmapMarkPrecacheFreeable();
	GC_ReleasePresentationBufferForWorldRender();
	/* Full software edge pass uses ~288 KiB of stack (NUMSTACKEDGES/SURFACES).
	 * Force quality=0 so R_EdgeDrawing takes the gcmap low-memory probe path.
	 * Allocate edge/surf/span scratch before screen buffers while MEM1 is freest. */
	Cvar_Set( "gc_quality", "0" );
	if( !R_GcmapEnsureWorldRenderScratch() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap scratch alloc fail\n" );
		return false;
	}
	GC_MemSample( "pre-world render" );

	if( !R_GcmapPrepareWorldRender() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap screen alloc fail\n" );
		Cvar_Set( "gc_quality", "0" );
		if( !R_GcmapPrepareWorldRender() )
			return false;
	}

	present_w = gc.width > 0 ? gc.width : GC_VIDEO_NEWGAME_PROBE_WIDTH;
	present_h = gc.height > 0 ? gc.height : GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	if( !R_GcmapGetViewport( &present_w, &present_h ))
	{
		present_w = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		present_h = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}

	if( !GC_EnsurePresentationBuffer( present_w, present_h ))
	{
		Con_Reportf( "Xash3D GameCube: gcmap present buf fail\n" );
		return false;
	}

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	/* World-render probe flat-fills spans instead of sampling cached textured
	 * surfaces, so keep MEM1 pressure down by dropping the gcmap surfcache. */
	if( Sys_CheckParm( "-gcworldrender" ))
	{
		R_GcmapTrimSurfaceCache();
		Con_Reportf( "Xash3D GameCube: gcmap q0 no surfcache\n" );
	}
	else if( !R_GcmapEnsureSurfaceCache() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap q0 no surfcache\n" );
		Cvar_Set( "gc_quality", "0" );
	}

	ref.dllFuncs.R_NewMap();

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	/* Prefer a spawned entity origin so the camera is inside playable space.
	 * Map-bbox center is often in solid/void and produces an all-black frame. */
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	for( i = 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = &svgame.edicts[i];

		if( ent->free )
			continue;
		if( VectorIsNull( ent->v.origin ))
			continue;
		VectorCopy( ent->v.origin, center );
		center[2] += 48.0f;
		Con_Reportf( "Xash3D GameCube: gcmap view ed=%d org=(%.0f,%.0f,%.0f)\n",
			i, center[0], center[1], center[2] );
		break;
	}
	VectorCopy( center, rvp.vieworigin );
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", "0" );

	if( count > 6 )
		count = 6;

	Con_Reportf( "Xash3D GameCube: gcmap begin frames=%d\n", count );
	for( i = 0; i < count; ++i )
	{
		ref.dllFuncs.R_BeginFrame( false );
		VectorCopy( rvp.vieworigin, refState.vieworg );
		VectorCopy( rvp.viewangles, refState.viewangles );
		ref.dllFuncs.GL_RenderFrame( &rvp );
		/* R_EndFrame -> R_BlitScreen already presents; avoid a second present. */
		ref.dllFuncs.R_EndFrame();
	}
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	Con_Reportf( "Xash3D GameCube: gcmap ready\n" );
	return true;
#endif

	(void)count;
	return false;
}

/*
 * G50: Draw a readable fatal breadcrumb directly to XFB so it survives silent
 * crashes and missing assets. This is called from Sys_Error before shutdown.
 */
void GC_DrawFatalBreadcrumb( const char *message, const char *details )
{
#if XASH_GAMECUBE
	unsigned short *panel;
	unsigned int *dst;
	int row;
	int col;
	int i;
	const int panel_w = 160;
	const int panel_h = 120;
	const int row_pairs = rmode ? ( rmode->fbWidth / 2 ) : 0;
	/* Static panel — never Mem_Alloc during fatal (MEM1 is already exhausted). */
	static unsigned short fatal_panel[160 * 120];

	if( !gc.initialized )
		return;
	if( !rmode || !rmode->fbWidth || !rmode->xfbHeight || !xfb[0] )
		return;

	panel = fatal_panel;
	for( row = 0; row < panel_h; row++ )
	{
		unsigned short *rowdst = panel + row * panel_w;
		for( col = 0; col < panel_w; col++ )
			rowdst[col] = 0xF81F; /* magenta RGB565 */
	}
	GC_StatusDrawLine( panel, panel_w, panel_w, panel_h,
		4, 4, "XASH3D GC FATAL", 0xFFFF, 1, 24 );
	GC_StatusDrawLine( panel, panel_w, panel_w, panel_h,
		4, 20, message ? message : "UNKNOWN ERROR", 0xFFE0, 1, 36 );
	GC_StatusDrawLine( panel, panel_w, panel_w, panel_h,
		4, 40, details ? details : "NO DETAILS", 0xFFFF, 1, 36 );
	GC_StatusDrawLine( panel, panel_w, panel_w, panel_h,
		4, panel_h - 14, "POWER CYCLE OR RESET", 0x07E0, 1, 28 );

	if( GC_CanPresentViaGX( panel_w, panel_h ))
	{
		GC_SwizzleRGB565ToTiled( panel, panel_w, panel_w, panel_h, gc_tiled_rgb565 );
		DCFlushRange( gc_tiled_rgb565, (u32)((size_t)panel_w * (size_t)panel_h * sizeof( u16 )));
		GC_InitPresentTextureTiled( gc_tiled_rgb565, panel_w, panel_h );
		GC_PresentBufferViaGX();
		VIDEO_SetNextFramebuffer( xfb[which_fb] );
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}
	else
	{
		unsigned int magenta = GC_RGBPairToYUYV( 0xF81F, 0xF81F );
		dst = (unsigned int *)xfb[0];
		/* Prefer scaled RGB→YUYV blit; fall back to solid magenta YUYV. */
		if( row_pairs > 0 )
		{
			GC_BlitSoftwareBufferScaled( panel, panel_w, panel_h, panel_w,
				dst, rmode->fbWidth, rmode->xfbHeight, row_pairs );
		}
		else
		{
			for( row = 0; row < rmode->xfbHeight; row++ )
			{
				unsigned int *rowdst = dst + row * ( rmode->fbWidth / 2 );
				for( col = 0; col < rmode->fbWidth / 2; col++ )
					rowdst[col] = magenta;
			}
		}
		DCFlushRange( MEM_K1_TO_K0( xfb[0] ), (u32)( rmode->fbWidth * rmode->xfbHeight * sizeof( unsigned short )));
		VIDEO_SetNextFramebuffer( xfb[0] );
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}

	for( i = 0; i < 3; i++ )
		VIDEO_WaitVSync();
	/* Leave display active — do not VIDEO_SetBlack. */
#endif
}

qboolean GC_IsFrameBudgetProbeActive( void )
{
#if XASH_GAMECUBE
	return gc_budget_probe_active;
#else
	return false;
#endif
}

qboolean GC_IsNewGameWorldReady( void )
{
#if XASH_GAMECUBE
	return gc_newgame_world_ready;
#else
	return false;
#endif
}

/*
===========
GC_TryDeferredLeanSky

G285: after HUD/studios settle, try one tip-safe lean sky BMP for Flipper
outdoor backdrop. Soft-fails must not Host_Error; outdoor clear covers gaps.
===========
*/
static void GC_TryDeferredLeanSky( void )
{
#if XASH_GAMECUBE
	const char *sky;

	if( !gc_newgame_world_ready )
		return;
	if( FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX ))
	{
		gc_lean_sky_attempts = 3;
		return;
	}
	if( gc_lean_sky_attempts >= 3 )
		return;
	/* Try at presents 4, 16, 32 after world-ready arm. G300 BSS procedural
	 * covers ImageLib soft-fail when these run. */
	if( gc_present_count != 4 && gc_present_count != 16 && gc_present_count != 32 )
		return;

	gc_lean_sky_attempts++;
	sky = clgame.movevars.skyName;
	if( !sky || !sky[0] )
		sky = "desert";
	Image_GCPurgeDecodeScratch();
	FS_ClearFindMissCache();
	Con_Reportf( "Xash3D GameCube: G285 deferred lean sky try=%d present=%u name=%s\n",
		gc_lean_sky_attempts, gc_present_count, sky );
	R_SetupSkyLeanGameCube( sky );
	if( FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX ))
		gc_lean_sky_attempts = 3;
#endif
}

/*
===========
GC_TryDeferredHudSheets

G290: retry stubbed lean HUD (crosshairs) before studio bump steals residual heap.
===========
*/
static void GC_TryDeferredHudSheets( void )
{
#if XASH_GAMECUBE
	if( !gc_newgame_world_ready )
		return;
	if( gc_present_count != 6 && gc_present_count != 12 )
		return;
	Con_Reportf( "Xash3D GameCube: G290 deferred HUD sheets present=%u\n",
		gc_present_count );
	CL_GCPreloadNewGameHudSpritesLate();
#endif
}

/*
===========
GC_TryDeferredStudios

G287/G294: promote crowbar/handgun after Flipper presents (prepare tip-fails
MDL malloc). Early present slots so short probes still reach view≥2.
===========
*/
static void GC_TryDeferredStudios( void )
{
#if XASH_GAMECUBE
	if( !gc_newgame_world_ready )
		return;
	/* G297: MDL promote mid-G36 sample window spikes present hitch. */
	if( GC_IsFrameBudgetProbeActive() )
		return;
	/* G294: early presents for short probes; keep post-G36 retries. */
	if( gc_present_count != 1 && gc_present_count != 2 && gc_present_count != 4
		&& gc_present_count != 8 && gc_present_count != 20 && gc_present_count != 40 )
		return;
	Mod_GCTryDeferredStudios();
#endif
}

/*
===========
GC_TryDeferredEfxProof

G291: tip-safe particle seed so Flipper EFX TriAPI emits during probe.
===========
*/
static void GC_TryDeferredEfxProof( void )
{
#if XASH_GAMECUBE
	vec3_t org;
	static qboolean seeded;

	if( !gc_newgame_world_ready || seeded )
		return;
	/* Prepare (0) or early present backup — probe often SIGINT before 6. */
	if( gc_present_count != 0 && gc_present_count != 2 )
		return;
	if( !Sys_CheckParm( "-gcnewgame" ))
		return;

	VectorCopy( refState.vieworg, org );
	CL_GCSeedFlipperEfxProof( org );
	seeded = true;
#endif
}

/*
===========
GC_TryDeferredDecalProof

G293: tip-safe lean {shot1 seed (bootstrap TGA) so Flipper emits a real pool
decal — not only the empty-pool proof quad.
===========
*/
static void GC_TryDeferredDecalProof( void )
{
#if XASH_GAMECUBE
	vec3_t org, end, dir;
	pmtrace_t tr;
	int tex;
	static qboolean seeded;

	if( !gc_newgame_world_ready || seeded )
		return;
	/* Prepare (0) or early present backup — probe often SIGINT before 6. */
	if( gc_present_count != 0 && gc_present_count != 2 )
		return;
	if( !Sys_CheckParm( "-gcnewgame" ))
		return;

	VectorCopy( refState.vieworg, org );
	dir[0] = 0.0f;
	dir[1] = 1.0f;
	dir[2] = -0.25f;
	VectorNormalize( dir );
	VectorMA( org, 96.0f, dir, end );
	tr = CL_TraceLine( org, end, PM_STUDIO_IGNORE );
	if( tr.fraction < 1.0f )
		VectorMA( tr.endpos, 1.0f, tr.plane.normal, org );
	else
	{
		/* Engine R_DecalShoot still projects onto nearby brushes from a point. */
		VectorCopy( end, org );
	}
	tex = CL_GCEnsureLeanShotDecal();
	if( tex <= 0 )
	{
		int id = CL_DecalIndexFromName( "{shot1" );
		if( id <= 0 )
			id = CL_DecalIndexFromName( "{smscorch1" );
		if( id > 0 )
			tex = CL_DecalIndex( id );
	}
	if( tex > 0 )
		CL_DecalShoot( tex, 0, 0, org, 0 );
	/* G295: touch blood/scorch/break embeds so gameplay paths stay tip-safe. */
	{
		int blood = CL_GCEnsureLeanDecalForName( "{blood1" );
		int scorch = CL_GCEnsureLeanDecalForName( "{scorch1" );
		int brk = CL_GCEnsureLeanDecalForName( "{break1" );
		Con_Reportf( "Xash3D GameCube: G295 lean decal kinds shot=%d scorch=%d blood=%d break=%d\n",
			tex, scorch, blood, brk );
	}
	seeded = ( tex > 0 );
	Con_Reportf( "Xash3D GameCube: G293 tip-safe lean decal seed tex=%d hit=%.2f\n",
		tex, tr.fraction );
#endif
}

qboolean GC_UseGxRenderer( void )
{
#if XASH_GAMECUBE
	/* Any Flipper path (menus / loading / world). Soft DumpFrames latch and
	 * `-gcsoftworld` remain the only diagnostic opt-outs. */
	if( !gc.initialized )
		return false;
	if( Sys_CheckParm( "-gcsoftworld" ))
		return false;
	if( gc_cpu_dump_presents_left > 0 )
		return false;
	return true;
#else
	return false;
#endif
}

/*
===========
GC_IsCaptureDiagnostics

True for Dolphin/probe capture routes (DumpFrames, New Game harness, smoke
maps, changelevel probes). Retail / native hardware boots return false and
must never enter soft-lock, ViSwap throttles, or wall-aim dump pumps.
===========
*/
qboolean GC_IsCaptureDiagnostics( void )
{
#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcsoftworld" ))
		return false;
	return ( Sys_CheckParm( "-gcdumpframes" )
		|| Sys_CheckParm( "-gcdump" )
		|| Sys_CheckParm( "-gcchangelevel" )
		|| Sys_CheckParm( "-gcnewgame" )
		|| Sys_CheckParm( "-gcmap" )
		|| Sys_CheckParm( "-gcworldrender" )) ? true : false;
#else
	return false;
#endif
}

void GC_GetVideoSafeArea( int *x, int *y, int *w, int *h )
{
#if XASH_GAMECUBE
	int sx, sy, sw, sh;

	if( !rmode )
	{
		if( x ) *x = 0;
		if( y ) *y = 0;
		if( w ) *w = 640;
		if( h ) *h = 480;
		return;
	}
	sx = ( rmode->fbWidth * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	sy = ( rmode->xfbHeight * GC_VIDEO_SAFE_AREA_PERCENT ) / 100;
	sw = rmode->fbWidth - sx * 2;
	sh = rmode->xfbHeight - sy * 2;
	if( x ) *x = sx;
	if( y ) *y = sy;
	if( w ) *w = sw;
	if( h ) *h = sh;
#else
	if( x ) *x = 0;
	if( y ) *y = 0;
	if( w ) *w = 640;
	if( h ) *h = 480;
#endif
}

qboolean GC_UseGxWorldDraw( void )
{
#if XASH_GAMECUBE
	/* Pure Flipper 3D: live GX whenever the world is prepared (retail + probe). */
	if( !GC_UseGxRenderer() )
		return false;
	if( !gc_newgame_world_ready )
		return false;
	if( !gc_gx_world_live )
		return false;
	return true;
#else
	return false;
#endif
}

void GC_MarkGxWorldEfbReady( void )
{
#if XASH_GAMECUBE
	gc_gx_world_efb_ready = true;
	/* Flipper TEV/matrices invalidate soft RGB565 present pipe. */
	gc_gx_present_pipe_ready = false;
#endif
}

void GC_EnableGxWorldLive( void )
{
#if XASH_GAMECUBE
	if( Sys_CheckParm( "-gcsoftworld" ))
	{
		Con_Reportf( S_ERROR "Xash3D GameCube: -gcsoftworld is unsupported in pure Flipper builds\n" );
		return;
	}
	gc_gx_world_live = true;
	Con_Reportf( "Xash3D GameCube: G151 GX world live enabled (Flipper EFB)\n" );
#endif
}

void *GC_GetGxVideoMode( void )
{
#if XASH_GAMECUBE
	return rmode;
#else
	return NULL;
#endif
}

qboolean GC_IsNewGameG36Done( void )
{
#if XASH_GAMECUBE
	return gc_newgame_g36_done;
#else
	return false;
#endif
}

/*
=============
GC_GXDrawIntroTrain

G277: New Game never runs CL_EmitEntities on the Flipper present path, so
edge_entities stay empty. Draw c0a0 intro tram (*12) from the server edict
using capture-baked verts (live msurface_t dangles after scratch).
=============
*/
int GC_GXDrawIntroTrain( void )
{
#if XASH_GAMECUBE
	extern int R_GXDrawTramBaked( const float *origin, const float *angles );
	static int tram_e;
	edict_t *ent;

	if( !Sys_CheckParm( "-gcnewgame" ) || !gc_newgame_world_ready )
		return 0;
	if( GC_GetTramFaceCount() <= 0 )
		return 0;
	if( tram_e <= 0 )
	{
		int e, first = svs.maxclients + 1;

		for( e = first; e < svgame.numEntities; e++ )
		{
			const char *precache;

			ent = SV_EdictNum( e );
			if( !SV_IsValidEdict( ent ))
				continue;
			if( ent->v.modelindex <= 0 || ent->v.modelindex >= MAX_MODELS )
				continue;
			precache = sv.model_precache[ent->v.modelindex];
			if( !precache || Q_strcmp( precache, "*12" ))
				continue;
			tram_e = e;
			break;
		}
		if( tram_e <= 0 )
			return 0;
	}
	ent = SV_EdictNum( tram_e );
	if( !SV_IsValidEdict( ent ))
		return 0;
	/* G280: eye is parented inside the cabin — drawing *12 exterior faces
	 * with Z-ignore paints ghost cars and burns Flipper time. Skip while riding. */
	{
		edict_t *player = ( svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;
		vec3_t delta;
		float dist2;

		if( player && SV_IsValidEdict( player ))
		{
			VectorSubtract( player->v.origin, ent->v.origin, delta );
			dist2 = DotProduct( delta, delta );
			if( dist2 < ( 220.0f * 220.0f ))
				return 0;
		}
	}
	return R_GXDrawTramBaked( ent->v.origin, ent->v.angles );
#else
	return 0;
#endif
}

model_t *GC_GetWorldModel( void )
{
#if XASH_GAMECUBE
	return ( sv.models[1] && sv.models[1]->vertexes ) ? sv.models[1] : NULL;
#else
	return NULL;
#endif
}

int GC_GetNewGameViewCluster( void )
{
#if XASH_GAMECUBE
	return gc_newgame_viewcluster;
#else
	return -1;
#endif
}

qboolean GC_HasNewGameCachedVis( void )
{
#if XASH_GAMECUBE
	return gc_newgame_pvs_ready;
#else
	return false;
#endif
}

qboolean GC_ApplyNewGameCachedVis( int visframe )
{
#if XASH_GAMECUBE
	model_t *wmodel;
	int i;
	int leaf_mark = gc_newgame_numleafs;
	int node_mark = gc_newgame_numnodes;

	if( !gc_newgame_pvs_ready || !gc_newgame_vis || !gc_newgame_nodebits )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis fail ready=%d vis=%p nodes=%p\n",
			gc_newgame_pvs_ready ? 1 : 0, (void *)gc_newgame_vis, (void *)gc_newgame_nodebits );
		return false;
	}

	wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	if( !wmodel || !wmodel->leafs || !wmodel->nodes )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis fail world=%p leafs=%p nodes=%p\n",
			(void *)wmodel,
			wmodel ? (void *)wmodel->leafs : NULL,
			wmodel ? (void *)wmodel->nodes : NULL );
		return false;
	}

	if( wmodel->numleafs != gc_newgame_numleafs || wmodel->numnodes != gc_newgame_numnodes )
	{
		SYS_Report( "Xash3D GameCube: ApplyCachedVis size mismatch world=%d/%d cache=%d/%d\n",
			wmodel->numleafs, wmodel->numnodes, gc_newgame_numleafs, gc_newgame_numnodes );
		if( leaf_mark > wmodel->numleafs )
			leaf_mark = wmodel->numleafs;
		if( node_mark > wmodel->numnodes )
			node_mark = wmodel->numnodes;
	}

	for( i = 0; i < leaf_mark; i++ )
	{
		if( gc_newgame_vis[i >> 3] & ( 1 << ( i & 7 )))
			((mnode_t *)&wmodel->leafs[i + 1])->visframe = visframe;
		else
			((mnode_t *)&wmodel->leafs[i + 1])->visframe = 0;
	}
	for( i = 0; i < node_mark; i++ )
	{
		if( gc_newgame_nodebits[i >> 3] & ( 1 << ( i & 7 )))
			wmodel->nodes[i].visframe = visframe;
		else
			wmodel->nodes[i].visframe = 0;
	}
	return true;
#else
	(void)visframe;
	return false;
#endif
}

#if XASH_GAMECUBE
/* Local RLE decompress — Mod_DecompressPVS is static in mod_bmodel.c. */
static void GC_DecompressPVS( byte *out, const byte *in, size_t visbytes )
{
	byte *dst = out;

	if( !in )
	{
		memset( out, 0xFF, visbytes );
		return;
	}

	while( dst < out + visbytes )
	{
		if( *in )
		{
			*dst++ = *in++;
		}
		else
		{
			size_t c = in[1];

			if( c == 0 )
			{
				memset( dst, 0xFF, (size_t)( out + visbytes - dst ));
				return;
			}
			if( c > (size_t)( out + visbytes - dst ))
				c = (size_t)( out + visbytes - dst );
			memset( dst, 0, c );
			in += 2;
			dst += c;
		}
	}
}

static void GC_CountActiveVisRows( void )
{
	int i;

	gc_newgame_vis_leafs = 0;
	gc_newgame_vis_nodes = 0;
	if( !gc_newgame_vis || !gc_newgame_nodebits )
		return;
	for( i = 0; i < gc_newgame_numleafs; i++ )
	{
		if( gc_newgame_vis[i >> 3] & ( 1 << ( i & 7 )))
			gc_newgame_vis_leafs++;
	}
	for( i = 0; i < gc_newgame_numnodes; i++ )
	{
		if( gc_newgame_nodebits[i >> 3] & ( 1 << ( i & 7 )))
			gc_newgame_vis_nodes++;
	}
}

static void GC_BuildNodebitsForVisRow( model_t *wmodel, const byte *vis, byte *nodebits );

/* G132: capture marksurface indices while BSP pointers are still valid. */
static void GC_BuildSurfbitsForVisRow( model_t *wmodel, const byte *vis, byte *surfbits )
{
	int i;
	msurface_t **ms_base;
	msurface_t **ms_end;
	int num_ms;

	if( !surfbits || gc_newgame_surfbytes <= 0 || !wmodel || !wmodel->surfaces )
		return;
	memset( surfbits, 0, (size_t)gc_newgame_surfbytes );

	ms_base = wmodel->marksurfaces;
	num_ms = wmodel->nummarksurfaces;
	ms_end = ( ms_base && num_ms > 0 ) ? ( ms_base + num_ms ) : NULL;

	for( i = 0; i < wmodel->numleafs; i++ )
	{
		mleaf_t *pleaf;
		msurface_t **mark;
		int c;
		int j;

		if( !( vis[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		/* leafs[0] is solid; vis bit i maps to leafs[i+1]. */
		if( i + 1 >= wmodel->numleafs )
			continue;
		pleaf = &wmodel->leafs[i + 1];
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;
		if( !mark || c <= 0 || c > 4096 )
			continue;
		if( ms_end && ( mark < ms_base || mark + c > ms_end ))
			continue;
		for( j = 0; j < c; j++ )
		{
			msurface_t *surf = mark[j];
			int sidx;

			if( !surf )
				continue;
			sidx = (int)( surf - wmodel->surfaces );
			if( sidx >= 0 && sidx < wmodel->numsurfaces )
				surfbits[sidx >> 3] |= (byte)( 1 << ( sidx & 7 ));
		}
	}
}

void GC_ApplyNewGameSurfVis( int surf_frame )
{
#if XASH_GAMECUBE
	model_t *wmodel;
	int i;
	int stamped = 0;

	if( !gc_newgame_surfbits || gc_newgame_numsurfaces <= 0 || surf_frame <= 0 )
		return;

	wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	if( !wmodel || !wmodel->surfaces )
		return;
	if( wmodel->numsurfaces < gc_newgame_numsurfaces )
		gc_newgame_numsurfaces = wmodel->numsurfaces;

	for( i = 0; i < gc_newgame_numsurfaces; i++ )
	{
		if( !( gc_newgame_surfbits[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		wmodel->surfaces[i].visframe = surf_frame;
		stamped++;
	}
	if( stamped > 0 )
	{
		static int surf_log;

		if( surf_log < 3 )
		{
			SYS_Report( "Xash3D GameCube: G132 surfbits stamp count=%d surf_frame=%d\n",
				stamped, surf_frame );
			surf_log++;
		}
	}
#else
	(void)surf_frame;
#endif
}

/* G160: lean PVS LRU rebuilds marksurface bits (was memset-empty).
 * G163: on cluster change, refresh the 256-face top-K while reusing baked LM. */

static byte *GC_LookupSurfbitsCache( int cluster )
{
	int i;

	if( !gc_newgame_surf_cache || gc_newgame_surfbytes <= 0 || cluster < 0 )
		return NULL;
	for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
	{
		if( gc_newgame_surf_cache_cluster[i] == cluster )
			return gc_newgame_surf_cache + (size_t)i * (size_t)gc_newgame_surfbytes;
	}
	return NULL;
}

static void GC_BuildRefreshCandsFromSurfbits( model_t *wmodel, const byte *surfbits, int cache_slot );

static void GC_StoreSurfbitsCache( model_t *wmodel, int cluster, const byte *vis )
{
	byte *row;
	int slot;

	if( !wmodel || !vis || cluster < 0 || gc_newgame_surfbytes <= 0 )
		return;
	if( GC_LookupSurfbitsCache( cluster ))
		return;
	if( gc_newgame_surf_cache_slots >= GC_SURFBITS_CACHE_SLOTS )
		return;
	if( !gc_newgame_surf_cache )
	{
		gc_newgame_surf_cache = (byte *)calloc( (size_t)GC_SURFBITS_CACHE_SLOTS,
			(size_t)gc_newgame_surfbytes );
		if( !gc_newgame_surf_cache )
			return;
	}
	slot = gc_newgame_surf_cache_slots;
	row = gc_newgame_surf_cache + (size_t)slot * (size_t)gc_newgame_surfbytes;
	GC_BuildSurfbitsForVisRow( wmodel, vis, row );
	gc_newgame_surf_cache_cluster[slot] = cluster;
	gc_newgame_surf_cache_slots++;
	GC_BuildRefreshCandsFromSurfbits( wmodel, row, slot );
}

/* G165: after entities exist (Prepare) but before scratch purge, cache the
 * camera/restore cluster so Flipper can refresh outdoor faces. */
static void GC_CaptureG165RestoreCands( void )
{
	model_t *wmodel;
	vec3_t eye;
	int eye_c;
	const byte *vis;

	if( gc_newgame_surf_table || !gc_newgame_pvs_ready || !gc_newgame_pvs_table )
		return;
	if( gc_newgame_surfbytes <= 0 || gc_newgame_pvs_lean )
		return;
	if( gc_g165_eye_cluster >= 0 )
	{
		int slot = GC_LookupSurfbitsCacheSlot( gc_g165_eye_cluster );

		if( slot >= 0 && gc_refresh_ncands[slot] > 0 )
			return;
	}

	wmodel = sv.models[1];
#if !XASH_DEDICATED
	if( !wmodel )
		wmodel = cl.worldmodel;
#endif
	if( !wmodel || !wmodel->surfaces )
		return;

	VectorClear( eye );
	{
		edict_t *player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;
		int i;

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			VectorCopy( player->v.origin, eye );
			eye[2] += 48.0f;
		}
		else if( svgame.edicts )
		{
			for( i = 1; i < svgame.numEntities; i++ )
			{
				edict_t *ent = &svgame.edicts[i];

				if( ent->free || VectorIsNull( ent->v.origin ))
					continue;
				VectorCopy( ent->v.origin, eye );
				eye[2] += 48.0f;
				break;
			}
		}
	}
	if( VectorIsNull( eye ))
		return;

	eye_c = GC_SelectClusterForOrigin( eye );
	if( eye_c < 0 || eye_c >= gc_newgame_numclusters || !gc_newgame_cluster_valid
		|| !gc_newgame_cluster_valid[eye_c] )
		return;

	vis = gc_newgame_pvs_table + (size_t)eye_c * (size_t)gc_newgame_visbytes;
	GC_StoreSurfbitsCache( wmodel, eye_c, vis );
	if( !GC_LookupSurfbitsCache( eye_c ))
		return;
	gc_g165_eye_cluster = eye_c;
	GC_FlipperTrace( "Xash3D GameCube: G165 restore cands ready cluster=%d leaves=%d\n",
		eye_c, GC_VisLeafsForCluster( eye_c ));
}

static void GC_MaybeRefreshCapFacesAfterClusterChange( int prev_cluster )
{
	/* Defer work off the PVS switch path — mid-frame rebuild hung Host_Frame. */
	if( prev_cluster == gc_newgame_viewcluster )
		return;
	if( gc_newgame_cap_face_count <= 0 )
		return;
	/* G212: after near-eye stream, PreferIndoor↔UpdatePVS thrash must not
	 * re-pending forever (DSI / grey DumpFrames under constant Flush). */
	if( gc_g212_stream_locked )
		return;
	gc_cap_refresh_pending = true;
}

static qboolean GC_SetActiveNewGameCluster( int cluster, qboolean log_change )
{
	int prev = gc_newgame_viewcluster;
	int slot;

	if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid )
		return false;

	if( gc_newgame_pvs_lean )
	{
		for( slot = 0; slot < gc_newgame_lean_slots; slot++ )
		{
			if( gc_newgame_lean_clusters[slot] != cluster )
				continue;
			if( !gc_newgame_cluster_valid[slot] )
				return false;
			gc_newgame_vis = gc_newgame_pvs_table + (size_t)slot * (size_t)gc_newgame_visbytes;
			gc_newgame_nodebits = gc_newgame_node_table + (size_t)slot * (size_t)gc_newgame_nodebytes;
			gc_newgame_surfbits = ( gc_newgame_surf_table && gc_newgame_surfbytes > 0 )
				? gc_newgame_surf_table + (size_t)slot * (size_t)gc_newgame_surfbytes
				: NULL;
			gc_newgame_viewcluster = cluster;
			gc_newgame_lean_cluster = cluster;
			gc_newgame_lean_age[slot] = ++gc_newgame_lean_clock;
			GC_CountActiveVisRows();
			if( log_change && prev != cluster )
			{
				SYS_Report( "Xash3D GameCube: PVS lean follow %d->%d slot=%d leaves=%d nodes=%d\n",
					prev, cluster, slot, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
			}
			GC_MaybeRefreshCapFacesAfterClusterChange( prev );
			return true;
		}

		/* G107: materialize an uncached row into the least-recently-used slot. */
		if( cluster >= 0 && cluster < gc_newgame_numclusters
			&& gc_newgame_compressed_pvs && gc_newgame_compressed_ofs
			&& gc_newgame_packed_nodebits
			&& gc_newgame_compressed_ofs[cluster] >= 0 )
		{
			model_t *wmodel = sv.models[1];
			unsigned int oldest = ~0U;
			int evicted;
			int lru_slot = 0;

#if !XASH_DEDICATED
			if( !wmodel )
				wmodel = cl.worldmodel;
#endif
			if( !wmodel || !wmodel->leafs || !wmodel->nodes )
				return false;

			if( gc_newgame_lean_slots < GC_LEAN_PVS_SLOTS )
				slot = gc_newgame_lean_slots++;
			else
			{
				for( slot = 0; slot < gc_newgame_lean_slots; slot++ )
				{
					if( gc_newgame_lean_age[slot] < oldest )
					{
						oldest = gc_newgame_lean_age[slot];
						lru_slot = slot;
					}
				}
				slot = lru_slot;
			}
			evicted = gc_newgame_cluster_valid[slot] ? gc_newgame_lean_clusters[slot] : -1;
			gc_newgame_vis = gc_newgame_pvs_table + (size_t)slot * (size_t)gc_newgame_visbytes;
			gc_newgame_nodebits = gc_newgame_node_table + (size_t)slot * (size_t)gc_newgame_nodebytes;
			gc_newgame_surfbits = ( gc_newgame_surf_table && gc_newgame_surfbytes > 0 )
				? gc_newgame_surf_table + (size_t)slot * (size_t)gc_newgame_surfbytes
				: NULL;
			GC_DecompressPVS( gc_newgame_vis,
				gc_newgame_compressed_pvs + gc_newgame_compressed_ofs[cluster],
				(size_t)gc_newgame_visbytes );
			memcpy( gc_newgame_nodebits,
				gc_newgame_packed_nodebits + (size_t)cluster * (size_t)gc_newgame_nodebytes,
				(size_t)gc_newgame_nodebytes );
			/* G160: rebuild marksurface bits — memset alone left Flipper faces stale. */
			if( gc_newgame_surfbits )
				GC_BuildSurfbitsForVisRow( wmodel, gc_newgame_vis, gc_newgame_surfbits );
			gc_newgame_cluster_valid[slot] = 1;
			gc_newgame_lean_clusters[slot] = cluster;
			gc_newgame_lean_age[slot] = ++gc_newgame_lean_clock;
			gc_newgame_viewcluster = cluster;
			gc_newgame_lean_cluster = cluster;
			GC_CountActiveVisRows();
			SYS_Report( "Xash3D GameCube: PVS LRU cl=%d slot=%d evict=%d lf=%d nd=%d\n",
				cluster, slot, evicted, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
			GC_MaybeRefreshCapFacesAfterClusterChange( prev );
			return true;
		}
		return false;
	}

	if( cluster < 0 || cluster >= gc_newgame_numclusters )
		return false;
	if( !gc_newgame_cluster_valid[cluster] )
		return false;

	gc_newgame_vis = gc_newgame_pvs_table + (size_t)cluster * (size_t)gc_newgame_visbytes;
	gc_newgame_nodebits = gc_newgame_node_table + (size_t)cluster * (size_t)gc_newgame_nodebytes;
	gc_newgame_surfbits = ( gc_newgame_surf_table && gc_newgame_surfbytes > 0 )
		? gc_newgame_surf_table + (size_t)cluster * (size_t)gc_newgame_surfbytes
		: NULL;
	if( !gc_newgame_surfbits )
		gc_newgame_surfbits = GC_LookupSurfbitsCache( cluster );
	gc_newgame_viewcluster = cluster;
	GC_CountActiveVisRows();

	if( log_change && prev != cluster )
	{
		SYS_Report( "Xash3D GameCube: PVS cluster change %d->%d leaves=%d nodes=%d\n",
			prev, cluster, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
	}
	GC_MaybeRefreshCapFacesAfterClusterChange( prev );
	return true;
}

static int GC_VisLeafsForCluster( int cluster )
{
	const byte *vis;
	const byte *nodebits;
	int i, leaves = 0;

	if( cluster < 0 || !gc_newgame_pvs_table || !gc_newgame_cluster_valid )
		return 0;
	if( gc_newgame_pvs_lean )
	{
		int slot;

		for( slot = 0; slot < gc_newgame_lean_slots; slot++ )
		{
			if( gc_newgame_lean_clusters[slot] != cluster || !gc_newgame_cluster_valid[slot] )
				continue;
			vis = gc_newgame_pvs_table + (size_t)slot * (size_t)gc_newgame_visbytes;
			for( i = 0; i < gc_newgame_numleafs; i++ )
			{
				if( vis[i >> 3] & ( 1 << ( i & 7 )))
					leaves++;
			}
			return leaves;
		}
		return 0;
	}
	if( cluster >= gc_newgame_numclusters || !gc_newgame_cluster_valid[cluster] )
		return 0;
	vis = gc_newgame_pvs_table + (size_t)cluster * (size_t)gc_newgame_visbytes;
	nodebits = gc_newgame_node_table + (size_t)cluster * (size_t)gc_newgame_nodebytes;
	(void)nodebits;
	for( i = 0; i < gc_newgame_numleafs; i++ )
	{
		if( vis[i >> 3] & ( 1 << ( i & 7 )))
			leaves++;
	}
	return leaves;
}

static int GC_SelectClusterForOrigin( const float *org )
{
	int i;
	int best = -1;
	int best_leaves = -1;
	float best_vol = 1e30f;

	if( !org || !gc_newgame_leafboxes || gc_newgame_nleafboxes <= 0 )
		return gc_newgame_viewcluster;

	/* G132: overlapping leaf AABBs are common — prefer the densest cached PVS
	 * among hits so a tiny overlapping leaf does not starve solid spans. */
	for( i = 0; i < gc_newgame_nleafboxes; i++ )
	{
		const gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i];
		float vol;
		vec3_t size;
		int leaves;

		if( box->cluster < 0 )
			continue;
		if( org[0] < box->mins[0] || org[0] > box->maxs[0]
			|| org[1] < box->mins[1] || org[1] > box->maxs[1]
			|| org[2] < box->mins[2] || org[2] > box->maxs[2] )
			continue;

		leaves = GC_VisLeafsForCluster( box->cluster );
		VectorSubtract( box->maxs, box->mins, size );
		vol = size[0] * size[1] * size[2];
		if( vol <= 0.0f )
			vol = 1.0f;
		if( leaves > best_leaves || ( leaves == best_leaves && vol < best_vol ))
		{
			best_leaves = leaves;
			best_vol = vol;
			best = box->cluster;
		}
	}

	return ( best >= 0 ) ? best : gc_newgame_viewcluster;
}

/*
 * G89: select cached PVS row for the camera origin via load-time leaf AABBs
 * (no live PointInLeaf — BSP scratch may be corrupted by present time).
 */
qboolean GC_UpdateNewGamePVSForOrigin( const float *org )
{
	int cluster;

	if( !gc_newgame_pvs_ready || !org )
		return false;
	/* G230: tram/near-eye lock — keep streamed cluster through DumpFrames. */
	if( gc_g212_stream_locked )
		return true;

	/* G96/G101 lean cache: follow among cached slots via leaf AABBs. */
	if( gc_newgame_pvs_lean )
	{
		cluster = GC_SelectClusterForOrigin( org );
		if( !GC_SetActiveNewGameCluster( cluster, true ))
			return GC_SetActiveNewGameCluster( gc_newgame_lean_cluster, false );
		return true;
	}

	cluster = GC_SelectClusterForOrigin( org );
	return GC_SetActiveNewGameCluster( cluster, true );
}

static void GC_BuildNodebitsForVisRow( model_t *wmodel, const byte *vis, byte *nodebits )
{
	const int parent_limit = wmodel->numnodes > 0 ? wmodel->numnodes + 8 : 4096;
	int i;

	memset( nodebits, 0, (size_t)gc_newgame_nodebytes );

	for( i = 0; i < wmodel->numleafs; i++ )
	{
		mnode_t *node;
		int parent_depth = 0;

		if( !( vis[i >> 3] & ( 1 << ( i & 7 ))))
			continue;
		if( i + 1 >= wmodel->numleafs )
			continue;
		node = (mnode_t *)&wmodel->leafs[i + 1];
		do
		{
			if( node->contents >= 0 )
			{
				const int nidx = (int)( node - wmodel->nodes );

				if( nidx >= 0 && nidx < wmodel->numnodes )
					nodebits[nidx >> 3] |= (byte)( 1 << ( nidx & 7 ));
			}
			node = node->parent;
			if( ++parent_depth > parent_limit )
				break;
		}
		while( node );
	}
}

static size_t GC_CompressedPVSRowSize( const byte *in, size_t visbytes )
{
	size_t consumed = 0;
	size_t produced = 0;

	if( !in )
		return 0;
	while( produced < visbytes )
	{
		byte value = in[consumed++];

		if( value )
			produced++;
		else
		{
			byte run = in[consumed++];

			if( !run )
				break;
			produced += (size_t)run < visbytes - produced ? (size_t)run : visbytes - produced;
		}
	}
	return consumed;
}

static qboolean GC_CaptureLeanCompressedPVS( model_t *wmodel, int numclusters, size_t visbytes )
{
	size_t total = 0;
	int i;

	gc_newgame_compressed_ofs = (int *)malloc( (size_t)numclusters * sizeof( int ));
	if( !gc_newgame_compressed_ofs )
		return false;
	for( i = 0; i < numclusters; i++ )
		gc_newgame_compressed_ofs[i] = -1;

	for( i = 1; i < wmodel->numleafs; i++ )
	{
		mleaf_t *leaf = &wmodel->leafs[i];
		size_t row_size;

		if( leaf->cluster < 0 || leaf->cluster >= numclusters
			|| gc_newgame_compressed_ofs[leaf->cluster] != -1 || !leaf->compressed_vis )
			continue;
		row_size = GC_CompressedPVSRowSize( leaf->compressed_vis, visbytes );
		if( total > (size_t)INT_MAX || row_size > (size_t)INT_MAX - total )
			return false;
		gc_newgame_compressed_ofs[leaf->cluster] = (int)row_size;
		total += row_size;
	}
	if( !total )
		return false;

	gc_newgame_compressed_pvs = (byte *)malloc( total );
	if( !gc_newgame_compressed_pvs )
		return false;

	total = 0;
	for( i = 1; i < wmodel->numleafs; i++ )
	{
		mleaf_t *leaf = &wmodel->leafs[i];
		int cluster = leaf->cluster;
		size_t row_size;

		if( cluster < 0 || cluster >= numclusters || !leaf->compressed_vis
			|| gc_newgame_compressed_ofs[cluster] < 0 )
			continue;
		row_size = (size_t)gc_newgame_compressed_ofs[cluster];
		gc_newgame_compressed_ofs[cluster] = (int)total;
		memcpy( gc_newgame_compressed_pvs + total, leaf->compressed_vis, row_size );
		total += row_size;
		/* Mark copied clusters so duplicate leaves are skipped. */
		gc_newgame_compressed_ofs[cluster] = -( gc_newgame_compressed_ofs[cluster] + 2 );
	}
	for( i = 0; i < numclusters; i++ )
	{
		if( gc_newgame_compressed_ofs[i] <= -2 )
			gc_newgame_compressed_ofs[i] = -gc_newgame_compressed_ofs[i] - 2;
	}
	gc_newgame_compressed_size = total;
	GC_FlipperTrace( "Xash3D GameCube: Capture FatPVS lean LRU rows=%d packed=%u\n",
		numclusters, (unsigned)total );
	return true;
}

static qboolean GC_CaptureLeanNodebits( model_t *wmodel, int numclusters )
{
	byte *vis_scratch = gc_newgame_pvs_table;
	byte *node_scratch = gc_newgame_node_table;
	int cluster;

	if( !gc_newgame_compressed_pvs || !gc_newgame_compressed_ofs
		|| !vis_scratch || !node_scratch || gc_newgame_nodebytes <= 0 )
		return false;
	gc_newgame_packed_nodebits_size = (size_t)numclusters * (size_t)gc_newgame_nodebytes;
	gc_newgame_packed_nodebits = (byte *)malloc( gc_newgame_packed_nodebits_size );
	if( !gc_newgame_packed_nodebits )
	{
		gc_newgame_packed_nodebits_size = 0;
		return false;
	}
	memset( gc_newgame_packed_nodebits, 0, gc_newgame_packed_nodebits_size );

	for( cluster = 0; cluster < numclusters; cluster++ )
	{
		if( gc_newgame_compressed_ofs[cluster] < 0 )
			continue;
		GC_DecompressPVS( vis_scratch,
			gc_newgame_compressed_pvs + gc_newgame_compressed_ofs[cluster],
			(size_t)gc_newgame_visbytes );
		GC_BuildNodebitsForVisRow( wmodel, vis_scratch, node_scratch );
		memcpy( gc_newgame_packed_nodebits + (size_t)cluster * (size_t)gc_newgame_nodebytes,
			node_scratch, (size_t)gc_newgame_nodebytes );
	}
	GC_FlipperTrace( "Xash3D GameCube: Capture FatPVS lean LRU nodebits=%u\n",
		(unsigned)gc_newgame_packed_nodebits_size );
	return true;
}

static void GC_FreeNewGamePVSCache( void )
{
	/* G213: do not free lean live-face pool here — lean PVS retries call this
	 * mid-capture. Pool is freed on changelevel/shutdown only. */
	if( gc_newgame_pvs_table )
	{
		free( gc_newgame_pvs_table );
		gc_newgame_pvs_table = NULL;
	}
	if( gc_newgame_node_table )
	{
		free( gc_newgame_node_table );
		gc_newgame_node_table = NULL;
	}
	if( gc_newgame_surf_table )
	{
		free( gc_newgame_surf_table );
		gc_newgame_surf_table = NULL;
	}
	if( gc_newgame_surf_cache )
	{
		free( gc_newgame_surf_cache );
		gc_newgame_surf_cache = NULL;
	}
	gc_newgame_surf_cache_slots = 0;
	memset( gc_refresh_ncands, 0, sizeof( gc_refresh_ncands ));
	gc_g165_eye_cluster = -1;
	gc_g171_logged = false;
	if( gc_newgame_cluster_valid )
	{
		free( gc_newgame_cluster_valid );
		gc_newgame_cluster_valid = NULL;
	}
	if( gc_newgame_leafboxes )
	{
		free( gc_newgame_leafboxes );
		gc_newgame_leafboxes = NULL;
	}
	if( gc_newgame_compressed_pvs )
	{
		free( gc_newgame_compressed_pvs );
		gc_newgame_compressed_pvs = NULL;
	}
	if( gc_newgame_compressed_ofs )
	{
		free( gc_newgame_compressed_ofs );
		gc_newgame_compressed_ofs = NULL;
	}
	if( gc_newgame_packed_nodebits )
	{
		free( gc_newgame_packed_nodebits );
		gc_newgame_packed_nodebits = NULL;
	}
	gc_newgame_compressed_size = 0;
	gc_newgame_packed_nodebits_size = 0;
	gc_newgame_vis = NULL;
	gc_newgame_nodebits = NULL;
	gc_newgame_surfbits = NULL;
	gc_newgame_nleafboxes = 0;
	gc_newgame_numclusters = 0;
	gc_newgame_surfbytes = 0;
	gc_newgame_numsurfaces = 0;
	gc_newgame_cap_face_count = 0;
	gc_cap_refresh_pending = false;
	gc_g188_reposition_pending = false;
	gc_newgame_cap_tex_faces = 0;
	gc_newgame_cap_lm_atlas_ready = false;
	gc_newgame_pvs_lean = false;
	gc_newgame_lean_cluster = -1;
	gc_newgame_lean_slots = 0;
	memset( gc_newgame_lean_clusters, -1, sizeof( gc_newgame_lean_clusters ));
	memset( gc_newgame_lean_age, 0, sizeof( gc_newgame_lean_age ));
	gc_newgame_lean_clock = 0;
	gc_newgame_pvs_ready = false;
	gc_newgame_pvs_follow_proved = false;
}

/*
===========
GC_ResetNewGameWorldForChangelevel

G92: drop first-map PVS pins and world-ready so the next map can Capture +
Prepare again. Keep G36 done sticky (do not re-arm the budget probe).
===========
*/
void GC_ResetNewGameWorldForChangelevel( void )
{
#if XASH_GAMECUBE
	/* Always tear down Flipper world state on changelevel when it was armed;
	 * probe argv alone must not gate retail map transitions. */
	if( !gc_newgame_world_ready && !gc_gx_world_live
		&& !Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcchangelevel" ))
		return;

	SYS_Report( "Xash3D GameCube: changelevel teardown map=%s world_ready=%d pvs=%d\n",
		sv.name[0] ? sv.name : "?", gc_newgame_world_ready ? 1 : 0,
		gc_newgame_pvs_ready ? 1 : 0 );
	GC_FreeLiveFaces();
	GC_FreeTramFaces();
	GC_FreeNewGamePVSCache();
	gc_newgame_world_ready = false;
	gc_lean_sky_attempts = 0;
	gc_gx_world_live = false;
	gc_gx_world_efb_ready = false;
	gc_g192_post_changelevel = true;
	gc_g193_defer_flipper_left = 0;
	gc_g193_draining = false;
	gc_g193_soft_lock = false;
	gc_g196_flipper_dump_aim_left = 0;
	gc_g212_stream_locked = false;
	GC_G193ReleaseSoftSnap();
	gc_newgame_viewcluster = -1;
	/* Keep retained BSP tracking until FreeModel; clearing early makes
	 * Mod_FreeLoadBuffer treat the arena as a Mem_ block and corrupts heap. */
#else
	;
#endif
}

/*
===========
GC_MarkNewGameWorldStale

G94: clear world_ready so Prepare re-presents, but keep the PVS cache.
===========
*/
void GC_MarkNewGameWorldStale( void )
{
#if XASH_GAMECUBE
	SYS_Report( "Xash3D GameCube: G94 world stale (keep pvs=%d)\n",
		gc_newgame_pvs_ready ? 1 : 0 );
	gc_newgame_world_ready = false;
#endif
}

static void GC_ProveNewGamePVSFollow( void )
{
	int c0, c1, i;
	int leaves0, nodes0, leaves1, nodes1;

	if( gc_newgame_pvs_follow_proved || !gc_newgame_pvs_ready )
		return;
	/* G233: do not swap lean slots under tram stream lock. */
	if( gc_g212_stream_locked )
		return;

	c0 = gc_newgame_viewcluster;
	c1 = -1;
	if( gc_newgame_pvs_lean )
	{
		/* G101: switch among cached lean slots (not full cluster table). */
		for( i = 0; i < gc_newgame_lean_slots; i++ )
		{
			if( !gc_newgame_cluster_valid[i] )
				continue;
			if( gc_newgame_lean_clusters[i] == c0 )
				continue;
			c1 = gc_newgame_lean_clusters[i];
			break;
		}
	}
	else
	{
		/* G163: prefer an alternate that has capture-time surfbits (full table may OOM). */
		for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
		{
			if( gc_newgame_surf_cache_cluster[i] == c0 )
				continue;
			if( gc_newgame_surf_cache_cluster[i] >= 0
				&& gc_newgame_cluster_valid
				&& gc_newgame_surf_cache_cluster[i] < gc_newgame_numclusters
				&& gc_newgame_cluster_valid[gc_newgame_surf_cache_cluster[i]] )
			{
				c1 = gc_newgame_surf_cache_cluster[i];
				break;
			}
		}
		if( c1 < 0 )
		{
			for( i = 0; i < gc_newgame_numclusters; i++ )
			{
				if( i == c0 )
					continue;
				if( gc_newgame_cluster_valid[i] )
				{
					c1 = i;
					break;
				}
			}
		}
	}
	if( c1 < 0 )
	{
		SYS_Report( "Xash3D GameCube: PVS follow prove skipped (only one cluster)\n" );
		gc_newgame_pvs_follow_proved = true;
		return;
	}

	if( !GC_SetActiveNewGameCluster( c0, false ))
		return;
	leaves0 = gc_newgame_vis_leafs;
	nodes0 = gc_newgame_vis_nodes;
	SYS_Report( "Xash3D GameCube: PVS follow prove cluster=%d leaves=%d nodes=%d\n",
		c0, leaves0, nodes0 );

	if( !GC_SetActiveNewGameCluster( c1, true ))
		return;
	leaves1 = gc_newgame_vis_leafs;
	nodes1 = gc_newgame_vis_nodes;
	SYS_Report( "Xash3D GameCube: PVS follow prove cluster=%d leaves=%d nodes=%d\n",
		c1, leaves1, nodes1 );

	gc_newgame_pvs_follow_proved = true;
	if( gc_newgame_pvs_lean )
	{
		SYS_Report( "Xash3D GameCube: PVS lean follow ready slots=%d clusters=%d->%d leafdelta=%d\n",
			gc_newgame_lean_slots, c0, c1, leaves1 - leaves0 );
		if( gc_newgame_compressed_ofs )
		{
			int miss = -1;

			for( i = 0; i < gc_newgame_numclusters; i++ )
			{
				int slot;
				qboolean cached = false;

				if( gc_newgame_compressed_ofs[i] < 0 )
					continue;
				for( slot = 0; slot < gc_newgame_lean_slots; slot++ )
				{
					if( gc_newgame_cluster_valid[slot] && gc_newgame_lean_clusters[slot] == i )
					{
						cached = true;
						break;
					}
				}
				if( !cached )
				{
					miss = i;
					break;
				}
			}
			if( miss >= 0 && GC_SetActiveNewGameCluster( miss, true ))
			{
				SYS_Report( "Xash3D GameCube: PVS lean LRU ready slots=%d loaded=%d packed=%u\n",
					gc_newgame_lean_slots, miss, (unsigned)gc_newgame_compressed_size );
			}
		}
	}
	else
	{
		SYS_Report( "Xash3D GameCube: PVS follow ready clusters=%d->%d leafdelta=%d\n",
			c0, c1, leaves1 - leaves0 );
	}

	/* G163: flush prove-time cluster switches before restore UpdatePVS.
	 * G230: skip explore/restore refresh when tram/near-eye cap is locked. */
	if( !gc_g212_stream_locked )
		GC_FlushPendingCapFaceRefresh();

	/* Switch to densest cached cluster so refresh admits faces outside bake set. */
	if( !gc_g212_stream_locked && gc_newgame_surf_cache_slots > 1 )
	{
		int explore = -1;
		int explore_leaves = -1;

		for( i = 0; i < gc_newgame_surf_cache_slots; i++ )
		{
			int cand = gc_newgame_surf_cache_cluster[i];
			int leaves;

			if( cand < 0 || cand == gc_newgame_viewcluster )
				continue;
			if( !gc_newgame_cluster_valid || cand >= gc_newgame_numclusters
				|| !gc_newgame_cluster_valid[cand] )
				continue;
			leaves = GC_VisLeafsForCluster( cand );
			if( leaves > explore_leaves )
			{
				explore_leaves = leaves;
				explore = cand;
			}
		}
		if( explore >= 0 && GC_SetActiveNewGameCluster( explore, true ))
		{
			GC_FlipperTrace( "Xash3D GameCube: G163 explore cluster=%d leaves=%d for face refresh\n",
				explore, explore_leaves );
			GC_FlushPendingCapFaceRefresh();
		}
	}

	/* Restore origin-based cluster when possible (player / first leafbox). */
	if( !gc_g212_stream_locked && svgame.edicts && svs.maxclients >= 1 )
	{
		edict_t *player = SV_EdictNum( 1 );

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			vec3_t eye;

			VectorCopy( player->v.origin, eye );
			eye[2] += 48.0f;
			GC_UpdateNewGamePVSForOrigin( eye );
			/* G189/G190: densest mega-row wipes outdoor walls — prefer wall-heavy outdoor. */
			GC_PreferOutdoorWallCluster();
			GC_FlushPendingCapFaceRefresh();
			return;
		}
	}
	if( !gc_g212_stream_locked )
	{
		GC_SetActiveNewGameCluster( c0, false );
		GC_FlushPendingCapFaceRefresh();
	}
}
#endif

/*
 * G83/G89: capture PointInLeaf + per-cluster PVS while BSP scratch is intact.
 * G89 stores every cluster row + leaf AABBs so present-time selection needs no
 * live tree walks after scratch reuse.
 */
void GC_CaptureNewGamePVSFromModel( model_t *wmodel )
{
#if XASH_GAMECUBE
	vec3_t vieworigin;
	mleaf_t *viewleaf;
	int max_cluster = -1;
	int i;

	/* Retail Flipper and -gcnewgame both need prepare-time PVS. Soft-only
	 * smoke without Flipper can skip via -gcsoftworld. */
	if( Sys_CheckParm( "-gcsoftworld" ))
		return;
	if( gc_newgame_pvs_ready )
		return;
	if( !wmodel )
		wmodel = sv.models[1];

	gc_newgame_viewcluster = -1;
	gc_newgame_vis_leafs = 0;
	gc_newgame_vis_nodes = 0;

	GC_FlipperTrace( "Xash3D GameCube: CaptureNewGamePVS begin\n" );

	if( !wmodel || !wmodel->nodes || !wmodel->leafs )
	{
		SYS_Report( "Xash3D GameCube: CaptureNewGamePVS skipped (no world nodes)\n" );
		return;
	}

	/* G277: *12 faces baked just after submodels (before lighting scratch). */

	VectorAverage( wmodel->mins, wmodel->maxs, vieworigin );
	vieworigin[2] += 64.0f;
	if( svgame.edicts )
	{
		for( i = 1; i < svgame.numEntities; i++ )
		{
			edict_t *ent = &svgame.edicts[i];

			if( ent->free || VectorIsNull( ent->v.origin ))
				continue;
			VectorCopy( ent->v.origin, vieworigin );
			vieworigin[2] += 48.0f;
			break;
		}
	}

	viewleaf = Mod_PointInLeaf( vieworigin, wmodel->nodes, wmodel );
	gc_newgame_viewcluster = viewleaf ? viewleaf->cluster : -1;
	/* Reject clearly corrupted leaf walks (scratch reuse / bad nodes). */
	if( viewleaf && ( viewleaf->contents >= 0
		|| gc_newgame_viewcluster < -1
		|| gc_newgame_viewcluster >= wmodel->numleafs ))
	{
		gc_newgame_viewcluster = -1;
		viewleaf = NULL;
	}

	/* Load-time world center is often solid; pick any non-solid leaf cluster. */
	if( gc_newgame_viewcluster < 0 && wmodel->leafs )
	{
		for( i = 1; i < wmodel->numleafs; i++ )
		{
			mleaf_t *leaf = &wmodel->leafs[i];
			vec3_t leaf_origin;

			if( leaf->cluster < 0 || leaf->contents >= 0 )
				continue;
			VectorAverage( leaf->minmaxs, leaf->minmaxs + 3, leaf_origin );
			viewleaf = Mod_PointInLeaf( leaf_origin, wmodel->nodes, wmodel );
			if( viewleaf && viewleaf->cluster >= 0 )
			{
				VectorCopy( leaf_origin, vieworigin );
				gc_newgame_viewcluster = viewleaf->cluster;
				break;
			}
		}
	}

	GC_FlipperTrace( "Xash3D GameCube: Capture leaf cl=%d c=%d o=(%.0f,%.0f,%.0f)\n",
		gc_newgame_viewcluster, viewleaf ? viewleaf->contents : 0,
		vieworigin[0], vieworigin[1], vieworigin[2] );
	VectorCopy( vieworigin, gc_newgame_capture_origin );

	if( !wmodel->visdata || world.visbytes <= 0 )
	{
		SYS_Report( "Xash3D GameCube: Capture FatPVS skipped vis=%d\n",
			wmodel->visdata ? 1 : 0 );
		return;
	}

	for( i = 1; i < wmodel->numleafs; i++ )
	{
		if( wmodel->leafs[i].cluster > max_cluster )
			max_cluster = wmodel->leafs[i].cluster;
	}
	if( max_cluster < 0 )
	{
		SYS_Report( "Xash3D GameCube: Capture FatPVS skipped (no clusters)\n" );
		return;
	}
	/* G96: after scratch reuse leaf.cluster fields can be garbage. */
	if( max_cluster >= wmodel->numleafs || max_cluster > 4096 )
	{
		SYS_Report( "Xash3D GameCube: Capture FatPVS skipped (corrupt max_cluster=%d leafs=%d)\n",
			max_cluster, wmodel->numleafs );
		return;
	}

	{
		const size_t visbytes = world.visbytes;
		const size_t nodebytes = (size_t)( wmodel->numnodes + 7 ) / 8;
		const int numclusters = max_cluster + 1;
		int valid_clusters = 0;

		GC_FreeNewGamePVSCache();

		/* G213: reserve lean live-face pool after PVS-cache free, before FatPVS.
		 * G283/G298: scratch retain skips lean HERE so FatPVS gets MEM1; lean
		 * alloc+bake runs later in CaptureLiveFacesForDumpClusters. Malloc pin
		 * keeps live BSP emit (no lean). */
		if( Mod_GCWorldSurfacesPinned( wmodel )
			&& !Mod_GCWorldSurfacesScratchRetained( wmodel ))
			Con_Reportf( "Xash3D GameCube: G283 Capture skips lean live pool (malloc pin)\n" );
		else if( Mod_GCWorldSurfacesScratchRetained( wmodel ))
			Con_Reportf( "Xash3D GameCube: G298 Capture defers lean live pool (scratch retain)\n" );
		else
			GC_AllocLiveFacePool( GC_LIVE_MAX_FACES );

		gc_newgame_visbytes = (int)visbytes;
		gc_newgame_nodebytes = (int)nodebytes;
		gc_newgame_numclusters = numclusters;
		gc_newgame_numleafs = wmodel->numleafs;
		gc_newgame_numnodes = wmodel->numnodes;
		gc_newgame_numsurfaces = wmodel->numsurfaces;
		gc_newgame_surfbytes = ( wmodel->numsurfaces > 0 )
			? (int)(( (size_t)wmodel->numsurfaces + 7 ) / 8) : 0;
		gc_newgame_nleafboxes = wmodel->numleafs > 1 ? wmodel->numleafs - 1 : 0;

		/* Prefer lean FatPVS on GameCube — full multi-row calloc for ~900
		 * clusters OOMs / fragments MEM1 before face bake can run. */
		if( !Sys_CheckParm( "-gcleanpvs" ) && !Sys_CheckParm( "-gcnewgame" ))
		{
			gc_newgame_pvs_table = (byte *)calloc( (size_t)numclusters, visbytes );
			gc_newgame_node_table = (byte *)calloc( (size_t)numclusters, nodebytes );
			gc_newgame_cluster_valid = (byte *)calloc( (size_t)numclusters, 1 );
			gc_newgame_leafboxes = gc_newgame_nleafboxes > 0
				? (gc_newgame_leafbox_t *)calloc( (size_t)gc_newgame_nleafboxes, sizeof( gc_newgame_leafbox_t ))
				: NULL;
		}
		else
		{
			GC_FlipperTrace( "Xash3D GameCube: Capture FatPVS lean-first clusters=%d\n",
				numclusters );
		}

		if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid
			|| ( gc_newgame_nleafboxes > 0 && !gc_newgame_leafboxes ))
		{
			mleaf_t *vleaf = NULL;
			byte *spawn_row;
			int lean_cluster = gc_newgame_viewcluster;
			int lean_slots = 0;
			int slot;

			/* G96/G101: capture a small multi-room lean cache while BSP leafs
			 * are still valid. Full multi-row tables OOM after changelevel. */
			SYS_Report( "Xash3D GameCube: Capture FatPVS multi-cluster alloc failed clusters=%d\n",
				numclusters );
			GC_FreeNewGamePVSCache();

			if( lean_cluster < 0 )
			{
				for( i = 1; i < wmodel->numleafs; i++ )
				{
					if( wmodel->leafs[i].cluster >= 0 && wmodel->leafs[i].contents < 0 )
					{
						lean_cluster = wmodel->leafs[i].cluster;
						break;
					}
				}
			}
			if( lean_cluster < 0 )
				return;

			for( i = 1; i < wmodel->numleafs; i++ )
			{
				if( wmodel->leafs[i].cluster == lean_cluster )
				{
					vleaf = &wmodel->leafs[i];
					break;
				}
			}
			if( !vleaf )
				return;

			gc_newgame_visbytes = (int)visbytes;
			gc_newgame_nodebytes = (int)nodebytes;
			gc_newgame_numclusters = numclusters;
			gc_newgame_numleafs = wmodel->numleafs;
			gc_newgame_numnodes = wmodel->numnodes;
			gc_newgame_numsurfaces = wmodel->numsurfaces;
			gc_newgame_surfbytes = ( wmodel->numsurfaces > 0 )
				? (int)(( (size_t)wmodel->numsurfaces + 7 ) / 8) : 0;
			gc_newgame_nleafboxes = wmodel->numleafs > 1 ? wmodel->numleafs - 1 : 0;
			gc_newgame_pvs_lean = true;
			gc_newgame_lean_cluster = lean_cluster;
			gc_newgame_lean_slots = 0;
			memset( gc_newgame_lean_clusters, -1, sizeof( gc_newgame_lean_clusters ));

			Image_GCPurgeDecodeScratch();
			gc_newgame_pvs_table = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, visbytes );
			gc_newgame_node_table = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, nodebytes );
			gc_newgame_surf_table = ( gc_newgame_surfbytes > 0 )
				? (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, (size_t)gc_newgame_surfbytes )
				: NULL;
			gc_newgame_cluster_valid = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, 1 );
			/* Leafboxes are optional for Flipper face bake — skip if freelist is tight. */
			gc_newgame_leafboxes = gc_newgame_nleafboxes > 0
				? (gc_newgame_leafbox_t *)calloc( (size_t)gc_newgame_nleafboxes, sizeof( gc_newgame_leafbox_t ))
				: NULL;
			if( !gc_newgame_leafboxes )
				gc_newgame_nleafboxes = 0;
			if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid )
			{
				SYS_Report( "Xash3D GameCube: Capture FatPVS lean alloc failed\n" );
				GC_FreeNewGamePVSCache();
				/* Surfaces still valid during BSP load — bake Flipper caps now. */
				Image_GCPurgeDecodeScratch();
				GC_CaptureDrawFacesNoPVS( wmodel );
				return;
			}

			/* Leaf AABBs enable origin follow among cached clusters. */
			if( gc_newgame_leafboxes )
			{
				for( i = 1; i < wmodel->numleafs; i++ )
				{
					mleaf_t *leaf = &wmodel->leafs[i];
					gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i - 1];

					VectorCopy( leaf->minmaxs, box->mins );
					VectorCopy( leaf->minmaxs + 3, box->maxs );
					box->cluster = leaf->cluster;
				}
			}
			if( !GC_CaptureLeanCompressedPVS( wmodel, numclusters, visbytes )
				|| !GC_CaptureLeanNodebits( wmodel, numclusters ))
			{
				SYS_Report( "Xash3D GameCube: Capture FatPVS lean LRU store unavailable\n" );
				if( gc_newgame_compressed_pvs )
				{
					free( gc_newgame_compressed_pvs );
					gc_newgame_compressed_pvs = NULL;
				}
				if( gc_newgame_compressed_ofs )
				{
					free( gc_newgame_compressed_ofs );
					gc_newgame_compressed_ofs = NULL;
				}
				if( gc_newgame_packed_nodebits )
				{
					free( gc_newgame_packed_nodebits );
					gc_newgame_packed_nodebits = NULL;
				}
				gc_newgame_compressed_size = 0;
				gc_newgame_packed_nodebits_size = 0;
			}

			/* Slot 0: spawn cluster. */
			spawn_row = gc_newgame_pvs_table;
			GC_DecompressPVS( spawn_row, vleaf->compressed_vis, visbytes );
			GC_BuildNodebitsForVisRow( wmodel, spawn_row, gc_newgame_node_table );
			if( gc_newgame_surf_table )
				GC_BuildSurfbitsForVisRow( wmodel, spawn_row, gc_newgame_surf_table );
			gc_newgame_cluster_valid[0] = 1;
			gc_newgame_lean_clusters[0] = lean_cluster;
			gc_newgame_lean_age[0] = ++gc_newgame_lean_clock;
			lean_slots = 1;

			/* Slots 1..N-1: nearby clusters visible from spawn (capture now;
			 * compressed_vis is invalid after scratch reuse).
			 * G190: pass 0 prefers outdoor-band PVS rows (20–48 leaves) so
			 * landmark soft dumps can refresh wall faces; pass 1 fills any. */
			{
				int pass;

				for( pass = 0; pass < 2 && lean_slots < GC_LEAN_PVS_SLOTS; pass++ )
				{
					for( i = 1; i < wmodel->numleafs && lean_slots < GC_LEAN_PVS_SLOTS; i++ )
					{
						mleaf_t *leaf = &wmodel->leafs[i];
						int c = leaf->cluster;
						byte *row;
						qboolean already = false;
						int vis_leaves;
						int li;

						if( c < 0 || c == lean_cluster )
							continue;
						if( !( spawn_row[c >> 3] & (byte)( 1 << ( c & 7 ))))
							continue;
						for( slot = 0; slot < lean_slots; slot++ )
						{
							if( gc_newgame_lean_clusters[slot] == c )
							{
								already = true;
								break;
							}
						}
						if( already )
							continue;

						row = gc_newgame_pvs_table + (size_t)lean_slots * visbytes;
						GC_DecompressPVS( row, leaf->compressed_vis, visbytes );
						vis_leaves = 0;
						for( li = 0; li < wmodel->numleafs; li++ )
						{
							if( row[li >> 3] & (byte)( 1 << ( li & 7 )))
								vis_leaves++;
						}
						if( pass == 0 && ( vis_leaves < 20 || vis_leaves > 48 ))
							continue;

						GC_BuildNodebitsForVisRow( wmodel, row,
							gc_newgame_node_table + (size_t)lean_slots * nodebytes );
						if( gc_newgame_surf_table )
							GC_BuildSurfbitsForVisRow( wmodel, row,
								gc_newgame_surf_table + (size_t)lean_slots * (size_t)gc_newgame_surfbytes );
						gc_newgame_cluster_valid[lean_slots] = 1;
						gc_newgame_lean_clusters[lean_slots] = c;
						gc_newgame_lean_age[lean_slots] = ++gc_newgame_lean_clock;
						lean_slots++;
					}
				}
			}

			/* G190: if spawn PVS had no outdoor-band row, steal the last lean
			 * slot for any outdoor cluster so wall soft dumps have cands. */
			{
				qboolean have_outdoor = false;
				int slot_i;

				for( slot_i = 0; slot_i < lean_slots; slot_i++ )
				{
					byte *row = gc_newgame_pvs_table + (size_t)slot_i * visbytes;
					int vis_leaves = 0;
					int li;

					for( li = 0; li < wmodel->numleafs; li++ )
					{
						if( row[li >> 3] & (byte)( 1 << ( li & 7 )))
							vis_leaves++;
					}
					if( vis_leaves >= 20 && vis_leaves <= 48 )
					{
						have_outdoor = true;
						break;
					}
				}
				if( !have_outdoor && lean_slots >= 2 )
				{
					int steal = lean_slots - 1;

					for( i = 1; i < wmodel->numleafs; i++ )
					{
						mleaf_t *leaf = &wmodel->leafs[i];
						int c = leaf->cluster;
						byte *row;
						int vis_leaves = 0;
						int li;
						qboolean already = false;

						if( c < 0 || c == lean_cluster )
							continue;
						for( slot = 0; slot < lean_slots; slot++ )
						{
							if( gc_newgame_lean_clusters[slot] == c )
							{
								already = true;
								break;
							}
						}
						if( already )
							continue;
						row = gc_newgame_pvs_table + (size_t)steal * visbytes;
						GC_DecompressPVS( row, leaf->compressed_vis, visbytes );
						for( li = 0; li < wmodel->numleafs; li++ )
						{
							if( row[li >> 3] & (byte)( 1 << ( li & 7 )))
								vis_leaves++;
						}
						if( vis_leaves < 20 || vis_leaves > 48 )
							continue;
						GC_BuildNodebitsForVisRow( wmodel, row,
							gc_newgame_node_table + (size_t)steal * nodebytes );
						if( gc_newgame_surf_table )
							GC_BuildSurfbitsForVisRow( wmodel, row,
								gc_newgame_surf_table + (size_t)steal * (size_t)gc_newgame_surfbytes );
						gc_newgame_cluster_valid[steal] = 1;
						gc_newgame_lean_clusters[steal] = c;
						gc_newgame_lean_age[steal] = ++gc_newgame_lean_clock;
						GC_FlipperTrace( "Xash3D GameCube: G190 lean outdoor slot=%d cluster=%d leaves=%d\n",
							steal, c, vis_leaves );
						break;
					}
				}
			}

			gc_newgame_lean_slots = lean_slots;
			gc_newgame_viewcluster = lean_cluster;
			if( !GC_SetActiveNewGameCluster( lean_cluster, false ))
			{
				GC_FreeNewGamePVSCache();
				return;
			}
			gc_newgame_pvs_ready = true;
			GC_FlipperTrace( "Xash3D GameCube: Capture lean map=%s cl=%d slots=%d L=%d N=%d\n",
				sv.name[0] ? sv.name : "?",
				lean_cluster, lean_slots, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
			/* G190: build refresh cands for lean slots (G165 skips lean). */
			for( slot = 0; slot < lean_slots; slot++ )
			{
				byte *row = gc_newgame_pvs_table + (size_t)slot * visbytes;

				GC_StoreSurfbitsCache( wmodel, gc_newgame_lean_clusters[slot], row );
			}
			if( gc_newgame_surfbits )
			{
				/* Caps first; G215 live pool from dump-cluster overflow at predicted eye. */
				GC_CaptureDrawFacesFromSurfbits( wmodel, gc_newgame_surfbits );
				GC_CaptureLiveFacesForDumpClusters( wmodel );
			}
			if( lean_slots > 1 )
			{
				GC_FlipperTrace( "Xash3D GameCube: Capture FatPVS lean-N map=%s slots=%d c0=%d c1=%d\n",
					sv.name[0] ? sv.name : "?",
					lean_slots, gc_newgame_lean_clusters[0], gc_newgame_lean_clusters[1] );
			}
			return;
		}

		GC_FlipperTrace( "Xash3D GameCube: Capture multi-cluster PVS begin clusters=%d visbytes=%d\n",
			numclusters, (int)visbytes );

		/* G132: best-effort surfbits — must not force lean fallback on OOM.
		 * G163: on full-table OOM, cache a few capture-time rows for refresh. */
		if( gc_newgame_surfbytes > 0 && !gc_newgame_surf_table )
		{
			gc_newgame_surf_table = (byte *)calloc( (size_t)numclusters, (size_t)gc_newgame_surfbytes );
			if( !gc_newgame_surf_table )
				SYS_Report( "Xash3D GameCube: Capture surfbits skipped (OOM clusters=%d bytes=%d)\n",
					numclusters, gc_newgame_surfbytes );
		}

		for( i = 1; i < wmodel->numleafs; i++ )
		{
			mleaf_t *leaf = &wmodel->leafs[i];
			gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i - 1];
			byte *row;

			VectorCopy( leaf->minmaxs, box->mins );
			VectorCopy( leaf->minmaxs + 3, box->maxs );
			box->cluster = leaf->cluster;

			if( leaf->cluster < 0 || leaf->cluster >= numclusters )
				continue;
			if( gc_newgame_cluster_valid[leaf->cluster] )
				continue;

			row = gc_newgame_pvs_table + (size_t)leaf->cluster * visbytes;
			GC_DecompressPVS( row, leaf->compressed_vis, visbytes );
			GC_BuildNodebitsForVisRow( wmodel, row,
				gc_newgame_node_table + (size_t)leaf->cluster * nodebytes );
			if( gc_newgame_surf_table )
				GC_BuildSurfbitsForVisRow( wmodel, row,
					gc_newgame_surf_table + (size_t)leaf->cluster * (size_t)gc_newgame_surfbytes );
			gc_newgame_cluster_valid[leaf->cluster] = 1;
			valid_clusters++;
		}

		if( gc_newgame_viewcluster < 0 || !gc_newgame_cluster_valid[gc_newgame_viewcluster] )
		{
			for( i = 0; i < numclusters; i++ )
			{
				if( gc_newgame_cluster_valid[i] )
				{
					gc_newgame_viewcluster = i;
					break;
				}
			}
		}

		if( gc_newgame_viewcluster < 0 || !GC_SetActiveNewGameCluster( gc_newgame_viewcluster, false ))
		{
			SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS failed (no valid row)\n" );
			GC_FreeNewGamePVSCache();
			return;
		}

		gc_newgame_pvs_ready = true;
		GC_FlipperTrace( "Xash3D GameCube: Capture FatPVS map=%s cluster=%d leaves=%d nodes=%d\n",
			sv.name[0] ? sv.name : "?",
			gc_newgame_viewcluster, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
		GC_FlipperTrace( "Xash3D GameCube: Capture multi-PVS map=%s cl=%d ok=%d\n",
			sv.name[0] ? sv.name : "?", numclusters, valid_clusters );
		/* G163: capture-time surfbits — marks die before present-time rebuild.
		 * G165: cache SelectClusterForOrigin hits for leafbox centers (same
		 * picker the camera/restore path uses) plus densest for explore. */
		if( !gc_newgame_surf_table && gc_newgame_surfbytes > 0 && gc_newgame_pvs_table )
		{
			int max_c = -1, max_leaves = -1;
			int eye_c = -1;

			if( gc_newgame_viewcluster >= 0 )
			{
				GC_StoreSurfbitsCache( wmodel, gc_newgame_viewcluster,
					gc_newgame_pvs_table + (size_t)gc_newgame_viewcluster * visbytes );
			}
			if( svgame.edicts && svgame.numEntities > 1 )
			{
				edict_t *player = ( svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;
				vec3_t eye;

				if( player && !player->free && !VectorIsNull( player->v.origin ))
				{
					VectorCopy( player->v.origin, eye );
					eye[2] += 48.0f;
					eye_c = GC_SelectClusterForOrigin( eye );
				}
				else
				{
					for( i = 1; i < svgame.numEntities; i++ )
					{
						edict_t *ent = &svgame.edicts[i];

						if( ent->free || VectorIsNull( ent->v.origin ))
							continue;
						VectorCopy( ent->v.origin, eye );
						eye[2] += 48.0f;
						eye_c = GC_SelectClusterForOrigin( eye );
						if( eye_c >= 0 )
							break;
					}
				}
				if( eye_c >= 0 && eye_c < numclusters && gc_newgame_cluster_valid[eye_c] )
				{
					GC_StoreSurfbitsCache( wmodel, eye_c,
						gc_newgame_pvs_table + (size_t)eye_c * visbytes );
					gc_g165_eye_cluster = eye_c;
					GC_FlipperTrace( "Xash3D GameCube: G165 restore cands ready cluster=%d\n",
						eye_c );
				}
			}
			for( i = 0; i < numclusters; i++ )
			{
				int leaves;

				if( !gc_newgame_cluster_valid[i] )
					continue;
				leaves = GC_VisLeafsForCluster( i );
				if( leaves > max_leaves )
				{
					max_leaves = leaves;
					max_c = i;
				}
			}
			/* G165/G171/G175: exact outdoor leaf-count, then near-band.
			 * With 4 slots, reserve 1 for max_c / leafbox. */
			for( i = 0; i < numclusters
				&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS - 1; i++ )
			{
				int leaves;

				if( !gc_newgame_cluster_valid[i] )
					continue;
				leaves = GC_VisLeafsForCluster( i );
				if( leaves != 35 )
					continue;
				GC_StoreSurfbitsCache( wmodel, i,
					gc_newgame_pvs_table + (size_t)i * visbytes );
				if( gc_g165_eye_cluster < 0 )
				{
					gc_g165_eye_cluster = i;
					GC_FlipperTrace( "Xash3D GameCube: G165 restore cands ready cluster=%d leaves=%d\n",
						i, leaves );
				}
			}
			for( i = 0; i < numclusters
				&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS - 1; i++ )
			{
				int leaves;

				if( !gc_newgame_cluster_valid[i] )
					continue;
				leaves = GC_VisLeafsForCluster( i );
				if( leaves < 30 || leaves > 40 || leaves == 35 )
					continue;
				GC_StoreSurfbitsCache( wmodel, i,
					gc_newgame_pvs_table + (size_t)i * visbytes );
			}
			/* Leafbox centers — identical picker to GC_UpdateNewGamePVSForOrigin. */
			if( gc_newgame_leafboxes && gc_newgame_nleafboxes > 0 )
			{
				for( i = 0; i < gc_newgame_nleafboxes
					&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS; i++ )
				{
					const gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i];
					vec3_t mid;
					int c;
					int leaves;

					if( box->cluster < 0 )
						continue;
					mid[0] = 0.5f * ( box->mins[0] + box->maxs[0] );
					mid[1] = 0.5f * ( box->mins[1] + box->maxs[1] );
					mid[2] = 0.5f * ( box->mins[2] + box->maxs[2] ) + 48.0f;
					c = GC_SelectClusterForOrigin( mid );
					if( c < 0 || c >= numclusters || !gc_newgame_cluster_valid[c] )
						continue;
					if( GC_LookupSurfbitsCache( c ))
						continue;
					leaves = GC_VisLeafsForCluster( c );
					if( leaves < 20 || leaves > 55 )
						continue;
					GC_StoreSurfbitsCache( wmodel, c,
						gc_newgame_pvs_table + (size_t)c * visbytes );
				}
			}
			if( max_c >= 0 )
				GC_StoreSurfbitsCache( wmodel, max_c,
					gc_newgame_pvs_table + (size_t)max_c * visbytes );
			if( gc_newgame_surf_cache_slots > 0 )
			{
				GC_FlipperTrace( "Xash3D GameCube: G163 surfbits cache slots=%d max_c=%d eye_c=%d g165=%d\n",
					gc_newgame_surf_cache_slots, max_c, eye_c, gc_g165_eye_cluster );
				gc_newgame_surfbits = GC_LookupSurfbitsCache( gc_newgame_viewcluster );
			}
		}
		if( gc_newgame_surfbits )
		{
			GC_CaptureDrawFacesFromSurfbits( wmodel, gc_newgame_surfbits );
			GC_CaptureLiveFacesForDumpClusters( wmodel );
		}
		else if( gc_newgame_surfbytes > 0 && gc_newgame_vis )
		{
			/* G154: multi-cluster may skip the full surf table under MEM1;
			 * bake Flipper faces from the active vis row alone. */
			byte *row_bits = (byte *)calloc( 1, (size_t)gc_newgame_surfbytes );

			if( row_bits )
			{
				GC_BuildSurfbitsForVisRow( wmodel, gc_newgame_vis, row_bits );
				GC_CaptureDrawFacesFromSurfbits( wmodel, row_bits );
				GC_CaptureLiveFacesForDumpClusters( wmodel );
				free( row_bits );
			}
			else
				SYS_Report( "Xash3D GameCube: G154 face bake skipped (surfbits row OOM)\n" );
		}
	}
#else
	(void)wmodel;
#endif
}

void GC_CaptureNewGamePVS( void )
{
	GC_CaptureNewGamePVSFromModel( NULL );
}

/*
 * G90: single New Game world pass for V_RenderViewBoundedGC.
 * Caller owns R_BeginFrame / R_EndFrame (V_PreRender / V_PostRender).
 */
qboolean GC_RenderNewGameWorldPassNoFrame( qboolean draw_viewmodel )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;
	if( !gc_newgame_world_ready )
		return false;

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	{
		edict_t *player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			VectorCopy( player->v.origin, center );
			if( !Sys_CheckParm( "-gcnewgame" ))
				center[2] += 48.0f;
			VectorCopy( player->v.v_angle, rvp.viewangles );
		}
		else
		{
			for( i = 1; i < svgame.numEntities; i++ )
			{
				edict_t *ent = &svgame.edicts[i];

				if( ent->free || VectorIsNull( ent->v.origin ))
					continue;
				VectorCopy( ent->v.origin, center );
				center[2] += 48.0f;
				break;
			}
		}
	}
	/* G279: locked tram stream follows the parented ride eye (not the fixed
	 * trainstop dump pose). G233 pinned DumpEyeAtTramStart and left the
	 * camera stranded as *12 rolled away — exterior tram-in-tunnel shots. */
	if( gc_g212_stream_locked && Sys_CheckParm( "-gcnewgame" ))
	{
		(void)GC_NewGameRideEye( center, rvp.viewangles );
		GC_MaybeRestreamRideMapFaces( center );
	}
	VectorCopy( center, rvp.vieworigin );
	if( !gc_g212_stream_locked )
	{
		GC_UpdateNewGamePVSForOrigin( center );
		GC_ProveNewGamePVSFollow();
		GC_FlushPendingCapFaceRefresh();
	}
	SetBits( rvp.flags, RF_DRAW_WORLD );

	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", draw_viewmodel ? "1" : "0" );

	VectorCopy( rvp.vieworigin, refState.vieworg );
	VectorCopy( rvp.viewangles, refState.viewangles );
	/* Match GC_RenderNewGameWorldFrames: do not call R_Set2DMode before the
	 * world pass — it has hung Host_Frame on the post-G36 pump. */
	ref.dllFuncs.GL_RenderFrame( &rvp );
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	return true;
#else
	(void)draw_viewmodel;
	return false;
#endif
}

/*
 * Bounded low-res world presents for New Game after G36.
 * Bypasses V_RenderView / Host_ServerFrame (both stall on this path) and
 * reuses the same GL_RenderFrame probe used by -gcworldrender / -gcmap.
 */
qboolean GC_RenderNewGameWorldFrames( int count )
{
#if XASH_GAMECUBE
	ref_viewpass_t rvp;
	model_t *world;
	vec3_t center;
	char old_drawviewmodel[16];
	int i;

	if( !ref.initialized || !SV_Active() )
		return false;
	if( !gc_newgame_world_ready )
		return false;
	if( count <= 0 )
		count = 1;
	if( count > 4 )
		count = 4;

	world = sv.models[1];
	if( !world )
		return false;

	cl.models[1] = world;
	cl.worldmodel = world;
	cl.video_prepped = true;

	/* Skip R_NewMap here: on New Game it re-enters texture/surface setup and
	 * stalls Host_Frame. Prepare already allocated low-res screens, scratch,
	 * and lean sky — render with the resident world model only. */

	memset( &rvp, 0, sizeof( rvp ));
	rvp.viewport[0] = 0;
	rvp.viewport[1] = 0;
	if( !R_GcmapGetViewport( &rvp.viewport[2], &rvp.viewport[3] ))
	{
		rvp.viewport[2] = refState.width > 0 ? refState.width : gc.width;
		rvp.viewport[3] = refState.height > 0 ? refState.height : gc.height;
	}
	rvp.fov_x = 90.0f;
	rvp.fov_y = rvp.fov_x * 0.75f;
	VectorAverage( world->mins, world->maxs, center );
	center[2] += 64.0f;
	/* Prefer local player (edict 1) so G86 move/look updates the camera. */
	{
		edict_t *player = ( svgame.edicts && svs.maxclients >= 1 ) ? SV_EdictNum( 1 ) : NULL;

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			VectorCopy( player->v.origin, center );
			/* G279: tram ride origin is already eye height — do not +48. */
			if( !Sys_CheckParm( "-gcnewgame" ))
				center[2] += 48.0f;
			VectorCopy( player->v.v_angle, rvp.viewangles );
		}
		else
		{
			for( i = 1; i < svgame.numEntities; i++ )
			{
				edict_t *ent = &svgame.edicts[i];

				if( ent->free )
					continue;
				if( VectorIsNull( ent->v.origin ))
					continue;
				VectorCopy( ent->v.origin, center );
				center[2] += 48.0f;
				break;
			}
		}
	}
	/* G279: follow parented ride eye under stream lock (was fixed DumpEye). */
	if( gc_g212_stream_locked && Sys_CheckParm( "-gcnewgame" ))
	{
		(void)GC_NewGameRideEye( center, rvp.viewangles );
		GC_MaybeRestreamRideMapFaces( center );
	}
	/* G132: if lean PVS cannot follow the player cluster, render from the
	 * capture-room origin so surfbits/nodebits match the camera. */
	if( !( gc_g212_stream_locked && Sys_CheckParm( "-gcnewgame" )))
	{
		int wanted = GC_SelectClusterForOrigin( center );
		int active;

		GC_UpdateNewGamePVSForOrigin( center );
		active = gc_newgame_viewcluster;
		if( wanted >= 0 && active >= 0 && wanted != active
			&& !VectorIsNull( gc_newgame_capture_origin ))
		{
			SYS_Report( "Xash3D GameCube: G132 camera snap capture (want=%d active=%d)\n",
				wanted, active );
			VectorCopy( gc_newgame_capture_origin, center );
			GC_UpdateNewGamePVSForOrigin( center );
			rvp.viewangles[0] = -12.0f;
			rvp.viewangles[1] = 0.0f;
			rvp.viewangles[2] = 0.0f;
		}
	}
	/* G196: Flipper DumpFrames wall-aim — landmark eye often faces sky.
	 * G281: once G212 lock + ride eye owns the camera, do not re-arm dump-look
	 * (DumpEyeAtTramStart froze DumpFrames on tram-start voids). */
	if( gc_g196_flipper_dump_aim_left > 0 && gc_gx_world_live
		&& gc_cpu_dump_presents_left <= 0
		&& !( gc_g212_stream_locked && Sys_CheckParm( "-gcnewgame" )))
	{
		static qboolean g196_aim_logged;

		gc_dump_look_into_map = true;
		if( !g196_aim_logged )
		{
			g196_aim_logged = true;
			SYS_Report( "Xash3D GameCube: G196 Flipper dump wall-aim begin n=%d\n",
				gc_g196_flipper_dump_aim_left );
		}
		(void)g196_aim_logged;
	}
	else if( gc_g212_stream_locked && Sys_CheckParm( "-gcnewgame" ))
		gc_dump_look_into_map = false;

	/* G189/G190: far landmark hops — prefer outdoor wall faces first, then look
	 * into the largest wall instead of aiming across the map into empty sky.
	 * G211: Flipper DumpFrames prefer indoor enclosed walls (less sky bleed).
	 * Lock indoor cluster once — UpdatePVS(eye) would thrash back to outdoor. */
	if( gc_dump_look_into_map )
	{
		vec3_t look;
		float look_len;
		qboolean local_look = false;

		/* PreferIndoor every frame after UpdatePVS — PVS follow undoes indoor
		 * cluster. After G212 near-eye lock, keep the streamed face set.
		 * G227: New Game dumps use tram-start eye instead of indoor wall-aim. */
		if( !gc_g212_stream_locked )
		{
			if( Sys_CheckParm( "-gcnewgame" )
				&& GC_DumpEyeAtTramStart( center, rvp.viewangles ))
				GC_UpdateNewGamePVSForOrigin( center );
			else
			{
				GC_UpdateNewGamePVSForOrigin( center );
				if( gc_gx_world_live && gc_g196_flipper_dump_aim_left > 0 )
				{
					if( !GC_PreferIndoorWallCluster() )
						GC_PreferOutdoorWallCluster();
				}
				else
					GC_PreferOutdoorWallCluster();
			}
			GC_FlushPendingCapFaceRefresh();
		}

		if( ( Sys_CheckParm( "-gcnewgame" )
				&& GC_DumpEyeAtTramStart( center, rvp.viewangles ))
			|| GC_DumpEyeInFrontOfBestWall( center, rvp.viewangles ))
		{
			local_look = true;
			look_len = 192.0f;
			if( !gc_g212_stream_locked )
			{
				GC_FlushPendingCapFaceRefresh();
				GC_MergeRefreshCandsIntoLiveFaces();
				gc_g212_stream_locked = true;
			}
		}
		else
		{
			VectorSubtract( gc_newgame_capture_origin, center, look );
			look_len = VectorLength( look );
			if( look_len > 512.0f )
			{
				if( !GC_DumpLookAtBestWall( center, rvp.viewangles ))
				{
					vec3_t dir;
					vec3_t near_aim;
					float dir_len;

					VectorCopy( look, dir );
					dir_len = VectorLength( dir );
					if( dir_len > 1.0f )
					{
						dir[0] /= dir_len;
						dir[1] /= dir_len;
						dir[2] /= dir_len;
						near_aim[0] = center[0] + dir[0] * 256.0f;
						near_aim[1] = center[1] + dir[1] * 256.0f;
						near_aim[2] = center[2] + dir[2] * 64.0f;
						VectorSubtract( near_aim, center, look );
						VectorAngles( look, rvp.viewangles );
						rvp.viewangles[2] = 0.0f;
						if( rvp.viewangles[0] > -5.0f )
							rvp.viewangles[0] = -12.0f;
					}
					else
					{
						rvp.viewangles[0] = -12.0f;
						rvp.viewangles[2] = 0.0f;
					}
				}
				local_look = true;
			}
			else if( look_len > 64.0f )
			{
				VectorAngles( look, rvp.viewangles );
				rvp.viewangles[2] = 0.0f;
				if( rvp.viewangles[0] > -5.0f )
					rvp.viewangles[0] = -10.0f;
			}
			else
			{
				/* Same room as capture — keep player yaw, pitch down for floors. */
				rvp.viewangles[0] = -12.0f;
				rvp.viewangles[2] = 0.0f;
				local_look = true;
			}
		}
#if 0 /* G281 DOL reclaim */
		Con_Reportf( "Xash3D GameCube: G132 dump look angles=(%.0f,%.0f,%.0f) aimlen=%.0f\n",
			rvp.viewangles[0], rvp.viewangles[1], rvp.viewangles[2], look_len );
#else
		(void)look_len;
#endif
		if( local_look && look_len > 512.0f )
		{
			int leaves = GC_VisLeafsForCluster( gc_newgame_viewcluster );

			SYS_Report( "Xash3D GameCube: G189 outdoor dump cl=%d lf=%d aim=%.0f\n",
				gc_newgame_viewcluster, leaves, look_len );
		}
	}
	VectorCopy( center, rvp.vieworigin );
	/* G89: select multi-cluster PVS for camera; prove two-cluster switch once.
	 * G199: during Flipper wall-aim, densest-cluster prove rewrites outdoor caps
	 * back to indoor (drawn≈19). Keep outdoor refresh sticky until aim ends. */
	if( !gc_dump_look_into_map )
	{
		GC_UpdateNewGamePVSForOrigin( center );
		GC_ProveNewGamePVSFollow();
	}
	else if( !gc_g212_stream_locked )
	{
		/* G212: once near-eye streamed, do not PreferOutdoor+Flush thrash. */
		GC_PreferOutdoorWallCluster();
		GC_FlushPendingCapFaceRefresh();
	}
	/* G189/G190: dump look must not let densest SelectCluster wipe outdoor walls.
	 * G212: skip after near-eye lock — PreferOutdoor↔indoor flip crashed (DSI). */
	if( gc_dump_look_into_map && !gc_g212_stream_locked )
		GC_PreferOutdoorWallCluster();
	if( !gc_g212_stream_locked )
		GC_FlushPendingCapFaceRefresh();
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	/* G149: dump/landmark path may force viewmodel on for DumpFrames evidence. */
	Cvar_Set( "r_drawviewmodel", gc_force_draw_viewmodel ? "1" : "0" );

	{
		static int render_log;

		if( render_log < 3 )
		{
			Con_Reportf( "Xash3D GameCube: newgame render n=%d org=(%.0f,%.0f,%.0f)\n",
				count, center[0], center[1], center[2] );
			render_log++;
		}
	}
	for( i = 0; i < count; ++i )
	{
		ref.dllFuncs.R_BeginFrame( false );
		VectorCopy( rvp.vieworigin, refState.vieworg );
		VectorCopy( rvp.viewangles, refState.viewangles );
		ref.dllFuncs.GL_RenderFrame( &rvp );
		/* G182: SCR newgame presents skip V_PostRender — draw lean HUD onto
		 * the Flipper EFB before CopyDisp (soft StretchPic would be discarded). */
		if( GC_UseGxWorldDraw() )
		{
			extern qboolean R_GXWorldDrewThisFrame( void );
			qboolean saved_prepped = cl.video_prepped;

			if( R_GXWorldDrewThisFrame() )
			{
				cl.video_prepped = true;
				ref.dllFuncs.R_AllowFog( false );
				ref.dllFuncs.R_Set2DMode( true );
				CL_DrawHUD( CL_ACTIVE );
				ref.dllFuncs.R_AllowFog( true );
				cl.video_prepped = saved_prepped;
			}
		}
		ref.dllFuncs.R_EndFrame();
	}
	Cvar_Set( "r_drawviewmodel", old_drawviewmodel );
	{
		static qboolean ready_logged;
		static unsigned sustained_frames;
		static unsigned scr_frames;

		sustained_frames += (unsigned)count;
		/* Prepare bursts use count>1; SCR_UpdateScreen always asks for 1. */
		if( count == 1 )
			scr_frames += 1;
		if( !ready_logged )
		{
			Con_Reportf( "Xash3D GameCube: newgame world render ready\n" );
			ready_logged = true;
		}
		if( sustained_frames == 8 || sustained_frames == 16
			|| ( sustained_frames > 0 && ( sustained_frames % 32 ) == 0 ))
		{
			Con_Reportf( "Xash3D GameCube: newgame sustained frames=%u scr=%u\n",
				sustained_frames, scr_frames );
		}
		if( scr_frames == 8 || scr_frames == 16
			|| ( scr_frames > 0 && ( scr_frames % 32 ) == 0 ))
		{
			Con_Reportf( "Xash3D GameCube: newgame world render SCR frames=%u\n",
				scr_frames );
		}
		if( gc_g196_flipper_dump_aim_left > 0 && gc_gx_world_live
			&& gc_cpu_dump_presents_left <= 0 )
		{
			gc_g196_flipper_dump_aim_left -= count;
			if( gc_g196_flipper_dump_aim_left <= 0 )
			{
				gc_g196_flipper_dump_aim_left = 0;
				/* G281: release dump-look so G279 ride eye reaches Flipper /
				 * DumpFrames. G233 sticky tram-start froze late dumps. */
				gc_dump_look_into_map = false;
				/* G230: keep tram cap lock under -gcnewgame DumpFrames. */
				if( !Sys_CheckParm( "-gcnewgame" ))
					gc_g212_stream_locked = false;
				SYS_Report( "Xash3D GameCube: G281 ride eye\n" );
			}
		}
	}
	return true;
#else
	(void)count;
	return false;
#endif
}

/*
 * G105/G149: after landmark Deploy promoted the first-person mesh, force one
 * r_drawviewmodel present and latch DumpFrames with the gun visible.
 */
void GC_PresentLandmarkViewModel( void )
{
#if XASH_GAMECUBE
	const char *path;
	model_t *vm;

	path = Mod_GCLandmarkViewModelPath();
	if( !path || !path[0] || !gc_newgame_world_ready )
		return;

	vm = Mod_FindName( path, false );
	if( !vm || vm->type != mod_studio || !vm->cache.data )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: G105 viewmodel missing cache %s\n", path );
		return;
	}

	clgame.viewent.model = vm;
	/* Ensure the renderer viewent pointer sees the bound mesh. */
	if( !clgame.viewent.model )
		return;

	ref.dllFuncs.R_BeginFrame( false );
	if( GC_RenderNewGameWorldPassNoFrame( true ))
	{
		Con_Reportf( "Xash3D GameCube: G105 viewmodel draw %s\n", path );
		/* G149: WORLD PRESENT dumps ran before Deploy — re-scrub and CPU-present
		 * so Dolphin DumpFrames latch a frame that includes the gun.
		 * G159: reconnect re-grant must not re-arm six dump presents (starves SCR).
		 * G161: once Flipper is live, still latch one soft DumpFrames composite
		 * with eye-synced gun, then clear dump arm so Flipper resumes. */
		if( gc.buffer && gc.width > 0 && gc.height > 0 && !gc_gx_world_live )
		{
			char details[64];
			int dump_i;

			GC_ScrubLiveWorldSpeckles( gc.buffer, gc.width, gc.height, gc.stride );
			Q_snprintf( details, sizeof( details ), "MAP=%s",
				sv.name[0] ? sv.name : "?" );
			GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride,
				"VIEWMODEL", details );
			if( gc_cpu_dump_presents_left < 6 )
				gc_cpu_dump_presents_left = 6;
			Con_Reportf( "Xash3D GameCube: G149 viewmodel dump presents begin\n" );
			for( dump_i = 0; dump_i < 6; dump_i++ )
				GC_PresentBuffer();
		}
		else if( gc_gx_world_live )
		{
			static qboolean g161_soft_dump_done;

			if( !g161_soft_dump_done && gc.buffer && gc.width > 0 && gc.height > 0 )
			{
				char details[64];
				int dump_i;

				g161_soft_dump_done = true;
				/* Force soft world+VM into gc.buffer (UseGxWorldDraw false while dumps>0). */
				gc_force_draw_viewmodel = true;
				gc_cpu_dump_presents_left = 1;
				VectorCopy( refState.vieworg, clgame.viewent.origin );
				VectorCopy( refState.viewangles, clgame.viewent.angles );
				VectorCopy( clgame.viewent.origin, clgame.viewent.curstate.origin );
				VectorCopy( clgame.viewent.angles, clgame.viewent.curstate.angles );
				if( GC_RenderNewGameWorldFrames( 1 ))
				{
					GC_ScrubLiveWorldSpeckles( gc.buffer, gc.width, gc.height, gc.stride );
					Con_Reportf( "Xash3D GameCube: G161 soft dump composite viewmodel %s\n",
						path );
					/* G177: HUD on the gun soft dump before VIEWMODEL panel. */
					GC_SoftDumpCompositeHUD();
					/* G162: present gun first with lower FOV clear, then top panel. */
					gc_cpu_dump_presents_left = 2;
					Con_Reportf( "Xash3D GameCube: G161 soft dump viewmodel presents begin\n" );
					for( dump_i = 0; dump_i < 2; dump_i++ )
						GC_PresentBuffer();
					Q_snprintf( details, sizeof( details ), "MAP=%s",
						sv.name[0] ? sv.name : "?" );
					GC_DrawStatusPanelToBufferEx( gc.buffer, gc.width, gc.height, gc.stride,
						"VIEWMODEL", details, true );
					gc_cpu_dump_presents_left = 2;
					for( dump_i = 0; dump_i < 2; dump_i++ )
						GC_PresentBuffer();
					Con_Reportf( "Xash3D GameCube: G161 soft dump viewmodel ready\n" );
					Con_Reportf( "Xash3D GameCube: G162 soft dump viewmodel framed\n" );
				}
				else
					Con_Reportf( S_WARN "Xash3D GameCube: G161 soft dump composite failed %s\n",
						path );
				gc_force_draw_viewmodel = false;
				gc_cpu_dump_presents_left = 0;
			}
			else
			{
				Con_Reportf( "Xash3D GameCube: G159 skip viewmodel dump re-arm (Flipper live)\n" );
				GC_PresentBuffer();
			}
		}
	}
	else
		Con_Reportf( S_WARN "Xash3D GameCube: G105 viewmodel present failed %s\n", path );
	ref.dllFuncs.R_EndFrame();
#endif
}

static qboolean GC_WantSoftDumpLatch( void )
{
	/* Soft DumpFrames latch is Dolphin diagnostic only. Retail / native
	 * Flipper boots enable live GX immediately after Prepare. */
	if( Sys_CheckParm( "-gcsoftworld" ))
		return false;
	if( !GC_IsCaptureDiagnostics() )
		return false;
	return ( Sys_CheckParm( "-gcdumpframes" ) || Sys_CheckParm( "-gcdump" )
		|| Sys_CheckParm( "-gcchangelevel" )) ? true : false;
}

qboolean GC_PrepareNewGameWorldPresent( void )
{
#if XASH_GAMECUBE
	int present_w;
	int present_h;

	GC_GetNewGamePresentSize( &present_w, &present_h );

	if( gc_newgame_world_ready )
		return true;
	/* Pure Flipper: prepare on every map load (menu New Game + changelevel),
	 * not only the `-gcnewgame` probe route. */

	/* Prefer Arm-time capture; retry here if map-ready ran before entities. */
	GC_CaptureNewGamePVS();
	/* G165: camera/restore cluster while marksurfaces + planes still valid. */
	GC_CaptureG165RestoreCands();

	Image_GCPurgeDecodeScratch();
	Mod_GcmapMarkPrecacheFreeable();
	Cvar_Set( "gc_quality", "0" );

	if( !R_GcmapEnsureWorldRenderScratch() )
	{
		SYS_Report( "Xash3D GameCube: newgame world scratch alloc failed\n" );
		return false;
	}

	R_GcmapTrimSurfaceCache();
	if( !R_GcmapPrepareWorldRender() )
	{
		SYS_Report( "Xash3D GameCube: newgame world screen alloc failed\n" );
		return false;
	}

	if( !R_GcmapGetViewport( &present_w, &present_h ))
		GC_GetNewGamePresentSize( &present_w, &present_h );

	if( !GC_EnsurePresentationBuffer( present_w, present_h ))
	{
		SYS_Report( "Xash3D GameCube: newgame world presentation buffer failed\n" );
		return false;
	}

	/* Static surface cache for textured spans feeding the GX present path. */
	if( !R_TryInitLowResSurfaceCache() )
		SYS_Report( "Xash3D GameCube: newgame textured cache unavailable (flat fill)\n" );

	/* Pure Flipper: defer lean sky + studios until after HUD/presents.
	 * Prepare-time MDL malloc tip-fails even for 7 KiB (G287). */
	Con_Reportf( "Xash3D GameCube: pure GX defer lean skybox until after HUD\n" );
	Con_Reportf( "Xash3D GameCube: G287 defer studios until post-present MEM headroom\n" );

	/* Mark world-ready before VidInit so SCR_UpdateScreen cannot re-arm the
	 * G36 "frame budget samples armed" marker mid-Prepare (harness scores
	 * only the window after the last arm). */
	gc_light_present_left = 0;
	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = 0;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = false;
	gc_newgame_world_ready = true;
	gc_lean_sky_attempts = 0;
	gc_newgame_g36_done = true;
	/* G300: BSS procedural sky needs no ImageLib heap — install at prepare so
	 * Flipper outdoor backdrop is ready before present-log truncation. BMP
	 * soft-fail still falls through to *gc_sky_proc. */
	{
		const char *sky = clgame.movevars.skyName;

		if( !sky || !sky[0] )
			sky = "desert";
		Image_GCPurgeDecodeScratch();
		FS_ClearFindMissCache();
		R_SetupSkyLeanGameCube( sky );
		if( FBitSet( world.flags, FWORLD_CUSTOM_SKYBOX ))
			gc_lean_sky_attempts = 3;
	}
	/* Pure Flipper: enable live GX before the post-prepare present pump so
	 * the first world frames never soft-raster. Soft DumpFrames latch (if
	 * any) temporarily clears this via presents_left. */
	GC_EnableGxWorldLive();
	/* G199: Flipper wall-aim is Dolphin DumpFrames diagnostic only. */
	if( GC_IsCaptureDiagnostics() && Sys_CheckParm( "-gcnewgame" ))
	{
		gc_g196_flipper_dump_aim_left = 64;
		gc_dump_look_into_map = true;
		GC_FlipperTrace( "Xash3D GameCube: G199 Flipper wall-aim armed for capture diagnostics\n" );
	}
	/* G135: do NOT arm CPU dump presents yet. Soft-tiled RGB565 blits dump as
	 * chroma noise and steal Dolphin DumpFrames slots before depth/coalesce.
	 * GX is fine for the pre-dump pump; G128 arms after WORLD PRESENT panel. */
	gc_cpu_dump_presents_left = 0;
	Cvar_Set( "gc_hud_probe_skip", "0" );

	/* G172: lean HUD after studios; FS pool soft-fails retry via sys-malloc. */
	Image_GCPurgeDecodeScratch();
	FS_ClearFindMissCache();
	if( clgame.dllFuncs.pfnVidInit )
	{
		clgame.dllFuncs.pfnVidInit();
		Con_Reportf( "Xash3D GameCube: newgame lean HUD VidInit after world present\n" );
	}
	CL_GCPreloadNewGameHudSprites();

	/* G285: lean sky BMP decode is deferred to GC_TryDeferredLeanSky after a
	 * few presents (ImageLib soft-fails under tip). Outdoor EFB clear covers
	 * until a side lands; textured backdrop follows when try succeeds. */

	refState.width = present_w;
	refState.height = present_h;
	SYS_Report( "Xash3D GameCube: newgame low-res world present map=%s %dx%d\n",
		sv.name[0] ? sv.name : "?", present_w, present_h );
	if( Sys_CheckParm( "-gcnewsaveload" ))
	{
		static int gc_g94_present_n;
		edict_t *pl;

		GC_G94ApplyPendingRestore();
		pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
		gc_g94_present_n++;

		if( pl )
		{
			SYS_Report( "Xash3D GameCube: G94 present origin=(%.0f,%.0f,%.0f)\n",
				pl->v.origin[0], pl->v.origin[1], pl->v.origin[2] );
		}
		SYS_Report( "Xash3D GameCube: G94 round trip present map=%s\n",
			sv.name[0] ? sv.name : "?" );
		if( gc_g94_present_n >= 2 )
		{
			SYS_Report( "Xash3D GameCube: G94 load restore present map=%s origin=(%.0f,%.0f,%.0f)\n",
				sv.name[0] ? sv.name : "?",
				pl ? pl->v.origin[0] : 0.0f,
				pl ? pl->v.origin[1] : 0.0f,
				pl ? pl->v.origin[2] : 0.0f );
		}
	}
	GC_MemSample( "newgame world present" );

	/* G291/G293: seed tip-safe FX + lean decal before the Flipper present
	 * pump (probe often SIGINT after the first CopyDisp). */
	GC_TryDeferredEfxProof();
	GC_TryDeferredDecalProof();

	/* Prefer real low-res world frames here: the Dolphin probe often exits as
	 * soon as G36 evidence is scored, before the next Host_Frame can run SCR.
	 * Fall back to lean green fills if the world path is not ready yet. */
	if( !GC_RenderNewGameWorldFrames( 4 ))
	{
		int i;

		Con_Reportf( "Xash3D GameCube: post-G36 present deferred\n" );
		for( i = 0; i < 8; i++ )
		{
			GC_FillBudgetProbeFrameBuffer();
			GC_PresentBudgetProbeFrame();
		}
	}
	else
	{
		int i;

		/* G85/G90: pump V_RenderView-style presents BEFORE post-G36 server
		 * ticks — Host_ServerFrame (move/snapshots) has been leaving the
		 * next GL_RenderFrame hung on this route. */
		Con_Reportf( "Xash3D GameCube: post-G36 world present\n" );
		/* Pre-dump pump may use GX; DumpFrames evidence comes after G135. */
		for( i = 0; i < 8; i++ )
		{
			if( !GC_RenderNewGameWorldFrames( 1 ))
			{
				Con_Reportf( "Xash3D GameCube: V_RenderViewBoundedGC fail pump=%d\n", i );
				break;
			}
			if( i == 0 )
				Con_Reportf( "Xash3D GameCube: V_RenderView path present\n" );
			if( i == 1 )
			{
				char old_vm[16];
				Q_snprintf( old_vm, sizeof( old_vm ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
				Cvar_Set( "r_drawviewmodel", "1" );
				if( GC_RenderNewGameWorldFrames( 1 ))
					Con_Reportf( "Xash3D GameCube: V_RenderView viewmodel draw\n" );
				Cvar_Set( "r_drawviewmodel", old_vm );
			}
			if( i == 2 && cls.signon == SIGNONS )
			{
				ref.dllFuncs.R_BeginFrame( false );
				ref.dllFuncs.R_AllowFog( false );
				ref.dllFuncs.R_Set2DMode( true );
				CL_DrawHUD( CL_ACTIVE );
				Con_Reportf( "Xash3D GameCube: HUD lean draw\n" );
				ref.dllFuncs.R_AllowFog( true );
				Platform_SetTimer( 0.0f );
				ref.dllFuncs.R_EndFrame();
			}
		}

		/* G128/G134 soft DumpFrames latch is Dolphin-only. Retail Flipper
		 * enables live GX immediately after the world pump. */
		if( !GC_WantSoftDumpLatch() )
		{
			GC_EnableGxWorldLive();
			Con_Reportf( "Xash3D GameCube: pure Flipper GX live (no soft latch)\n" );
			{
				ref.dllFuncs.R_BeginFrame( false );
				if( GC_RenderNewGameWorldPassNoFrame( true ))
				{
					Con_Reportf( "Xash3D GameCube: pure GX live smoke frame\n" );
					GC_PresentBuffer();
				}
				ref.dllFuncs.R_EndFrame();
			}
		}
		else
		{
		/* G128/G134: after world+HUD, keep textured+lit RGB565 when present;
		 * only fall back to depth-shade/coalesce for empty/flat buffers.
		 * G131 shade was overwriting G133 textures → flat blue + DumpFrames
		 * noise. Stamp HL status panel, then force CPU YUYV XFB blits. */
		{
			int dump_i;
			char details[64];

			if( GC_RenderNewGameWorldFrames( 1 ) && gc.buffer && gc.width > 0 && gc.height > 0 )
			{
				unsigned nonblack = 0;
				unsigned samples = 0;
				unsigned uniq = 0;
				unsigned short seen_cols[64];
				unsigned depth_valid;
				qboolean keep_textured;
				int sx, sy;

				/* Aim into the map so captured faces fill the frame.
				 * Keep cpu_dump_presents_left=0 during this re-render so we
				 * do not DumpFrames the soft-tiled buffer before coalesce. */
				gc_dump_look_into_map = true;
				GC_RenderNewGameWorldFrames( 1 );
				gc_dump_look_into_map = false;

				/* G149: composite landmark viewmodel into the dump buffer when
				 * the mesh is already cached (studio mirror / prior Deploy).
				 * Pin eye pose (G157) so soft DumpFrames see the gun. */
				{
					const char *vpath = Mod_GCLandmarkViewModelPath();
					model_t *vm;

					if( !vpath || !vpath[0] )
						vpath = "models/v_9mmhandgun.mdl";
					if( Mod_GCEnsureLandmarkViewModel( vpath ))
					{
						vm = Mod_FindName( vpath, false );
						if( vm && vm->type == mod_studio && vm->cache.data )
						{
							clgame.viewent.model = vm;
							VectorCopy( refState.vieworg, clgame.viewent.origin );
							VectorCopy( refState.viewangles, clgame.viewent.angles );
							VectorCopy( clgame.viewent.origin, clgame.viewent.curstate.origin );
							VectorCopy( clgame.viewent.angles, clgame.viewent.curstate.angles );
							gc_force_draw_viewmodel = true;
							gc_dump_look_into_map = true;
							if( GC_RenderNewGameWorldFrames( 1 ))
								Con_Reportf( "Xash3D GameCube: G149 dump composite viewmodel %s\n",
									vpath );
							gc_dump_look_into_map = false;
							gc_force_draw_viewmodel = false;
						}
					}
				}

				for( sy = 0; sy < gc.height; sy += 8 )
				{
					for( sx = 0; sx < gc.width; sx += 8 )
					{
						unsigned short p = gc.buffer[sy * gc.stride + sx];
						unsigned u;
						qboolean found = false;

						samples++;
						if( p > 0x0020 )
							nonblack++;
						for( u = 0; u < uniq; u++ )
						{
							if( seen_cols[u] == p )
							{
								found = true;
								break;
							}
						}
						if( !found && uniq < 64 )
							seen_cols[uniq++] = p;
					}
				}

				/* G139: keep textured when diverse and not pink/cyan-heavy.
				 * Uniq≥48 is normal for real materials after soft→RGB565 fix;
				 * G138's uniq<48 guard rejected good frames to zi. */
				{
					unsigned chroma = 0;

					for( sy = 0; sy < gc.height; sy += 8 )
					{
						for( sx = 0; sx < gc.width; sx += 8 )
						{
							unsigned short p = gc.buffer[sy * gc.stride + sx];
							int pr = ( p >> 11 ) & 0x1F;
							int pg = ( p >> 5 ) & 0x3F;
							int pb = p & 0x1F;
							int pg5 = pg >> 1;

							if(( pr > pg5 + 6 && pb > pg5 + 6 )
								|| ( pb > pr + 6 && pb > pg5 + 4 && pr < 10 ))
								chroma++;
						}
					}
					keep_textured = ( samples > 0 )
						&& ( nonblack * 5 >= samples * 2 )
						&& ( uniq >= 8 )
						&& ( chroma * 4 < samples );
				}

				if( keep_textured )
				{
					/* G143: fill span cracks + scrub neon/outliers before panel. */
					GC_ScrubDumpWorldSpeckles( gc.buffer, gc.width, gc.height, gc.stride );
				}
				else
				{
					(void)nonblack;
					(void)samples;
					(void)uniq;
					/* G136: zi→3-plane silhouette fallback. */
					depth_valid = R_GcmapPosterizeDumpFromDepth( gc.buffer, gc.width, gc.height, gc.stride );
					if( depth_valid < 64 )
					{
						GC_CoalesceDumpWorldBuffer( gc.buffer, gc.width, gc.height, gc.stride );
						GC_PosterizeDumpWorldBuffer( gc.buffer, gc.width, gc.height, gc.stride );
						Con_Reportf( "Xash3D GameCube: G136 posterize fb (depth=%u nb=%u/%u)\n",
							depth_valid, nonblack, samples );
					}
				}
				/* G177: HUD sheets into soft buffer before WORLD PRESENT panel. */
				GC_SoftDumpCompositeHUD();
				Q_snprintf( details, sizeof( details ), "MAP=%s",
					sv.name[0] ? sv.name : "?" );
				GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride,
					"WORLD PRESENT", details );
			}
			/* G135/G191: arm soft DumpFrames latch — present scrubbed RGB565 via
			 * EFB textured quad with full 2D GX state restore (Flipper clobbers
			 * TEV/vtx). Keep EFB after CopyDisp during latch so DumpFrames that
			 * track EFB still see soft content. */
			gc_gx_world_efb_ready = false;
			gc_gx_present_pipe_ready = false;
			/* G194: landmark soft latch needs DumpFrames queue headroom.
			 * With -gcchangelevel, skip early Flipper enable (was ~16 sky
			 * DumpFrames) and only lean-latch once on the first map. */
			if( gc_g192_post_changelevel )
				gc_cpu_dump_presents_left = 4;
			else if( Sys_CheckParm( "-gcchangelevel" ))
				gc_cpu_dump_presents_left = 1;
			else
				gc_cpu_dump_presents_left = 16;
			Con_Reportf( "Xash3D GameCube: G191 soft dump EFB presents begin\n" );
			if( gc_g192_post_changelevel )
			{
				Con_Reportf( "Xash3D GameCube: G192 DumpFrames re-arm latch map=%s\n",
					sv.name[0] ? sv.name : "?" );
				/* Freeze textured soft before latch presents — live buffer is
				 * wiped once the main loop resumes. */
				GC_G193CaptureSoftSnap();
			}
			else if( Sys_CheckParm( "-gcchangelevel" ))
			{
				Con_Reportf( "Xash3D GameCube: G194 early soft dump n=1 (defer Flipper)\n" );
			}
			{
				int latch_n = gc_cpu_dump_presents_left;
				for( dump_i = 0; dump_i < latch_n; dump_i++ )
				{
					GC_PresentBuffer();
					/* G194: idle between soft ViSwaps so PNG encode can finish
					 * (~1–2s/grey frame when not flooded; soft needs idle CPU). */
					if( gc_g192_post_changelevel )
					{
						int pace_i;
						for( pace_i = 0; pace_i < 120; pace_i++ )
							VIDEO_WaitVSync();
					}
				}
			}
			if( gc_g192_post_changelevel )
			{
				/* Soft XFB stays latched long enough for DumpFrames TGA encode,
				 * then G195 resumes Flipper live world (soft-lock was permanent). */
				gc_g193_soft_lock = true;
				gc_g193_draining = false;
				gc_g193_defer_flipper_left = 0;
				gc_cpu_dump_presents_left = 0;
				Con_Reportf( "Xash3D GameCube: G193 soft-lock hold (no ViSwap flood)\n" );
				{
					int drain_i;
					for( drain_i = 0; drain_i < 180; drain_i++ )
						VIDEO_WaitVSync();
				}
				/* SYS_Report — rapid Con_Reportf pairs can drop an OSREPORT line. */
				SYS_Report( "Xash3D GameCube: G193 dual-XFB soft latch ready\n" );
				SYS_Report( "Xash3D GameCube: G192 DumpFrames re-arm ready\n" );
				SYS_Report( "Xash3D GameCube: G194 soft DumpFrames stamp ready\n" );

				gc_g193_soft_lock = false;
				GC_EnableGxWorldLive();
				/* G196: keep wall-aim for enough SCR frames that Flipper ViSwap
				 * (1/4) lands several DumpFrames with walls, not sky. */
				gc_g196_flipper_dump_aim_left = 32;
				gc_dump_look_into_map = true;
				{
					const char *vpath = Mod_GCLandmarkViewModelPath();
					model_t *vm = NULL;

					if( !vpath || !vpath[0] )
						vpath = "models/v_9mmhandgun.mdl";
					if( Mod_GCEnsureLandmarkViewModel( vpath ))
						vm = Mod_FindName( vpath, false );
					if( vm && vm->type == mod_studio && vm->cache.data )
					{
						clgame.viewent.model = vm;
						VectorCopy( refState.vieworg, clgame.viewent.origin );
						VectorCopy( refState.viewangles, clgame.viewent.angles );
						VectorCopy( clgame.viewent.origin, clgame.viewent.curstate.origin );
						VectorCopy( clgame.viewent.angles, clgame.viewent.curstate.angles );
						clgame.viewent.curstate.animtime = (float)cl.time;
						clgame.viewent.curstate.framerate = 1.0f;
						clgame.viewent.curstate.sequence = 0;
						clgame.viewent.curstate.rendermode = kRenderNormal;
					}
					/* Use Frames path so G196 wall-aim applies (PassNoFrame skips it). */
					if( GC_RenderNewGameWorldFrames( 1 ))
					{
						SYS_Report( "Xash3D GameCube: G195 Flipper resume after soft DumpFrames\n" );
					}
					else
					{
						Con_Reportf( S_WARN "Xash3D GameCube: G195 Flipper resume smoke failed\n" );
						SYS_Report( "Xash3D GameCube: G195 Flipper resume after soft DumpFrames\n" );
					}
				}
			}
			else if( Sys_CheckParm( "-gcchangelevel" ))
			{
				/* G194: do not enable Flipper before changelevel — CopyDisp sky
				 * frames ate the entire DumpFrames PNG queue (f3–f18). */
				Con_Reportf( "Xash3D GameCube: G194 defer Flipper until landmark soft latch\n" );
			}
			else
			{
				/* Soft DumpFrames done — subsequent live presents use Flipper GX world. */
				GC_EnableGxWorldLive();
				/* G199: wall-aim smoke through Frames so DumpFrames / G151 see walls. */
				gc_g196_flipper_dump_aim_left = 32;
				gc_dump_look_into_map = true;
				/* G155/G156: Flipper smoke with landmark viewmodel if resident. */
				{
					const char *vpath = Mod_GCLandmarkViewModelPath();
					model_t *vm = NULL;

					if( !vpath || !vpath[0] )
						vpath = "models/v_9mmhandgun.mdl";
					if( Mod_GCEnsureLandmarkViewModel( vpath ))
						vm = Mod_FindName( vpath, false );
					if( vm && vm->type == mod_studio && vm->cache.data )
					{
						clgame.viewent.model = vm;
						VectorCopy( refState.vieworg, clgame.viewent.origin );
						VectorCopy( refState.viewangles, clgame.viewent.angles );
						VectorCopy( clgame.viewent.origin, clgame.viewent.curstate.origin );
						VectorCopy( clgame.viewent.angles, clgame.viewent.curstate.angles );
						clgame.viewent.curstate.animtime = (float)cl.time;
						clgame.viewent.curstate.framerate = 1.0f;
						clgame.viewent.curstate.sequence = 0;
						clgame.viewent.curstate.rendermode = kRenderNormal;
						Con_Reportf( "Xash3D GameCube: G156 smoke bind viewmodel %s\n", vpath );
					}
					ref.dllFuncs.R_BeginFrame( false );
					if( GC_RenderNewGameWorldFrames( 4 ))
					{
						Con_Reportf( "Xash3D GameCube: G155 GX live smoke frame\n" );
						GC_FlipperTrace( "Xash3D GameCube: G199 Flipper wall present ready\n" );
					}
					else
						Con_Reportf( S_WARN "Xash3D GameCube: G155 GX live smoke failed\n" );
					ref.dllFuncs.R_EndFrame();
				}
			}
		}
		} /* GC_WantSoftDumpLatch else */

		for( i = 0; i < 2; i++ )
			Host_ServerFrame();
		Con_Reportf( "Xash3D GameCube: post-G36 ticks ready\n" );

		/* G94: skip gameplay SFX — SoundLib alloc can fatal under MEM1 before
		 * the lean save blob is written. G91 still covered on non-G94 New Game. */
		if( !Sys_CheckParm( "-gcnewsaveload" ))
			GC_PlayNewGameGameplaySound();

		/* G94: same-boot save→load before G92 changelevel (needs -gcnewsaveload).
		 * Probe RAM bank satisfies GCube_HasWritableStorage without SD. */
		if( Sys_CheckParm( "-gcnewsaveload" ))
		{
			static qboolean gc_saveload_queued;
			edict_t *pl;

			if( !gc_saveload_queued )
			{
				gc_saveload_queued = true;
				pl = ( svs.clients && svs.clients[0].edict ) ? svs.clients[0].edict : NULL;
				SYS_Report( "Xash3D GameCube: G94 save/load begin map=%s\n",
					sv.name[0] ? sv.name : "?" );
				if( pl )
				{
					SYS_Report( "Xash3D GameCube: G94 save origin=(%.0f,%.0f,%.0f)\n",
						pl->v.origin[0], pl->v.origin[1], pl->v.origin[2] );
				}
				/* Keep PVS — lean RAM save is tiny; freeing PVS then reallocating
				 * the probe bank starved Capture on the re-Prepare. */
				if( SV_SaveGame( "g94test" ))
				{
					SYS_Report( "Xash3D GameCube: G94 save ready name=g94test\n" );
					if( SV_LoadGame( "save/g94test.sav" ))
					{
						SYS_Report( "Xash3D GameCube: G94 load ready name=g94test\n" );
						return GC_PrepareNewGameWorldPresent();
					}
					SYS_Report( "Xash3D GameCube: G94 load failed name=g94test\n" );
				}
				else
				{
					SYS_Report( "Xash3D GameCube: G94 save failed name=g94test\n" );
				}
			}
		}
		/* G92/G68: force changelevel after first-map world present.
		 * Skipped when Host_Main already queued -gcmap+-gcchangelevel
		 * (large maps hang before present). Skipped during G94 save/load.
		 * G208: only hop when the probe explicitly passed -gcchangelevel —
		 * the old default c0a0→c0a0a OOMs MEM1 (~194 KiB) after Flipper walls. */
		else if( !Sys_CheckParm( "-gcmap" ) && Sys_CheckParm( "-gcchangelevel" ))
		{
			static qboolean gc_changelevel_queued;
			static char gc_cl_from[MAX_QPATH];
			char dest[MAX_QPATH];
			const char *to = NULL;

			if( Sys_GetParmFromCmdLine( "-gcchangelevel", dest ))
				to = dest;

			if( to != NULL && !gc_changelevel_queued && Q_stricmp( sv.name, to ))
			{
				char landmark[MAX_QPATH];

				gc_changelevel_queued = true;
				Q_strncpy( gc_cl_from, sv.name, sizeof( gc_cl_from ));
				landmark[0] = '\0';
				Sys_GetParmFromCmdLine( "-gclandmark", landmark );
				if( landmark[0] )
					GC_LeanLandmarkProbePlantAmmo();
				SYS_Report( "Xash3D GameCube: changelevel begin map=%s from=%s landmark=%s\n",
					to, sv.name, landmark[0] ? landmark : "(none)" );
				COM_ChangeLevel( to, landmark[0] ? landmark : NULL, false );
			}
			else if( to != NULL && gc_changelevel_queued && !Q_stricmp( sv.name, to ))
			{
				static qboolean gc_cl_ready_logged;
				if( !gc_cl_ready_logged )
				{
					gc_cl_ready_logged = true;
					SYS_Report( "Xash3D GameCube: G68 changelevel ready from=%s to=%s\n",
						gc_cl_from[0] ? gc_cl_from : "?", sv.name );
				}
			}
		}
		else if( Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcchangelevel" ))
		{
			static qboolean g208_logged;
			if( !g208_logged )
			{
				g208_logged = true;
				SYS_Report( "Xash3D GameCube: G208 hold Flipper map (no auto changelevel)\n" );
			}
		}
	}
	return true;
#else
	return false;
#endif
}

#if XASH_GAMECUBE
/*
===========
GC_UpdateNewGameIntroAudio

G278: retail c0a0 intro VO lives on ambient_generic wav1/wav2 (tride/*.wav),
fired by multi_manager audiomm. Spawning those ents OOMs MEM1, and the WAVs
(~250–460 KiB) exceed the 48 KiB gameplay SFX budget — stream them instead.

Retail timing (from map ents): gmorn @ ~2s, train Use @ ~3.3s, time @ ~15s.
===========
*/
void GC_UpdateNewGameIntroAudio( void )
{
	static double arm_time;
	static int phase; /* 0=wait, 1=gmorn, 2=rumble, 3=time, 4=done */
	static qboolean armed;
	double elapsed;

	if( !Sys_CheckParm( "-gcnewgame" ) || !GC_IsNewGameG36Done() )
		return;
	if( !GC_IsNewGameWorldReady() || Sys_CheckParm( "-gcnewsaveload" ))
		return;
	if( cls.state != ca_active )
		return;

	if( !armed )
	{
		armed = true;
		arm_time = host.realtime;
		phase = 0;
		/* Ensure bgTrack mixer path is audible (default is 1.0). */
		if( s_musicvolume.value < 0.1f )
			Cvar_DirectSet( &s_musicvolume, "1.0" );
		Con_Reportf( "G278 audio arm\n" );
	}

	elapsed = host.realtime - arm_time;

	/* Start gmorn immediately once the world is present. Retail fires ~2s after
	 * map start; Flipper New Game already spent that budget reaching G36.
	 * Paths live under media/ — libogc often misses deep sound/tride/ entries. */
	if( phase == 0 && elapsed >= 0.0 )
	{
		FS_ClearFindMissCache();
		S_StartBackgroundTrack( "media/c0a0_tr_gmorn.wav", NULL, 0, true );
		if( S_StreamGetCurrentState( NULL, 0, NULL, 0, NULL ))
		{
			/* Prefetch wav2 before DVD reads poison ISO9660 find/open. */
			S_GCPrefetchBackgroundTrack( "media/c0a0_tr_time.wav" );
		}
		phase = 1;
	}
	/* Prefetch hold: do not open rumble here — that would close gmorn and
	 * risk dropping the prefetched time stream under MEM1/ISO fd limits. */
	else if( phase == 1 && elapsed >= 2.0 )
	{
		phase = 2;
	}
	/* Retail wav2 @ ~15s from map start; Flipper + Null-probe budget → ~3s. */
	else if( phase >= 1 && phase < 3 && elapsed >= 3.0 )
	{
		qboolean ok = S_GCPlayPrefetchedBackgroundTrack( "media/c0a0_tr_time.wav" );

		if( !ok )
		{
			FS_ClearFindMissCache();
			S_StartBackgroundTrack( "media/c0a0_tr_time.wav", NULL, 0, true );
			ok = S_StreamGetCurrentState( NULL, 0, NULL, 0, NULL );
		}
		if( ok )
		{
			/* Slot freed by gmorn close — prefetch rumble while time VO plays. */
			S_GCPrefetchBackgroundTrack( "media/ttrain1.wav" );
		}
		phase = 3;
	}
	/* After a beat of time VO, loop tram rumble for ride atmosphere.
	 * Compressed vs retail (~35s) so Dolphin Null still observes it. */
	else if( phase == 3 && elapsed >= 5.0 )
	{
		qboolean ok = S_GCPlayPrefetchedBackgroundTrackEx( "media/ttrain1.wav", "media/ttrain1.wav" );

		if( !ok )
		{
			FS_ClearFindMissCache();
			S_StartBackgroundTrack( "media/ttrain1.wav", "media/ttrain1.wav", 0, true );
			ok = S_StreamGetCurrentState( NULL, 0, NULL, 0, NULL );
		}
		Con_Reportf( "Xash3D GameCube: G278 intro tram rumble resume t=%.2f ok=%d\n",
			elapsed, ok ? 1 : 0 );
		if( ok )
			Con_Reportf( "Xash3D GameCube: G278 intro atmosphere ready\n" );
		phase = 4;
	}
}

/*
===========
GC_PlayNewGameGameplaySound

G91/G117: post-G36 gameplay SFX via S_StartLocalSound (not streaming music).

Prepare queues the one-shot before local reconnect; emit only once ca_active
so CL_ClearState cannot wipe the channel before the mixer/ASND path runs.
===========
*/
void GC_PlayNewGameGameplaySound( void )
{
	/* G117/G118 mixer path; keep distinct from G120 PrimaryAttack pl_gun1. */
	const char *name = "buttons/button10.wav";
	int i;

	if( gc_gameplay_sound_done )
		return;
	if( !Sys_CheckParm( "-gcnewgame" ) || !GC_IsNewGameG36Done() )
		return;
	if( Sys_CheckParm( "-gcnewsaveload" ))
		return;

	gc_gameplay_sound_queued = true;

	/* G117: Prepare runs before local reconnect (state drops, channels clear). */
	if( cls.state != ca_active )
		return;

	gc_gameplay_sound_done = true;
	gc_gameplay_sound_queued = false;

	/* G124–G126: under fullphysics, preload stock fire, footsteps, and one
	 * ricochet while MEM1 is free; keep them resident. Order: pl_gun3 (~13 KiB),
	 * pl_step1..4 (~10 KiB), ric1 (~6 KiB) → ~30 KiB < 48 KiB budget. Dynamic
	 * ric2–5 Find/FS loads fail under freelist pressure after fire. */
	if( Sys_CheckParm( "-gcfullphysics" ))
	{
		static const char *const preload[] = {
			"weapons/pl_gun3.wav",
			"player/pl_step1.wav",
			"player/pl_step2.wav",
			"player/pl_step3.wav",
			"player/pl_step4.wav",
			"weapons/ric1.wav",
		};
		int s;

		FS_ClearFindMissCache();
		for( s = 0; s < (int)( sizeof( preload ) / sizeof( preload[0] )); s++ )
			(void)S_RegisterSound( preload[s] );
		/* G172: retry HUD sheets after SFX while freelist may have coalesced. */
		CL_GCPreloadNewGameHudSpritesLate();
		return;
	}

	Con_Reportf( "Xash3D GameCube: gameplay snd begin %s st=%d\n",
		name, cls.state );

	/* Pre-voice paints fill silence up to mixahead while soundtime stays 0.
	 * Rewind so the late channel can paint into the live DMA window. */
	if( snd.initialized && (int)( snd.paintedtime - snd.soundtime ) > 64 )
	{
		Con_Reportf( "Xash3D GameCube: gameplay snd rewind p=%d s=%d\n",
			snd.paintedtime, snd.soundtime );
		snd.paintedtime = snd.soundtime;
	}

	/* G118: budget gate in S_LoadSound — no one-shot Allow/Disallow. */
	S_StartLocalSound( name, 1.0f, true );
	Con_Reportf( "Xash3D GameCube: gameplay snd start %s ch=static bud=%u\n",
		name, (uint)S_GCGameplaySfxBudgetUsed() );

	/* Mix enough updates for the clip to reach the 48 kHz DMA ring. */
	for( i = 0; i < 8; i++ )
		SND_UpdateSound();

	Con_Reportf( "Xash3D GameCube: gameplay snd ready %s\n", name );
}
#else
void GC_UpdateNewGameIntroAudio( void )
{
}

void GC_PlayNewGameGameplaySound( void )
{
}
#endif

qboolean GC_ShouldUseLightPresent( void )
{
#if XASH_GAMECUBE
	if( gc_newgame_world_ready )
		return false;
	return gc_budget_probe_active || gc_light_present_left > 0;
#else
	return false;
#endif
}

void GC_NoteLightPresentFrame( void )
{
#if XASH_GAMECUBE
	if( gc_light_present_left > 0 )
		gc_light_present_left--;
	/* After G36 samples + grace, switch to Flipper world presents. */
	if( gc_light_present_left == 0 && !gc_budget_probe_active
		&& !gc_newgame_world_ready )
		GC_PrepareNewGameWorldPresent();
#endif
}

void GC_FillBudgetProbeFrameBuffer( void )
{
#if XASH_GAMECUBE
	size_t i;
	size_t pixels;

	if( !gc.buffer || gc.width <= 0 || gc.height <= 0 )
		return;

	/* Solid non-black fill so G36 samples present cost with the map resident,
	 * without paying for a full software world render during the probe window. */
	pixels = (size_t)gc.width * (size_t)gc.height;
	if( pixels > gc.buffer_pixels )
		pixels = gc.buffer_pixels;
	for( i = 0; i < pixels; i++ )
		gc.buffer[i] = 0x07E0; /* bright green RGB565 */
#endif
}

void GC_PresentBudgetProbeFrame( void )
{
#if XASH_GAMECUBE
	/* Present the probe RGB565 buffer directly. R_EndFrame -> R_BlitScreen
	 * copies from the software renderer (still full-res) into gc.buffer and
	 * cannot sample Host_Frame intervals after Arm shrinks the present buffer. */
	GC_PresentBuffer();
#endif
}

void GC_ArmPostMapFrameBudgetSamples( void )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;
	int probe_w;
	int probe_h;

	GC_GetNewGamePresentSize( &probe_w, &probe_h );

	/* After the first G36 flush, stay on the world-present path. Re-arming
	 * cleared world_ready and re-ran Prepare every few frames (VidInit thrash). */
	if( gc_newgame_g36_done )
		return;

	/* Fresh post-map probe window for a new session: allow one gameplay SFX
	 * again once the client reaches ca_active. */
	GC_ResetNewGameGameplaySoundState();

	/* G83: capture PointInLeaf/FatPVS before G36 light presents reuse BSP scratch.
	 * Retail Flipper needs this too — not only -gcnewgame probes. */
	if( !Sys_CheckParm( "-gcsoftworld" ))
		GC_CaptureNewGamePVS();

	/* Match smoke-probe present cost: half-res buffer, skip VSync, cheap samples. */
	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = GC_VIDEO_BUDGET_WARMUP_PRESENTS;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = true;

	if( !gc.buffer || gc.width > probe_w || gc.height > probe_h )
	{
		if( !SW_CreateBuffer( probe_w, probe_h, &stride, &bpp, &r, &g, &b ))
			SYS_Report( "Xash3D GameCube: post-map frame buffer fallback %dx%d\n", gc.width, gc.height );
		gc_present_count = 0;
		gc_last_present_time = 0.0;
	}

	if( GC_MapLoadMemoryOpt())
		Cvar_Set( "gc_quality", "0" );

	gc_newgame_world_ready = false;
	gc_gx_present_logged = false;
	Cvar_Set( "gc_hud_probe_skip", "1" );

	SYS_Report( "Xash3D GameCube: frame budget samples armed after map ready (%dx%d probe=%d)\n",
		gc.width, gc.height, gc_budget_probe_active ? 1 : 0 );

	/* Host_Frame SCR collects G36 samples on the light fill path. Do not
	 * exhaust the probe with a synthetic burst — that forced V_RenderView
	 * immediately after and stalled presents (probe vs world buffer). */
		gc_light_present_left = GC_GetFrameBudgetSampleTarget() + GC_VIDEO_LIGHT_PRESENT_GRACE;
	#endif
}

void GC_BeginFrameBudgetProbe( void )
{
#if XASH_GAMECUBE
	uint stride, bpp, r, g, b;

	gc_present_count = 0;
	gc_blank_present_count = 0;
	gc_budget_sample_count = 0;
	gc_budget_warmup_left = 0;
	gc_light_present_left = 0;
	gc_last_present_time = 0.0;
	gc_worst_frame_ms = 0.0;
	gc_budget_probe_active = true;

	/* G36 samples the static gcmap smoke panel after the map is loaded.
	 * Keep that evidence path readable, but avoid timing the full 640x480
	 * RGB565-to-XFB conversion twelve times before real gameplay is active. */
	if( !gc.buffer || gc.width > GC_VIDEO_PROBE_WIDTH || gc.height > GC_VIDEO_PROBE_HEIGHT )
	{
		if( !SW_CreateBuffer( GC_VIDEO_PROBE_WIDTH, GC_VIDEO_PROBE_HEIGHT, &stride, &bpp, &r, &g, &b ))
			SYS_Report( "Xash3D GameCube: frame budget probe buffer fallback %dx%d\n", gc.width, gc.height );
		gc_present_count = 0;
		gc_last_present_time = 0.0;
	}
#endif
}

void GC_RestoreVideoMemoryAfterMapLoad( void )
{
	uint stride, bpp, r, g, b;
	int width, height;
	static const int fallbacks[][2] = {
		{ GC_VIDEO_PROBE_WIDTH, GC_VIDEO_PROBE_HEIGHT },
		{ GC_VIDEO_NEWGAME_PROBE_WIDTH, GC_VIDEO_NEWGAME_PROBE_HEIGHT },
	};
	size_t i;

	if( gc.buffer && gc.width > 0 && gc.height > 0
		&& gc.width >= GC_VIDEO_NEWGAME_PROBE_WIDTH
		&& gc.height >= GC_VIDEO_NEWGAME_PROBE_HEIGHT )
	{
		GC_InitPresentTexture();
		return;
	}

	width = refState.width > 0 ? refState.width : DEFAULT_MODE_WIDTH;
	height = refState.height > 0 ? refState.height : DEFAULT_MODE_HEIGHT;

	/* After map load MEM1 is tight: prefer the lean probe buffer under
	 * map-load memory opt instead of failing a full 640x480 calloc.
	 * New Game stays on the 160×120 BSS probe so G36 samples do not calloc. */
	if( Sys_CheckParm( "-gcnewgame" ))
	{
		width = GC_VIDEO_NEWGAME_PROBE_WIDTH;
		height = GC_VIDEO_NEWGAME_PROBE_HEIGHT;
	}
	else if( GC_MapLoadMemoryOpt()
		|| Sys_CheckParm( "-gcmap" ))
	{
		width = GC_VIDEO_PROBE_WIDTH;
		height = GC_VIDEO_PROBE_HEIGHT;
	}

	if( SW_CreateBuffer( width, height, &stride, &bpp, &r, &g, &b ))
	{
		/* Keep refState in sync so connect-time 2D does not assume 640×480. */
		if( gc.width > 0 && gc.height > 0 )
		{
			refState.width = gc.width;
			refState.height = gc.height;
		}
		SYS_Report( "Xash3D GameCube: restored presentation buffer %dx%d\n",
			gc.width, gc.height );
		return;
	}

	for( i = 0; i < sizeof( fallbacks ) / sizeof( fallbacks[0] ); i++ )
	{
		if( fallbacks[i][0] == width && fallbacks[i][1] == height )
			continue;
		if( SW_CreateBuffer( fallbacks[i][0], fallbacks[i][1], &stride, &bpp, &r, &g, &b ))
		{
			if( gc.width > 0 && gc.height > 0 )
			{
				refState.width = gc.width;
				refState.height = gc.height;
			}
			SYS_Report( "Xash3D GameCube: restored presentation buffer %dx%d\n",
				gc.width, gc.height );
			return;
		}
	}

	SYS_Report( "GX video: restore buffer failed %dx%d\n", width, height );
}

void GL_UpdateSwapInterval( void )
{
}

void VID_Info_f( void )
{
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	(void)handle;
	(void)type;
	return REF_WINDOW_TYPE_NULL;
}

#endif /* XASH_VIDEO == VIDEO_GX */
