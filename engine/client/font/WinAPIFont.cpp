/*
WinAPIFont.cpp - Win32 Font backend
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

#if defined( _WIN32 ) && !defined( XASH_ENGINE_FONT_STB )

extern "C" {
#include "common.h"
}

#include "WinAPIFont.h"

#include <malloc.h>


CWinAPIFont::CWinAPIFont( ) : CTrueTypeFont( )
{
}

CWinAPIFont::~CWinAPIFont( )
{

}

int CALLBACK FontEnumProc( const LOGFONTA *, const TEXTMETRICA *, DWORD, LPARAM lpParam )
{
	CWinAPIFont *font = ( CWinAPIFont * )lpParam;
	font->m_bFound = true;

	return 0;
}

bool CWinAPIFont::Create( const char *name, int tall, int weight, int blur, float brighten, int outlineSize, int scanlineOffset, float scanlineScale, int flags )
{
	Q_strncpy( m_szName, name, sizeof( m_szName )  );

	m_iTall = tall + 6;
	m_iWeight = weight;
	m_iFlags = flags;

	m_iBlur = blur;
	m_fBrighten = brighten;

	m_iOutlineSize = outlineSize;

	m_iScanlineOffset = scanlineOffset;
	m_fScanlineScale = scanlineScale;

	// create device context
	m_hDC = ::CreateCompatibleDC( NULL );

	int charset = DEFAULT_CHARSET;

	// check exists or not
	LOGFONTA font = { 0 };
	font.lfCharSet = DEFAULT_CHARSET; //!!!
	font.lfPitchAndFamily = 0;
	Q_strncpy( font.lfFaceName, m_szName, sizeof( font.lfFaceName ) );
	::EnumFontFamiliesExA( m_hDC, &font, &FontEnumProc, (LPARAM)this, 0 );
	if( !m_bFound )
	{
		Con_Printf( "Couldn't create windows font %s: no font found\n", name );
		return false;
	}

	m_hFont = ::CreateFontA( m_iTall, 0, 0, 0, m_iWeight,
		m_iFlags & TTF_ITALIC,
		m_iFlags & TTF_UNDERLINE,
		m_iFlags & TTF_STRIKEOUT,
		charset, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		PROOF_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font.lfFaceName );

	if( !m_hFont )
	{
		Con_Printf( "Couldn't create windows font %s: CreateFont failed\n", name );
		return false;
	}

	// set as active font
	::SetMapMode( m_hDC, MM_TEXT );
	::SelectObject( m_hDC, m_hFont );
	::SetTextAlign( m_hDC, TA_LEFT | TA_TOP | TA_UPDATECP );

	::TEXTMETRIC tm = { 0 };
	if( !GetTextMetrics( m_hDC, &tm ) )
	{
		Con_Printf( "Couldn't create windows font %s: GetTextMetrics failed\n", name );
		return false;
	}

	m_iHeight = tm.tmHeight + 2 * m_iOutlineSize;
	m_iMaxCharWidth = tm.tmMaxCharWidth;
	m_iAscent = tm.tmAscent;

	m_rgiBitmapSize[0] = tm.tmMaxCharWidth + m_iOutlineSize * 2;
	m_rgiBitmapSize[1] = tm.tmHeight + m_iOutlineSize * 2;

	::BITMAPINFOHEADER header = { 0 };
	header.biSize = sizeof( header );
	header.biWidth = m_rgiBitmapSize[0];
	header.biHeight = -m_rgiBitmapSize[1];
	header.biPlanes = 1;
	header.biBitCount = 32;
	header.biCompression = BI_RGB;

	m_hDIB = ::CreateDIBSection( m_hDC, ( BITMAPINFO* )&header, DIB_RGB_COLORS, ( void** )&m_pBuf, NULL, 0 );
	::SelectObject( m_hDC, m_hDIB );

	return true;
}

void CWinAPIFont::GetCharRGBA( int ch, Point pt, Size sz, unsigned char *rgba, Size &drawSize )
{
	// set us up to render into our dib
	::SelectObject( m_hDC, m_hFont );

	int a, b, c;
	GetCharABCWidths( ch, a, b, c );
	
	int wide = b;
	if( m_iFlags & TTF_UNDERLINE )
	{
		wide += ( a + c );
	}
	
	int tall = m_iHeight;
	GLYPHMETRICS glyphMetrics;
	MAT2 mat2 = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
	int bytesNeeded = 0;
	
	// try and get the glyph directly

	::SelectObject( m_hDC, m_hFont );
	bytesNeeded = ::GetGlyphOutline( m_hDC, ch, GGO_GRAY8_BITMAP, &glyphMetrics, 0, NULL, &mat2 );
	
	if( bytesNeeded > 0 )
	{
		// take it
		unsigned char *lpbuf = ( unsigned char * )_alloca( bytesNeeded );
		::GetGlyphOutline( m_hDC, ch, GGO_GRAY8_BITMAP, &glyphMetrics, bytesNeeded, lpbuf, &mat2 );
		
		// rows are on DWORD boundaries
		wide = glyphMetrics.gmBlackBoxX;
		while( wide % 4 != 0 )
		{
			wide++;
		}

		// see where we should start rendering
		int pushDown = m_iAscent - glyphMetrics.gmptGlyphOrigin.y;
		
		// set where we start copying from
		int xstart = 0;
		
		// don't copy the first set of pixels if the antialiased bmp is bigger than the char width
		if( ( int )glyphMetrics.gmBlackBoxX >= b + 2 )
		{
			xstart = ( glyphMetrics.gmBlackBoxX - b ) / 2;
		}

		// iterate through copying the generated dib into the texture
		for( unsigned int j = 0; j < glyphMetrics.gmBlackBoxY; j++ )
		{
			for( unsigned int i = xstart; i < glyphMetrics.gmBlackBoxX; i++ )
			{
				int x = i - xstart + m_iBlur + m_iOutlineSize;
				int y = j + pushDown;
				if( ( x < sz.w ) && ( y < sz.h ) )
				{
					unsigned char grayscale = lpbuf[( j*wide + i )];

					float r, g, b, a;
					if( grayscale )
					{
						r = g = b = 1.0f;
						a = ( grayscale + 0 ) / 64.0f;
						if( a > 1.0f ) a = 1.0f;
					}
					else
					{
						r = g = b = 0.0f;
						a = 0.0f;
					}

					unsigned char *dst = &rgba[( y*sz.w + x ) * 4];
					dst[0] = ( unsigned char )( r * 255.0f );
					dst[1] = ( unsigned char )( g * 255.0f );
					dst[2] = ( unsigned char )( b * 255.0f );
					dst[3] = ( unsigned char )( a * 255.0f );
				}
			}
		}

		drawSize.w = glyphMetrics.gmBlackBoxX - xstart + m_iOutlineSize * 2 + m_iBlur * 2;;
		drawSize.h = glyphMetrics.gmBlackBoxY;
	}
	else
	{
		// use render-to-bitmap to get our font texture
		::SetBkColor( m_hDC, RGB( 0, 0, 0 ) );
		::SetTextColor( m_hDC, RGB( 255, 255, 255 ) );
		::SetBkMode( m_hDC, OPAQUE );
		if( m_iFlags & TTF_UNDERLINE )
		{
			::MoveToEx( m_hDC, 0, 0, NULL );
		}
		else
		{
			::MoveToEx( m_hDC, -a, 0, NULL );
		}

		// render the character
		wchar_t wch = ( wchar_t )ch;

		// just use the unicode renderer
		::ExtTextOutW( m_hDC, 0, 0, 0, NULL, &wch, 1, NULL );

		::SetBkMode( m_hDC, TRANSPARENT );
		if( wide > m_rgiBitmapSize[0] )
		{
			wide = m_rgiBitmapSize[0];
		}
		if( tall > m_rgiBitmapSize[1] )
		{
			tall = m_rgiBitmapSize[1];
		}

		// iterate through copying the generated dib into the texture
		for( int j = ( int )m_iOutlineSize; j < tall - ( int )m_iOutlineSize; j++ )
		{
			// only copy from within the dib, ignore the outline border we are artificially adding
			for( int i = ( int )m_iOutlineSize; i < wide - ( int )m_iOutlineSize; i++ )
			{
				if( ( i < sz.w ) && ( j < sz.h ) )
				{
					unsigned char *src = &m_pBuf[( i + j*m_rgiBitmapSize[0] ) * 4];
					unsigned char *dst = &rgba[( i + j*sz.w ) * 4];

					// Don't want anything drawn for tab characters.
					unsigned char r, g, b;
					if( ch == '\t' )
					{
						r = g = b = 0;
					}
					else
					{
						r = src[0];
						g = src[1];
						b = src[2];
					}

					// generate alpha based on luminance conversion
					dst[0] = r;
					dst[1] = g;
					dst[2] = b;
					dst[3] = ( unsigned char )( ( float )r * 0.34f + ( float )g * 0.55f + ( float )b * 0.11f );
				}
			}
		}

		drawSize.w = wide + m_iOutlineSize + m_iBlur * 2;
		drawSize.h = tall;
	}
	ApplyBlur( sz, rgba );
	ApplyOutline( Point( 0, 0 ), sz, rgba );
	ApplyScanline( sz, rgba );
	ApplyStrikeout( sz, rgba );
}

void CWinAPIFont::GetCharABCWidthsNoCache( int ch, int &a, int &b, int &c )
{
	// not in the cache, get from windows (this call is a little slow)
	::SelectObject( m_hDC, m_hFont );
	ABC abc;
	if( ::GetCharABCWidthsW( m_hDC, ch, ch, &abc ) || ::GetCharABCWidthsA( m_hDC, ch, ch, &abc ) )
	{
		a = abc.abcA;
		b = abc.abcB;
		c = abc.abcC;
	}
	else
	{
		// failed to get width, just use the max width
		a = 0;
		b = m_iMaxCharWidth;
		c = 0;
	}
}

bool CWinAPIFont::HasChar( int ch ) const
{
	return true;
}

#endif // _WIN32 && !XASH_ENGINE_FONT_STB
