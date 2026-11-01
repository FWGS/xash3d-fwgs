/*
text.c - UTF-8 helpers and growable string buffer for xrcon-tui
Copyright (C) 2026 Xash3D FWGS contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "crtlib.h"
#include "utflib.h"
#include "text.h"

static qboolean Buf_Reserve( strbuf_t *b, size_t extra )
{
	if( b->len + extra < b->cap )
		return true;

	size_t cap = b->cap ? b->cap : 256;
	while( cap <= b->len + extra )
		cap *= 2;

	char *data = (char *)realloc( b->data, cap );
	if( !data )
		return false;

	b->data = data;
	b->cap = cap;
	return true;
}

void Buf_Free( strbuf_t *b )
{
	free( b->data );
	memset( b, 0, sizeof( *b ));
}

void Buf_Reset( strbuf_t *b )
{
	b->len = 0;
	if( b->data )
		b->data[0] = '\0';
}

void Buf_Append( strbuf_t *b, const char *s, size_t len )
{
	if( !Buf_Reserve( b, len ))
		return;

	memcpy( b->data + b->len, s, len );
	b->len += len;
	b->data[b->len] = '\0';
}

void Buf_AppendStr( strbuf_t *b, const char *s )
{
	Buf_Append( b, s, Q_strlen( s ));
}

void Buf_AppendChar( strbuf_t *b, uint32_t uc )
{
	char enc[4];
	size_t len = Q_EncodeUTF8( enc, uc );

	Buf_Append( b, enc, len );
}

void Buf_Repeat( strbuf_t *b, char c, size_t count )
{
	if( !Buf_Reserve( b, count ))
		return;

	memset( b->data + b->len, c, count );
	b->len += count;
	b->data[b->len] = '\0';
}

void Buf_Printf( strbuf_t *b, const char *fmt, ... )
{
	char tmp[1024];
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( tmp, sizeof( tmp ), fmt, ap );
	va_end( ap );

	Buf_AppendStr( b, tmp );
}

qboolean Text_IsPrintable( uint32_t uc )
{
	if( uc < 0x20 || ( uc >= 0x7f && uc < 0xa0 ))
		return false;

	return uc <= 0x10ffff && ( uc < 0xd800 || uc > 0xdfff );
}

// the engine console has only cp1251 and cp1252 glyphs, all of them take one cell
int Text_CharWidth( uint32_t uc )
{
	return Text_IsPrintable( uc ) ? 1 : 0;
}

int Text_Width( const char *s, size_t len )
{
	int width = 0;
	size_t i = 0;

	while( i < len )
	{
		uint32_t uc;

		i += Q_DecodeUTF8Char( s + i, len - i, &uc );
		width += Text_CharWidth( uc );
	}

	return width;
}

size_t Text_FitWidth( const char *s, size_t len, int max_width )
{
	int width = 0;
	size_t i = 0;

	while( i < len )
	{
		uint32_t uc;
		size_t n = Q_DecodeUTF8Char( s + i, len - i, &uc );

		width += Text_CharWidth( uc );
		if( width > max_width )
			break;

		i += n;
	}

	return i;
}

size_t Text_Sanitize( char *dst, size_t size, const char *src, size_t len )
{
	size_t out = 0;
	size_t i = 0;
	int col = 0;

	if( size == 0 )
		return 0;

	while( i < len )
	{
		char enc[4];
		uint32_t uc;
		size_t n;

		i += Q_DecodeUTF8Char( src + i, len - i, &uc );

		if( uc == '\t' )
		{
			n = TEXT_TAB_WIDTH - col % TEXT_TAB_WIDTH;
			if( out + n >= size )
				break;

			memset( dst + out, ' ', n );
			out += n;
			col += (int)n;
			continue;
		}

		if( !Text_IsPrintable( uc ))
			continue;

		n = Q_EncodeUTF8( enc, uc );
		if( out + n >= size )
			break;

		memcpy( dst + out, enc, n );
		out += n;
		col += Text_CharWidth( uc );
	}

	dst[out] = '\0';
	return out;
}
