/*
FreeTypeFont.h -- freetype2 font renderer
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
#ifndef FREETYPEFONT_H
#define FREETYPEFONT_H

#if defined( XASH_ENGINE_FONT_FREETYPE )

#include "BaseFontBackend.h"

extern "C"
{
    #include <ft2build.h>
    #include FT_FREETYPE_H
}

class CFreeTypeFont : public CTrueTypeFont
{
public:
	CFreeTypeFont();
	~CFreeTypeFont() override;

	bool Create(const char *name,
		int tall, int weight,
		int blur, float brighten,
		int outlineSize,
		int scanlineOffset, float scanlineScale,
		int flags) override;
	void GetCharRGBA(int ch, Point pt, Size sz, unsigned char *rgba, Size &drawSize) override;
	void GetCharABCWidthsNoCache( int ch, int &a, int &b, int &c ) override;
	bool HasChar( int ch ) const override;
	const char *GetBackendName() const override { return "ft2"; }

	static void InitLibrary();
	static void DoneLibrary();

private:
	FT_Face face;
	static FT_Library m_Library;
	byte *m_pFontData;

	friend class CTrueTypeFontManager;
};

#endif // XASH_ENGINE_FONT_FREETYPE

#endif // FREETYPEFONT_H
