/*
Copyright (C) 2026 Xash3D FWGS GameCube port

G151–G154: Flipper GX world draw — cap faces as EFB triangles + LM.
G155: Flipper GX studio/viewmodel via TriAPI → EFB overlay.
G157: viewmodel eye-pose sync + narrower Flipper FOV.
G178: cache world TEV/vtx + TEXMAP0 binds.
G179: lean sync — skip hot InvalidateTexAll, Flush not DrawDone, cache LM objs.
G180: pack face lightmaps into one TEXMAP1 atlas.
G181: cluster TEXMAP0 binds within area-order bands.
G182: Flipper GX 2D StretchPic for live HUD on EFB.
G183: pin HUD TEXMAP0 slots + TriColor tint; richer Flipper pic count.
G184: HUD RGB5A3 + alpha compare so SPR_DrawHoles drop transparent texels.
G185: cut Flipper HUD fill (lean crosshair cell + fill-px telemetry).
G186: skip tiny / far-small Flipper faces before ST/LM emit (fill lean).
G187: HUD holes punch near-black ink (crosshair sheet non-255 dark texels).
G201: guFrustum projection for GX_PERSPECTIVE (Xash GL proj was invisible).
G202: Flipper diffuse REPLACE (no LM crush on eye-centered quads).
G206: REPLACE on EDGE/TEX verts too (real ST shapes; defer LM until overbright).
G207: LM restored — boost bake, corner atlas UV, TEV ×2 overbright.
G203: plane-quad ST 0..1 + LINEAR world filter.
G213: live BSP30 opaque when surfaces pinned off scratch.
*/
#include "r_local.h"

#if XASH_GAMECUBE
#include <gccore.h>
#include <ogc/gx.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

extern qboolean GC_UseGxWorldDraw( void );
extern qboolean GC_UseGxRenderer( void );
extern void GC_MarkGxWorldEfbReady( void );
extern void *GC_GetGxVideoMode( void );
extern void GC_GetNewGameCapLightmapAtlasUV( int slot, float s, float t, float *out_s, float *out_t );
extern qboolean GC_WorldSurfacesLive( void );
extern int GC_GetLiveFaceCount( void );
extern qboolean GC_FillLiveDrawSurf( int index, msurface_t *out, mtexinfo_t *tex_out );
extern msurface_t *GC_GetLiveDrawSurfs( void );
extern unsigned R_GXGetTriColorRGBA( void );

#define GC_GX_TEX_SLOTS		32
#define GC_GX_TEX_HUD_RESERVE	4	/* last slots preferred for live HUD sheets */
#define GC_GX_TEX_MAX_DIM	64	/* MEM1: keep tiled staging lean (32×64²×2 working set) */
/* G199: 4 BSS world tiles (~32 KiB) — 8 tipped clipnodes pin on c0a0. */
#define GC_GX_TEX_WORLD_POOL	4
static u16 r_gx_tex_world_pool[GC_GX_TEX_WORLD_POOL][GC_GX_TEX_MAX_DIM * GC_GX_TEX_MAX_DIM]
	__attribute__((aligned( 32 )));

typedef struct
{
	unsigned	texnum;
	int		w, h;
	size_t		alloc_bytes;
	u8		fmt;	/* GX_TF_RGB565 or GX_TF_RGB5A3 */
	GXTexObj	obj;
	u16		*tiled;	/* MEM1 aligned */
	qboolean	valid;
	qboolean	hud_pin;	/* G183: do not LRU-evict for world binds */
} gc_gx_tex_t;

static gc_gx_tex_t r_gx_tex[GC_GX_TEX_SLOTS];
static int r_gx_tex_clock;
static int r_gx_tex_lru[GC_GX_TEX_SLOTS];
static model_t *r_gx_tex_world;
static qboolean r_gx_world_drew;
static qboolean r_gx_world_logged;
static qboolean r_gx_tex_logged;
static int r_gx_tex_draws;
static int r_gx_flat_draws;
/* G178: avoid re-emitting identical world TEV/vtx state for every face. */
enum
{
	GC_GX_FACE_MODE_NONE = 0,
	GC_GX_FACE_MODE_FLAT,
	GC_GX_FACE_MODE_TEXTURED,
	GC_GX_FACE_MODE_LIT
};
static int r_gx_face_mode;
static unsigned r_gx_bound_texnum;
static int r_gx_state_sets;
static int r_gx_state_reuses;
static int r_gx_tex_loads;
static int r_gx_tex_reuses;
static qboolean r_gx_state_cache_logged;
/* G179: lightmap atlas bind (G180) + lean texture invalidation. */
static GXTexObj r_gx_lm_atlas_obj;
static qboolean r_gx_lm_atlas_valid;
static int r_gx_lm_atlas_gen = -1;
static qboolean r_gx_lm_atlas_bound;
static int r_gx_efb_dump_hold; /* G200: keep Flipper EFB for DumpFramesAsImages */

/* Dolphin DumpFramesAsImages samples EFB asynchronously — clearing at the
 * start of the next world pass left solid sky dumps despite drawn=179. */
void R_GXHoldEfbForDump( int frames )
{
	if( frames > r_gx_efb_dump_hold )
		r_gx_efb_dump_hold = frames;
}
static int r_gx_cap_generation = -1;
static int r_gx_lm_inits;
static int r_gx_lm_loads;
static int r_gx_lm_reuses;
static int r_gx_tex_invalidates;
static qboolean r_gx_sync_lean_logged;
static qboolean r_gx_lm_atlas_logged;
static qboolean r_gx_tex_band_logged;
/* G186: Flipper world fill cull (extents dust + far-small). */
/* G186/G199: lean cull — prior 3072/512/8192 left outdoor wall-aim at ~20 faces. */
#ifndef GC_GX_MIN_FACE_AREA
#define GC_GX_MIN_FACE_AREA 1024	/* extents product; skip dust detail */
#endif
#ifndef GC_GX_FAR_FACE_DIST
#define GC_GX_FAR_FACE_DIST 2048.0f
#endif
#ifndef GC_GX_FAR_MIN_AREA
#define GC_GX_FAR_MIN_AREA 4096	/* keep medium walls when far */
#endif
static int r_gx_face_skips;
static int r_gx_face_skip_area;
static int r_gx_face_skip_far;
static qboolean r_gx_face_cull_logged;

/* G155 studio overlay */
static qboolean r_gx_studio_active;
static qboolean r_gx_studio_viewmodel;
static qboolean r_gx_studio_logged;
static qboolean r_gx_studio_vm_logged;
static int r_gx_studio_tris;
static int r_gx_studio_world_tris_acc;
static int r_gx_studio_vm_tris_acc;
static u32 r_gx_studio_color = 0xFFFFFFFF;
static unsigned r_gx_studio_bound_tex;
static unsigned r_gx_studio_shade_mask; /* G164: luminance buckets seen this pass */
static qboolean r_gx_studio_gouraud_logged;
static qboolean r_gx_studio_zrange_logged; /* G167 */
/* G157: NDC band of viewmodel verts (Quake-style clip / w). */
static matrix4x4 r_gx_studio_mvp;
static qboolean r_gx_studio_mvp_valid;
static float r_gx_vm_ndc_ymin = 1e9f;
static float r_gx_vm_ndc_ymax = -1e9f;
static int r_gx_vm_ndc_samples;
static float r_gx_vm_fov_x;
static qboolean r_gx_hud_2d_ready;
static qboolean r_gx_hud_2d_logged;
static qboolean r_gx_hud_rich_logged;
static qboolean r_gx_hud_holes_logged;
static qboolean r_gx_hud_fill_logged;
static qboolean r_gx_hud_pool_ready;
static int r_gx_hud_2d_pics;
static int r_gx_hud_holes_pics;
static int r_gx_hud_bind_fails;
static int r_gx_hud_fill_px;
static int r_gx_hud_holes_fill_px;
/* G183: keep convert scratch off the deep world→HUD stack. */
static u16 r_gx_tex_linear[GC_GX_TEX_MAX_DIM * GC_GX_TEX_MAX_DIM];

#define GC_GX_TEX_HUD_SLOT0	( GC_GX_TEX_SLOTS - GC_GX_TEX_HUD_RESERVE )

/* G184: soft TRANSPARENT_COLOR → RGB5A3 with alpha 0 (not opaque black).
 * G187: also punch near-black — crosshair sheets use dark non-255 ink. */
static int r_gx_hud_nearblack_punched;
static qboolean r_gx_hud_nearblack_logged;

static u16 R_GXSoftToRGB5A3( pixel_t soft )
{
	u16 rgb565;
	u32 r5, g6, b5, g5;

	if( soft == TRANSPARENT_COLOR )
		return 0;
	rgb565 = (u16)R_GCSoftToRGB565( soft );
	r5 = ( rgb565 >> 11 ) & 31u;
	g6 = ( rgb565 >> 5 ) & 63u;
	b5 = rgb565 & 31u;
	g5 = g6 >> 1;
	/* Near-black → α=0 so GX_GREATER alpha-compare drops the texel. */
	if( r5 <= 1u && g5 <= 1u && b5 <= 1u )
	{
		r_gx_hud_nearblack_punched++;
		return 0;
	}
	return (u16)( 0x8000u | ( r5 << 10 ) | ( g5 << 5 ) | b5 );
}

/*
 * G184: carve 4×64×64 TEXMAP0 slabs before world fills MEM1 so crosshair /
 * hud sheets never memalign-fail on the Flipper HUD pass.
 */
static void R_GXReserveHudPool( void )
{
	int i;
	const size_t bytes = (size_t)GC_GX_TEX_MAX_DIM * (size_t)GC_GX_TEX_MAX_DIM * sizeof( u16 );

	if( r_gx_hud_pool_ready )
		return;
	for( i = 0; i < GC_GX_TEX_HUD_RESERVE; i++ )
	{
		int slot = GC_GX_TEX_HUD_SLOT0 + i;

		if( r_gx_tex[slot].tiled && r_gx_tex[slot].alloc_bytes >= bytes )
			continue;
		if( r_gx_tex[slot].tiled )
		{
			free( r_gx_tex[slot].tiled );
			r_gx_tex[slot].tiled = NULL;
			r_gx_tex[slot].alloc_bytes = 0;
			r_gx_tex[slot].valid = false;
		}
		r_gx_tex[slot].tiled = (u16 *)memalign( 32, bytes );
		if( !r_gx_tex[slot].tiled )
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G184 HUD pool alloc fail slot=%d\n", slot );
			continue;
		}
		r_gx_tex[slot].alloc_bytes = bytes;
		r_gx_tex[slot].valid = false;
		r_gx_tex[slot].hud_pin = true;
	}
	r_gx_hud_pool_ready = true;
}

qboolean R_GXWorldDrewThisFrame( void )
{
	return r_gx_world_drew;
}

static void R_GXPrepareStudioState( qboolean viewmodel );

qboolean R_GXStudioIsActive( void )
{
	return r_gx_studio_active;
}

/* True while Flipper should consume TriAPI (studio or particles/sprites/beams). */
static qboolean r_gx_effects_tri;

qboolean R_GXTriApiIsActive( void )
{
	return r_gx_studio_active || r_gx_effects_tri;
}

void R_GXEffectsTriBegin( void )
{
	if( !GC_UseGxWorldDraw() )
		return;
	if( r_gx_studio_active )
		return;
	if( !r_gx_effects_tri )
	{
		R_GXPrepareStudioState( false );
		r_gx_effects_tri = true;
		r_gx_face_mode = GC_GX_FACE_MODE_NONE;
		r_gx_bound_texnum = 0;
		GC_MarkGxWorldEfbReady();
	}
}

void R_GXEffectsTriEnd( void )
{
	if( !r_gx_effects_tri )
		return;
	r_gx_effects_tri = false;
	GX_Flush();
}

int R_GXStudioLastTriCount( void )
{
	return r_gx_studio_tris;
}

static void R_GXLoadMtxFromXashMV( Mtx out, const matrix4x4 in )
{
	float gl[16];
	int i, j;

	Matrix4x4_ToArrayFloatGL( in, gl );
	for( i = 0; i < 3; i++ )
		for( j = 0; j < 4; j++ )
			out[i][j] = gl[j * 4 + i];
}

/*
 * G201: GX_LoadProjectionMtx(…, GX_PERSPECTIVE) expects a guFrustum /
 * guPerspective matrix — not an OpenGL-style Matrix4x4_CreateProjection.
 * Feeding Xash GL proj made drawn=179 faces invisible while a camera-space
 * guPerspective magenta marker still showed in DumpFrames.
 */
static void R_GXBuildWorldProjection( Mtx44 out )
{
	f32 zNear = 4.0f;
	f32 zFar = Q_max( 256.0f, RI.farClip > 1.0f ? RI.farClip : 4096.0f );
	f32 fov_y = RI.rvp.fov_y > 1.0f ? RI.rvp.fov_y : 90.0f;
	f32 fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	f32 yMax = zNear * tanf( fov_y * (f32)M_PI_F / 360.0f );
	f32 xMax = zNear * tanf( fov_x * (f32)M_PI_F / 360.0f );

	/* guFrustum(mt, top, bottom, left, right, near, far) */
	guFrustum( out, yMax, -yMax, -xMax, xMax, zNear, zFar );
}

/*
 * G201b: build GX modelview from Quake view basis directly.
 * Xash Matrix4x4_CreateModelview + R_GXLoadMtxFromXashMV still left walls
 * invisible under guFrustum (DumpFrames ~97% sky despite drawn=179).
 * Eye space: +X right, +Y up, -Z forward (libogc guPerspective expectation).
 */
static void R_GXBuildWorldModelview( Mtx out )
{
	vec3_t	forward, right, up;
	const float *org = RI.rvp.vieworigin;

	AngleVectors( RI.rvp.viewangles, forward, right, up );

	out[0][0] = right[0];
	out[0][1] = right[1];
	out[0][2] = right[2];
	out[0][3] = -DotProduct( right, org );

	out[1][0] = up[0];
	out[1][1] = up[1];
	out[1][2] = up[2];
	out[1][3] = -DotProduct( up, org );

	out[2][0] = -forward[0];
	out[2][1] = -forward[1];
	out[2][2] = -forward[2];
	out[2][3] = DotProduct( forward, org );
}

static u32 R_GXFaceColor( const msurface_t *surf )
{
	const char *name;
	u32 h = 0x2a4c6u;
	int i;

	if( surf->texinfo && surf->texinfo->texture && surf->texinfo->texture->name[0] )
		name = surf->texinfo->texture->name;
	else
		name = "flat";

	for( i = 0; name[i]; i++ )
		h = h * 131u + (u32)(byte)name[i];

	{
		u32 r = 64u + (( h >> 0 ) & 127u );
		u32 g = 64u + (( h >> 8 ) & 127u );
		u32 b = 64u + (( h >> 16 ) & 127u );
		return ( r << 24 ) | ( g << 16 ) | ( b << 8 ) | 0xFFu;
	}
}

static void R_GXSwizzleRGB565( const u16 *src, int stride, int w, int h, u16 *dst )
{
	int tile_x, tile_y, y;
	u16 *out = dst;

	for( tile_y = 0; tile_y < h; tile_y += 4 )
	{
		for( tile_x = 0; tile_x < w; tile_x += 4 )
		{
			for( y = 0; y < 4; y++ )
			{
				const u16 *row = src + ( tile_y + y ) * stride + tile_x;
				out[0] = row[0];
				out[1] = row[1];
				out[2] = row[2];
				out[3] = row[3];
				out += 4;
			}
		}
	}
}

static int R_GXSnapDim( int n )
{
	int d;

	if( n < 4 )
		return 4;
	if( n > GC_GX_TEX_MAX_DIM )
		n = GC_GX_TEX_MAX_DIM;
	/* Round down to multiple of 4 (GX_TF_RGB565 tile). */
	d = n & ~3;
	return d < 4 ? 4 : d;
}

static void R_GXTexCacheReset( void )
{
	int i;
	const size_t pool_bytes =
		(size_t)GC_GX_TEX_MAX_DIM * (size_t)GC_GX_TEX_MAX_DIM * sizeof( u16 );

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].tiled && i >= GC_GX_TEX_WORLD_POOL )
			free( r_gx_tex[i].tiled );
		memset( &r_gx_tex[i], 0, sizeof( r_gx_tex[i] ));
		r_gx_tex_lru[i] = 0;
		if( i < GC_GX_TEX_WORLD_POOL )
		{
			r_gx_tex[i].tiled = r_gx_tex_world_pool[i];
			r_gx_tex[i].alloc_bytes = pool_bytes;
		}
	}
	r_gx_tex_clock = 0;
	r_gx_tex_world = NULL;
	r_gx_hud_pool_ready = false;
}

static gc_gx_tex_t *R_GXBindTexnum( unsigned texnum, qboolean hud_bind )
{
	image_t *img;
	int i, slot, victim, src_w, src_h, dst_w, dst_h;
	int x, y, step_x, step_y;
	size_t bytes;
	gc_gx_tex_t *t;
	const u8 want_fmt = hud_bind ? (u8)GX_TF_RGB5A3 : (u8)GX_TF_RGB565;

	if( texnum == 0 )
		return NULL;

	img = R_GetTexture( texnum );
	if( !img || !img->pixels[0] || img->width < 1 || img->height < 1 )
		return NULL;

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].valid && r_gx_tex[i].texnum == texnum
			&& r_gx_tex[i].fmt == want_fmt )
		{
			r_gx_tex_lru[i] = ++r_gx_tex_clock;
			if( hud_bind )
				r_gx_tex[i].hud_pin = true;
			if( r_gx_bound_texnum != texnum )
			{
				GX_LoadTexObj( &r_gx_tex[i].obj, GX_TEXMAP0 );
				r_gx_bound_texnum = texnum;
				r_gx_tex_loads++;
			}
			else
				r_gx_tex_reuses++;
			return &r_gx_tex[i];
		}
	}

	/* Prefer free slot; HUD only uses reserved high slots (preallocated). */
	slot = -1;
	victim = -1;
	if( hud_bind )
	{
		R_GXReserveHudPool();
		for( i = GC_GX_TEX_HUD_SLOT0; i < GC_GX_TEX_SLOTS; i++ )
		{
			if( !r_gx_tex[i].valid )
			{
				slot = i;
				break;
			}
		}
		if( slot < 0 )
		{
			for( i = GC_GX_TEX_HUD_SLOT0; i < GC_GX_TEX_SLOTS; i++ )
			{
				if( victim < 0 || r_gx_tex_lru[i] < r_gx_tex_lru[victim] )
					victim = i;
			}
			slot = victim;
		}
	}
	else
	{
		/* G199: world diffuse only uses BSS pool — never heap memalign. */
		for( i = 0; i < GC_GX_TEX_WORLD_POOL; i++ )
		{
			if( !r_gx_tex[i].valid )
			{
				slot = i;
				break;
			}
		}
		if( slot < 0 )
		{
			for( i = 0; i < GC_GX_TEX_WORLD_POOL; i++ )
			{
				if( victim < 0 || r_gx_tex_lru[i] < r_gx_tex_lru[victim] )
					victim = i;
			}
			if( victim < 0 )
				victim = 0;
			slot = victim;
		}
	}

	t = &r_gx_tex[slot];

	src_w = img->width;
	src_h = img->height;
	dst_w = R_GXSnapDim( src_w );
	dst_h = R_GXSnapDim( src_h );
	step_x = src_w / dst_w;
	step_y = src_h / dst_h;
	if( step_x < 1 )
		step_x = 1;
	if( step_y < 1 )
		step_y = 1;

	for( y = 0; y < dst_h; y++ )
	{
		int sy = y * step_y;
		if( sy >= src_h )
			sy = src_h - 1;
		for( x = 0; x < dst_w; x++ )
		{
			int sx = x * step_x;
			pixel_t soft;

			if( sx >= src_w )
				sx = src_w - 1;
			soft = img->pixels[0][sy * src_w + sx];
			if( hud_bind )
				r_gx_tex_linear[y * dst_w + x] = R_GXSoftToRGB5A3( soft );
			else if( soft == TRANSPARENT_COLOR )
				r_gx_tex_linear[y * dst_w + x] = 0;
			else
				r_gx_tex_linear[y * dst_w + x] = (u16)R_GCSoftToRGB565( soft );
		}
	}

	bytes = (size_t)dst_w * (size_t)dst_h * sizeof( u16 );
	/*
	 * G183: never free-before-alloc — MEM1 is fragmented after world upload.
	 * Reuse any staging buffer large enough (HUD snaps are ≤ 64×64).
	 */
	{
		const size_t max_tile = (size_t)GC_GX_TEX_MAX_DIM * (size_t)GC_GX_TEX_MAX_DIM * sizeof( u16 );
		size_t want = bytes;

		/* HUD uploads prefer a full 64×64 staging slab so later sheets reuse. */
		if( hud_bind && want < max_tile )
			want = max_tile;

		if( slot < GC_GX_TEX_WORLD_POOL )
		{
			t->tiled = r_gx_tex_world_pool[slot];
			t->alloc_bytes = max_tile;
		}
		else if( !t->tiled || t->alloc_bytes < bytes )
		{
			u16 *neu = (u16 *)memalign( 32, want );

			if( !neu )
			{
				if( !t->tiled || t->alloc_bytes < bytes )
				{
					r_gx_hud_bind_fails++;
					return NULL;
				}
			}
			else
			{
				if( t->tiled )
					free( t->tiled );
				t->tiled = neu;
				t->alloc_bytes = want;
			}
		}
	}

	R_GXSwizzleRGB565( r_gx_tex_linear, dst_w, dst_w, dst_h, t->tiled );
	DCFlushRange( t->tiled, (u32)bytes );

	GX_InitTexObj( &t->obj, t->tiled, (u16)dst_w, (u16)dst_h,
		want_fmt, GX_REPEAT, GX_REPEAT, GX_FALSE );
	/* G203: LINEAR on world diffuse; HUD stays NEAR for crisp sprites. */
	if( hud_bind )
		GX_InitTexObjFilterMode( &t->obj, GX_NEAR, GX_NEAR );
	else
		GX_InitTexObjFilterMode( &t->obj, GX_LINEAR, GX_LINEAR );
	t->texnum = texnum;
	t->w = dst_w;
	t->h = dst_h;
	t->fmt = want_fmt;
	t->valid = true;
	t->hud_pin = hud_bind;
	r_gx_tex_lru[slot] = ++r_gx_tex_clock;
	/* New tiled texels — invalidate once here, not every world pass. */
	GX_InvalidateTexAll();
	r_gx_tex_invalidates++;
	GX_LoadTexObj( &t->obj, GX_TEXMAP0 );
	r_gx_bound_texnum = texnum;
	r_gx_tex_loads++;
	return t;
}

static gc_gx_tex_t *R_GXBindTexture( texture_t *mt )
{
	gc_gx_tex_t *gxt;

	if( !mt || mt->gl_texturenum <= 0 )
		return NULL;
	/* World faces must not inherit HUD RGB5A3/slot mode from StretchPic. */
	gxt = R_GXBindTexnum( (unsigned)mt->gl_texturenum, false );
	if( !gxt )
	{
		static qboolean bind_fail_logged;
		image_t *img;

		if( !bind_fail_logged )
		{
			bind_fail_logged = true;
			img = R_GetTexture( (unsigned)mt->gl_texturenum );
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G198 GX tex bind fail texnum=%d name=%s pixels=%d %dx%d\n",
				mt->gl_texturenum, mt->name[0] ? mt->name : "?",
				( img && img->pixels[0] ) ? 1 : 0,
				img ? img->width : 0, img ? img->height : 0 );
		}
	}
	return gxt;
}

/*
================
G181: keep area-major order (coverage) but stable-sort by texture inside
coarse bands so TEXMAP0 runs cluster without a full texture-only reorder.
================
*/
#define GC_GX_TEX_BANDS 8

static unsigned R_GXSurfTexKey( const msurface_t *surf )
{
	if( !surf || !surf->texinfo || !surf->texinfo->texture )
		return 0u;
	return (unsigned)surf->texinfo->texture->gl_texturenum;
}

static void R_GXOrderFacesByTexBands( const msurface_t *draw, int n, int *order )
{
	int b, i, j;

	for( i = 0; i < n; i++ )
		order[i] = i;

	for( b = 0; b < GC_GX_TEX_BANDS; b++ )
	{
		const int band0 = ( b * n ) / GC_GX_TEX_BANDS;
		const int band1 = (( b + 1 ) * n ) / GC_GX_TEX_BANDS;

		for( i = band0 + 1; i < band1; i++ )
		{
			const int slot = order[i];
			const unsigned key = R_GXSurfTexKey( &draw[slot] );

			j = i;
			while( j > band0 && R_GXSurfTexKey( &draw[order[j - 1]] ) > key )
			{
				order[j] = order[j - 1];
				j--;
			}
			order[j] = slot;
		}
	}
}

static void R_GXClearEfbSky( GXRModeObj *rmode )
{
	Mtx44 ortho;
	Mtx ident;
	const f32 fb_w = (f32)rmode->fbWidth;
	const f32 fb_h = (f32)rmode->efbHeight;
	const u32 sky = 0x5A8CD2FFu;

	/* Color-only fill — never write Z here. Ortho depths poison the later
	 * perspective LEQUAL test (DumpFrames stayed solid sky with drawn=179). */
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetNumChans( 1 );
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );

	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( fb_w, 0.0f, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( fb_w, fb_h, -0.5f );
	GX_Color1u32( sky );
	GX_Position3f32( 0.0f, fb_h, -0.5f );
	GX_Color1u32( sky );
	GX_End();
}

static void R_GXClearDepthPerspective( GXRModeObj *rmode )
{
	Mtx44 ortho;
	Mtx ident;
	const f32 fb_w = (f32)rmode->fbWidth;
	const f32 fb_h = (f32)rmode->efbHeight;

	/* Far-Z fill under a throwaway ortho so perspective world LEQUAL starts clean. */
	GX_SetColorUpdate( GX_FALSE );
	GX_SetZMode( GX_TRUE, GX_ALWAYS, GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetNumChans( 0 );
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	guOrtho( ortho, 0.0f, fb_h, 0.0f, fb_w, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );
	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( 0.0f, 0.0f, -1.0f );
	GX_Position3f32( fb_w, 0.0f, -1.0f );
	GX_Position3f32( fb_w, fb_h, -1.0f );
	GX_Position3f32( 0.0f, fb_h, -1.0f );
	GX_End();
	GX_SetColorUpdate( GX_TRUE );
}

static void R_GXSetupWorld3DState( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( !rmode )
		return;

	if( r_gx_efb_dump_hold > 0 )
		r_gx_efb_dump_hold--;
	else
	{
		R_GXClearEfbSky( rmode );
		R_GXClearDepthPerspective( rmode );
	}

	GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );
	/* G210: EDGE verts restore BSP winding — HW backface cull on again. */
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetCullMode( GX_CULL_BACK );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );

	/* Default untextured; textured faces enable TEX0 per draw. */
	GX_SetNumTexGens( 0 );
	GX_SetNumTevStages( 1 );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );

	/* G201: native GX frustum + Quake view basis (not Xash GL matrices). */
	R_GXBuildWorldProjection( proj );
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXBuildWorldModelview( mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

	/* Optional camera-space marker: -gcmagenta (proved EFB→DumpFrames in G200). */
	if( gEngfuncs.Sys_CheckParm( "-gcmagenta" ))
	{
		static int diag_left = 8;
		if( diag_left > 0 )
		{
			Mtx44 persp;
			Mtx ident;
			diag_left--;
			guPerspective( persp, 60.0f, (f32)rmode->fbWidth / Q_max( 1.0f, (f32)rmode->efbHeight ), 1.0f, 4096.0f );
			GX_LoadProjectionMtx( persp, GX_PERSPECTIVE );
			guMtxIdentity( ident );
			GX_LoadPosMtxImm( ident, GX_PNMTX0 );
			GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
			GX_SetCullMode( GX_CULL_NONE );
			GX_SetNumChans( 1 );
			GX_SetNumTexGens( 0 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
			GX_Position3f32( -80.0f, -60.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_Position3f32( 80.0f, -60.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_Position3f32( 0.0f, 70.0f, -120.0f );
			GX_Color1u32( 0xFF00FFFFu );
			GX_End();
			if( diag_left == 7 )
				gEngfuncs.Con_Reportf( "Xash3D GameCube: G200 Flipper camera-space magenta diag\n" );
			GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
			GX_LoadPosMtxImm( mv, GX_PNMTX0 );
			GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
		}
	}
	else
	{
		static qboolean g201_logged;
		if( !g201_logged && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
		{
			vec3_t	forward, right, up;
			g201_logged = true;
			AngleVectors( RI.rvp.viewangles, forward, right, up );
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G201 Flipper guFrustum+lookAt fov_y=%.1f eye=(%.0f,%.0f,%.0f) fwd=(%.2f,%.2f,%.2f)\n",
				RI.rvp.fov_y,
				RI.rvp.vieworigin[0], RI.rvp.vieworigin[1], RI.rvp.vieworigin[2],
				forward[0], forward[1], forward[2] );
		}
	}

	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
	GX_InvVtxCache();
	/* G179: InvalidateTexAll only on cap rewrite / tex upload, not every pass. */
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_lm_atlas_bound = false;
}

static void R_GXFaceST( const msurface_t *surf, const float *pos, float *s, float *t )
{
	const mtexinfo_t *ti = surf->texinfo;
	float tw, th;

	*s = DotProduct( pos, ti->vecs[0] ) + ti->vecs[0][3];
	*t = DotProduct( pos, ti->vecs[1] ) + ti->vecs[1][3];

	tw = ( ti->texture && ti->texture->width > 0 ) ? (float)ti->texture->width : 64.0f;
	th = ( ti->texture && ti->texture->height > 0 ) ? (float)ti->texture->height : 64.0f;
	*s /= tw;
	*t /= th;
}

static void R_GXFaceLMST( const msurface_t *surf, const float *pos, float *s, float *t )
{
	const mextrasurf_t *info = surf->info;
	float lw, lh;
	int sample_size = 16;

	if( !info )
	{
		*s = *t = 0.0f;
		return;
	}
	if( gEngfuncs.Mod_SampleSizeForFace )
		sample_size = gEngfuncs.Mod_SampleSizeForFace( surf );
	if( sample_size < 1 )
		sample_size = 16;

	*s = DotProduct( pos, info->lmvecs[0] ) + info->lmvecs[0][3] - info->lightmapmins[0];
	*t = DotProduct( pos, info->lmvecs[1] ) + info->lmvecs[1][3] - info->lightmapmins[1];
	lw = (float)( info->lightextents[0] + sample_size );
	lh = (float)( info->lightextents[1] + sample_size );
	if( lw < 1.0f )
		lw = 1.0f;
	if( lh < 1.0f )
		lh = 1.0f;
	*s /= lw;
	*t /= lh;
}

static qboolean R_GXBindLightmapAtlas( void )
{
	extern const unsigned short *GC_GetNewGameCapLightmapAtlas( int *w, int *h );
	extern int GC_GetNewGameCapGeneration( void );
	const unsigned short *atlas;
	int w = 0, h = 0;
	int gen;

	atlas = GC_GetNewGameCapLightmapAtlas( &w, &h );
	if( !atlas || w < 4 || h < 4 )
		return false;

	gen = GC_GetNewGameCapGeneration();
	if( !r_gx_lm_atlas_valid || gen != r_gx_lm_atlas_gen )
	{
		GX_InitTexObj( &r_gx_lm_atlas_obj, (void *)atlas, (u16)w, (u16)h,
			GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
		GX_InitTexObjFilterMode( &r_gx_lm_atlas_obj, GX_LINEAR, GX_LINEAR );
		r_gx_lm_atlas_valid = true;
		r_gx_lm_atlas_gen = gen;
		r_gx_lm_inits++;
	}

	if( !r_gx_lm_atlas_bound )
	{
		GX_LoadTexObj( &r_gx_lm_atlas_obj, GX_TEXMAP1 );
		r_gx_lm_atlas_bound = true;
		r_gx_lm_loads++;
	}
	else
		r_gx_lm_reuses++;
	return true;
}

static int r_gx_lm_draws;
static qboolean r_gx_lm_logged;

static int R_GXEmitFace( const msurface_t *surf, model_t *world, int slot )
{
	mvertex_t *pverts;
	vec3_t pts[32];
	float sts[32][2];
	float lmst[32][2];
	int nverts = 0;
	int i;
	u32 color;
	gc_gx_tex_t *gxt = NULL;
	qboolean textured = false;
	qboolean lit = false;

	if( !surf || !world )
		return 0;

	/* G199: prefer capture-baked verts — live edges/surfedges dangle after scratch. */
	if( slot >= 0 )
	{
		extern int GC_GetNewGameCapFaceVerts( int slot, float out[][3], int maxverts );
		nverts = GC_GetNewGameCapFaceVerts( slot, pts, 32 );
	}
	if( nverts < 3 )
	{
		if( !world->vertexes || !world->surfedges )
			return 0;
		if( surf->numedges < 3 || surf->numedges > 32 )
			return 0;
		if( !world->edges16 && !world->edges32 )
			return 0;

		pverts = world->vertexes;
		nverts = 0;
		for( i = 0; i < surf->numedges; i++ )
		{
			int lindex = world->surfedges[surf->firstedge + i];
			int v;

			if( world->edges32 )
			{
				medge32_t *e = ( lindex > 0 ) ? &world->edges32[lindex] : &world->edges32[-lindex];
				v = ( lindex > 0 ) ? e->v[0] : e->v[1];
			}
			else
			{
				medge16_t *e = ( lindex > 0 ) ? &world->edges16[lindex] : &world->edges16[-lindex];
				v = ( lindex > 0 ) ? e->v[0] : e->v[1];
			}
			if( v < 0 || v >= world->numvertexes )
				return 0;
			VectorCopy( pverts[v].position, pts[nverts] );
			nverts++;
		}
	}

	if( nverts < 3 )
		return 0;

	color = 0xFFFFFFFFu;
	if( surf->texinfo && surf->texinfo->texture )
	{
		gxt = R_GXBindTexture( surf->texinfo->texture );
		if( gxt )
		{
			textured = true;
			for( i = 0; i < nverts; i++ )
			{
				float ls, lt;

				R_GXFaceST( surf, pts[i], &sts[i][0], &sts[i][1] );
				if( slot >= 0 )
				{
					/* G207: 4-vert TEX/EDGE quads match the downsampled 4×4
					 * face LM tile — map bake corners 0..1 into the atlas cell
					 * instead of lmvecs (often mismatch → near-black samples). */
					if( nverts == 4 )
					{
						static const float corner_uv[4][2] = {
							{ 0.0f, 0.0f }, { 1.0f, 0.0f },
							{ 1.0f, 1.0f }, { 0.0f, 1.0f }
						};
						ls = corner_uv[i][0];
						lt = corner_uv[i][1];
					}
					else
						R_GXFaceLMST( surf, pts[i], &ls, &lt );
					GC_GetNewGameCapLightmapAtlasUV( slot, ls, lt, &lmst[i][0], &lmst[i][1] );
				}
			}
			/* Cap atlas lightmaps only when we have a matching slot. */
			lit = ( slot >= 0 ) ? R_GXBindLightmapAtlas() : false;
			/* G207: re-enable LM for EDGE/TEX (boosted bake + corner atlas UV).
			 * Plane-fallback quads still REPLACE — LM ST meaningless there. */
			if( lit && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
			{
				extern int GC_GetNewGameCapBakeSrc( int slot );
				int bake = GC_GetNewGameCapBakeSrc( slot );
				if( bake != 1 && bake != 3 )
				{
					static qboolean g202_logged;
					lit = false;
					if( !g202_logged )
					{
						g202_logged = true;
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: G202 Flipper REPLACE (plane bake; EDGE/TEX use LM)\n" );
					}
				}
				else
				{
					static qboolean g207_logged;
					if( !g207_logged )
					{
						g207_logged = true;
						gEngfuncs.Con_Reportf(
							"Xash3D GameCube: G209 Flipper LM on EDGE/TEX (boost*3 + TEV*4)\n" );
					}
				}
			}
		}
	}
	if( !textured )
		color = R_GXFaceColor( surf );
	else if( slot >= 0 && nverts == 4 )
	{
		extern int GC_GetNewGameCapBakeSrc( int slot );
		int bake = GC_GetNewGameCapBakeSrc( slot );
		/* G203/G205: only force 0..1 ST on plane-fallback quads. */
		if( bake != 1 && bake != 3 )
		{
			static const float plane_uv[4][2] = {
				{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
			};
			static qboolean g203_logged;
			for( i = 0; i < 4; i++ )
			{
				sts[i][0] = plane_uv[i][0];
				sts[i][1] = plane_uv[i][1];
			}
			if( !g203_logged && gEngfuncs.Sys_CheckParm( "-gcnewgame" ))
			{
				g203_logged = true;
				gEngfuncs.Con_Reportf( "Xash3D GameCube: G203 Flipper plane-quad ST 0..1\n" );
			}
		}
	}

	/* Turbulent / translucent surfaces: textured + alpha blend, no LM. */
	if( textured && ( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT )))
	{
		lit = false;
		if( surf->flags & SURF_TRANSPARENT )
			color = 0xFFFFFFC0u;
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_FALSE );
	}

	if( textured && lit )
	{
		if( r_gx_face_mode != GC_GX_FACE_MODE_LIT )
		{
			GX_SetNumTexGens( 2 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
			GX_SetNumTevStages( 2 );
			/* Stage0: diffuse REPLACE (ignore vertex color dimming). */
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL );
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			/* Stage1: * LM with G209 overbright (×4) toward REPLACE brightness. */
			GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
			GX_SetTevColorIn( GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO );
			GX_SetTevColorOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV );
			GX_SetTevAlphaIn( GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV );
			GX_SetTevAlphaOp( GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX1, GX_DIRECT );
			GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0 );
			r_gx_face_mode = GC_GX_FACE_MODE_LIT;
			r_gx_state_sets++;
		}
		else
			r_gx_state_reuses++;

		GX_Begin( GX_TRIANGLES, GX_VTXFMT0, (u16)(( nverts - 2 ) * 3 ));
		for( i = 1; i < nverts - 1; i++ )
		{
			GX_Position3f32( pts[0][0], pts[0][1], pts[0][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[0][0], sts[0][1] );
			GX_TexCoord2f32( lmst[0][0], lmst[0][1] );
			GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i][0], sts[i][1] );
			GX_TexCoord2f32( lmst[i][0], lmst[i][1] );
			GX_Position3f32( pts[i + 1][0], pts[i + 1][1], pts[i + 1][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i + 1][0], sts[i + 1][1] );
			GX_TexCoord2f32( lmst[i + 1][0], lmst[i + 1][1] );
		}
		GX_End();
		r_gx_tex_draws++;
		r_gx_lm_draws++;
	}
	else if( textured )
	{
		if( r_gx_face_mode != GC_GX_FACE_MODE_TEXTURED )
		{
			GX_SetNumTexGens( 1 );
			GX_SetNumTevStages( 1 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
			/* G202: REPLACE shows the diffuse sheet clearly. */
			GX_SetTevOp( GX_TEVSTAGE0, GX_REPLACE );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
			r_gx_face_mode = GC_GX_FACE_MODE_TEXTURED;
			r_gx_state_sets++;
		}
		else
			r_gx_state_reuses++;

		GX_Begin( GX_TRIANGLES, GX_VTXFMT0, (u16)(( nverts - 2 ) * 3 ));
		for( i = 1; i < nverts - 1; i++ )
		{
			GX_Position3f32( pts[0][0], pts[0][1], pts[0][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[0][0], sts[0][1] );
			GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i][0], sts[i][1] );
			GX_Position3f32( pts[i + 1][0], pts[i + 1][1], pts[i + 1][2] );
			GX_Color1u32( color );
			GX_TexCoord2f32( sts[i + 1][0], sts[i + 1][1] );
		}
		GX_End();
		r_gx_tex_draws++;
	}
	else
	{
		if( r_gx_face_mode != GC_GX_FACE_MODE_FLAT )
		{
			GX_SetNumTexGens( 0 );
			GX_SetNumTevStages( 1 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
			GX_ClearVtxDesc();
			GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
			GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
			r_gx_face_mode = GC_GX_FACE_MODE_FLAT;
			r_gx_state_sets++;
		}
		else
			r_gx_state_reuses++;

		GX_Begin( GX_TRIANGLES, GX_VTXFMT0, (u16)(( nverts - 2 ) * 3 ));
		for( i = 1; i < nverts - 1; i++ )
		{
			GX_Position3f32( pts[0][0], pts[0][1], pts[0][2] );
			GX_Color1u32( color );
			GX_Position3f32( pts[i][0], pts[i][1], pts[i][2] );
			GX_Color1u32( color );
			GX_Position3f32( pts[i + 1][0], pts[i + 1][1], pts[i + 1][2] );
			GX_Color1u32( color );
		}
		GX_End();
		r_gx_flat_draws++;
	}

	/* Restore opaque depth/blend after translucent faces. */
	if( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT ))
	{
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	return 1;
}

/*
================
R_GXMarkVisibleSurfaces

Stamp surf->visframe from marked leaves (MarkLeaves only tags nodes/leaves;
soft RecursiveWorldNode used to stamp surfaces during the edge walk).
================
*/
static void R_GXMarkVisibleSurfaces( model_t *world )
{
	int i;

	if( !world || !world->leafs || world->numleafs <= 0 )
		return;

	for( i = 0; i < world->numleafs; i++ )
	{
		mleaf_t *leaf = &world->leafs[i + 1];
		msurface_t **mark;
		int c;

		if(((mnode_t *)leaf )->visframe != tr.visframecount )
			continue;
		mark = leaf->firstmarksurface;
		c = leaf->nummarksurfaces;
		if( !mark || c <= 0 )
			continue;
		do
		{
			( *mark )->visframe = tr.framecount;
			mark++;
		}
		while( --c );
	}
}

/*
================
R_GXDrawWorldLiveSurfaces

Per-frame BSP/PVS helpers. When lightmapped cap faces already cover opaque
geometry, only emit water/translucents here. When no cap is available, emit
all marked opaque surfaces textured (no LM atlas).
================
*/
static int R_GXDrawWorldLiveSurfaces( model_t *world, qboolean opaque_too )
{
	msurface_t *surf;
	int i, drawn = 0;
	int opaque_drawn = 0;
	int trans_drawn = 0;
	static qboolean live_logged;

	if( !world || !world->surfaces || world->numsurfaces <= 0 )
		return 0;

	/* Opaque pass — only when cap atlas is unavailable. */
	if( opaque_too )
	{
		/* G212/G213: raise Flipper live BSP emit toward full PVS when pinned. */
		int emit_budget = GC_WorldSurfacesLive() ? 2048 : 768;

		for( i = 0; i < world->numsurfaces && emit_budget > 0; i++ )
		{
			float dot;

			surf = &world->surfaces[i];
			if( surf->visframe != tr.framecount )
				continue;
			if( !surf->plane || surf->numedges < 3 )
				continue;
			if( surf->flags & SURF_DRAWSKY )
				continue;
			if( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT ))
				continue;

			dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
			if( surf->flags & SURF_PLANEBACK )
			{
				if( dot > -BACKFACE_EPSILON )
					continue;
			}
			else if( dot < BACKFACE_EPSILON )
				continue;

			/* Prefer closer / larger walls under the emit budget. */
			{
				int area = (int)surf->extents[0] * (int)surf->extents[1];
				if( area > 0 && area < GC_GX_MIN_FACE_AREA )
					continue;
				if( area > 0 && area < GC_GX_FAR_MIN_AREA
					&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
					continue;
			}

			opaque_drawn += R_GXEmitFace( surf, world, -1 );
			emit_budget--;
		}
	}

	/* Translucent / water pass (no Z write) — always from live PVS. */
	for( i = 0; i < world->numsurfaces; i++ )
	{
		float dot;

		surf = &world->surfaces[i];
		if( surf->visframe != tr.framecount )
			continue;
		if( !surf->plane || surf->numedges < 3 )
			continue;
		if( !( surf->flags & ( SURF_DRAWTURB | SURF_TRANSPARENT )))
			continue;
		if( surf->flags & SURF_DRAWSKY )
			continue;

		dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
		if( surf->flags & SURF_PLANEBACK )
		{
			if( dot > -BACKFACE_EPSILON )
				continue;
		}
		else if( dot < BACKFACE_EPSILON )
			continue;

		trans_drawn += R_GXEmitFace( surf, world, -1 );
	}

	drawn = opaque_drawn + trans_drawn;
	if( !live_logged && drawn > 0 )
	{
		live_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: pure GX live BSP faces opaque=%d trans=%d frame=%d\n",
			opaque_drawn, trans_drawn, tr.framecount );
	}
	return drawn;
}

int R_GXDrawNewGameCapFaces( void )
{
	extern int GC_GetNewGameCapFaceCount( void );
	extern int GC_GetNewGameCapGeneration( void );
	extern msurface_t *GC_GetNewGameDrawSurfs( void );
	msurface_t *draw;
	model_t *world;
	int n, i, drawn = 0;
	int gen;
	int order[320];

	if( !GC_UseGxWorldDraw() )
		return 0;

	/* Reserve HUD TEXMAP0 slabs before world uploads fill MEM1. */
	R_GXReserveHudPool();

	world = WORLDMODEL;
	if( !world )
		return 0;

	if( r_gx_tex_world != world )
	{
		R_GXTexCacheReset();
		R_GXReserveHudPool();
		r_gx_tex_world = world;
		r_gx_tex_logged = false;
		r_gx_lm_logged = false;
		r_gx_sync_lean_logged = false;
		r_gx_lm_atlas_logged = false;
		r_gx_tex_band_logged = false;
		r_gx_face_cull_logged = false;
	}

	gen = GC_GetNewGameCapGeneration();
	if( gen != r_gx_cap_generation )
	{
		GX_InvalidateTexAll();
		r_gx_tex_invalidates++;
		r_gx_lm_atlas_valid = false;
		r_gx_lm_atlas_bound = false;
		r_gx_cap_generation = gen;
	}

	RI.currentmodel = world;
	r_gx_tex_draws = 0;
	r_gx_flat_draws = 0;
	r_gx_lm_draws = 0;
	r_gx_state_sets = 0;
	r_gx_state_reuses = 0;
	r_gx_tex_loads = 0;
	r_gx_tex_reuses = 0;
	r_gx_lm_inits = 0;
	r_gx_lm_loads = 0;
	r_gx_lm_reuses = 0;
	r_gx_face_skips = 0;
	r_gx_face_skip_area = 0;
	r_gx_face_skip_far = 0;
	R_GXSetupWorld3DState();

	/* Stamp marksurfaces only when leaf→surface links are still valid.
	 * After G132 scratch reuse on -gcnewgame, MarkLeaves already full-stamped
	 * surf->visframe; walking dangling firstmarksurface hangs the guest. */
	if( !( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_UseLowResWorldProbe() ))
		R_GXMarkVisibleSurfaces( world );

	draw = GC_GetNewGameDrawSurfs();
	n = GC_GetNewGameCapFaceCount();

	/* G213: when surfaces are pinned off scratch, walk live BSP30 via surfbits
	 * visframe (MarkLeaves). Cap faces remain LM fallback if promote OOM'd. */
	drawn = 0;
	if( GC_WorldSurfacesLive() )
	{
		static qboolean g213_logged;
		int live_n = GC_GetLiveFaceCount();

		/* Lean early-pool faces (G213) — walk mempool edges; full promote may OOM. */
		if( live_n > 0 )
		{
			int li, live_drawn = 0;

			for( li = 0; li < live_n; li++ )
			{
				float dot;
				msurface_t surf;
				mtexinfo_t tex;

				if( !GC_FillLiveDrawSurf( li, &surf, &tex ))
					continue;
				if( !surf.plane || surf.numedges < 3 )
					continue;
				dot = DotProduct( tr.modelorg, surf.plane->normal ) - surf.plane->dist;
				if( surf.flags & SURF_PLANEBACK )
				{
					if( dot > -BACKFACE_EPSILON )
						continue;
				}
				else if( dot < BACKFACE_EPSILON )
					continue;
				live_drawn += R_GXEmitFace( &surf, world, -1 );
			}
			drawn += live_drawn;
			if( !g213_logged && live_drawn > 0 )
			{
				g213_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G213 live Flipper faces=%d of %d (lean early pool, edges mempool)\n",
					live_drawn, live_n );
			}
		}
		else
		{
			drawn = R_GXDrawWorldLiveSurfaces( world, true );
			if( !g213_logged && drawn > 0 )
			{
				g213_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G213 live BSP Flipper opaque=%d (surfaces pinned)\n",
					drawn );
			}
		}
		/* Also emit near-eye cap faces with LM atlas for readable walls. */
		if( draw && n > 0 )
		{
			int backface_skips = 0;
			int emit_fails = 0;
			int cap_drawn = 0;

			if( n > (int)( sizeof( order ) / sizeof( order[0] )))
				n = (int)( sizeof( order ) / sizeof( order[0] ));

			R_GXOrderFacesByTexBands( draw, n, order );

			for( i = 0; i < n; i++ )
			{
				float dot;
				int area;
				int got;
				const int slot = order[i];
				msurface_t *surf = &draw[slot];

				if( !surf->plane )
				{
					emit_fails++;
					continue;
				}
				dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
				if( surf->flags & SURF_PLANEBACK )
				{
					if( dot > -BACKFACE_EPSILON )
					{
						backface_skips++;
						continue;
					}
				}
				else if( dot < BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
				area = (int)surf->extents[0] * (int)surf->extents[1];
				if( area > 0 && area < GC_GX_MIN_FACE_AREA )
				{
					r_gx_face_skips++;
					r_gx_face_skip_area++;
					continue;
				}
				if( area > 0 && area < GC_GX_FAR_MIN_AREA
					&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
				{
					r_gx_face_skips++;
					r_gx_face_skip_far++;
					continue;
				}
				got = R_GXEmitFace( surf, world, slot );
				if( got <= 0 )
					emit_fails++;
				else
					cap_drawn += got;
			}
			drawn += cap_drawn;
			(void)backface_skips;
			(void)emit_fails;
		}
	}
	else if( draw && n > 0 )
	{
		int backface_skips = 0;
		int emit_fails = 0;

		if( n > (int)( sizeof( order ) / sizeof( order[0] )))
			n = (int)( sizeof( order ) / sizeof( order[0] ));

		R_GXOrderFacesByTexBands( draw, n, order );

		for( i = 0; i < n; i++ )
		{
			float dot;
			int area;
			int got;
			const int slot = order[i];
			msurface_t *surf = &draw[slot];

			if( !surf->plane )
			{
				emit_fails++;
				continue;
			}
			dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
			if( surf->flags & SURF_PLANEBACK )
			{
				if( dot > -BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
			}
			else
			{
				if( dot < BACKFACE_EPSILON )
				{
					backface_skips++;
					continue;
				}
			}
			area = (int)surf->extents[0] * (int)surf->extents[1];
			if( area > 0 && area < GC_GX_MIN_FACE_AREA )
			{
				r_gx_face_skips++;
				r_gx_face_skip_area++;
				continue;
			}
			if( area > 0 && area < GC_GX_FAR_MIN_AREA
				&& fabsf( dot ) > GC_GX_FAR_FACE_DIST )
			{
				r_gx_face_skips++;
				r_gx_face_skip_far++;
				continue;
			}
			got = R_GXEmitFace( surf, world, slot );
			if( got <= 0 )
				emit_fails++;
			else
				drawn += got;
		}
		if( !r_gx_world_logged && ( drawn > 0 || backface_skips > 0 || emit_fails > 0 ))
		{
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G199 GX face filter drawn=%d backface=%d emit_fail=%d cull=%d of %d\n",
				drawn, backface_skips, emit_fails, r_gx_face_skips, n );
		}
	}
	else if( !( gEngfuncs.Sys_CheckParm( "-gcnewgame" ) && GC_UseLowResWorldProbe() ))
	{
		/* Retail / non-scratch path: live BSP/PVS textured emit. */
		drawn = R_GXDrawWorldLiveSurfaces( world, true );
	}
	else
	{
		static qboolean sky_only_logged;
		/* Cap empty + surfaces unpromoted: still present sky-cleared EFB. */
		if( !sky_only_logged )
		{
			sky_only_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: pure GX sky-only present (cap=0, surfaces unpromoted)\n" );
		}
		drawn = 1; /* mark EFB ready so CopyDisp shows sky clear */
	}

	/* Also draw brush entities already marked on the edge list path via
	 * R_DrawBEntities — handled later by studio/brush GX entity hooks. */

	GX_Flush();
	r_gx_world_drew = ( drawn > 0 );
	if( r_gx_world_drew )
		GC_MarkGxWorldEfbReady();

	if( !r_gx_world_logged && drawn > 0 )
	{
		r_gx_world_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G151 GX world faces drawn=%d of %d (Flipper EFB)\n",
			drawn, n );
	}
	if( !r_gx_tex_logged && drawn > 0 )
	{
		r_gx_tex_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G152 GX textured faces=%d flat=%d (Flipper TEV)\n",
			r_gx_tex_draws, r_gx_flat_draws );
	}
	if( !r_gx_lm_logged && drawn > 0 )
	{
		r_gx_lm_logged = true;
		gEngfuncs.Con_Reportf( "Xash3D GameCube: G154 GX lightmapped faces=%d of %d (Flipper TEV2)\n",
			r_gx_lm_draws, drawn );
	}
	if( !r_gx_state_cache_logged && drawn > 0 && r_gx_state_reuses > 0 )
	{
		r_gx_state_cache_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G178 GX world state cache faces=%d sets=%d reuses=%d texloads=%d texreuses=%d\n",
			drawn, r_gx_state_sets, r_gx_state_reuses, r_gx_tex_loads, r_gx_tex_reuses );
	}
	if( !r_gx_sync_lean_logged && drawn > 0 )
	{
		r_gx_sync_lean_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G179 GX world sync lean faces=%d lm_inits=%d lm_loads=%d lm_reuses=%d tex_inv=%d flush=1\n",
			drawn, r_gx_lm_inits, r_gx_lm_loads, r_gx_lm_reuses, r_gx_tex_invalidates );
	}
	if( !r_gx_lm_atlas_logged && drawn > 0 && r_gx_lm_loads > 0 && r_gx_lm_loads <= 2 )
	{
		r_gx_lm_atlas_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G180 GX lightmap atlas faces=%d lm_inits=%d lm_loads=%d lm_reuses=%d\n",
			drawn, r_gx_lm_inits, r_gx_lm_loads, r_gx_lm_reuses );
	}
	if( !r_gx_tex_band_logged && drawn > 0 )
	{
		r_gx_tex_band_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G181 GX tex band order faces=%d bands=%d texloads=%d texreuses=%d\n",
			drawn, GC_GX_TEX_BANDS, r_gx_tex_loads, r_gx_tex_reuses );
	}
	if( !r_gx_face_cull_logged && drawn > 0 )
	{
		r_gx_face_cull_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G186 GX Flipper face cull skips=%d area=%d far=%d drawn=%d min_area=%d\n",
			r_gx_face_skips, r_gx_face_skip_area, r_gx_face_skip_far, drawn,
			GC_GX_MIN_FACE_AREA );
	}
	return drawn;
}

/*
================
G155: Flipper studio / viewmodel overlay (TriAPI → EFB)
================
*/
static void R_GXStudioNoteNdcVert( float x, float y, float z )
{
	float clip_y, clip_w, ndc_y;

	if( !r_gx_studio_viewmodel || !r_gx_studio_mvp_valid )
		return;

	clip_y = r_gx_studio_mvp[1][0] * x + r_gx_studio_mvp[1][1] * y
		+ r_gx_studio_mvp[1][2] * z + r_gx_studio_mvp[1][3];
	clip_w = r_gx_studio_mvp[3][0] * x + r_gx_studio_mvp[3][1] * y
		+ r_gx_studio_mvp[3][2] * z + r_gx_studio_mvp[3][3];
	if( fabsf( clip_w ) < 1e-5f )
		return;
	ndc_y = clip_y / clip_w;
	if( ndc_y < r_gx_vm_ndc_ymin )
		r_gx_vm_ndc_ymin = ndc_y;
	if( ndc_y > r_gx_vm_ndc_ymax )
		r_gx_vm_ndc_ymax = ndc_y;
	r_gx_vm_ndc_samples++;
}

static void R_GXPrepareStudioState( qboolean viewmodel )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( rmode )
	{
		/* Viewport near/far filled below (G167 compresses viewmodel depth). */
		GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
		GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );
		GX_SetColorUpdate( GX_TRUE );
	}

	/* Drop world lightmap stage; studio is TEX0 MODULATE only. */
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetCullMode( viewmodel ? GX_CULL_NONE : GX_CULL_BACK );
	/*
	 * G167: match GL studio viewmodel depth — glDepthRange(min, min+0.3*(max-min)).
	 * Z-always (G155) never buried the gun in walls but also never clipped it
	 * when looking into geometry. Compress EFB depth so the gun still wins
	 * against mid/far world while nearby walls can occlude it.
	 */
	if( viewmodel )
	{
		if( rmode )
			GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
				0.0f, 0.3f );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	else
	{
		if( rmode )
			GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
				0.0f, 1.0f );
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	}
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	r_gx_studio_mvp_valid = false;
	r_gx_vm_fov_x = RI.rvp.fov_x > 1.0f ? RI.rvp.fov_x : 90.0f;
	/* G201: same guFrustum world projection for studio/viewmodel. */
	R_GXBuildWorldProjection( proj );
	if( viewmodel )
	{
		/* G157: eye-pose sync frames the gun; keep Xash MVP for NDC telemetry. */
		Matrix4x4_Concat( r_gx_studio_mvp, RI.projectionMatrix, RI.worldviewMatrix );
		r_gx_studio_mvp_valid = true;
		r_gx_vm_ndc_ymin = 1e9f;
		r_gx_vm_ndc_ymax = -1e9f;
		r_gx_vm_ndc_samples = 0;
	}

	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXBuildWorldModelview( mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );
}

void R_GXStudioBegin( qboolean viewmodel )
{
	if( !GC_UseGxWorldDraw() )
		return;

	r_gx_studio_active = true;
	r_gx_studio_viewmodel = viewmodel;
	r_gx_studio_tris = 0;
	r_gx_studio_bound_tex = 0;
	r_gx_studio_color = 0xFFFFFFFFu;
	r_gx_studio_shade_mask = 0;
	R_GXPrepareStudioState( viewmodel );
}

void R_GXStudioEnd( void )
{
	GXRModeObj *rmode;

	if( !r_gx_studio_active )
		return;

	GX_Flush();
	/* Restore full EFB depth range after a compressed viewmodel pass. */
	rmode = (GXRModeObj *)GC_GetGxVideoMode();
	if( rmode )
		GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
			0.0f, 1.0f );
	GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );

	if( r_gx_studio_tris > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		/* G156: accumulate world vs viewmodel — forced roach often draws first
		 * and would otherwise own the one-shot log with viewmodel=0. */
		if( r_gx_studio_viewmodel )
			r_gx_studio_vm_tris_acc += r_gx_studio_tris;
		else
			r_gx_studio_world_tris_acc += r_gx_studio_tris;
		if( r_gx_studio_viewmodel && !r_gx_studio_vm_logged )
		{
			r_gx_studio_vm_logged = true;
			r_gx_studio_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G155 GX studio tris=%d viewmodel=1 (Flipper EFB)\n",
				r_gx_studio_vm_tris_acc );
			if( r_gx_vm_ndc_samples > 0 )
			{
				float mid = 0.5f * ( r_gx_vm_ndc_ymin + r_gx_vm_ndc_ymax );
				float span = r_gx_vm_ndc_ymax - r_gx_vm_ndc_ymin;
				/* Quake clip: +ndc_y is up. Hands should sit in the lower
				 * half with a visible on-screen band (ymax > -1).
				 * G162: also require a useful on-screen span (ymin > -1.6). */
				int lower = ( mid < 0.0f && r_gx_vm_ndc_ymax > -1.0f
					&& span > 0.15f ) ? 1 : 0;
				int framed = ( lower && r_gx_vm_ndc_ymin > -1.6f
					&& r_gx_vm_ndc_ymax > -0.55f ) ? 1 : 0;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G157 viewmodel fov=%.0f ndc_y=[%.2f,%.2f] mid=%.2f lower=%d samples=%d\n",
					r_gx_vm_fov_x, r_gx_vm_ndc_ymin, r_gx_vm_ndc_ymax, mid, lower,
					r_gx_vm_ndc_samples );
				if( framed )
					gEngfuncs.Con_Reportf(
						"Xash3D GameCube: G162 viewmodel framed ndc_y=[%.2f,%.2f] mid=%.2f\n",
						r_gx_vm_ndc_ymin, r_gx_vm_ndc_ymax, mid );
			}
			/* G164: distinct luminance buckets prove per-vertex Gouraud (flat = 1). */
			if( !r_gx_studio_gouraud_logged )
			{
				unsigned mask = r_gx_studio_shade_mask;
				int shades = 0;

				while( mask )
				{
					shades += (int)( mask & 1u );
					mask >>= 1;
				}
				r_gx_studio_gouraud_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G164 studio gouraud shades=%d mask=0x%08x viewmodel=1\n",
					shades, r_gx_studio_shade_mask );
			}
			/* G167: prove compressed depth range (not G155 Z-always overlay). */
			if( !r_gx_studio_zrange_logged )
			{
				r_gx_studio_zrange_logged = true;
				gEngfuncs.Con_Reportf(
					"Xash3D GameCube: G167 viewmodel depth range near=0.00 far=0.30 ztest=1\n" );
			}
		}
		else if( !r_gx_studio_logged )
		{
			r_gx_studio_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G155 GX studio tris=%d viewmodel=0 (Flipper EFB)\n",
				r_gx_studio_world_tris_acc );
		}
	}
	r_gx_studio_active = false;
}

void R_GXStudioBindTexnum( unsigned texnum )
{
	if( !r_gx_studio_active )
		return;
	if( texnum == r_gx_studio_bound_tex && texnum != 0 )
		return;
	if( R_GXBindTexnum( texnum, false ))
		r_gx_studio_bound_tex = texnum;
}

void R_GXStudioTexCoord( float u, float v )
{
	(void)u;
	(void)v;
	/* UVs passed per-vertex through TriAPI → R_GXStudioEmitTri. */
}

void R_GXStudioColor( unsigned light8 )
{
	/* light is 0–31 from TriAPI soft path (<<8 in TriVertex). Approximate grey. */
	unsigned l = light8 & 0xFFu;
	unsigned c;

	if( l > 31 )
		l = 31;
	c = ( l * 255u ) / 31u;
	r_gx_studio_color = ( c << 24 ) | ( c << 16 ) | ( c << 8 ) | 0xFFu;
}

/* G164: track distinct vertex shades so Gouraud vs flat is provable in logs. */
static void R_GXStudioNoteShade( unsigned rgba )
{
	unsigned lum = ((( rgba >> 24 ) & 0xFFu ) + (( rgba >> 16 ) & 0xFFu )
		+ (( rgba >> 8 ) & 0xFFu )) / 3u;

	r_gx_studio_shade_mask |= 1u << ( lum >> 3 ); /* 32 luminance buckets */
}

void R_GXStudioEmitTriC(
	float x0, float y0, float z0, float u0, float v0, unsigned c0,
	float x1, float y1, float z1, float u1, float v1, unsigned c1,
	float x2, float y2, float z2, float u2, float v2, unsigned c2 )
{
	if( !r_gx_studio_active )
		return;

	if( r_gx_studio_bound_tex == 0 )
		R_GXStudioBindTexnum( (unsigned)tr.whiteTexture );

	R_GXStudioNoteNdcVert( x0, y0, z0 );
	R_GXStudioNoteNdcVert( x1, y1, z1 );
	R_GXStudioNoteNdcVert( x2, y2, z2 );
	R_GXStudioNoteShade( c0 );
	R_GXStudioNoteShade( c1 );
	R_GXStudioNoteShade( c2 );

	GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
	GX_Position3f32( x0, y0, z0 );
	GX_Color1u32( c0 );
	GX_TexCoord2f32( u0, v0 );
	GX_Position3f32( x1, y1, z1 );
	GX_Color1u32( c1 );
	GX_TexCoord2f32( u1, v1 );
	GX_Position3f32( x2, y2, z2 );
	GX_Color1u32( c2 );
	GX_TexCoord2f32( u2, v2 );
	GX_End();
	r_gx_studio_tris++;
}

void R_GXStudioEmitTri(
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2 )
{
	R_GXStudioEmitTriC(
		x0, y0, z0, u0, v0, r_gx_studio_color,
		x1, y1, z1, u1, v1, r_gx_studio_color,
		x2, y2, z2, u2, v2, r_gx_studio_color );
}

/*
================
G182: live HUD / 2D into Flipper EFB (soft StretchPic is discarded when
CopyDisp presents the world EFB).
================
*/
static void R_GXPrepareHud2DState( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 ortho;
	Mtx ident;
	f32 vb_w, vb_h;

	if( !rmode || vid.width < 1 || vid.height < 1 )
		return;

	vb_w = (f32)rmode->fbWidth;
	vb_h = (f32)rmode->efbHeight;
	GX_SetViewport( 0.0f, 0.0f, vb_w, vb_h, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	GX_SetCullMode( GX_CULL_NONE );
	GX_SetColorUpdate( GX_TRUE );
	GX_SetNumChans( 1 );
	GX_SetChanCtrl( GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
		GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE );
	GX_SetNumTexGens( 1 );
	GX_SetNumTevStages( 1 );
	GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
	GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
	GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
	/* Default: no hole punch until a holes/alpha StretchPic. */
	GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	GX_ClearVtxDesc();
	GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
	GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0 );
	GX_SetVtxAttrFmt( GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0 );

	/* Match soft FB coordinates (y down) onto the full EFB. */
	guOrtho( ortho, 0.0f, (f32)vid.height, 0.0f, (f32)vid.width, 0.0f, 1.0f );
	GX_LoadProjectionMtx( ortho, GX_ORTHOGRAPHIC );
	guMtxIdentity( ident );
	GX_LoadPosMtxImm( ident, GX_PNMTX0 );

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_lm_atlas_bound = false;
	r_gx_hud_2d_ready = true;
}

qboolean R_GXDrawStretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int texnum )
{
	gc_gx_tex_t *gxt;
	u32 color;
	int alpha;
	f32 x0, y0, x1, y1;
	qboolean holes;

	if( !GC_UseGxRenderer() )
		return false;
	if( texnum <= 0 || w < 1.0f || h < 1.0f )
		return false;

	if( !r_gx_hud_2d_ready )
		R_GXPrepareHud2DState();
	if( !r_gx_hud_2d_ready )
		return false;

	gxt = R_GXBindTexnum( (unsigned)texnum, true );
	if( !gxt )
		return false;

	alpha = vid.alpha;
	if( alpha < 0 )
		alpha = 0;
	else if( alpha > 7 )
		alpha = 7;
	/* G183: modulate with TriColor (SPR_Set tint), not flat white. */
	color = R_GXGetTriColorRGBA();
	color = ( color & 0xFFFFFF00u ) | (u32)(( alpha * 255 ) / 7 );

	/* G184: SPR_DrawHoles / TransAlpha — punch RGB5A3 alpha==0 texels. */
	holes = ( vid.rendermode == kRenderTransColor
		|| vid.rendermode == kRenderTransAlpha );
	if( holes )
		GX_SetAlphaCompare( GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0 );
	else
		GX_SetAlphaCompare( GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0 );

	if( vid.rendermode == kRenderTransAdd )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_NOOP );
	else if( vid.rendermode == kRenderTransTexture
		|| vid.rendermode == kRenderTransAlpha
		|| vid.rendermode == kRenderTransColor
		|| alpha < 7 )
		GX_SetBlendMode( GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP );
	else
		GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

	x0 = x;
	y0 = y;
	x1 = x + w;
	y1 = y + h;

	GX_Begin( GX_QUADS, GX_VTXFMT0, 4 );
	GX_Position3f32( x0, y0, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s1, t1 );
	GX_Position3f32( x1, y0, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s2, t1 );
	GX_Position3f32( x1, y1, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s2, t2 );
	GX_Position3f32( x0, y1, 0.0f );
	GX_Color1u32( color );
	GX_TexCoord2f32( s1, t2 );
	GX_End();

	r_gx_hud_2d_pics++;
	{
		const int px = (int)( w * h );

		if( px > 0 )
			r_gx_hud_fill_px += px;
		if( holes )
		{
			r_gx_hud_holes_pics++;
			if( px > 0 )
				r_gx_hud_holes_fill_px += px;
		}
	}
	return true;
}

/*
=============
R_GXDrawBrushModel

Flipper path for doors / trains / func_ brushes — emit model surfaces with
entity transform applied via GX modelview (object * worldview).
=============
*/
int R_GXDrawBrushModel( cl_entity_t *e )
{
	model_t *mod;
	msurface_t *psurf;
	int i, drawn = 0;
	Mtx mv, obj, world;
	matrix4x4 xash_obj;

	if( !GC_UseGxWorldDraw() || !e || !e->model )
		return 0;
	mod = e->model;
	if( mod->type != mod_brush || mod->nummodelsurfaces <= 0 || !mod->surfaces )
		return 0;

	/* Entity modelview: worldview * object */
	Matrix4x4_CreateFromEntity( xash_obj, e->angles, e->origin, 1.0f );
	R_GXLoadMtxFromXashMV( obj, xash_obj );
	R_GXLoadMtxFromXashMV( world, RI.worldviewMatrix );
	guMtxConcat( world, obj, mv );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

	RI.currententity = e;
	RI.currentmodel = mod;
	VectorSubtract( RI.rvp.vieworigin, e->origin, tr.modelorg );

	r_gx_face_mode = GC_GX_FACE_MODE_NONE;
	r_gx_bound_texnum = 0;
	r_gx_lm_atlas_bound = false;

	psurf = &mod->surfaces[mod->firstmodelsurface];
	for( i = 0; i < mod->nummodelsurfaces; i++, psurf++ )
	{
		float dot;

		if( !psurf->plane || psurf->numedges < 3 )
			continue;
		if( psurf->flags & SURF_DRAWSKY )
			continue;

		dot = DotProduct( tr.modelorg, psurf->plane->normal ) - psurf->plane->dist;
		if( psurf->flags & SURF_PLANEBACK )
		{
			if( dot > -BACKFACE_EPSILON )
				continue;
		}
		else if( dot < BACKFACE_EPSILON )
			continue;

		drawn += R_GXEmitFace( psurf, mod, -1 );
	}

	/* Restore world modelview for subsequent draws. */
	R_GXLoadMtxFromXashMV( world, RI.worldviewMatrix );
	GX_LoadPosMtxImm( world, GX_PNMTX0 );
	r_gx_face_mode = GC_GX_FACE_MODE_NONE;

	if( drawn > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		GX_Flush();
	}
	return drawn;
}

void R_GXClearWorldDrewFlag( void )
{
	if( !r_gx_hud_2d_logged && r_gx_hud_2d_pics > 0 )
	{
		r_gx_hud_2d_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G182 GX HUD stretch pics=%d (Flipper EFB 2D)\n",
			r_gx_hud_2d_pics );
	}
	if( !r_gx_hud_rich_logged && r_gx_hud_2d_pics >= 8 )
	{
		r_gx_hud_rich_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G183 GX HUD rich pics=%d (Flipper EFB 2D)\n",
			r_gx_hud_2d_pics );
	}
	if( !r_gx_hud_holes_logged && r_gx_hud_holes_pics > 0 )
	{
		r_gx_hud_holes_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G184 GX HUD alpha holes pics=%d (RGB5A3)\n",
			r_gx_hud_holes_pics );
	}
	if( !r_gx_hud_nearblack_logged && r_gx_hud_nearblack_punched > 0
		&& r_gx_hud_holes_pics > 0 )
	{
		r_gx_hud_nearblack_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G187 GX HUD nearblack holes punched=%d pics=%d\n",
			r_gx_hud_nearblack_punched, r_gx_hud_holes_pics );
	}
	/* G185: full-sheet cross was ≥4096 px holes-fill; cell target ≪ that. */
	if( !r_gx_hud_fill_logged && r_gx_hud_2d_pics >= 8 && r_gx_hud_holes_fill_px > 0
		&& r_gx_hud_holes_fill_px < 4096 )
	{
		r_gx_hud_fill_logged = true;
		gEngfuncs.Con_Reportf(
			"Xash3D GameCube: G185 GX HUD fill lean px=%d holes_px=%d pics=%d\n",
			r_gx_hud_fill_px, r_gx_hud_holes_fill_px, r_gx_hud_2d_pics );
	}
	r_gx_hud_bind_fails = 0;
	r_gx_world_drew = false;
	r_gx_hud_2d_ready = false;
	r_gx_hud_2d_pics = 0;
	r_gx_hud_holes_pics = 0;
	r_gx_hud_fill_px = 0;
	r_gx_hud_holes_fill_px = 0;
}

#else /* !XASH_GAMECUBE */

qboolean R_GXWorldDrewThisFrame( void ) { return false; }
void R_GXClearWorldDrewFlag( void ) {}
qboolean R_GXDrawStretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, int texnum )
{
	(void)x; (void)y; (void)w; (void)h;
	(void)s1; (void)t1; (void)s2; (void)t2; (void)texnum;
	return false;
}
int R_GXDrawNewGameCapFaces( void ) { return 0; }
qboolean R_GXStudioIsActive( void ) { return false; }
qboolean R_GXTriApiIsActive( void ) { return false; }
void R_GXEffectsTriBegin( void ) {}
void R_GXEffectsTriEnd( void ) {}
int R_GXDrawBrushModel( cl_entity_t *e ) { (void)e; return 0; }
void R_GXStudioBegin( qboolean viewmodel ) { (void)viewmodel; }
void R_GXStudioEnd( void ) {}
void R_GXStudioBindTexnum( unsigned texnum ) { (void)texnum; }
void R_GXStudioTexCoord( float u, float v ) { (void)u; (void)v; }
void R_GXStudioColor( unsigned light8 ) { (void)light8; }
void R_GXStudioEmitTri(
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2 )
{
	(void)x0; (void)y0; (void)z0; (void)u0; (void)v0;
	(void)x1; (void)y1; (void)z1; (void)u1; (void)v1;
	(void)x2; (void)y2; (void)z2; (void)u2; (void)v2;
}
void R_GXStudioEmitTriC(
	float x0, float y0, float z0, float u0, float v0, unsigned c0,
	float x1, float y1, float z1, float u1, float v1, unsigned c1,
	float x2, float y2, float z2, float u2, float v2, unsigned c2 )
{
	(void)x0; (void)y0; (void)z0; (void)u0; (void)v0; (void)c0;
	(void)x1; (void)y1; (void)z1; (void)u1; (void)v1; (void)c1;
	(void)x2; (void)y2; (void)z2; (void)u2; (void)v2; (void)c2;
}

#endif
