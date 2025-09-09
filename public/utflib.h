/*
utflib.h - small unicode conversion library
Copyright (C) 2024 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef UTFLIB_H
#define UTFLIB_H

#include STDINT_H
#include <stddef.h>

typedef struct utfstate_s
{
	uint32_t uc;
	uint8_t len;
	uint8_t k;
} utfstate_t;

// feed utf8 characters one by one
// if it returns 0, feed more
// utfstate_t must be zero initialized
uint32_t Q_DecodeUTF8( utfstate_t *s, uint32_t ch );
uint32_t Q_DecodeUTF16( utfstate_t *s, uint32_t ch );
size_t Q_EncodeUTF8( char dst[4], uint32_t ch );

size_t Q_UTF8Length( const char *s );

// srcsize in byte pairs
size_t Q_UTF16ToUTF8( char *dst, size_t dstsize, const uint16_t *src, size_t srcsize );

// function to convert Unicode codepoints into CP1251 or CP1252
uint32_t Q_UnicodeToCP1251( uint32_t uc );
uint32_t Q_UnicodeToCP1252( uint32_t uc );

#endif // UTFLIB_H
