/*
WinAPIFont.h - Win32 Font backend
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
#if !defined(WINAPIFONT_H) && defined( _WIN32 ) && !defined( XASH_ENGINE_FONT_STB )
#define WINAPIFONT_H

#define WIN32_LEAN_AND_MEAN
#define UNICODE // use unicode fonts
#include <windows.h>
#undef GetCharABCWidths

#include "BaseFontBackend.h"

class CWinAPIFont : public CTrueTypeFont
{
public:
	CWinAPIFont( );
	~CWinAPIFont( );

	bool Create( const char *name,
		int tall, int weight,
		int blur, float brighten,
		int outlineSize,
		int scanlineOffset, float scanlineScale,
		int flags ) override;
	void GetCharRGBA( int ch, Point pt, Size sz, unsigned char *rgba, Size &drawSize ) override;
	void GetCharABCWidthsNoCache( int ch, int &a, int &b, int &c ) override;
	bool HasChar( int ch ) const override;
	const char *GetBackendName() const override { return "win32"; }

	bool m_bFound;

private:
	HFONT m_hFont;
	HDC m_hDC;
	HBITMAP m_hDIB;

	int m_rgiBitmapSize[2];

	// pointer to buffer for use when generated bitmap versions of a texture
	unsigned char	*m_pBuf;

	friend class CTrueTypeFontManager;
	friend int CALLBACK FontEnumProc( const LOGFONT *, const TEXTMETRIC *, DWORD, LPARAM lpParam );
};

#endif // WINAPIFONT_H
