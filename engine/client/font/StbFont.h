/*
StbFont.h - stb_truetype font backend
Copyright (C) 2017 a1batross
*/
#pragma once

#if defined( XASH_ENGINE_FONT_STB )
#include "TrueTypeFont.h"
#include "stb_truetype.h"

class CStbFont : public CTrueTypeFont
{
public:
	CStbFont();
	~CStbFont();

	bool Create(const char *name,
		int tall, int weight,
		int blur, float brighten,
		int outlineSize,
		int scanlineOffset, float scanlineScale,
		int flags) override;
	void GetCharRGBA(int ch, Point pt, Size sz, unsigned char *rgba, Size &drawSize) override;
	void GetCharABCWidthsNoCache( int ch, int &a, int &b, int &c ) override;
	bool HasChar( int ch ) const override;
	const char *GetBackendName() const override { return "stb"; }

private:
	byte *m_pFontData;
	stbtt_fontinfo m_fontInfo;

	double scale;

	friend class CTrueTypeFontManager;
};

#endif // XASH_ENGINE_FONT_STB
