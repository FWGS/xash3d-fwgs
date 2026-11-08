/*
transform.c - source post-processing pass
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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "xcf.h"

#define XCF_RAW_DELIM_MAX 16 // C++11: 16 chars max in raw-string delimiter

/*
============
BufFree

============
*/
void BufFree( xcf_buf_t *b )
{
	free( b->data );
	b->data = NULL;
	b->len = b->cap = 0;
}

/*
============
BufGrow

============
*/
static qboolean BufGrow( xcf_buf_t *b, size_t need )
{
	size_t cap;
	char *p;

	if( b->len + need <= b->cap )
		return true;

	cap = b->cap ? b->cap : 256;
	while( cap < b->len + need )
		cap *= 2;

	p = realloc( b->data, cap );
	if( !p )
		return false;

	b->data = p;
	b->cap = cap;
	return true;
}

/*
============
BufPutChar

============
*/
qboolean BufPutChar( xcf_buf_t *b, char c )
{
	if( !BufGrow( b, 1 ))
		return false;
	b->data[b->len++] = c;
	return true;
}

/*
============
BufPutMem

============
*/
qboolean BufPutMem( xcf_buf_t *b, const char *src, size_t n )
{
	if( !n )
		return true;
	if( !BufGrow( b, n ))
		return false;
	memcpy( b->data + b->len, src, n );
	b->len += n;
	return true;
}

/*
============
TryCollapse

Collapse spaces between the same type parentheses
============
*/
static void TryCollapse( xcf_buf_t *out, char paren )
{
	size_t i = out->len;

	while( i > 0 && ( out->data[i - 1] == ' ' || out->data[i - 1] == '\t' ))
		i--;

	if( i == 0 || i == out->len )
		return; // nothing to collapse, or no whitespace between

	if( out->data[i - 1] != paren )
		return; // different bracket types or different char entirely

	out->len = i;
}

/*
============
DetectRaw

Detect C++11 raw string because why not
============
*/
static qboolean DetectRaw( const char *s, size_t len, size_t i, size_t *prefix_end, size_t *paren )
{
	size_t j = i, k;

	// must not be in the middle of an identifier
	if( i > 0 && ( isalnum( s[i - 1] ) || s[i - 1] == '_' ))
		return false;

	if( j + 2 < len && s[j] == 'u' && s[j + 1] == '8' && s[j + 2] == 'R' )
		j += 3;
	else if( j + 1 < len && ( s[j] == 'u' || s[j] == 'U' || s[j] == 'L' ) && s[j + 1] == 'R' )
		j += 2;
	else if( j < len && s[j] == 'R' )
		j += 1;
	else
		return false;

	if( j >= len || s[j] != '"' )
		return false;
	j++; // past the opening quote
	*prefix_end = j;

	k = j;
	while( k < len && s[k] != '(' && k - j < XCF_RAW_DELIM_MAX )
		k++;

	if( k >= len || s[k] != '(' )
		return false;

	*paren = k;
	return true;
}

enum
{
	S_NORMAL,
	S_LINE_CMT,
	S_BLOCK_CMT,
	S_STRING,
	S_CHAR,
	S_RAW,
};

/*
============
Transform

Parse source, find parens and try collapse them
============
*/
qboolean Transform( const char *in, size_t in_len, xcf_buf_t *out )
{
	size_t raw_dlen = 0;
	char raw_delim[XCF_RAW_DELIM_MAX + 1];
	int state = S_NORMAL;

	for( size_t i = 0; i < in_len; i++ )
	{
		char c1 = in[i];
		char c2 = ( i + 1 < in_len ) ? in[i + 1] : 0;

		switch( state )
		{
		case S_NORMAL:
		{
			size_t pe, pp;

			if( c1 == '/' && c2 == '/' )
			{
				if( !BufPutMem( out, in + i, 2 ))
					return false;
				i++;
				state = S_LINE_CMT;
				break;
			}

			if( c1 == '/' && c2 == '*' )
			{
				if( !BufPutMem( out, in + i, 2 ))
					return false;
				i++;
				state = S_BLOCK_CMT;
				break;
			}

			if( DetectRaw( in, in_len, i, &pe, &pp ))
			{
				size_t dl = pp - pe;

				if( dl > XCF_RAW_DELIM_MAX )
					dl = XCF_RAW_DELIM_MAX;

				if( !BufPutMem( out, in + i, pp - i + 1 ))
					return false;
				memcpy( raw_delim, in + pe, dl );
				raw_delim[dl] = 0;
				raw_dlen = dl;
				i = pp; // sit on the '('
				state = S_RAW;
				break;
			}

			if( c1 == '"' )
			{
				if( !BufPutChar( out, c1 ))
					return false;
				state = S_STRING;
				break;
			}

			if( c1 == '\'' )
			{
				if( !BufPutChar( out, c1 ))
					return false;
				state = S_CHAR;
				break;
			}

			if( c1 == '(' || c1 == ')' )
				TryCollapse( out, c1 );

			if( !BufPutChar( out, c1 ))
				return false;
			break;
		}

		case S_LINE_CMT:
			if( !BufPutChar( out, c1 ))
				return false;
			if( c1 == '\n' )
				state = S_NORMAL;
			break;

		case S_BLOCK_CMT:
			if( !BufPutChar( out, c1 ))
				return false;
			if( c1 == '*' && c2 == '/' )
			{
				if( !BufPutChar( out, c2 ))
					return false;
				i++;
				state = S_NORMAL;
			}
			break;

		case S_STRING:
			if( !BufPutChar( out, c1 ))
				return false;
			if( c1 == '\\' && c2 )
			{
				if( !BufPutChar( out, c2 ))
					return false;
				i++;
			}
			else if( c1 == '"' )
			{
				state = S_NORMAL;
			}
			else if( c1 == '\n' )
			{
				// unterminated; recover at next line
				state = S_NORMAL;
			}
			break;

		case S_CHAR:
			if( !BufPutChar( out, c1 ))
				return false;
			if( c1 == '\\' && c2 )
			{
				if( !BufPutChar( out, c2 ))
					return false;
				i++;
			}
			else if( c1 == '\'' )
			{
				state = S_NORMAL;
			}
			else if( c1 == '\n' )
			{
				state = S_NORMAL;
			}
			break;

		case S_RAW:
			if( !BufPutChar( out, c1 ))
				return false;
			if( c1 == ')'
				&& i + 1 + raw_dlen + 1 <= in_len
				&& memcmp( in + i + 1, raw_delim, raw_dlen ) == 0
				&& in[i + 1 + raw_dlen] == '"' )
			{
				if( !BufPutMem( out, in + i + 1, raw_dlen + 1 ))
					return false;
				i += raw_dlen + 1;
				state = S_NORMAL;
			}
			break;
		}
	}

	return true;
}
