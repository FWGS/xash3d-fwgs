/*
StbFont.cpp -- stb_truetype font renderer
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#if defined( XASH_ENGINE_FONT_STB )
#include <stdarg.h>

#ifndef _WIN32
#include <stdint.h>
#include <unistd.h>
#endif

extern "C" {
#include "common.h"
}
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "StbFont.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

CStbFont::CStbFont() : CTrueTypeFont(),
	m_pFontData( NULL )
{
}

CStbFont::~CStbFont()
{
}

bool CStbFont::Create( const char *name, int tall, int weight, int blur, float brighten, int outlineSize, int scanlineOffset, float scanlineScale, int flags )
{
	char font_face_path[256];

	Q_strncpy( m_szName, name, sizeof( m_szName ) );
	m_iTall = tall;
	m_iWeight = weight;
	m_iFlags = flags;

	m_iBlur = blur;
	m_fBrighten = brighten;

	m_iOutlineSize = outlineSize;

	m_iScanlineOffset = scanlineOffset;
	m_fScanlineScale = scanlineScale;


	if( !g_TTFontMgr->FindFontDataFile( name, tall, weight, flags, font_face_path, sizeof( font_face_path ) ) )
	{
		Con_Printf( "Unable to find font named %s\n", name );
		m_szName[0] = 0;
		return false;
	}

	m_pFontData = g_TTFontMgr->LoadFontDataFile( font_face_path, NULL );

	if( !m_pFontData )
	{
		Con_Printf( "Unable to read font file %s!\n", font_face_path );
		return false;
	}

	if( !stbtt_InitFont( &m_fontInfo, m_pFontData, 0 ) )
	{
		Con_Printf( "Unable to create font %s!\n", font_face_path );
		m_szName[0] = 0;
		return false;
	}

	// HACKHACK: for some reason size scales between ft2 and stbtt are different
	scale = stbtt_ScaleForPixelHeightPrecision( &m_fontInfo, tall + 4 );
	int x0, y0, x1, y1;

	stbtt_GetFontVMetrics( &m_fontInfo, &m_iAscent, NULL, NULL );
	m_iAscent = round( m_iAscent * scale );

	stbtt_GetFontBoundingBox( &m_fontInfo, &x0, &y0, &x1, &y1 );
	m_iHeight = round(( y1 - y0 ) * scale ); // maybe wrong!
	m_iMaxCharWidth = round(( x1 - x0 ) * scale ); // maybe wrong!

	return true;
}

void CStbFont::GetCharRGBA(int ch, Point pt, Size sz, unsigned char *rgba, Size &drawSize )
{
	byte *buf, *dst;
	int a, b, c;

	GetCharABCWidths( ch, a, b, c ); // speed up cache

	int bm_top, bm_left, bm_rows, bm_width;

	buf = stbtt_GetCodepointBitmap( &m_fontInfo, scale, scale, ch, &bm_width, &bm_rows, &bm_left, &bm_top );

	// see where we should start rendering
	const int pushDown = m_iAscent + bm_top;
	const int pushLeft = bm_left;

	// set where we start copying from
	int ystart = 0;
	if( pushDown < 0 )
		ystart = -pushDown;

	int xstart = 0;
	if( pushLeft < 0 )
		xstart = -pushLeft;

	int yend = bm_rows;
	if( pushDown + yend > sz.h )
		yend += sz.h - ( pushDown + yend );

	int xend = bm_width;
	if( pushLeft + xend > sz.w )
		xend += sz.w - ( pushLeft + xend );

	buf = &buf[ ystart * bm_width ];
	dst = rgba + 4 * sz.w * ( ystart + pushDown );

	// iterate through copying the generated dib into the texture
	for (int j = ystart; j < yend; j++, dst += 4 * sz.w, buf += bm_width )
	{
		unsigned int *xdst = (unsigned int*)(dst + 4 * ( m_iBlur + m_iOutlineSize ));
		for (int i = xstart; i < xend; i++, xdst++)
		{
			if( buf[i] > 0 )
			{
				// paint white and gamma-corrected alpha
				byte a = (byte)(powf( buf[i] / 255.0f, 1.0f / 2.2f ) * 255.0f + 0.5f);
				*xdst = TTF_PackRGBA( 0xFF, 0xFF, 0xFF, a );
			}
			else
			{
				// paint black and null alpha
				*xdst = 0;
			}
		}
	}

	drawSize.w = xend - xstart + m_iBlur * 2 + m_iOutlineSize * 2;
	drawSize.h = yend - ystart + m_iBlur * 2 + m_iOutlineSize * 2;

	ApplyBlur( sz, rgba );
	ApplyOutline( Point( xstart, ystart ), sz, rgba );
	ApplyScanline( sz, rgba );
	ApplyStrikeout( sz, rgba );
}

void CStbFont::GetCharABCWidthsNoCache(int ch, int &a, int &b, int &c)
{
	int glyphId = stbtt_FindGlyphIndex( &m_fontInfo, ch );

	int x0, x1;
	int width, horiBearingX, horiAdvance;

	stbtt_GetGlyphBox( &m_fontInfo, glyphId, &x0, NULL, &x1, NULL );
	stbtt_GetCodepointHMetrics( &m_fontInfo, ch, &horiAdvance, &horiBearingX );
	width = x1 - x0;

	a = round( horiBearingX * scale );
	b = round( width * scale );
	c = round(( horiAdvance - horiBearingX - width ) * scale );
}

bool CStbFont::HasChar(int ch) const
{
	return true;
}

#endif // XASH_ENGINE_FONT_STB
