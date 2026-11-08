/*
text.h - UTF-8 helpers and growable string buffer for xrcon-tui
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

#ifndef XRCON_TUI_TEXT_H
#define XRCON_TUI_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "xash3d_types.h"

#define TEXT_TAB_WIDTH 8

typedef struct
{
	char *data;
	size_t len;
	size_t cap;
} strbuf_t;

void Buf_Free( strbuf_t *b );
void Buf_Reset( strbuf_t *b );
void Buf_Append( strbuf_t *b, const char *s, size_t len );
void Buf_AppendStr( strbuf_t *b, const char *s );
void Buf_AppendChar( strbuf_t *b, uint32_t uc );
void Buf_Repeat( strbuf_t *b, char c, size_t count );
void Buf_Printf( strbuf_t *b, const char *fmt, ... ) FORMAT_CHECK( 2 );

qboolean Text_IsPrintable( uint32_t uc );
int Text_CharWidth( uint32_t uc );
int Text_Width( const char *s, size_t len );
size_t Text_FitWidth( const char *s, size_t len, int max_width );

// copies only printable characters to valid UTF-8, expands tabs, returns length
size_t Text_Sanitize( char *dst, size_t size, const char *src, size_t len );

#endif // XRCON_TUI_TEXT_H
