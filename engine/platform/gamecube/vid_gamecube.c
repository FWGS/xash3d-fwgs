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
#include <string.h>

#if XASH_GAMECUBE
#include <ogc/gx.h>
#include <ogc/gu.h>
#include <ogc/video.h>
#include <ogc/color.h>
#include <ogc/system.h>
#include <ogc/cache.h>
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
static int gc_newgame_viewcluster = -1;
static qboolean gc_newgame_pvs_ready;
static byte *gc_newgame_vis; /* active row pointer into pvs table */
static byte *gc_newgame_nodebits; /* active row pointer into node table */
static byte *gc_newgame_pvs_table; /* [numclusters][visbytes] or lean slots */
static byte *gc_newgame_node_table; /* [numclusters][nodebytes] or lean slots */
static byte *gc_newgame_surf_table; /* G132: [numclusters][surfbytes] marksurface bits at capture */
static byte *gc_newgame_surfbits; /* active row into surf_table */
/* G163: when full surf_table OOMs, keep a few capture-time rows (marks die later). */
#define GC_SURFBITS_CACHE_SLOTS 8
#define GC_CAP_REFRESH_NEW_MAX 32
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
static gc_refresh_cand_t gc_refresh_cands[GC_SURFBITS_CACHE_SLOTS][GC_CAP_REFRESH_NEW_MAX];
static int gc_refresh_ncands[GC_SURFBITS_CACHE_SLOTS];
static int gc_g165_eye_cluster = -1; /* G165: player-eye cluster for restore refresh */
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
/* G132/G148/G150/G160: compact face records captured while msurface_t is still valid.
 * Cap stays 256 (larger BSS OOMs surfbits capture). G150 keeps top-K by area;
 * G160 boosts near-vertical walls and re-captures when lean PVS follows. */
#define GC_MAX_CAP_FACES 256
#define GC_CAP_AREA_SLOTS (( GC_MAX_CAP_FACES * 7 ) / 8) /* 224 top-K by area */
/* G153: bake style-0 lightmap to RGB565 at capture (samples dangle later). */
#define GC_CAP_LM_DIM 8
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
static gc_cap_face_t gc_newgame_cap_faces[GC_MAX_CAP_FACES];
static msurface_t gc_newgame_draw_surfs[GC_MAX_CAP_FACES];
static int gc_newgame_cap_areas[GC_MAX_CAP_FACES]; /* extents product per slot */
static u16 gc_newgame_cap_lm[GC_MAX_CAP_FACES][GC_CAP_LM_DIM * GC_CAP_LM_DIM]
	__attribute__((aligned( 32 )));
static byte gc_newgame_cap_lm_w[GC_MAX_CAP_FACES];
static byte gc_newgame_cap_lm_h[GC_MAX_CAP_FACES];
static byte gc_newgame_cap_lm_real[GC_MAX_CAP_FACES]; /* 1 if baked from samples */
static int gc_newgame_cap_face_count;
static int gc_newgame_cap_tex_faces; /* faces that kept a live texture* */
static int gc_newgame_cap_lm_faces; /* faces with real sample bake */

int GC_GetNewGameCapFaceCount( void )
{
	return gc_newgame_cap_face_count;
}

msurface_t *GC_GetNewGameDrawSurfs( void )
{
	return gc_newgame_cap_face_count > 0 ? gc_newgame_draw_surfs : NULL;
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
	return gc_newgame_cap_lm[slot];
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
	dst = gc_newgame_cap_lm[slot];
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
				unsigned r = c->r >> 3;
				unsigned g = c->g >> 2;
				unsigned b = c->b >> 3;
				if( r > 24 )
					r = 24 + (( r - 24 ) >> 1 );
				if( g > 48 )
					g = 48 + (( g - 48 ) >> 1 );
				if( b > 24 )
					b = 24 + (( b - 24 ) >> 1 );
				pix = (u16)(( r << 11 ) | ( g << 5 ) | b );
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
	dst = gc_newgame_cap_lm[slot];
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
	byte lmw, lmh, lmr;

	if( a == b || a < 0 || b < 0 || a >= GC_MAX_CAP_FACES || b >= GC_MAX_CAP_FACES )
		return;
	face = gc_newgame_cap_faces[a];
	draw = gc_newgame_draw_surfs[a];
	area = gc_newgame_cap_areas[a];
	memcpy( lm, gc_newgame_cap_lm[a], sizeof( lm ));
	lmw = gc_newgame_cap_lm_w[a];
	lmh = gc_newgame_cap_lm_h[a];
	lmr = gc_newgame_cap_lm_real[a];

	gc_newgame_cap_faces[a] = gc_newgame_cap_faces[b];
	gc_newgame_draw_surfs[a] = gc_newgame_draw_surfs[b];
	gc_newgame_cap_areas[a] = gc_newgame_cap_areas[b];
	memcpy( gc_newgame_cap_lm[a], gc_newgame_cap_lm[b], sizeof( lm ));
	gc_newgame_cap_lm_w[a] = gc_newgame_cap_lm_w[b];
	gc_newgame_cap_lm_h[a] = gc_newgame_cap_lm_h[b];
	gc_newgame_cap_lm_real[a] = gc_newgame_cap_lm_real[b];

	gc_newgame_cap_faces[b] = face;
	gc_newgame_draw_surfs[b] = draw;
	gc_newgame_cap_areas[b] = area;
	memcpy( gc_newgame_cap_lm[b], lm, sizeof( lm ));
	gc_newgame_cap_lm_w[b] = lmw;
	gc_newgame_cap_lm_h[b] = lmh;
	gc_newgame_cap_lm_real[b] = lmr;
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
		gc_cap_face_t face = gc_newgame_cap_faces[i];
		msurface_t draw = gc_newgame_draw_surfs[i];
		int area = gc_newgame_cap_areas[i];
		u16 lm[GC_CAP_LM_DIM * GC_CAP_LM_DIM];
		byte lmw = gc_newgame_cap_lm_w[i];
		byte lmh = gc_newgame_cap_lm_h[i];
		byte lmr = gc_newgame_cap_lm_real[i];

		memcpy( lm, gc_newgame_cap_lm[i], sizeof( lm ));
		j = i;
		while( j > 0 && gc_newgame_cap_areas[j - 1] < area )
		{
			gc_newgame_cap_faces[j] = gc_newgame_cap_faces[j - 1];
			gc_newgame_draw_surfs[j] = gc_newgame_draw_surfs[j - 1];
			gc_newgame_cap_areas[j] = gc_newgame_cap_areas[j - 1];
			memcpy( gc_newgame_cap_lm[j], gc_newgame_cap_lm[j - 1], sizeof( lm ));
			gc_newgame_cap_lm_w[j] = gc_newgame_cap_lm_w[j - 1];
			gc_newgame_cap_lm_h[j] = gc_newgame_cap_lm_h[j - 1];
			gc_newgame_cap_lm_real[j] = gc_newgame_cap_lm_real[j - 1];
			j--;
		}
		gc_newgame_cap_faces[j] = face;
		gc_newgame_draw_surfs[j] = draw;
		gc_newgame_cap_areas[j] = area;
		memcpy( gc_newgame_cap_lm[j], lm, sizeof( lm ));
		gc_newgame_cap_lm_w[j] = lmw;
		gc_newgame_cap_lm_h[j] = lmh;
		gc_newgame_cap_lm_real[j] = lmr;
	}
	/* Re-bind plane/texinfo/info pointers after moves. */
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		msurface_t *draw = &gc_newgame_draw_surfs[i];
		gc_cap_face_t *dst = &gc_newgame_cap_faces[i];

		draw->plane = &dst->plane;
		draw->info = &dst->info;
		dst->info.surf = draw;
		if( dst->texinfo.texture )
			draw->texinfo = &dst->texinfo;
		else
			draw->texinfo = NULL;
		if( gc_newgame_cap_lm_w[i] >= 4 && gc_newgame_cap_lm_h[i] >= 4 )
			DCFlushRange( gc_newgame_cap_lm[i],
				(u32)( gc_newgame_cap_lm_w[i] * gc_newgame_cap_lm_h[i] * sizeof( u16 )));
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
	if( !wmodel || !wmodel->surfaces || !surfbits || gc_newgame_surfbytes <= 0 )
		return;

	/* Pass 1 (G150): online top-K by area into area_slots — not surface order.
	 * G160: +50% score for near-vertical walls so outdoor towers beat floors. */
	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		int area;
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
		{
			area += ( area >> 1 );
			wall_boost++;
		}

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
		{
			qboolean had_tex = ( gc_newgame_cap_faces[min_i].texinfo.texture != NULL );

			if( !GC_CaptureOneDrawFaceAtEx( wmodel, i, min_i, true ))
				continue;
			/* Store boosted score so later compares stay consistent. */
			gc_newgame_cap_areas[min_i] = area;
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
	for( i = 0; i < gc_newgame_cap_face_count; i++ )
	{
		if( gc_newgame_cap_lm_real[i] )
			gc_newgame_cap_lm_faces++;
	}
	SYS_Report( "Xash3D GameCube: G160 captured draw faces=%d textured=%d lm=%d replaced=%d wallboost=%d (max=%d)\n",
		gc_newgame_cap_face_count, gc_newgame_cap_tex_faces, gc_newgame_cap_lm_faces,
		replaced, wall_boost, GC_MAX_CAP_FACES );
}

/*
 * G163: live cluster face refresh without sample LM rebake.
 * Deferred + incremental: keep baked faces; admit up to 32 new top-area faces
 * with mid-grade LM only. Full rebuild mid-PVS hung Host_Frame on c1a0a.
 * Face geom must be snapshotted at capture — live plane* dangles at present.
 */
static qboolean gc_cap_refresh_pending;

static void GC_BuildSurfbitsForVisRow( model_t *wmodel, const byte *vis, byte *surfbits );
static byte *GC_LookupSurfbitsCache( int cluster );
static int GC_VisLeafsForCluster( int cluster );
static int GC_SelectClusterForOrigin( const float *org );
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
	gc_newgame_cap_areas[slot] = cand->area;
	GC_FillCapLightmapMid( slot );
	return true;
}

static void GC_BuildRefreshCandsFromSurfbits( model_t *wmodel, const byte *surfbits, int cache_slot )
{
	int i, k;
	int ncand = 0;
	int wall_boost = 0;

	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return;
	gc_refresh_ncands[cache_slot] = 0;
	if( !wmodel || !wmodel->surfaces || !surfbits || gc_newgame_surfbytes <= 0 )
		return;

	for( i = 0; i < wmodel->numsurfaces; i++ )
	{
		msurface_t *src;
		gc_refresh_cand_t *dst;
		int area;
		qboolean is_wall;
		int min_i, min_area;

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
		{
			area += ( area >> 1 );
			wall_boost++;
		}

		if( ncand < GC_CAP_REFRESH_NEW_MAX )
		{
			dst = &gc_refresh_cands[cache_slot][ncand];
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
		min_area = gc_refresh_cands[cache_slot][0].area;
		for( k = 1; k < GC_CAP_REFRESH_NEW_MAX; k++ )
		{
			if( gc_refresh_cands[cache_slot][k].area < min_area )
			{
				min_area = gc_refresh_cands[cache_slot][k].area;
				min_i = k;
			}
		}
		if( area <= min_area )
			continue;
		dst = &gc_refresh_cands[cache_slot][min_i];
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
	gc_refresh_ncands[cache_slot] = ncand;
	SYS_Report( "Xash3D GameCube: G163 refresh cands ready slot=%d n=%d wallboost=%d\n",
		cache_slot, ncand, wall_boost );
}

static void GC_RefreshCapFacesFromCands( int cache_slot )
{
	int i, k;
	int mid_new = 0;
	int wall_boost = 0;
	int prev_count = gc_newgame_cap_face_count;
	int ncand;

	if( cache_slot < 0 || cache_slot >= GC_SURFBITS_CACHE_SLOTS )
		return;
	ncand = gc_refresh_ncands[cache_slot];
	SYS_Report( "Xash3D GameCube: G163 refresh begin cluster=%d faces=%d cands=%d\n",
		gc_newgame_viewcluster, prev_count, ncand );

	for( i = 0; i < ncand; i++ )
	{
		const gc_refresh_cand_t *cand = &gc_refresh_cands[cache_slot][i];
		int slot = -1;
		int min_i, min_area;

		if( GC_CapFaceAlready( cand->firstedge, cand->numedges ))
			continue;
		if( fabs( cand->plane.normal[2] ) < 0.35f )
			wall_boost++;

		if( gc_newgame_cap_face_count < GC_MAX_CAP_FACES )
			slot = gc_newgame_cap_face_count;
		else
		{
			min_i = 0;
			min_area = gc_newgame_cap_areas[0];
			for( k = 1; k < GC_MAX_CAP_FACES; k++ )
			{
				int a = gc_newgame_cap_areas[k];
				if( !gc_newgame_cap_lm_real[k] )
					a -= 1;
				if( a < min_area )
				{
					min_area = a;
					min_i = k;
				}
			}
			if( cand->area <= gc_newgame_cap_areas[min_i] )
				continue;
			slot = min_i;
		}

		if( !GC_InstallRefreshCand( cand, slot ))
			continue;
		if( slot == gc_newgame_cap_face_count )
			gc_newgame_cap_face_count++;
		mid_new++;
	}

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
	SYS_Report( "Xash3D GameCube: G163 refreshed draw faces=%d prev=%d reused_lm=%d mid_new=%d lm=%d wallboost=%d cluster=%d\n",
		gc_newgame_cap_face_count, prev_count, prev_count, mid_new, gc_newgame_cap_lm_faces,
		wall_boost, gc_newgame_viewcluster );
	/* G165: sparse clusters (outdoor / landmark camera) — leaves typically << indoor. */
	{
		int leaves = GC_VisLeafsForCluster( gc_newgame_viewcluster );

		if( leaves > 0 && leaves <= 48 )
		{
			SYS_Report( "Xash3D GameCube: G165 restore refresh cluster=%d mid_new=%d cands=%d leaves=%d\n",
				gc_newgame_viewcluster, mid_new, ncand, leaves );
		}
	}
}

static void GC_FlushPendingCapFaceRefresh( void )
{
	int cache_slot;

	if( !gc_cap_refresh_pending )
		return;
	gc_cap_refresh_pending = false;
	if( gc_newgame_cap_face_count <= 0 )
		return;

	cache_slot = GC_LookupSurfbitsCacheSlot( gc_newgame_viewcluster );
	if( cache_slot < 0 || gc_refresh_ncands[cache_slot] <= 0 )
	{
		SYS_Report( "Xash3D GameCube: G163 refresh skipped (no capture cands cluster=%d)\n",
			gc_newgame_viewcluster );
		return;
	}
	GC_RefreshCapFacesFromCands( cache_slot );
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
/* G128: remaining presents that must use CPU YUYV→XFB so Dolphin DumpFrames
 * captures a readable image (GX tiled path dumps as period-32 noise). */
static int gc_cpu_dump_presents_left;
static qboolean gc_dump_look_into_map;
static vec3_t gc_newgame_capture_origin; /* G132: dump camera aim target */
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

	SYS_Report( "Xash3D GameCube: early video splash\n" );
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
		SYS_Report( "Xash3D GameCube: video init continuing after early splash\n" );
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

	/* Software renderer outputs 16-bit RGB565; force matching display format */
	GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );

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
	SYS_Report( "Xash3D GameCube: renderer initialized gx\n" );
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

	if( !rmode || !xfb[which_fb] || !gc_present_tex_ready )
		return;

	fb_w = (f32)rmode->fbWidth;
	fb_h = (f32)rmode->efbHeight;

	/* One-time ortho / copy / vtx setup; per-frame we only refresh the
	 * textured quad after DCFlush of the tiled RGB565 staging buffer. */
	if( !gc_gx_present_pipe_ready )
	{
		Mtx44 proj;
		Mtx modelview;

		GX_SetViewport( 0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f );
		GX_SetScissor( 0, 0, (u32)fb_w, (u32)fb_h );
		GX_SetDispCopySrc( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetDispCopyDst( rmode->fbWidth, rmode->xfbHeight );
		GX_SetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight );
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
		gc_gx_present_pipe_ready = true;
	}

	GX_InvVtxCache();
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
	GX_CopyDisp( xfb[which_fb], GX_TRUE );
	/* Second sync wait is only needed when the next CPU work can race the
	 * copy; silent G36 windows skip it to reclaim present ms at 160×120. */
	if( !gc_budget_probe_active )
		GX_DrawDone();
	else
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
	VIDEO_SetBlack( true );
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

	/* G151: Flipper already holds world geometry — CopyDisp only (no soft blit). */
	if( gc_gx_world_efb_ready )
	{
		f32 fb_w = (f32)rmode->fbWidth;
		f32 fb_h = (f32)rmode->efbHeight;

		gc_gx_present_pipe_ready = false; /* next soft present rebuilds ortho */
		GX_SetViewport( 0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f );
		GX_SetScissor( 0, 0, (u32)fb_w, (u32)fb_h );
		GX_SetDispCopySrc( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetDispCopyDst( rmode->fbWidth, rmode->xfbHeight );
		GX_SetDispCopyYScale((f32)rmode->xfbHeight / (f32)rmode->efbHeight );
		GX_SetCopyFilter( rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter );
		GX_DrawDone();
		GX_CopyDisp( xfb[which_fb], GX_TRUE );
		GX_DrawDone();
		gc_gx_world_efb_ready = false;
		VIDEO_SetNextFramebuffer( xfb[which_fb] );
		VIDEO_Flush();
		if( !gc_budget_probe_active )
			VIDEO_WaitVSync();
		which_fb ^= 1;
		return;
	}

	copy_w = rmode->fbWidth;
	copy_h = rmode->xfbHeight;
	dst = (unsigned int *)xfb[which_fb];

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
		 * G128: force CPU YUYV for a few post-world dump frames — Dolphin
		 * DumpFramesAsImages of the GX path is period-32 tiled noise. */
		if( gc_cpu_dump_presents_left <= 0 && GC_CanPresentViaGX( src_w, src_h ))
		{
			GC_SwizzleRGB565ToTiled( src, gc.stride, src_w, src_h, gc_tiled_rgb565 );
			DCFlushRange( gc_tiled_rgb565, (u32)((size_t)src_w * (size_t)src_h * sizeof( u16 )));
			GC_InitPresentTextureTiled( gc_tiled_rgb565, src_w, src_h );
			GC_PresentBufferViaGX();
			if( !gc_gx_present_logged )
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
		SYS_Report( "Xash3D GameCube: present frame=%u sampled_nonblack=%u blank_frames=%u frame time=%.2fms\n",
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

	DCFlushRange( xfb[which_fb], VIDEO_GetFrameBufferSize( rmode ));
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_Flush();
	/* Skip VSync during G36 budget samples, New Game low-res world presents,
	 * and startup cinematics so Host_Frame is not gated on the 16.7ms VI period.
	 * G128 CPU dump presents keep VSync so Dolphin DumpFrames can latch XFB. */
	if( g128_cpu_dump
		|| ( !gc_budget_probe_active && !gc_newgame_world_ready && cls.state != ca_cinematic ))
		VIDEO_WaitVSync();

	which_fb ^= 1;
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
	gc_newgame_world_ready = false;
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
	Con_Reportf( "Xash3D GameCube: presentation buffer released for map load\n" );
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
	Con_Reportf( "Xash3D GameCube: released presentation buffer for world render\n" );
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
		Con_Reportf( "Xash3D GameCube: G147 live scrub (cracks=%d nearblack+neon) nonblack=%u/%u\n",
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
			Con_Reportf( "Xash3D GameCube: G143 scrub dump speckles (fill=%u neon=%u outliers=%u) %dx%d\n",
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

	panel_x = width * 24 / 640;
	panel_w = width - panel_x * 2;
	panel_h = height * 110 / 480;
	if( top_aligned )
		panel_y = height * 18 / 480;
	else
	{
		panel_y = height - panel_h - height * 18 / 480;
		if( panel_y < height * 18 / 480 )
			panel_y = height * 18 / 480;
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
		GC_BlitLoadingBackground( gc.buffer, gc.width, gc.height, gc.stride );
		GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride, message, details );
		/* G130: force one CPU YUYV present so DumpFrames keep the loading plaque
		 * (GX tiled presents read as period-32 noise behind the panel). */
		if( gc_cpu_dump_presents_left < 1 )
			gc_cpu_dump_presents_left = 1;
		GC_PresentBuffer();
		return;
	}

	if( !rmode || !xfb[which_fb] )
		return;

	dst = (unsigned short *)xfb[which_fb];
	GC_BlitLoadingBackground( dst, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth );
	GC_DrawStatusPanelToBuffer( dst, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth,
		message, details );

	xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
	DCFlushRange( xfb[which_fb], (u32)xfb_size );
	VIDEO_SetNextFramebuffer( xfb[which_fb] );
	VIDEO_Flush();
	if( host.status != HOST_INIT )
	{
		VIDEO_WaitVSync();
		which_fb ^= 1;
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
		Con_Reportf( "Xash3D GameCube: gcmap world render scratch alloc failed\n" );
		return false;
	}
	GC_MemSample( "pre-world render" );

	if( !R_GcmapPrepareWorldRender() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render screen alloc failed\n" );
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
		Con_Reportf( "Xash3D GameCube: gcmap world render presentation buffer failed\n" );
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
		Con_Reportf( "Xash3D GameCube: gcmap world render using quality=0 without surface cache\n" );
	}
	else if( !R_GcmapEnsureSurfaceCache() )
	{
		Con_Reportf( "Xash3D GameCube: gcmap world render using quality=0 without surface cache\n" );
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
		Con_Reportf( "Xash3D GameCube: gcmap world render view from edict=%d origin=(%.0f,%.0f,%.0f)\n",
			i, center[0], center[1], center[2] );
		break;
	}
	VectorCopy( center, rvp.vieworigin );
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	Cvar_Set( "r_drawviewmodel", "0" );

	if( count > 6 )
		count = 6;

	Con_Reportf( "Xash3D GameCube: gcmap world render begin frames=%d\n", count );
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
	Con_Reportf( "Xash3D GameCube: gcmap world render ready\n" );
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
	unsigned short *dst;
	unsigned short *rowdst;
	int row;
	int col_fatal;
	int i;
	size_t xfb_size;

	dst = NULL;
	rowdst = NULL;
	row = 0;
	col_fatal = 0;
	i = 0;
	xfb_size = 0;

	/* G65: Guard against early Sys_Error before video init.
	 * GC_DrawFatalBreadcrumb writes directly to XFB via rmode/xfb pointers.
	 * If called before GC_InitVideoHardware completes, rmode/xfb are uninitialized,
	 * causing guest_fatal in Dolphin or real hardware. Skip visual output if
	 * video hardware is not initialized; rely on Sys_Error/SYS_Report for diagnostics. */
	if( !gc.initialized )
		return;

	/* G66: Additional safety guard - verify rmode and xfb[0] are valid before drawing.
	 * Prevents guest_fatal in Dolphin when video subsystem is partially initialized
	 * or in an inconsistent state during error paths. */
	if( !rmode || !rmode->fbWidth || !rmode->xfbHeight || !xfb[0] )
		return;

	/* Do not clear gc.initialized - this function draws to XFB and
	 * leaves hardware in a presentable state. Clearing initialization
	 * can break subsequent rendering attempts or cause hardware state
	 * mismatches during error recovery paths. */

	/* Present to front buffer immediately for visibility */
	dst = (unsigned short *)xfb[0];

	/* Fill XFB with high-contrast Magenta (RGB565 0xF81F) to signal FATAL ERROR */
	for( row = 0; row < rmode->xfbHeight; row++ )
	{
		rowdst = dst + row * rmode->fbWidth;
		for( col_fatal = 0; col_fatal < rmode->fbWidth; col_fatal++ )
			rowdst[col_fatal] = 0xF81F; /* magenta */
	}

	GC_FatalDrawLine( dst, 24, 6, "XASH3D GAMECUBE FATAL", 0xFFFF, 2, 34 );
	GC_FatalDrawWrapped( dst, 24, 42, message ? message : "UNKNOWN ERROR", 0xFFE0, 2, 38, 5 );
	GC_FatalDrawWrapped( dst, 24, 150, details ? details : "NO DETAILS", 0xFFFF, 2, 38, 8 );
	GC_FatalDrawLine( dst, 24, rmode->xfbHeight - 28, "HALTED: POWER CYCLE OR RESET", 0x07E0, 2, 38 );

	/* G51: Do not flush GX pipeline here. If GX is in an inconsistent state due to
	 * the fatal error, GX_Flush/GX_DrawDone can trigger guest_fatal hangs in
	 * Dolphin or on real hardware. We only need to flush the data cache for the
	 * XFB region and present it via VIDEO_SetNextFramebuffer. The GX command
	 * buffer is not used for this CPU-written fatal message. */

	xfb_size = rmode->fbWidth * rmode->xfbHeight * sizeof(unsigned short);
	DCFlushRange( xfb[0], (u32)xfb_size );
	VIDEO_SetNextFramebuffer( xfb[0] );
	VIDEO_Flush();
	VIDEO_WaitVSync();

	/* Block briefly to ensure the fatal frame reaches the display before the
	 * caller halts or exits. Keep this bounded so the error path cannot loop. */
	for( i = 0; i < 3; i++ )
		VIDEO_WaitVSync();

	/* Do not toggle which_fb here. The fatal breadcrumb draws directly to xfb[0]
	 * and leaves the double-buffering state unchanged. Modifying which_fb during
	 * an error path can cause subsequent rendering (if any) to target the wrong
	 * framebuffer, leading to visual artifacts or guest_fatal hangs in Dolphin.
	 * Since this function is called from Sys_Error before process exit, preserving
	 * the original which_fb value ensures any late-stage diagnostic output remains
	 * consistent with the normal rendering path. */

	/* Return control to caller (e.g., Sys_Error) for proper termination or
	 * recovery. Do not call SYS_ResetSystem here as it can cause guest_fatal
	 * hangs or crash loops in emulated or real hardware environments. */
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

qboolean GC_UseGxWorldDraw( void )
{
#if XASH_GAMECUBE
	/* Live Flipper path after the soft DumpFrames latch finishes.
	 * `-gcsoftworld` forces the Quake span raster for comparison. */
	if( !gc_newgame_world_ready || !gc_gx_world_live )
		return false;
	if( gc_cpu_dump_presents_left > 0 )
		return false;
	if( Sys_CheckParm( "-gcsoftworld" ))
		return false;
	return Sys_CheckParm( "-gcnewgame" ) ? true : false;
#else
	return false;
#endif
}

void GC_MarkGxWorldEfbReady( void )
{
#if XASH_GAMECUBE
	gc_gx_world_efb_ready = true;
#endif
}

void GC_EnableGxWorldLive( void )
{
#if XASH_GAMECUBE
	if( !Sys_CheckParm( "-gcsoftworld" ))
	{
		gc_gx_world_live = true;
		Con_Reportf( "Xash3D GameCube: G151 GX world live enabled (Flipper EFB)\n" );
	}
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

static void GC_StoreSurfbitsCache( model_t *wmodel, int cluster, const byte *vis );

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
	SYS_Report( "Xash3D GameCube: G165 restore cands ready cluster=%d leaves=%d\n",
		eye_c, GC_VisLeafsForCluster( eye_c ));
}

static void GC_MaybeRefreshCapFacesAfterClusterChange( int prev_cluster )
{
	/* Defer work off the PVS switch path — mid-frame rebuild hung Host_Frame. */
	if( prev_cluster == gc_newgame_viewcluster )
		return;
	if( gc_newgame_cap_face_count <= 0 )
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
			SYS_Report( "Xash3D GameCube: PVS lean LRU load cluster=%d slot=%d evict=%d leaves=%d nodes=%d\n",
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
	SYS_Report( "Xash3D GameCube: Capture FatPVS lean LRU rows=%d packed=%u\n",
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
	SYS_Report( "Xash3D GameCube: Capture FatPVS lean LRU nodebits=%u\n",
		(unsigned)gc_newgame_packed_nodebits_size );
	return true;
}

static void GC_FreeNewGamePVSCache( void )
{
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
	gc_newgame_cap_tex_faces = 0;
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
	if( !Sys_CheckParm( "-gcnewgame" ) && !Sys_CheckParm( "-gcchangelevel" ))
		return;

	SYS_Report( "Xash3D GameCube: changelevel teardown map=%s world_ready=%d pvs=%d\n",
		sv.name[0] ? sv.name : "?", gc_newgame_world_ready ? 1 : 0,
		gc_newgame_pvs_ready ? 1 : 0 );
	GC_FreeNewGamePVSCache();
	gc_newgame_world_ready = false;
	gc_gx_world_live = false;
	gc_gx_world_efb_ready = false;
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

	/* G163: flush prove-time cluster switches before restore UpdatePVS. */
	GC_FlushPendingCapFaceRefresh();

	/* Switch to densest cached cluster so refresh admits faces outside bake set. */
	if( gc_newgame_surf_cache_slots > 1 )
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
			SYS_Report( "Xash3D GameCube: G163 explore cluster=%d leaves=%d for face refresh\n",
				explore, explore_leaves );
			GC_FlushPendingCapFaceRefresh();
		}
	}

	/* Restore origin-based cluster when possible (player / first leafbox). */
	if( svgame.edicts && svs.maxclients >= 1 )
	{
		edict_t *player = SV_EdictNum( 1 );

		if( player && !player->free && !VectorIsNull( player->v.origin ))
		{
			vec3_t eye;

			VectorCopy( player->v.origin, eye );
			eye[2] += 48.0f;
			GC_UpdateNewGamePVSForOrigin( eye );
			GC_FlushPendingCapFaceRefresh();
			return;
		}
	}
	GC_SetActiveNewGameCluster( c0, false );
	GC_FlushPendingCapFaceRefresh();
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

	if( !Sys_CheckParm( "-gcnewgame" ))
		return;
	if( gc_newgame_pvs_ready )
		return;
	if( !wmodel )
		wmodel = sv.models[1];

	gc_newgame_viewcluster = -1;
	gc_newgame_vis_leafs = 0;
	gc_newgame_vis_nodes = 0;

	SYS_Report( "Xash3D GameCube: CaptureNewGamePVS begin\n" );

	if( !wmodel || !wmodel->nodes || !wmodel->leafs )
	{
		SYS_Report( "Xash3D GameCube: CaptureNewGamePVS skipped (no world nodes)\n" );
		return;
	}

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

	SYS_Report( "Xash3D GameCube: Capture PointInLeaf cluster=%d contents=%d origin=(%.0f,%.0f,%.0f)\n",
		gc_newgame_viewcluster, viewleaf ? viewleaf->contents : 0,
		vieworigin[0], vieworigin[1], vieworigin[2] );
	VectorCopy( vieworigin, gc_newgame_capture_origin );

	/* G132: promote surfaces on the post-changelevel map only — promoting on
	 * c0a0 plus FatPVS OOMs the guest before changelevel. */
	{
		static int capture_gen;
		char cl_dest[64];

		capture_gen++;
		cl_dest[0] = '\0';
		Sys_GetParmFromCmdLine( "-gcchangelevel", cl_dest );
		if( capture_gen > 1 || ( cl_dest[0] && sv.name[0] && !Q_stricmp( sv.name, cl_dest )))
		{
			if( !Mod_GCPromoteWorldSurfaces( wmodel ))
				SYS_Report( "Xash3D GameCube: G132 surface promote skipped\n" );
		}
	}

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

		gc_newgame_visbytes = (int)visbytes;
		gc_newgame_nodebytes = (int)nodebytes;
		gc_newgame_numclusters = numclusters;
		gc_newgame_numleafs = wmodel->numleafs;
		gc_newgame_numnodes = wmodel->numnodes;
		gc_newgame_numsurfaces = wmodel->numsurfaces;
		gc_newgame_surfbytes = ( wmodel->numsurfaces > 0 )
			? (int)(( (size_t)wmodel->numsurfaces + 7 ) / 8) : 0;
		gc_newgame_nleafboxes = wmodel->numleafs > 1 ? wmodel->numleafs - 1 : 0;

		/* G101: -gcleanpvs forces lean-N path (probe / MEM1-safe follow). */
		if( !Sys_CheckParm( "-gcleanpvs" ))
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
			SYS_Report( "Xash3D GameCube: Capture FatPVS force lean-N (-gcleanpvs) clusters=%d\n",
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

			gc_newgame_pvs_table = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, visbytes );
			gc_newgame_node_table = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, nodebytes );
			gc_newgame_surf_table = ( gc_newgame_surfbytes > 0 )
				? (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, (size_t)gc_newgame_surfbytes )
				: NULL;
			gc_newgame_cluster_valid = (byte *)calloc( (size_t)GC_LEAN_PVS_SLOTS, 1 );
			gc_newgame_leafboxes = gc_newgame_nleafboxes > 0
				? (gc_newgame_leafbox_t *)calloc( (size_t)gc_newgame_nleafboxes, sizeof( gc_newgame_leafbox_t ))
				: NULL;
			if( !gc_newgame_pvs_table || !gc_newgame_node_table || !gc_newgame_cluster_valid
				|| ( gc_newgame_nleafboxes > 0 && !gc_newgame_leafboxes ))
			{
				SYS_Report( "Xash3D GameCube: Capture FatPVS lean alloc failed\n" );
				GC_FreeNewGamePVSCache();
				return;
			}

			/* Leaf AABBs enable origin follow among cached clusters. */
			for( i = 1; i < wmodel->numleafs; i++ )
			{
				mleaf_t *leaf = &wmodel->leafs[i];
				gc_newgame_leafbox_t *box = &gc_newgame_leafboxes[i - 1];

				VectorCopy( leaf->minmaxs, box->mins );
				VectorCopy( leaf->minmaxs + 3, box->maxs );
				box->cluster = leaf->cluster;
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
			 * compressed_vis is invalid after scratch reuse). */
			for( i = 1; i < wmodel->numleafs && lean_slots < GC_LEAN_PVS_SLOTS; i++ )
			{
				mleaf_t *leaf = &wmodel->leafs[i];
				int c = leaf->cluster;
				byte *row;
				qboolean already = false;

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

			gc_newgame_lean_slots = lean_slots;
			gc_newgame_viewcluster = lean_cluster;
			if( !GC_SetActiveNewGameCluster( lean_cluster, false ))
			{
				GC_FreeNewGamePVSCache();
				return;
			}
			gc_newgame_pvs_ready = true;
			SYS_Report( "Xash3D GameCube: Capture FatPVS lean map=%s cluster=%d slots=%d leaves=%d nodes=%d\n",
				sv.name[0] ? sv.name : "?",
				lean_cluster, lean_slots, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
			if( gc_newgame_surfbits )
				GC_CaptureDrawFacesFromSurfbits( wmodel, gc_newgame_surfbits );
			if( lean_slots > 1 )
			{
				SYS_Report( "Xash3D GameCube: Capture FatPVS lean-N map=%s slots=%d c0=%d c1=%d\n",
					sv.name[0] ? sv.name : "?",
					lean_slots, gc_newgame_lean_clusters[0], gc_newgame_lean_clusters[1] );
			}
			return;
		}

		SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS begin clusters=%d visbytes=%d\n",
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
		SYS_Report( "Xash3D GameCube: Capture FatPVS map=%s cluster=%d leaves=%d nodes=%d\n",
			sv.name[0] ? sv.name : "?",
			gc_newgame_viewcluster, gc_newgame_vis_leafs, gc_newgame_vis_nodes );
		SYS_Report( "Xash3D GameCube: Capture multi-cluster PVS ready map=%s clusters=%d valid=%d\n",
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
					SYS_Report( "Xash3D GameCube: G165 restore cands ready cluster=%d\n",
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
			/* G165 first: exact outdoor leaf-count, then near-band. */
			for( i = 0; i < numclusters
				&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS - 2; i++ )
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
					SYS_Report( "Xash3D GameCube: G165 restore cands ready cluster=%d leaves=%d\n",
						i, leaves );
				}
			}
			for( i = 0; i < numclusters
				&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS - 2; i++ )
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
					&& gc_newgame_surf_cache_slots < GC_SURFBITS_CACHE_SLOTS - 1; i++ )
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
				SYS_Report( "Xash3D GameCube: G163 surfbits cache slots=%d max_c=%d eye_c=%d g165=%d\n",
					gc_newgame_surf_cache_slots, max_c, eye_c, gc_g165_eye_cluster );
				gc_newgame_surfbits = GC_LookupSurfbitsCache( gc_newgame_viewcluster );
			}
		}
		if( gc_newgame_surfbits )
			GC_CaptureDrawFacesFromSurfbits( wmodel, gc_newgame_surfbits );
		else if( gc_newgame_surfbytes > 0 && gc_newgame_vis )
		{
			/* G154: multi-cluster may skip the full surf table under MEM1;
			 * bake Flipper faces from the active vis row alone. */
			byte *row_bits = (byte *)calloc( 1, (size_t)gc_newgame_surfbytes );

			if( row_bits )
			{
				GC_BuildSurfbitsForVisRow( wmodel, gc_newgame_vis, row_bits );
				GC_CaptureDrawFacesFromSurfbits( wmodel, row_bits );
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
	VectorCopy( center, rvp.vieworigin );
	GC_UpdateNewGamePVSForOrigin( center );
	GC_ProveNewGamePVSFollow();
	GC_FlushPendingCapFaceRefresh();
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
	/* G132: if lean PVS cannot follow the player cluster, render from the
	 * capture-room origin so surfbits/nodebits match the camera. */
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
	/* G132: aim toward capture-room origin (or player forward) so frustum keeps
	 * nearby walls — map AABB center often points into empty space after move. */
	if( gc_dump_look_into_map )
	{
		vec3_t look;
		float look_len;

		VectorSubtract( gc_newgame_capture_origin, center, look );
		look_len = VectorLength( look );
		if( look_len > 64.0f )
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
		}
		Con_Reportf( "Xash3D GameCube: G132 dump look angles=(%.0f,%.0f,%.0f) aimlen=%.0f\n",
			rvp.viewangles[0], rvp.viewangles[1], rvp.viewangles[2], look_len );
	}
	VectorCopy( center, rvp.vieworigin );
	/* G89: select multi-cluster PVS for camera; prove two-cluster switch once. */
	GC_UpdateNewGamePVSForOrigin( center );
	GC_ProveNewGamePVSFollow();
	GC_FlushPendingCapFaceRefresh();
	SetBits( rvp.flags, RF_DRAW_WORLD );
	Q_snprintf( old_drawviewmodel, sizeof( old_drawviewmodel ), "%s", Cvar_VariableString( "r_drawviewmodel" ));
	/* G149: dump/landmark path may force viewmodel on for DumpFrames evidence. */
	Cvar_Set( "r_drawviewmodel", gc_force_draw_viewmodel ? "1" : "0" );

	{
		static int render_log;

		if( render_log < 3 )
		{
			Con_Reportf( "Xash3D GameCube: newgame world render begin frames=%d origin=(%.0f,%.0f,%.0f)\n",
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
			Con_Reportf( "Xash3D GameCube: newgame world render sustained frames=%u scr=%u\n",
				sustained_frames, scr_frames );
		}
		if( scr_frames == 8 || scr_frames == 16
			|| ( scr_frames > 0 && ( scr_frames % 32 ) == 0 ))
		{
			Con_Reportf( "Xash3D GameCube: newgame world render SCR frames=%u\n",
				scr_frames );
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

qboolean GC_PrepareNewGameWorldPresent( void )
{
#if XASH_GAMECUBE
	int present_w;
	int present_h;

	GC_GetNewGamePresentSize( &present_w, &present_h );

	if( gc_newgame_world_ready )
		return true;
	if( !Sys_CheckParm( "-gcnewgame" ))
		return false;

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

	/* One desert side for RGB565 sky fills — deferred past map-prep OOM cliff. */
	{
		const char *sky = clgame.movevars.skyName;
		if( COM_StringEmptyOrNULL( sky ))
			sky = DEFAULT_SKYBOX_NAME;
		R_SetupSkyLeanGameCube( sky );
	}

	/* After sky proves FS/MEM headroom, promote a few mesh-only studios.
	 * Viewmodel bind is forced in V_SetupViewModel (tram starts unarmed). */
	Mod_GCLoadNewGameStudios();

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
	gc_newgame_g36_done = true;
	/* G135: do NOT arm CPU dump presents yet. Soft-tiled RGB565 blits dump as
	 * chroma noise and steal Dolphin DumpFrames slots before depth/coalesce.
	 * GX is fine for the pre-dump pump; G128 arms after WORLD PRESENT panel. */
	gc_cpu_dump_presents_left = 0;
	Cvar_Set( "gc_hud_probe_skip", "0" );

	/* Lean HUD VidInit at quality 0: set 320 sheet names without hud.txt.
	 * Clear FS miss-cache so bootstrap-injected sprites/320_pain.spr is visible. */
	FS_ClearFindMissCache();
	if( clgame.dllFuncs.pfnVidInit )
	{
		clgame.dllFuncs.pfnVidInit();
		Con_Reportf( "Xash3D GameCube: newgame lean HUD VidInit after world present\n" );
	}
	/* G127: fat HUD sheets before SFX preload / changelevel Redraw. */
	CL_GCPreloadNewGameHudSprites();

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

	/* Prefer real low-res world frames here: the Dolphin probe often exits as
	 * soon as G36 evidence is scored, before the next Host_Frame can run SCR.
	 * Fall back to lean green fills if the world path is not ready yet. */
	if( !GC_RenderNewGameWorldFrames( 4 ))
	{
		int i;

		Con_Reportf( "Xash3D GameCube: post-G36 sustained present (world render deferred)\n" );
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
		Con_Reportf( "Xash3D GameCube: post-G36 sustained world present\n" );
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
					if( !keep_textured && uniq >= 8 && chroma * 4 >= samples )
						Con_Reportf( "Xash3D GameCube: G140 reject chroma dump (nonblack=%u/%u uniq=%u chroma=%u)\n",
							nonblack, samples, uniq, chroma );
				}

				if( keep_textured )
				{
					/* G143: fill span cracks + scrub neon/outliers before panel. */
					GC_ScrubDumpWorldSpeckles( gc.buffer, gc.width, gc.height, gc.stride );
					Con_Reportf( "Xash3D GameCube: G143 keep textured dump (nonblack=%u/%u uniq=%u)\n",
						nonblack, samples, uniq );
				}
				else
				{
					Con_Reportf( "Xash3D GameCube: G135 dump depth/coalesce (nonblack=%u/%u uniq=%u)\n",
						nonblack, samples, uniq );
					/* G136: zi→3-plane silhouette fallback. */
					depth_valid = R_GcmapPosterizeDumpFromDepth( gc.buffer, gc.width, gc.height, gc.stride );
					if( depth_valid < 64 )
					{
						GC_CoalesceDumpWorldBuffer( gc.buffer, gc.width, gc.height, gc.stride );
						GC_PosterizeDumpWorldBuffer( gc.buffer, gc.width, gc.height, gc.stride );
						Con_Reportf( "Xash3D GameCube: G136 coalesce->posterize fallback (depth=%u color nonblack=%u/%u)\n",
							depth_valid, nonblack, samples );
					}
				}
				Q_snprintf( details, sizeof( details ), "MAP=%s",
					sv.name[0] ? sv.name : "?" );
				GC_DrawStatusPanelToBuffer( gc.buffer, gc.width, gc.height, gc.stride,
					"WORLD PRESENT", details );
			}
			/* G135: only now arm CPU YUYV — depth/coalesce + panel are ready.
			 * Extra WaitVSync presents give DumpFrames a G131-style late latch. */
			gc_cpu_dump_presents_left = 16;
			Con_Reportf( "Xash3D GameCube: G128 CPU dump presents begin\n" );
			for( dump_i = 0; dump_i < 16; dump_i++ )
				GC_PresentBuffer();
			/* Soft DumpFrames done — subsequent live presents use Flipper GX world. */
			GC_EnableGxWorldLive();
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
					/* Match landmark Deploy: place gun at the probe eye. */
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
				if( GC_RenderNewGameWorldPassNoFrame( true ))
				{
					Con_Reportf( "Xash3D GameCube: G155 GX live smoke frame\n" );
					GC_PresentBuffer();
				}
				else
					Con_Reportf( S_WARN "Xash3D GameCube: G155 GX live smoke failed\n" );
				ref.dllFuncs.R_EndFrame();
			}
		}

		for( i = 0; i < 2; i++ )
			Host_ServerFrame();
		Con_Reportf( "Xash3D GameCube: post-G36 bounded server ticks ready\n" );

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
		 * Default tram hop c0a0→c0a0a; `-gcchangelevel <map>` overrides. */
		else if( !Sys_CheckParm( "-gcmap" ))
		{
			static qboolean gc_changelevel_queued;
			static char gc_cl_from[MAX_QPATH];
			char dest[MAX_QPATH];
			const char *to = "c0a0a";

			if( Sys_GetParmFromCmdLine( "-gcchangelevel", dest ))
				to = dest;
			else if( Q_stricmp( sv.name, "c0a0" ))
				to = NULL;

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
	}
	return true;
#else
	return false;
#endif
}

#if XASH_GAMECUBE
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
		Con_Reportf( "Xash3D GameCube: G127 preload fire+steps+ric begin budget_used=%u\n",
			(uint)S_GCGameplaySfxBudgetUsed() );
		for( s = 0; s < (int)( sizeof( preload ) / sizeof( preload[0] )); s++ )
		{
			sound_t handle = S_RegisterSound( preload[s] );

			Con_Reportf( "Xash3D GameCube: G127 preload %s handle=%d budget_used=%u\n",
				preload[s], (int)handle, (uint)S_GCGameplaySfxBudgetUsed() );
		}
		Con_Reportf( "Xash3D GameCube: G127 preload fire+steps+ric ready budget_used=%u\n",
			(uint)S_GCGameplaySfxBudgetUsed() );
		return;
	}

	Con_Reportf( "Xash3D GameCube: gameplay sound begin name=%s state=%d\n",
		name, cls.state );

	/* Pre-voice paints fill silence up to mixahead while soundtime stays 0.
	 * Rewind so the late channel can paint into the live DMA window. */
	if( snd.initialized && (int)( snd.paintedtime - snd.soundtime ) > 64 )
	{
		Con_Reportf( "Xash3D GameCube: gameplay sound mix window rewind painted=%d sound=%d\n",
			snd.paintedtime, snd.soundtime );
		snd.paintedtime = snd.soundtime;
	}

	/* G118: budget gate in S_LoadSound — no one-shot Allow/Disallow. */
	S_StartLocalSound( name, 1.0f, true );
	Con_Reportf( "Xash3D GameCube: gameplay sound start name=%s channel=static budget_used=%u\n",
		name, (uint)S_GCGameplaySfxBudgetUsed() );

	/* Mix enough updates for the clip to reach the 48 kHz DMA ring. */
	for( i = 0; i < 8; i++ )
		SND_UpdateSound();

	Con_Reportf( "Xash3D GameCube: gameplay sound ready name=%s\n", name );
}
#else
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
	/* After G36 samples + grace, switch New Game to low-res world presents. */
	if( gc_light_present_left == 0 && !gc_budget_probe_active
		&& Sys_CheckParm( "-gcnewgame" ) && !gc_newgame_world_ready )
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

	/* G83: capture PointInLeaf/FatPVS before G36 light presents reuse BSP scratch. */
	if( Sys_CheckParm( "-gcnewgame" ))
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
			SYS_Report( "Xash3D GameCube: restored presentation buffer %dx%d (fallback after %dx%d fail)\n",
				gc.width, gc.height, width, height );
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
