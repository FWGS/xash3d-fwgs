/*
gl_draw.c - orthogonal drawing stuff
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "r_local.h"

#if XASH_GAMECUBE
/* Matches half-res intro decode/upload (avi_gc GC_AVI_DECODE_SCALE). */
#define GC_CINEMATIC_MAX_WIDTH 320
#define GC_CINEMATIC_MAX_HEIGHT 240
static pixel_t gc_rgb565_r[256];
static pixel_t gc_rgb565_g[256];
static pixel_t gc_rgb565_b[256];
static pixel_t gc_rgb565_to_sw[65536];
static pixel_t gc_cinematic_pixels[GC_CINEMATIC_MAX_WIDTH * GC_CINEMATIC_MAX_HEIGHT] __attribute__((aligned( 32 )));
static qboolean gc_rgb565_tables_ready;

qboolean R_GCIsStaticCinematicPixels( const pixel_t *pixels )
{
	return pixels == gc_cinematic_pixels;
}

static void GC_BuildRGB565Tables( void )
{
	if( gc_rgb565_tables_ready )
		return;

	for( int i = 0; i < 256; i++ )
	{
		unsigned int r = (unsigned int)i >> 3;
		unsigned int g = (unsigned int)i >> 2;
		unsigned int b = (unsigned int)i >> 3;
		unsigned int r_major = ((( r >> 2 ) & MASK( 3 )) << 5 ) << 8;
		unsigned int g_major = ((( g >> 3 ) & MASK( 3 )) << 2 ) << 8;
		unsigned int b_major = (( b >> 3 ) & MASK( 2 )) << 8;
		unsigned int r_minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 );
		unsigned int g_minor = MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 );
		unsigned int b_minor = MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );

		gc_rgb565_r[i] = (pixel_t)( r_major | r_minor );
		gc_rgb565_g[i] = (pixel_t)( g_major | g_minor );
		gc_rgb565_b[i] = (pixel_t)( b_major | b_minor );
	}

	for( unsigned int c = 0; c < ARRAYSIZE( gc_rgb565_to_sw ); c++ )
	{
		unsigned int r = ( c >> 11 ) & 0x1F;
		unsigned int g = ( c >> 5 ) & 0x3F;
		unsigned int b = c & 0x1F;
		unsigned int major = ((( r >> 2 ) & MASK( 3 )) << 5 ) |
			(((( g >> 3 ) & MASK( 3 )) << 2 )) |
			((( b >> 3 ) & MASK( 2 )));
		unsigned int minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 ) |
			MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 ) |
			MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );

		gc_rgb565_to_sw[c] = (pixel_t)( major << 8 ) | ( minor & 0xFF );
	}

	gc_rgb565_tables_ready = true;
}
#endif

/*
=============
R_GetImageParms
=============
*/
void R_GetTextureParms( int *w, int *h, int texnum )
{
	image_t *glt = R_GetTexture( texnum );

	if( w )
		*w = glt->srcWidth;
	if( h )
		*h = glt->srcHeight;
}

/*
=============
Draw_StretchPicImplementation
=============
*/
static void R_DrawStretchPicImplementation( int x, int y, int w, int h, int s1, int t1, int s2, int t2, image_t *pic )
{
	int      skip;
	qboolean transparent = false;
	pixel_t  *buffer;

	if( !pic )
		return;

#if XASH_GAMECUBE
	R_EnsureDrawBuffer();
#endif
	if( !vid.buffer || vid.rowbytes <= 0 || vid.width <= 0 || vid.height <= 0 )
		return;

	if( x < 0 )
	{
		s1 += ( -x ) * ( s2 - s1 ) / w;
		x = 0;
	}
	if( x + w > vid.width )
	{
		s2 -= ( x + w - vid.width ) * ( s2 - s1 ) / w;
		w = vid.width - x;
	}
	if( y + h > vid.height )
	{
		t2 -= ( y + h - vid.height ) * ( t2 - t1 ) / h;
		h = vid.height - y;
	}

	if( w <= 0 || h <= 0 )
		return;

	if( !pic->pixels[0] || s1 >= s2 || t1 >= t2 )
		return;

	// gEngfuncs.Con_Printf ("pixels is %p\n", pic->pixels[0] );

	unsigned int height = h;

	if( y < -h ) // out of display, out of bounds
		return;

	if( y < 0 )
	{
		skip = -y;
		height += y;
		y = 0;
	}
	else
		skip = 0;

	if( pic->alpha_pixels )
	{
		buffer = pic->alpha_pixels;
		transparent = true;
	}
	else
		buffer = pic->pixels[0];

#if XASH_GAMECUBE
	{
		int row_stride = pic->width;
		const qboolean rgb565_fb = GC_UseLowResWorldProbe();
		static qboolean gc_rgb565_2d_logged;

		if( row_stride <= 0 )
			return;

		if( rgb565_fb && !gc_rgb565_2d_logged )
		{
			gEngfuncs.Con_Reportf( "Xash3D GameCube: RGB565 2D/HUD draw active\n" );
			gc_rgb565_2d_logged = true;
		}

#pragma omp parallel for schedule(static)
		for( int v = 0; v < height; v++ )
		{
			int     alpha1 = vid.alpha;
			pixel_t *dest = vid.buffer + ( y + v ) * vid.rowbytes + x;
			uint    sv = ( skip + v ) * ( t2 - t1 ) / h + t1;
			pixel_t *source = buffer + sv * row_stride + s1;

			uint f = 0;
			uint fstep = (( s2 - s1 ) << 16 ) / w;

			for( uint u = 0; u < w; u++ )
			{
				pixel_t src = source[f >> 16];
				int     alpha = alpha1;
				f += fstep;

				if( transparent )
				{
					alpha &= src >> ( 16 - 3 );
					src = src << 3;
				}

				if( alpha == 0 )
					continue;

				if( vid.color != COLOR_WHITE )
					src = vid.modmap[( src & 0xff00 ) | ( vid.color >> 8 )] << 8 | ( src & vid.color & 0xff ) | (( src & 0xff ) >> 3 );

				/* New Game world FB is display RGB565; soft blend tables do not apply. */
				if( rgb565_fb )
				{
					pixel_t src565;
					unsigned int r, g, b;

					/* G187: SPR_DrawHoles — skip transparent + near-black ink. */
					if( src == TRANSPARENT_COLOR )
						continue;
					src565 = vid.screen[src];
					if( vid.rendermode == kRenderTransColor
						|| vid.rendermode == kRenderTransAlpha )
					{
						r = ( src565 >> 11 ) & 0x1Fu;
						g = ( src565 >> 5 ) & 0x3Fu;
						b = src565 & 0x1Fu;
						if( r <= 1u && ( g >> 1 ) <= 1u && b <= 1u )
							continue;
					}

					if( vid.rendermode == kRenderTransAdd )
					{
						pixel_t d = dest[u];
						unsigned int ar = (( d >> 11 ) & 0x1F ) + (( src565 >> 11 ) & 0x1F );
						unsigned int ag = (( d >> 5 ) & 0x3F ) + (( src565 >> 5 ) & 0x3F );
						unsigned int ab = ( d & 0x1F ) + ( src565 & 0x1F );
						if( ar > 31 ) ar = 31;
						if( ag > 63 ) ag = 63;
						if( ab > 31 ) ab = 31;
						dest[u] = (pixel_t)(( ar << 11 ) | ( ag << 5 ) | ab );
					}
					else if( alpha < 4 )
						continue;
					else
						dest[u] = src565;
					continue;
				}

				if( vid.rendermode == kRenderTransAdd )
				{
					pixel_t screen = dest[u];
					dest[u] = vid.addmap[( src & 0xff00 ) | ( screen >> 8 )] << 8 | ( screen & 0xff ) | (( src & 0xff ) >> 0 );
				}
				else if( vid.rendermode == kRenderScreenFadeModulate )
				{
					pixel_t screen = dest[u];
					dest[u] = BLEND_COLOR( screen, vid.color );
				}
				else if( alpha < 7 ) // && (vid.rendermode == kRenderTransAlpha || vid.rendermode == kRenderTransTexture ) )
				{
					pixel_t screen = dest[u];                    //  | 0xff & screen & src ;
					dest[u] = BLEND_ALPHA( alpha, src, screen ); // vid.alphamap[( alpha << 16)|(src & 0xff00)|(screen>>8)] << 8 | (screen & 0xff) >> 3 | ((src & 0xff) >> 3);
				}
				else
					dest[u] = src;

			}
		}
		return;
	}
#endif

#pragma omp parallel for schedule(static)
	for( int v = 0; v < height; v++ )
	{
		int     alpha1 = vid.alpha;
		pixel_t *dest = vid.buffer + ( y + v ) * vid.rowbytes + x;
		uint    sv = ( skip + v ) * ( t2 - t1 ) / h + t1;
		pixel_t *source = buffer + sv * pic->width + s1;

		uint f = 0;
		uint fstep = (( s2 - s1 ) << 16 ) / w;

		for( uint u = 0; u < w; u++ )
		{
			pixel_t src = source[f >> 16];
			int     alpha = alpha1;
			f += fstep;

			if( transparent )
			{
				alpha &= src >> ( 16 - 3 );
				src = src << 3;
			}

			if( alpha == 0 )
				continue;

			if( vid.color != COLOR_WHITE )
				src = vid.modmap[( src & 0xff00 ) | ( vid.color >> 8 )] << 8 | ( src & vid.color & 0xff ) | (( src & 0xff ) >> 3 );

			if( vid.rendermode == kRenderTransAdd )
			{
				pixel_t screen = dest[u];
				dest[u] = vid.addmap[( src & 0xff00 ) | ( screen >> 8 )] << 8 | ( screen & 0xff ) | (( src & 0xff ) >> 0 );
			}
			else if( vid.rendermode == kRenderScreenFadeModulate )
			{
				pixel_t screen = dest[u];
				dest[u] = BLEND_COLOR( screen, vid.color );
			}
			else if( alpha < 7 ) // && (vid.rendermode == kRenderTransAlpha || vid.rendermode == kRenderTransTexture ) )
			{
				pixel_t screen = dest[u];                    //  | 0xff & screen & src ;
				dest[u] = BLEND_ALPHA( alpha, src, screen ); // vid.alphamap[( alpha << 16)|(src & 0xff00)|(screen>>8)] << 8 | (screen & 0xff) >> 3 | ((src & 0xff) >> 3);
			}
			else
				dest[u] = src;

		}
	}
}


/*
=============
R_DrawStretchPic
=============
*/
void GAME_EXPORT R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	image_t *pic;

	if( texnum <= 0 || w < 1.0f || h < 1.0f )
		return;
	if( s2 > 1.0f || t2 > 1.0f )
		return;
	if( s1 < 0.0f || t1 < 0.0f )
		return;

#if XASH_GAMECUBE
	/* G182: live Flipper world already in EFB — soft HUD would be discarded. */
	if( GC_UseGxWorldDraw() && R_GXWorldDrewThisFrame()
		&& R_GXDrawStretchPic( x, y, w, h, s1, t1, s2, t2, texnum ))
		return;
#endif

	pic = R_GetTexture( texnum );
	if( !pic || pic->width <= 0 || pic->height <= 0 )
		return;

#if XASH_GAMECUBE
	if( !Q_strcmp( pic->name, "*cintexture" ) && pic->srcWidth > 0 && pic->srcHeight > 0 )
	{
		R_DrawStretchPicImplementation( x, y, w, h, pic->srcWidth * s1, pic->srcHeight * t1,
			pic->srcWidth * s2, pic->srcHeight * t2, pic );
		return;
	}
#endif

	R_DrawStretchPicImplementation( x, y, w, h, pic->width * s1, pic->height * t1,
		pic->width * s2, pic->height * t2, pic );
}

void Draw_Fill( int x, int y, int w, int h )
{
	pixel_t src = vid.color;
	int     alpha = vid.alpha;

#if XASH_GAMECUBE
	R_EnsureDrawBuffer();
	const qboolean rgb565_fb = GC_UseLowResWorldProbe();
	if( rgb565_fb )
		src = vid.screen[vid.color];
#endif
	if( !vid.buffer || vid.rowbytes <= 0 || vid.width <= 0 || vid.height <= 0 )
		return;

	if( x < 0 )
		x = 0;

	if( x + w > vid.width )
		w = vid.width - x;

	if( w <= 0 )
		return;

	if( y + h > vid.height )
		h = vid.height - y;

	if( h <= 0 )
		return;

	unsigned int height = h;
	if( y < 0 )
	{
		if( h <= -y )
			return;
		height += y;
		y = 0;
	}

#pragma omp parallel for schedule(static)
	for( int v = 0; v < height; v++ )
	{
		pixel_t *dest = vid.buffer + ( y + v ) * vid.rowbytes + x;

		for( uint u = 0; u < w; u++ )
		{
			if( alpha == 0 )
				continue;

#if XASH_GAMECUBE
			if( rgb565_fb )
			{
				if( vid.rendermode == kRenderTransAdd )
				{
					pixel_t d = dest[u];
					unsigned int r = (( d >> 11 ) & 0x1F ) + (( src >> 11 ) & 0x1F );
					unsigned int g = (( d >> 5 ) & 0x3F ) + (( src >> 5 ) & 0x3F );
					unsigned int b = ( d & 0x1F ) + ( src & 0x1F );
					if( r > 31 ) r = 31;
					if( g > 63 ) g = 63;
					if( b > 31 ) b = 31;
					dest[u] = (pixel_t)(( r << 11 ) | ( g << 5 ) | b );
				}
				else if( alpha < 4 )
					continue;
				else
					dest[u] = src;
				continue;
			}
#endif

			if( vid.rendermode == kRenderTransAdd )
			{
				pixel_t screen = dest[u];
				dest[u] = vid.addmap[( src & 0xff00 ) | ( screen >> 8 )] << 8 | ( screen & 0xff ) | (( src & 0xff ) >> 0 );
			}
			else if( alpha < 7 ) // && (vid.rendermode == kRenderTransAlpha || vid.rendermode == kRenderTransTexture ) )
			{
				pixel_t screen = dest[u];                    //  | 0xff & screen & src ;
				dest[u] = BLEND_ALPHA( alpha, src, screen ); // vid.alphamap[( alpha << 16)|(src & 0xff00)|(screen>>8)] << 8 | (screen & 0xff) >> 3 | ((src & 0xff) >> 3);
			}
			else
				dest[u] = src;
		}
	}
}

/*
=============
GL_UpdateTexture
=============
*/
void GAME_EXPORT GL_UpdateTexture( int texnum, int cols, int rows, int width, int height, const byte *buffer, pixformat_t fmt )
{
	image_t *tex;
	pixel_t *pixels;
	size_t pixel_count;
	int x, y;
#if XASH_GAMECUBE
	static unsigned int gc_update_count;
	unsigned int gc_mid_r = 0, gc_mid_g = 0, gc_mid_b = 0;
	pixel_t gc_mid_packed = 0;
#endif

	if( texnum <= 0 || texnum >= MAX_TEXTURES || !buffer )
		return;
	if( cols <= 0 || rows <= 0 || width <= 0 || height <= 0 )
		return;

	tex = R_GetTexture( texnum );
	if( !tex )
		return;

#if XASH_GAMECUBE
	{
		qboolean is_cin = !Q_strcmp( tex->name, "*cintexture" );

		if( is_cin )
		{
			width = cols;
			height = rows;
		}
	}
#endif

	switch( fmt )
	{
	case PF_RGBA_32:
	case PF_BGRA_32:
	case PF_RGB_24:
	case PF_BGR_24:
	case PF_LUMINANCE:
	case PF_RGB_565:
		break;
	default:
		gEngfuncs.Con_DPrintf( S_ERROR "%s: unsupported pixel format %i\n", __func__, fmt );
		return;
	}

	pixel_count = (size_t)width * (size_t)height;
	if( !tex->pixels[0] || tex->width != width || tex->height != height )
	{
		for( int i = 0; i < ARRAYSIZE( tex->pixels ); i++ )
		{
			if( tex->pixels[i] )
			{
	#if XASH_GAMECUBE
				if( !R_GCIsStaticCinematicPixels( tex->pixels[i] ))
	#endif
					Mem_Free( tex->pixels[i] );
				tex->pixels[i] = NULL;
			}
		}
		if( tex->alpha_pixels )
		{
			Mem_Free( tex->alpha_pixels );
			tex->alpha_pixels = NULL;
		}

	#if XASH_GAMECUBE
		if( !Q_strcmp( tex->name, "*cintexture" ) &&
			width <= GC_CINEMATIC_MAX_WIDTH && height <= GC_CINEMATIC_MAX_HEIGHT )
		{
			memset( gc_cinematic_pixels, 0, pixel_count * sizeof( pixel_t ));
			tex->pixels[0] = gc_cinematic_pixels;
		}
		else
	#endif
		tex->pixels[0] = (pixel_t *)Mem_Calloc( r_temppool, pixel_count * sizeof( pixel_t ));
		if( !tex->pixels[0] )
		{
			gEngfuncs.Con_Reportf( S_ERROR "%s: OOM updating %s %dx%d\n",
				__func__, tex->name, width, height );
			return;
		}

		tex->srcWidth = cols;
		tex->srcHeight = rows;
		tex->width = width;
		tex->height = height;
		tex->depth = 1;
		tex->numMips = 1;
		tex->size = pixel_count * sizeof( pixel_t );
		ClearBits( tex->flags, TF_HAS_ALPHA );
	}

	tex->width = width;
	tex->height = height;
	tex->srcWidth = cols;
	tex->srcHeight = rows;
	pixels = tex->pixels[0];
#if XASH_GAMECUBE
	GC_BuildRGB565Tables();
	if( fmt == PF_RGB_565 && cols == width && rows == height )
	{
		const pixel_t *src565 = (const pixel_t *)buffer;
		size_t i;

		for( i = 0; i < pixel_count; i++ )
			pixels[i] = gc_rgb565_to_sw[src565[i]];
		if( width > 0 && height > 0 )
		{
			size_t mid = (size_t)( height / 2 ) * (size_t)width + (size_t)( width / 2 );
			pixel_t mid_src = src565[mid];
			gc_mid_packed = pixels[mid];
			gc_mid_r = ( mid_src >> 11 ) & 0x1F;
			gc_mid_g = ( mid_src >> 5 ) & 0x3F;
			gc_mid_b = mid_src & 0x1F;
		}
		goto gc_texture_update_done;
	}
	if( fmt == PF_RGB_24 && cols == width && rows == height )
	{
		const byte *src = buffer;
		size_t i;

		for( i = 0; i < pixel_count; i++, src += 3 )
			pixels[i] = gc_rgb565_r[src[0]] | gc_rgb565_g[src[1]] | gc_rgb565_b[src[2]];

		if( width > 0 && height > 0 )
		{
			size_t mid = (size_t)( height / 2 ) * (size_t)width + (size_t)( width / 2 );
			const byte *mid_src = buffer + mid * 3;

			gc_mid_r = mid_src[0] >> 3;
			gc_mid_g = mid_src[1] >> 2;
			gc_mid_b = mid_src[2] >> 3;
			gc_mid_packed = pixels[mid];
		}
		goto gc_texture_update_done;
	}
#endif
	for( y = 0; y < height; y++ )
	{
		int src_y = y * rows / height;
		for( x = 0; x < width; x++ )
		{
			int src_x = x * cols / width;
			const byte *src;
			unsigned int r, g, b, major, minor;

			switch( fmt )
			{
			case PF_RGBA_32:
				src = buffer + (( src_y * cols + src_x ) * 4 );
				r = src[0]; g = src[1]; b = src[2];
				break;
			case PF_BGRA_32:
				src = buffer + (( src_y * cols + src_x ) * 4 );
				r = src[2]; g = src[1]; b = src[0];
				break;
			case PF_RGB_24:
				src = buffer + (( src_y * cols + src_x ) * 3 );
				r = src[0]; g = src[1]; b = src[2];
				break;
			case PF_BGR_24:
				src = buffer + (( src_y * cols + src_x ) * 3 );
				r = src[2]; g = src[1]; b = src[0];
				break;
			case PF_LUMINANCE:
				src = buffer + ( src_y * cols + src_x );
				r = g = b = src[0];
				break;
			case PF_RGB_565:
			{
				const pixel_t *src565 = (const pixel_t *)buffer;
				pixels[y * width + x] = gc_rgb565_to_sw[src565[src_y * cols + src_x]];
				continue;
			}
			default:
				return;
			}

			r = r * BIT( 5 ) / 256;
			g = g * BIT( 6 ) / 256;
			b = b * BIT( 5 ) / 256;
			major = ((( r >> 2 ) & MASK( 3 )) << 5 ) | (((( g >> 3 ) & MASK( 3 )) << 2 )) | ((( b >> 3 ) & MASK( 2 )));
			minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 ) | MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 ) | MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );
			pixels[y * width + x] = major << 8 | ( minor & 0xFF );
#if XASH_GAMECUBE
			if( x == width / 2 && y == height / 2 )
			{
				gc_mid_r = r;
				gc_mid_g = g;
				gc_mid_b = b;
				gc_mid_packed = pixels[y * width + x];
			}
#endif
		}
	}

#if XASH_GAMECUBE
gc_texture_update_done:
	gc_update_count++;
	if( gc_update_count <= 2 || gc_update_count == 15 || gc_update_count == 30 || gc_update_count == 60 )
	{
		gEngfuncs.Con_Reportf( "Xash3D GameCube: texture update %u tex=%s stored=%dx%d src=%dx%d fmt=%d mid565=%u,%u,%u packed=0x%04X\n",
			gc_update_count, tex->name, tex->width, tex->height, cols, rows, fmt,
			gc_mid_r, gc_mid_g, gc_mid_b, gc_mid_packed );
	}
#endif
}

/*
===============
R_Set2DMode
===============
*/
void GAME_EXPORT R_Set2DMode( qboolean enable )
{
	vid.color = COLOR_WHITE;
	vid.is2d = enable;
	vid.alpha = 7;

	if( enable )
	{
		RI.currententity = NULL;
		RI.currentmodel = NULL;
	}
}
