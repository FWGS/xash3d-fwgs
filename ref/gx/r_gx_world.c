/*
Copyright (C) 2026 Xash3D FWGS GameCube port

G151–G154: Flipper GX world draw — cap faces as EFB triangles + LM.
G155: Flipper GX studio/viewmodel via TriAPI → EFB overlay.
*/
#include "r_local.h"

#if XASH_GAMECUBE
#include <gccore.h>
#include <ogc/gx.h>
#include <malloc.h>
#include <string.h>

extern qboolean GC_UseGxWorldDraw( void );
extern void GC_MarkGxWorldEfbReady( void );
extern void *GC_GetGxVideoMode( void );

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

/* G155 studio overlay */
static qboolean r_gx_studio_active;
static qboolean r_gx_studio_viewmodel;
static qboolean r_gx_studio_logged;
static int r_gx_studio_tris;
static u32 r_gx_studio_color = 0xFFFFFFFF;
static unsigned r_gx_studio_bound_tex;

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
			GX_LoadTexObj( &r_gx_tex[i].obj, GX_TEXMAP0 );
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
	GX_LoadTexObj( &t->obj, GX_TEXMAP0 );
	return t;
}

static gc_gx_tex_t *R_GXBindTexture( texture_t *mt )
{
	if( !mt || mt->gl_texturenum <= 0 )
		return NULL;
	return R_GXBindTexnum( (unsigned)mt->gl_texturenum );
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
	GX_InvalidateTexAll();
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

static qboolean R_GXBindLightmap( int slot, GXTexObj *obj )
{
	extern const unsigned short *GC_GetNewGameCapLightmap( int slot, int *w, int *h );
	const unsigned short *lm;
	int w = 0, h = 0;

	lm = GC_GetNewGameCapLightmap( slot, &w, &h );
	if( !lm || w < 4 || h < 4 )
		return false;

	GX_InitTexObj( obj, (void *)lm, (u16)w, (u16)h,
		GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE );
	GX_InitTexObjFilterMode( obj, GX_LINEAR, GX_LINEAR );
	GX_LoadTexObj( obj, GX_TEXMAP1 );
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
	GXTexObj lmobj;

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
				R_GXFaceST( surf, pts[i], &sts[i][0], &sts[i][1] );
				R_GXFaceLMST( surf, pts[i], &lmst[i][0], &lmst[i][1] );
			}
			lit = R_GXBindLightmap( slot, &lmobj );
		}
	}
	if( !textured )
		color = R_GXFaceColor( surf );

	if( textured && lit )
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
		GX_SetNumTexGens( 1 );
		GX_SetNumTevStages( 1 );
		GX_SetTexCoordGen( GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_MODULATE );
		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_TEX0, GX_DIRECT );

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
		GX_SetNumTexGens( 0 );
		GX_SetNumTevStages( 1 );
		GX_SetTevOrder( GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0 );
		GX_SetTevOp( GX_TEVSTAGE0, GX_PASSCLR );
		GX_ClearVtxDesc();
		GX_SetVtxDesc( GX_VA_POS, GX_DIRECT );
		GX_SetVtxDesc( GX_VA_CLR0, GX_DIRECT );

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
	extern msurface_t *GC_GetNewGameDrawSurfs( void );
	msurface_t *draw;
	model_t *world;
	int n, i, drawn = 0;

	if( !GC_UseGxWorldDraw() )
		return 0;

	world = WORLDMODEL;
	draw = GC_GetNewGameDrawSurfs();
	n = GC_GetNewGameCapFaceCount();
	if( !world || !draw || n <= 0 )
		return 0;

	if( r_gx_tex_world != world )
	{
		R_GXTexCacheReset();
		r_gx_tex_world = world;
		r_gx_tex_logged = false;
		r_gx_lm_logged = false;
	}

	RI.currentmodel = world;
	r_gx_tex_draws = 0;
	r_gx_flat_draws = 0;
	r_gx_lm_draws = 0;
	R_GXSetupWorld3DState();

	for( i = 0; i < n; i++ )
	{
		float dot;
		msurface_t *surf = &draw[i];

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
		drawn += R_GXEmitFace( surf, world, i );
	}

	GX_DrawDone();
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
	return drawn;
}

/*
================
G155: Flipper studio / viewmodel overlay (TriAPI → EFB)
================
*/
static void R_GXPrepareStudioState( qboolean viewmodel )
{
	GXRModeObj *rmode = (GXRModeObj *)GC_GetGxVideoMode();
	Mtx44 proj;
	Mtx mv;

	if( rmode )
	{
		GX_SetViewport( 0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f );
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
	/* Viewmodel: always on top so the gun is not buried by world depth. */
	if( viewmodel )
		GX_SetZMode( GX_FALSE, GX_ALWAYS, GX_FALSE );
	else
		GX_SetZMode( GX_TRUE, GX_LEQUAL, GX_TRUE );
	GX_SetBlendMode( GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP );

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
	R_GXPrepareStudioState( viewmodel );
}

void R_GXStudioEnd( void )
{
	if( !r_gx_studio_active )
		return;

	GX_DrawDone();
	if( r_gx_studio_tris > 0 )
	{
		r_gx_world_drew = true;
		GC_MarkGxWorldEfbReady();
		if( !r_gx_studio_logged )
		{
			r_gx_studio_logged = true;
			gEngfuncs.Con_Reportf(
				"Xash3D GameCube: G155 GX studio tris=%d viewmodel=%d (Flipper EFB)\n",
				r_gx_studio_tris, r_gx_studio_viewmodel ? 1 : 0 );
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

void R_GXStudioEmitTri(
	float x0, float y0, float z0, float u0, float v0,
	float x1, float y1, float z1, float u1, float v1,
	float x2, float y2, float z2, float u2, float v2 )
{
	if( !r_gx_studio_active )
		return;

	if( r_gx_studio_bound_tex == 0 )
		R_GXStudioBindTexnum( (unsigned)tr.whiteTexture );

	GX_Begin( GX_TRIANGLES, GX_VTXFMT0, 3 );
	GX_Position3f32( x0, y0, z0 );
	GX_Color1u32( r_gx_studio_color );
	GX_TexCoord2f32( u0, v0 );
	GX_Position3f32( x1, y1, z1 );
	GX_Color1u32( r_gx_studio_color );
	GX_TexCoord2f32( u1, v1 );
	GX_Position3f32( x2, y2, z2 );
	GX_Color1u32( r_gx_studio_color );
	GX_TexCoord2f32( u2, v2 );
	GX_End();
	r_gx_studio_tris++;
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

#endif
