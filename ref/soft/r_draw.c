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

/*
=============
R_GetImageParms
=============
*/
void R_GetTextureParms( int *w, int *h, int texnum )
{
	image_t *glt;

	glt = R_GetTexture( texnum );
	if( w )
		*w = glt->srcWidth;
	if( h )
		*h = glt->srcHeight;
}

/*
=============
R_GetSpriteParms

same as GetImageParms but used
for sprite models
=============
*/
void GAME_EXPORT R_GetSpriteParms( int *frameWidth, int *frameHeight, int *numFrames, int currentFrame, const model_t *pSprite )
{
	mspriteframe_t *pFrame;

	if( !pSprite || pSprite->type != mod_sprite )
		return;                                       // bad model ?
	pFrame = R_GetSpriteFrame( pSprite, currentFrame, 0.0f );

	if( frameWidth )
		*frameWidth = pFrame->width;
	if( frameHeight )
		*frameHeight = pFrame->height;
	if( numFrames )
		*numFrames = pSprite->numframes;
}

int GAME_EXPORT R_GetSpriteTexture( const model_t *m_pSpriteModel, int frame )
{
	if( !m_pSpriteModel || m_pSpriteModel->type != mod_sprite || !m_pSpriteModel->cache.data )
		return 0;

	return R_GetSpriteFrame( m_pSpriteModel, frame, 0.0f )->gl_texturenum;
}


/*
=============
Draw_StretchPicImplementation
=============
*/
static void R_DrawStretchPicImplementation( int x, int y, int w, int h, int s1, int t1, int s2, int t2, image_t *pic )
{
	unsigned int height;
	int          skip, v;
	qboolean     transparent = false;
	pixel_t      *buffer;

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

	if( !pic->pixels[0] || s1 >= s2 || t1 >= t2 )
		return;

	// gEngfuncs.Con_Printf ("pixels is %p\n", pic->pixels[0] );

	height = h;

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


#pragma omp parallel for schedule(static)
	for( v = 0; v < height; v++ )
	{
		int     alpha1 = vid.alpha;
		pixel_t *dest = vid.buffer + ( y + v ) * vid.rowbytes + x;
		uint    sv = ( skip + v ) * ( t2 - t1 ) / h + t1;
		uint    u, f, fstep;
		pixel_t *source = buffer + sv * pic->width + s1;

		f = 0;
		fstep = (( s2 - s1 ) << 16 ) / w;

		for( u = 0; u < w; u++ )
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
	image_t *pic = R_GetTexture( texnum );
	int     width = pic->width, height = pic->height;
//	GL_Bind( XASH_TEXTURE0, texnum );
	if( s2 > 1.0f || t2 > 1.0f )
		return;
	if( s1 < 0.0f || t1 < 0.0f )
		return;
	if( w < 1.0f || h < 1.0f )
		return;
	R_DrawStretchPicImplementation( x, y, w, h, width * s1, height * t1, width * s2, height * t2, pic );
}

void Draw_Fill( int x, int y, int w, int h )
{
	unsigned int height;
	int          v;
	pixel_t      src = vid.color;
	int          alpha = vid.alpha;

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

	height = h;
	if( y < 0 )
	{
		if( h <= -y )
			return;
		height += y;
		y = 0;
	}

#pragma omp parallel for schedule(static)
	for( v = 0; v < height; v++ )
	{
		pixel_t *dest = vid.buffer + ( y + v ) * vid.rowbytes + x;
		uint    u;

		for( u = 0; u < w; u++ )
		{
			if( alpha == 0 )
				continue;

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
R_DrawStretchRaw
=============
*/
void GAME_EXPORT R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	byte    *raw = NULL;
	image_t *tex;

	raw = (byte *)data;

	// pglDisable( GL_BLEND );
	// pglDisable( GL_ALPHA_TEST );
	// pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

	tex = R_GetTexture( tr.cinTexture );
	GL_Bind( XASH_TEXTURE0, tr.cinTexture );
}

/*
=============
R_UploadStretchRaw
=============
*/
void GAME_EXPORT R_UploadStretchRaw( int texture, int cols, int rows, int width, int height, const byte *data )
{
	byte    *raw = NULL;
	image_t *tex;
	raw = (byte *)data;

	tex = R_GetTexture( texture );
	GL_Bind( GL_KEEP_UNIT, texture );
	tex->width = cols;
	tex->height = rows;
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
