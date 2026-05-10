/*
TrueTypeFont.h - engine truetype font
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
#pragma once
#ifndef TRUETYPEFONT_H
#define TRUETYPEFONT_H

#include "xash3d_types.h"
#include "wrect.h"
#include "utlrbtree.h"
#include "utlvector.h"
#include "BMPUtils.h"

// font flags (EFontFlags)
#define TTF_ITALIC    ( 1 << 0 )
#define TTF_UNDERLINE ( 1 << 1 )
#define TTF_STRIKEOUT ( 1 << 2 )

#if defined(__ANDROID__) || TARGET_OS_IPHONE || defined(XASH_SAILFISH) || defined(MAINUI_FONT_SCALE)
#define SCALE_FONTS
#endif

#if defined(_WIN32)
#undef GetCharABCWidths
#undef CreateFont
#endif // defined(_WIN32)

// packed color helpers
inline unsigned int TTF_PackRGBA( const unsigned int r, const unsigned int g, const unsigned int b, const unsigned int a )
{
	return ((a)<<24|(r)<<16|(g)<<8|(b));
}

inline void TTF_UnpackRGBA( int &r, int &g, int &b, int &a, const unsigned int c )
{
	a = (c >> 24) & 0xFF;
	r = (c >> 16) & 0xFF;
	g = (c >>  8) & 0xFF;
	b = (c      ) & 0xFF;
}

struct ttf_Point { int x, y; ttf_Point() : x(0),y(0){} ttf_Point(int x,int y):x(x),y(y){} };
struct ttf_Size  { int w, h; ttf_Size()  : w(0),h(0){} ttf_Size(int w,int h):w(w),h(h){} };

typedef ttf_Point Point;
typedef ttf_Size  Size;

struct charRange_t
{
	uint32_t chMin;
	uint32_t chMax;
	const uint32_t *sequence;
	size_t size;

	size_t Length() const
	{
		if( sequence )
			return size;
		return chMax - chMin;
	}

	int Character( size_t pos )
	{
		if( sequence )
			return sequence[pos];
		return chMin + pos;
	}
};

class CTrueTypeFont
{
public:
	CTrueTypeFont();
	virtual ~CTrueTypeFont( );

	virtual bool Create(
		const char *name,
		int tall, int weight,
		int blur, float brighten,
		int outlineSize,
		int scanlineOffset, float scanlineScale,
		int flags ) = 0;
	virtual void GetCharRGBA( int ch, Point pt, Size sz, byte *rgba, Size &drawSize ) = 0;
	virtual void GetCharABCWidthsNoCache( int ch, int &a, int &b, int &c ) = 0;
	virtual bool HasChar( int ch ) const = 0;
	virtual const char *GetBackendName() const = 0;
	virtual void GetCharABCWidths( int ch, int &a, int &b, int &c );
	virtual void UploadGlyphsForRanges( charRange_t *range, int rangeSize );
	virtual int  DrawCharacter(int ch, Point pt, int charH, const unsigned int color, bool forceAdditive = false);

	inline int GetHeight() const       { return m_iHeight + GetEfxOffset(); }
	inline int GetTall() const         { return m_iTall; }
	inline const char *GetName() const { return m_szName; }
	inline int GetAscent() const       { return m_iAscent; }
	inline int GetMaxCharWidth() const { return m_iMaxCharWidth; }
	inline int GetFlags() const        { return m_iFlags; }
	inline int GetWeight() const       { return m_iWeight; }
	inline int GetEfxOffset() const    { return m_iBlur + m_iOutlineSize; }

	void GetTextureName(char *dst, size_t len) const;

	inline int GetEllipsisWide( ) { return m_iEllipsisWide; }

protected:
	void ApplyBlur( Size rgbaSz, byte *rgba );
	void ApplyOutline(Point pt, Size rgbaSz, byte *rgba );
	void ApplyScanline( Size rgbaSz, byte *rgba );
	void ApplyStrikeout( Size rgbaSz, byte *rgba );

	char m_szName[32];
	int	 m_iTall, m_iWeight, m_iFlags, m_iHeight, m_iMaxCharWidth;
	int  m_iAscent;
	bool m_bAdditive;

	// blurring
	int  m_iBlur;
	float m_fBrighten;

	// Scanlines
	int  m_iScanlineOffset;
	float m_fScanlineScale;

	// Outlines
	int  m_iOutlineSize;
	int m_iEllipsisWide;

private:
	bool ReadFromCache( const char *filename, charRange_t *range, size_t rangeSize );
	void SaveToCache( const char *filename, charRange_t *range, size_t rangeSize, CBMP *bmp );

	void GetBlurValueForPixel( double *distribution, const byte *src, Point srcPt, Size srcSz, byte *dest );

	struct glyph_t
	{
		glyph_t() : ch( 0 ), texture( 0 ), rect(), offX( 0 ), offY( 0 ) { }
		glyph_t( int ch ) : ch( ch ), texture( 0 ), rect(), offX( 0 ), offY( 0 ) { }
		int ch;
		int texture; // engine GL texture handle
		wrect_t rect;
		int offX; // pixels from rect left to glyph origin
		int offY; // pixels from rect top to glyph baseline

		bool operator< (const glyph_t &a) const
		{
			return ch < a.ch;
		}
	};

	struct abc_t
	{
		int ch;
		int a, b, c;

		bool operator< ( const abc_t &a ) const
		{
			return ch < a.ch;
		}
	};

	CUtlRBTree<glyph_t, int> m_glyphs;
	CUtlRBTree<abc_t, int>   m_ABCCache;

	char m_szTextureName[256];
};

// font manager
class CTrueTypeFontManager
{
public:
	CTrueTypeFontManager();
	~CTrueTypeFontManager();

	void DeleteAllFonts();
	void DeleteFont( CTrueTypeFont *font );

	CTrueTypeFont *CreateFont( const char *name, int tall, int weight, int flags, int outlineSize = 0 );
	void UploadTextureForFont( CTrueTypeFont *font );

	bool FindFontDataFile( const char *name, int tall, int weight, int flags, char *dataFile, size_t dataFileChars );
	byte *LoadFontDataFile( const char *vfspath, int *plen );

private:
	CUtlVector<CTrueTypeFont*> m_Fonts;

	// cached TTF file bytes to avoid double-loading the same .ttf
	struct fontfile_t
	{
		char path[256];
		byte *data;
		int   len;
	};
	CUtlVector<fontfile_t> m_FontFiles;
};

extern CTrueTypeFontManager *g_TTFontMgr; // global instance, created in TrueTypeFont.cpp

#endif // TRUETYPEFONT_H
