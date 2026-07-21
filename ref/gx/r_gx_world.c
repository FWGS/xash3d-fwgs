/*
Copyright (C) 2026 Xash3D FWGS GameCube port

G151–G154: Flipper GX world draw — cap faces as EFB triangles + LM.
G155: Flipper GX studio/viewmodel via TriAPI → EFB overlay.
G157: viewmodel eye-pose sync + narrower Flipper FOV.
G178: cache world TEV/vtx + TEXMAP0 binds.
G179: lean sync — skip hot InvalidateTexAll, Flush not DrawDone, cache LM objs.
G180: pack face lightmaps into one TEXMAP1 atlas.
G181: cluster TEXMAP0 binds within area-order bands.
*/
#include "r_local.h"

#if XASH_GAMECUBE
#include <gccore.h>
#include <ogc/gx.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

extern qboolean GC_UseGxWorldDraw( void );
extern void GC_MarkGxWorldEfbReady( void );
extern void *GC_GetGxVideoMode( void );
extern void GC_GetNewGameCapLightmapAtlasUV( int slot, float s, float t, float *out_s, float *out_t );

#define GC_GX_TEX_SLOTS		24
#define GC_GX_TEX_MAX_DIM	64	/* MEM1: 24 × 64×64×2 ≈ 192 KiB tiled staging */

typedef struct
{
	unsigned	texnum;
	int		w, h;
	GXTexObj	obj;
	u16		*tiled;	/* MEM1 aligned */
	qboolean	valid;
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
static int r_gx_cap_generation = -1;
static int r_gx_lm_inits;
static int r_gx_lm_loads;
static int r_gx_lm_reuses;
static int r_gx_tex_invalidates;
static qboolean r_gx_sync_lean_logged;
static qboolean r_gx_lm_atlas_logged;
static qboolean r_gx_tex_band_logged;

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

qboolean R_GXWorldDrewThisFrame( void )
{
	return r_gx_world_drew;
}

void R_GXClearWorldDrewFlag( void )
{
	r_gx_world_drew = false;
}

qboolean R_GXStudioIsActive( void )
{
	return r_gx_studio_active;
}

int R_GXStudioLastTriCount( void )
{
	return r_gx_studio_tris;
}

static void R_GXLoadMtx44FromXash( Mtx44 out, const matrix4x4 in )
{
	float gl[16];
	int i, j;

	Matrix4x4_ToArrayFloatGL( in, gl );
	for( i = 0; i < 4; i++ )
		for( j = 0; j < 4; j++ )
			out[i][j] = gl[j * 4 + i];
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

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].tiled )
			free( r_gx_tex[i].tiled );
		memset( &r_gx_tex[i], 0, sizeof( r_gx_tex[i] ));
		r_gx_tex_lru[i] = 0;
	}
	r_gx_tex_clock = 0;
	r_gx_tex_world = NULL;
}

static gc_gx_tex_t *R_GXBindTexnum( unsigned texnum )
{
	image_t *img;
	int i, slot, victim, src_w, src_h, dst_w, dst_h;
	int x, y, step_x, step_y;
	u16 linear[GC_GX_TEX_MAX_DIM * GC_GX_TEX_MAX_DIM];
	size_t bytes;
	gc_gx_tex_t *t;

	if( texnum == 0 )
		return NULL;

	img = R_GetTexture( texnum );
	if( !img || !img->pixels[0] || img->width < 1 || img->height < 1 )
		return NULL;

	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( r_gx_tex[i].valid && r_gx_tex[i].texnum == texnum )
		{
			r_gx_tex_lru[i] = ++r_gx_tex_clock;
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

	/* LRU free slot */
	slot = -1;
	victim = 0;
	for( i = 0; i < GC_GX_TEX_SLOTS; i++ )
	{
		if( !r_gx_tex[i].valid )
		{
			slot = i;
			break;
		}
		if( r_gx_tex_lru[i] < r_gx_tex_lru[victim] )
			victim = i;
	}
	if( slot < 0 )
		slot = victim;

	t = &r_gx_tex[slot];
	if( t->tiled )
	{
		free( t->tiled );
		t->tiled = NULL;
	}
	memset( t, 0, sizeof( *t ));

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
			if( soft == TRANSPARENT_COLOR )
				linear[y * dst_w + x] = 0;
			else
				linear[y * dst_w + x] = (u16)R_GCSoftToRGB565( soft );
		}
	}

	bytes = (size_t)dst_w * (size_t)dst_h * sizeof( u16 );
	t->tiled = (u16 *)memalign( 32, bytes );
	if( !t->tiled )
		return NULL;

	R_GXSwizzleRGB565( linear, dst_w, dst_w, dst_h, t->tiled );
	DCFlushRange( t->tiled, (u32)bytes );

	GX_InitTexObj( &t->obj, t->tiled, (u16)dst_w, (u16)dst_h,
		GX_TF_RGB565, GX_REPEAT, GX_REPEAT, GX_FALSE );
	GX_InitTexObjFilterMode( &t->obj, GX_NEAR, GX_NEAR );
	t->texnum = texnum;
	t->w = dst_w;
	t->h = dst_h;
	t->valid = true;
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
	if( !mt || mt->gl_texturenum <= 0 )
		return NULL;
	return R_GXBindTexnum( (unsigned)mt->gl_texturenum );
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

	GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_TRUE );
	GX_SetCullMode( GX_CULL_NONE );
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

static void R_GXSetupWorld3DState( void )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( !rmode )
		return;

	R_GXClearEfbSky( rmode );

	GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
	GX_SetScissor( 0, 0, rmode->fbWidth, rmode->efbHeight );
	GX_SetPixelFmt( GX_PF_RGB565_Z16, GX_ZC_LINEAR );
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

	R_GXLoadMtx44FromXash( proj, RI.projectionMatrix );
	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXLoadMtxFromXashMV( mv, RI.worldviewMatrix );
	GX_LoadPosMtxImm( mv, GX_PNMTX0 );

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
	medge16_t *pedges;
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

	if( !surf || !world || !world->vertexes || !world->surfedges )
		return 0;
	if( surf->numedges < 3 || surf->numedges > 32 )
		return 0;

	pedges = world->edges16;
	if( !pedges )
		return 0;
	pverts = world->vertexes;

	for( i = 0; i < surf->numedges; i++ )
	{
		int lindex = world->surfedges[surf->firstedge + i];
		medge16_t *e;
		int v;

		if( lindex > 0 )
		{
			e = &pedges[lindex];
			v = e->v[0];
		}
		else
		{
			e = &pedges[-lindex];
			v = e->v[1];
		}
		if( v < 0 || v >= world->numvertexes )
			return 0;
		VectorCopy( pverts[v].position, pts[nverts] );
		nverts++;
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
				R_GXFaceLMST( surf, pts[i], &ls, &lt );
				GC_GetNewGameCapLightmapAtlasUV( slot, ls, lt, &lmst[i][0], &lmst[i][1] );
			}
			lit = R_GXBindLightmapAtlas();
		}
	}
	if( !textured )
		color = R_GXFaceColor( surf );

	if( textured && lit )
	{
		if( r_gx_face_mode != GC_GX_FACE_MODE_LIT )
		{
			GX_SetNumTexGens( 2 );
			GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
			GX_SetTexCoordGen( GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY );
			GX_SetNumTevStages( 2 );
			GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
			GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
			GX_SetTevOrder( GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL );
			GX_SetTevOp( GX_TEVSTAGE1, GX_MODULATE );
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
			GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
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
	return 1;
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

	world = WORLDMODEL;
	draw = GC_GetNewGameDrawSurfs();
	n = GC_GetNewGameCapFaceCount();
	if( !world || !draw || n <= 0 )
		return 0;
	if( n > (int)( sizeof( order ) / sizeof( order[0] )))
		n = (int)( sizeof( order ) / sizeof( order[0] ));

	if( r_gx_tex_world != world )
	{
		R_GXTexCacheReset();
		r_gx_tex_world = world;
		r_gx_tex_logged = false;
		r_gx_lm_logged = false;
		r_gx_sync_lean_logged = false;
		r_gx_lm_atlas_logged = false;
		r_gx_tex_band_logged = false;
	}

	gen = GC_GetNewGameCapGeneration();
	if( gen != r_gx_cap_generation )
	{
		/* Cap faces/LMs rewrote — invalidate once; atlas texobj rebuilds on bind. */
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
	R_GXSetupWorld3DState();

	R_GXOrderFacesByTexBands( draw, n, order );

	for( i = 0; i < n; i++ )
	{
		float dot;
		const int slot = order[i];
		msurface_t *surf = &draw[slot];

		if( !surf->plane )
			continue;
		dot = DotProduct( tr.modelorg, surf->plane->normal ) - surf->plane->dist;
		if( surf->flags & SURF_PLANEBACK )
		{
			if( dot > -BACKFACE_EPSILON )
				continue;
		}
		else
		{
			if( dot < BACKFACE_EPSILON )
				continue;
		}
		/* slot stays the atlas/LM index — only draw order changes. */
		drawn += R_GXEmitFace( surf, world, slot );
	}

	/* G179: defer GPU sync to present — Flush keeps the pipe moving. */
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
	if( viewmodel )
	{
		/* G157: same projection as the world; eye-pose sync frames the gun. */
		R_GXLoadMtx44FromXash( proj, RI.projectionMatrix );
		Matrix4x4_Concat( r_gx_studio_mvp, RI.projectionMatrix, RI.worldviewMatrix );
		r_gx_studio_mvp_valid = true;
		r_gx_vm_ndc_ymin = 1e9f;
		r_gx_vm_ndc_ymax = -1e9f;
		r_gx_vm_ndc_samples = 0;
	}
	else
		R_GXLoadMtx44FromXash( proj, RI.projectionMatrix );

	GX_LoadProjectionMtx( proj, GX_PERSPECTIVE );
	R_GXLoadMtxFromXashMV( mv, RI.worldviewMatrix );
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

	GX_DrawDone();
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
	if( R_GXBindTexnum( texnum ))
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

#else /* !XASH_GAMECUBE */

qboolean R_GXWorldDrewThisFrame( void ) { return false; }
void R_GXClearWorldDrewFlag( void ) {}
int R_GXDrawNewGameCapFaces( void ) { return 0; }
qboolean R_GXStudioIsActive( void ) { return false; }
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
