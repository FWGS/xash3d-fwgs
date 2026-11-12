/*
r_gx_skins.c - ARAM-streamed full-resolution studio skins for the GX renderer
Copyright (C) 2026 xash3d-gc contributors

Glue between the platform-independent policy in skin_stream.[ch] and the GX
native texture path (R_GXNativeTexture in r_gx_world.c).

A viewmodel skin is decoded to at most 128x128 by the image loader and kept in
the studio overflow window as before (the "low mip"). When the model's own
skin is larger (the DoD viewmodels are 256x256), R_StudioLoadTexture hands the
palettised source to R_GXSkinAttachVM, which builds the GX tiled texels once
(same chromakey rules as the low path), stores them in the ARAM asset cache and
keeps nothing of them in MEM1. A draw then asks R_GXSkinGet: while the full
skin is in the small MEM1 window it is used, otherwise the low mip draws and
the full skin is promoted from ARAM, a bounded number of bytes per frame.

See docs/GAMECUBE_ARAM_STREAMING_DESIGN.md (Stage 2b).
*/
#include "r_local.h"

#if XASH_GAMECUBE
#include <gccore.h>
#include <ogc/gx.h>
#include <malloc.h>
#include <string.h>
#include "../../engine/platform/gamecube/mem_gamecube.h"
#include "../../engine/platform/gamecube/skin_stream.h"

#define GC_SKIN_MAX_DIM        256
#define GC_SKIN_WINDOW_BYTES   ( 192 * 1024 )	/* a 256x256 body skin plus hands */
#define GC_SKIN_FRAME_BUDGET   ( 160 * 1024 )	/* promoted per frame at most */
#define GC_SKIN_LOG_FRAMES     600

static gc_skin_stream_t r_skin_stream;
static byte *r_skin_window;
static qboolean r_skin_ready;
static qboolean r_skin_failed;
static int r_skin_disabled = -1;
static unsigned r_skin_gen = 1;
static byte r_skin_alpha[GC_SKIN_MAX];
static unsigned r_skin_frames;
static unsigned r_skin_logged_promotions, r_skin_logged_low;

static qboolean R_GXSkinEnabled( void )
{
	if( r_skin_disabled < 0 )
		r_skin_disabled = gEngfuncs.Sys_CheckParm( "-gcnoskinstream" ) ? 1 : 0;
	return r_skin_disabled == 0;
}

static qboolean R_GXSkinInit( void )
{
	gc_aram_cache_t *cache;

	if( r_skin_ready )
		return true;
	if( r_skin_failed || !R_GXSkinEnabled( ))
		return false;
	r_skin_failed = true;
	cache = GC_AramAssetCacheGet();
	if( !cache )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM off (no ARAM cache)\n" );
		return false;
	}
	r_skin_window = (byte *)memalign( 32, GC_SKIN_WINDOW_BYTES );
	if( !r_skin_window )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM off (no %uK MEM1 window)\n",
			(unsigned)( GC_SKIN_WINDOW_BYTES / 1024 ));
		return false;
	}
	GC_SkinStreamInit( &r_skin_stream, cache, r_skin_window, GC_SKIN_WINDOW_BYTES, GC_SKIN_FRAME_BUDGET );
	r_skin_ready = true;
	r_skin_failed = false;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM ready window=%uK budget=%uK/frame\n",
		(unsigned)( GC_SKIN_WINDOW_BYTES / 1024 ), (unsigned)( GC_SKIN_FRAME_BUDGET / 1024 ));
	return true;
}

/* Linear RGB565 -> GX 4x4 tiles, in place, one tile row at a time. */
static void R_GXSkinTileInPlace( u16 *pix, int w, int h )
{
	static u16 strip[GC_SKIN_MAX_DIM * 4];
	int ty, tx, y;

	for( ty = 0; ty < h; ty += 4 )
	{
		u16 *rows = pix + ty * w;
		u16 *out = rows;

		memcpy( strip, rows, (size_t)w * 4 * sizeof( u16 ));
		for( tx = 0; tx < w; tx += 4 )
			for( y = 0; y < 4; y++, out += 4 )
				memcpy( out, strip + y * w + tx, 4 * sizeof( u16 ));
	}
}

/*
Build the tiled texels of a palettised viewmodel skin. This is the low path's
rules (r_image.c vm_rgb565 punch + R_GXNativeTexture VM branches) applied to the
source resolution: gun skins flood their chromakey holes into linear RGB565,
masked hands keep a 1-bit alpha in RGB5A3. *alpha reports which one it is.
*/
static void R_GXSkinBuildTiled( const byte *idx, const byte *pal, int w, int h,
	qboolean flag_masked, qboolean hands, u16 *pix, qboolean *alpha )
{
	const int n = w * h;
	qboolean masked = false;
	int i;

	for( i = 0; i < n; i++ )
	{
		const byte c = idx[i];
		const unsigned r8 = pal[c * 3 + 0], g8 = pal[c * 3 + 1], b8 = pal[c * 3 + 2];
		unsigned r = r8 >> 3, g = g8 >> 2, b = b8 >> 3;
		u16 soft = (u16)(( r << 11 ) | ( g << 5 ) | b );

		if( flag_masked && c == 255 )
			soft = TRANSPARENT_COLOR;
		else if( r8 > 200 && b8 > 180 && g8 + 40 < r8 && g8 + 40 < b8 )
			soft = TRANSPARENT_COLOR;
		else if( r8 < 40 && g8 < 40 && b8 > 200 )
			soft = TRANSPARENT_COLOR;
		else if( r8 > 220 && g8 < 40 && b8 > 220 )
			soft = TRANSPARENT_COLOR;
		else if( hands && r8 < 14 && g8 < 14 && b8 < 14 && ( r8 + g8 + b8 ) < 28 )
			soft = TRANSPARENT_COLOR;
		pix[i] = soft;
		if( soft == TRANSPARENT_COLOR )
			masked = true;
	}

	if( masked && hands )
	{
		for( i = 0; i < n; i++ )
		{
			const u16 soft = pix[i];
			const unsigned r = soft >> 11, g = ( soft >> 5 ) & 63, b = soft & 31;

			if( soft == TRANSPARENT_COLOR || ( r <= 1 && ( g >> 1 ) <= 1 && b <= 1 ))
				pix[i] = 0;
			else
				pix[i] = (u16)( 0x8000u | ( r << 10 ) | (( g >> 1 ) << 5 ) | b );
		}
		*alpha = true;
	}
	else
	{
		unsigned last_r = 12, last_g = 20, last_b = 8;	/* warm wood fallback */
		int pass, left = 1;
		u16 fallback;

		for( i = 0; i < n; i++ )
		{
			const u16 soft = pix[i];

			if( soft == TRANSPARENT_COLOR )
			{
				pix[i] = 1;		/* hole sentinel, authored black stays 0 */
				continue;
			}
			if( soft != 0 )
			{
				last_r = soft >> 11;
				last_g = ( soft >> 5 ) & 63;
				last_b = soft & 31;
			}
		}
		fallback = (u16)(( last_r << 11 ) | ( last_g << 5 ) | last_b );
		for( pass = 0; pass < 64 && left; pass++ )
		{
			left = 0;
			for( i = 0; i < n; i++ )
			{
				const int py = i / w, px = i % w;
				int oy, ox, found = 0;
				unsigned r = last_r, g = last_g, b = last_b;

				if( pix[i] != 1 )
					continue;
				for( oy = -1; oy <= 1 && !found; oy++ )
				{
					for( ox = -1; ox <= 1 && !found; ox++ )
					{
						const int sy = py + oy, sx = px + ox;
						unsigned nr, ng, nb;
						u16 c;

						if(( oy | ox ) == 0 || sy < 0 || sx < 0 || sy >= h || sx >= w )
							continue;
						c = pix[sy * w + sx];
						if( c == 1 )
							continue;
						nr = c >> 11;
						ng = ( c >> 5 ) & 63;
						nb = c & 31;
						if( nr >= 22 && nb >= 14 && nr + 2 > ng && nb + 2 >= ( ng >> 1 ) && nr > nb )
							continue;	/* magenta/salmon bleed */
						r = nr;
						g = ng;
						b = nb;
						found = 1;
					}
				}
				if( found )
				{
					pix[i] = (u16)(( r << 11 ) | ( g << 5 ) | b );
					last_r = r;
					last_g = g;
					last_b = b;
				}
				else
					left = 1;
			}
		}
		for( i = 0; i < n; i++ )
			if( pix[i] == 1 )
				pix[i] = fallback ? fallback : (u16)0x5A08;
		*alpha = false;
	}
	R_GXSkinTileInPlace( pix, w, h );
	DCFlushRange( pix, (u32)n * sizeof( u16 ));
}

static void R_GXSkinKey( const char *name, char *key, size_t size )
{
	unsigned h = 5381u;
	const char *p;

	for( p = name; *p; p++ )
		h = h * 33u + (unsigned char)*p;
	Q_snprintf( key, size, "skin:%08x:%s", h, name[0] == '#' ? name + 1 : name );
}

/*
Attach the source-resolution skin of a viewmodel texture to its image. src is
the palette index plane, pal the 768 byte RGB palette behind it. Safe to call
for any texture: it does nothing unless a larger-than-resident viewmodel skin
can be streamed.
*/
void R_GXSkinAttachVM( unsigned texnum, const byte *src, int w, int h, qboolean flag_masked )
{
	image_t *img;
	qboolean hands, alpha = false;
	char key[GC_ARAM_NAME_LEN];
	u16 *pix;
	int handle;

	if( !src || w < 8 || h < 8 || w > GC_SKIN_MAX_DIM || h > GC_SKIN_MAX_DIM || ( w & 3 ) || ( h & 3 ))
		return;
	img = R_GetTexture( texnum );
	if( !img || img->name[0] != '#' || !Q_stristr( img->name, "models/v_" ))
		return;
	if( !img->pixels[0] || img->gc_packed || img->gc_gx_native || img->gc_skin_handle
		|| img->width < 8 || img->height < 8
		|| ( img->width >= w && img->height >= h ))
		return;
	if( !R_GXSkinInit( ))
		return;
	pix = (u16 *)memalign( 32, (size_t)w * h * sizeof( u16 ));
	if( !pix )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM skip %s (no %uK scratch)\n",
			img->name, (unsigned)( w * h * 2 / 1024 ));
		return;
	}
	hands = Q_stristr( img->name, "hands" ) != NULL;
	R_GXSkinBuildTiled( src, src + w * h, w, h, flag_masked, hands, pix, &alpha );
	R_GXSkinKey( img->name, key, sizeof( key ));
	handle = GC_SkinStreamRegisterBpp( &r_skin_stream, key, (uint16_t)w, (uint16_t)h, pix,
		(uint16_t)img->width, (uint16_t)img->height, img->pixels[0], 2 );
	free( pix );
	if( handle < 0 )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM refused %s %dx%d (ARAM/table full)\n",
			img->name, w, h );
		return;
	}
	r_skin_alpha[handle] = alpha ? 1 : 0;
	img->gc_skin_handle = (short)( handle + 1 );
	img->gc_skin_gen = r_skin_gen;
	gEngfuncs.Con_Reportf( "Xash3D GameCube: SKIN-STREAM reg %s full=%dx%d low=%dx%d fmt=%s handle=%d\n",
		img->name, w, h, img->width, img->height, alpha ? "RGB5A3" : "RGB565", handle );
}

/*
The full-resolution texels of an image if they are resident this frame, else
NULL (the caller draws pixels[0]). *promoted is set when the window changed so
the caller re-points its texture object and the texture cache is invalidated.
*/
const void *R_GXSkinGet( image_t *img, int *w, int *h, qboolean *alpha, qboolean *promoted )
{
	gc_skin_t *k;
	unsigned before;
	const uint8_t *p;
	uint16_t sw, sh;
	int handle, is_full = 0;

	*promoted = false;
	if( !r_skin_ready || !img->gc_skin_handle || img->gc_skin_gen != r_skin_gen )
		return NULL;
	handle = img->gc_skin_handle - 1;
	k = &r_skin_stream.skins[handle];
	if( !k->used )
		return NULL;
	/* A promotion overwrites window bytes the GPU may still be reading for an
	 * earlier draw: drain the pipe first, only on the frames that promote. */
	if( k->win_off < 0 && r_skin_stream.budget_left >= k->full_bytes )
		GX_DrawDone();
	before = r_skin_stream.promotions;
	p = GC_SkinStreamGet( &r_skin_stream, handle, &sw, &sh, &is_full );
	if( !p || !is_full )
		return NULL;
	if( r_skin_stream.promotions != before )
	{
		DCFlushRange( (void *)p, k->full_bytes );
		GX_InvalidateTexAll();
		*promoted = true;
	}
	*w = sw;
	*h = sh;
	*alpha = r_skin_alpha[handle] ? true : false;
	return p;
}

void R_GXSkinBeginFrame( void )
{
	if( !r_skin_ready )
		return;
	GC_SkinStreamBeginFrame( &r_skin_stream );
	if( ++r_skin_frames % GC_SKIN_LOG_FRAMES == 0
		&& ( r_skin_stream.promotions != r_skin_logged_promotions
			|| r_skin_stream.low_served != r_skin_logged_low ))
	{
		r_skin_logged_promotions = r_skin_stream.promotions;
		r_skin_logged_low = r_skin_stream.low_served;
		R_GXSkinReport( "frame" );
	}
}

void R_GXSkinReport( const char *stage )
{
	if( !r_skin_ready )
		return;
	gEngfuncs.Con_Reportf(
		"Xash3D GameCube: SKIN-STREAM %s promotions=%u evictions=%u full=%u low=%u refusals=%u\n",
		stage ? stage : "-", r_skin_stream.promotions, r_skin_stream.evictions,
		r_skin_stream.full_served, r_skin_stream.low_served, r_skin_stream.refusals );
}

/* The image's texture is being freed (map change): drop its streamed skin. */
void R_GXSkinRelease( image_t *img )
{
	if( !img->gc_skin_handle )
		return;
	if( r_skin_ready && img->gc_skin_gen == r_skin_gen )
		GC_SkinStreamUnregister( &r_skin_stream, img->gc_skin_handle - 1 );
	img->gc_skin_handle = 0;
}

/* Every texture is going away. */
void R_GXSkinReset( void )
{
	if( !r_skin_ready )
		return;
	R_GXSkinReport( "reset" );
	GC_SkinStreamReset( &r_skin_stream );
	r_skin_gen++;
}
#endif
