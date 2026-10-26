/*
json.c - minimal JSON encoder and tokenising parser for xrcon-mcp
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
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

#include "crtlib.h"
#include "json.h"

typedef struct
{
	const char *js;
	int len;
	int pos;
	json_tok_t *toks;
	int ntoks;
	int max_toks;
	int superparent;
} json_parser_t;

static int JSON_AllocToken( json_parser_t *p )
{
	if( p->ntoks >= p->max_toks )
		return -1;

	json_tok_t *tok = &p->toks[p->ntoks++];
	tok->type = JSON_UNDEFINED;
	tok->start = -1;
	tok->end = -1;
	tok->size = 0;
	tok->parent = -1;
	return p->ntoks - 1;
}

static int JSON_ParseString( json_parser_t *p )
{
	int start = p->pos;
	p->pos++; // skip opening quote

	while( p->pos < p->len )
	{
		char c = p->js[p->pos];

		if( c == '"' )
		{
			int idx = JSON_AllocToken( p );
			if( idx < 0 )
				return -1;

			p->toks[idx].type = JSON_STRING;
			p->toks[idx].start = start + 1;
			p->toks[idx].end = p->pos;
			p->toks[idx].parent = p->superparent;
			p->pos++;
			return idx;
		}

		if( c == '\\' && p->pos + 1 < p->len )
		{
			char esc = p->js[p->pos + 1];
			switch( esc )
			{
			case '"': case '/': case '\\': case 'b':
			case 'f': case 'r': case 'n': case 't':
				p->pos += 2;
				continue;
			case 'u':
				if( p->pos + 5 >= p->len )
					return -1;
				for( int i = 2; i < 6; i++ )
				{
					if( !isxdigit((unsigned char)p->js[p->pos + i] ))
						return -1;
				}
				p->pos += 6;
				continue;
			default:
				return -1;
			}
		}

		if( (unsigned char)c < 0x20 )
			return -1;

		p->pos++;
	}

	return -1;
}

static int JSON_ParsePrimitive( json_parser_t *p )
{
	int start = p->pos;

	while( p->pos < p->len )
	{
		char c = p->js[p->pos];
		if( c == ',' || c == ']' || c == '}' || c == ' ' || c == '\t' || c == '\r' || c == '\n' )
			break;
		if( (unsigned char)c < 32 || (unsigned char)c >= 127 )
			return -1;
		p->pos++;
	}

	int idx = JSON_AllocToken( p );
	if( idx < 0 )
		return -1;

	p->toks[idx].type = JSON_PRIMITIVE;
	p->toks[idx].start = start;
	p->toks[idx].end = p->pos;
	p->toks[idx].parent = p->superparent;
	return idx;
}

int JSON_Parse( const char *js, int len, json_tok_t *toks, int max_toks )
{
	json_parser_t p;
	p.js = js;
	p.len = len;
	p.pos = 0;
	p.toks = toks;
	p.ntoks = 0;
	p.max_toks = max_toks;
	p.superparent = -1;

	while( p.pos < p.len )
	{
		char c = p.js[p.pos];

		if( c == ' ' || c == '\t' || c == '\r' || c == '\n' )
		{
			p.pos++;
			continue;
		}

		if( c == '{' || c == '[' )
		{
			int idx = JSON_AllocToken( &p );
			if( idx < 0 )
				return -1;

			if( p.superparent >= 0 )
			{
				toks[p.superparent].size++;
				toks[idx].parent = p.superparent;
			}

			toks[idx].type = ( c == '{' ) ? JSON_OBJECT : JSON_ARRAY;
			toks[idx].start = p.pos;
			p.superparent = idx;
			p.pos++;
			continue;
		}

		if( c == '}' || c == ']' )
		{
			json_type_t expected = ( c == '}' ) ? JSON_OBJECT : JSON_ARRAY;
			if( p.superparent < 0 || toks[p.superparent].type != expected )
				return -1;

			toks[p.superparent].end = p.pos + 1;
			p.superparent = toks[p.superparent].parent;
			p.pos++;
			continue;
		}

		if( c == '"' )
		{
			int idx = JSON_ParseString( &p );
			if( idx < 0 )
				return -1;

			if( p.superparent >= 0 )
				toks[p.superparent].size++;
			continue;
		}

		if( c == ':' )
		{
			// the most-recent string under an object becomes its key:
			// re-parent the next value at the same level
			p.pos++;
			continue;
		}

		if( c == ',' )
		{
			p.pos++;
			continue;
		}

		// primitive: digits, '-', 't', 'f', 'n'
		if( c == '-' || ( c >= '0' && c <= '9' ) || c == 't' || c == 'f' || c == 'n' )
		{
			int idx = JSON_ParsePrimitive( &p );
			if( idx < 0 )
				return -1;
			if( p.superparent >= 0 )
				toks[p.superparent].size++;
			continue;
		}

		return -1;
	}

	if( p.superparent != -1 )
		return -1;

	return p.ntoks;
}

int JSON_TokenLen( const json_doc_t *doc, int tok_idx )
{
	if( tok_idx < 0 || tok_idx >= doc->ntoks )
		return 0;
	return doc->toks[tok_idx].end - doc->toks[tok_idx].start;
}

const char *JSON_TokenPtr( const json_doc_t *doc, int tok_idx )
{
	if( tok_idx < 0 || tok_idx >= doc->ntoks )
		return NULL;
	return doc->json + doc->toks[tok_idx].start;
}

int JSON_StringEq( const json_doc_t *doc, int tok_idx, const char *s )
{
	if( tok_idx < 0 || tok_idx >= doc->ntoks )
		return 0;
	const json_tok_t *t = &doc->toks[tok_idx];
	if( t->type != JSON_STRING )
		return 0;
	int n = t->end - t->start;
	if( (int)Q_strlen( s ) != n )
		return 0;
	return Q_strncmp( doc->json + t->start, s, n ) == 0;
}

int JSON_StringDup( const json_doc_t *doc, int tok_idx, char *dst, size_t dstsize )
{
	if( tok_idx < 0 || tok_idx >= doc->ntoks || dstsize == 0 )
		return -1;

	const json_tok_t *t = &doc->toks[tok_idx];
	if( t->type != JSON_STRING && t->type != JSON_PRIMITIVE )
		return -1;

	size_t in_len = (size_t)( t->end - t->start );
	const char *in = doc->json + t->start;
	size_t out = 0;

	for( size_t i = 0; i < in_len && out + 1 < dstsize; i++ )
	{
		char c = in[i];
		if( c == '\\' && i + 1 < in_len )
		{
			char esc = in[++i];
			switch( esc )
			{
			case '"':  c = '"';  break;
			case '\\': c = '\\'; break;
			case '/':  c = '/';  break;
			case 'b':  c = '\b'; break;
			case 'f':  c = '\f'; break;
			case 'n':  c = '\n'; break;
			case 'r':  c = '\r'; break;
			case 't':  c = '\t'; break;
			case 'u':
				// \uXXXX: decode the codepoint and emit UTF-8.
				// Surrogate pairs are accepted byte-for-byte without re-encoding;
				// MCP traffic is ASCII-dominant in practice.
				if( i + 4 < in_len )
				{
					unsigned cp = 0;
					for( int k = 0; k < 4; k++ )
					{
						char hc = in[i + 1 + k];
						cp <<= 4;
						if( hc >= '0' && hc <= '9' ) cp |= hc - '0';
						else if( hc >= 'a' && hc <= 'f' ) cp |= 10 + hc - 'a';
						else if( hc >= 'A' && hc <= 'F' ) cp |= 10 + hc - 'A';
						else return -1;
					}
					i += 4;
					if( cp < 0x80 )
					{
						dst[out++] = (char)cp;
					}
					else if( cp < 0x800 && out + 2 < dstsize )
					{
						dst[out++] = (char)( 0xC0 | ( cp >> 6 ));
						dst[out++] = (char)( 0x80 | ( cp & 0x3F ));
					}
					else if( out + 3 < dstsize )
					{
						dst[out++] = (char)( 0xE0 | ( cp >> 12 ));
						dst[out++] = (char)( 0x80 | (( cp >> 6 ) & 0x3F ));
						dst[out++] = (char)( 0x80 | ( cp & 0x3F ));
					}
					continue;
				}
				return -1;
			default:
				return -1;
			}
		}
		dst[out++] = c;
	}
	dst[out] = '\0';
	return (int)out;
}

int JSON_TokenAsInt64( const json_doc_t *doc, int tok_idx, int64_t *out )
{
	if( tok_idx < 0 || tok_idx >= doc->ntoks )
		return -1;

	const json_tok_t *t = &doc->toks[tok_idx];
	if( t->type != JSON_PRIMITIVE )
		return -1;

	char buf[32];
	int n = t->end - t->start;
	if( n <= 0 || n >= (int)sizeof( buf ))
		return -1;

	memcpy( buf, doc->json + t->start, n );
	buf[n] = '\0';

	char *endp = NULL;
	long long v = strtoll( buf, &endp, 10 );
	if( endp == buf || *endp != '\0' )
		return -1;

	*out = (int64_t)v;
	return 0;
}

int JSON_ObjFind( const json_doc_t *doc, int obj_idx, const char *key )
{
	if( obj_idx < 0 || obj_idx >= doc->ntoks )
		return -1;
	if( doc->toks[obj_idx].type != JSON_OBJECT )
		return -1;

	// Walk forward; the first immediate child whose key matches is returned.
	// Object children alternate key, value, key, value ...
	int depth_target = obj_idx;
	int n_pairs_seen = 0;
	int n_pairs_total = doc->toks[obj_idx].size;

	for( int i = obj_idx + 1; i < doc->ntoks && n_pairs_seen < n_pairs_total; i++ )
	{
		if( doc->toks[i].parent != depth_target )
			continue;

		// At this depth, tokens come as key,value pairs.
		if( doc->toks[i].type == JSON_STRING && JSON_StringEq( doc, i, key ))
		{
			// Find the next sibling at same depth — that's the value.
			for( int j = i + 1; j < doc->ntoks; j++ )
			{
				if( doc->toks[j].parent == depth_target )
					return j;
			}
			return -1;
		}
		n_pairs_seen++;
	}

	return -1;
}

// --------------------------------------------------------------------------
// Encoder
// --------------------------------------------------------------------------

void JSON_WInit( json_w_t *w )
{
	w->buf = NULL;
	w->len = 0;
	w->cap = 0;
	w->oom = 0;
}

void JSON_WFree( json_w_t *w )
{
	free( w->buf );
	w->buf = NULL;
	w->len = 0;
	w->cap = 0;
}

void JSON_WReserve( json_w_t *w, size_t extra )
{
	if( w->oom )
		return;

	size_t need = w->len + extra + 1;
	if( need <= w->cap )
		return;

	size_t cap = w->cap ? w->cap : 256;
	while( cap < need )
		cap *= 2;

	char *nb = (char *)realloc( w->buf, cap );
	if( !nb )
	{
		w->oom = 1;
		return;
	}
	w->buf = nb;
	w->cap = cap;
}

void JSON_WPutc( json_w_t *w, char c )
{
	JSON_WReserve( w, 1 );
	if( w->oom )
		return;
	w->buf[w->len++] = c;
}

void JSON_WRaw( json_w_t *w, const char *s, size_t n )
{
	JSON_WReserve( w, n );
	if( w->oom )
		return;
	memcpy( w->buf + w->len, s, n );
	w->len += n;
}

void JSON_WStr( json_w_t *w, const char *s )
{
	JSON_WRaw( w, s, strlen( s ));
}

void JSON_WStrN( json_w_t *w, const char *s, size_t n )
{
	JSON_WRaw( w, s, n );
}

void JSON_WStrEscapedN( json_w_t *w, const char *s, size_t n )
{
	JSON_WPutc( w, '"' );
	for( size_t i = 0; i < n; i++ )
	{
		unsigned char c = (unsigned char)s[i];
		switch( c )
		{
		case '"':  JSON_WRaw( w, "\\\"", 2 ); break;
		case '\\': JSON_WRaw( w, "\\\\", 2 ); break;
		case '\b': JSON_WRaw( w, "\\b",  2 ); break;
		case '\f': JSON_WRaw( w, "\\f",  2 ); break;
		case '\n': JSON_WRaw( w, "\\n",  2 ); break;
		case '\r': JSON_WRaw( w, "\\r",  2 ); break;
		case '\t': JSON_WRaw( w, "\\t",  2 ); break;
		default:
			if( c < 0x20 )
			{
				char tmp[8];
				Q_snprintf( tmp, sizeof( tmp ), "\\u%04x", c );
				JSON_WRaw( w, tmp, 6 );
			}
			else
			{
				JSON_WPutc( w, (char)c );
			}
		}
	}
	JSON_WPutc( w, '"' );
}

void JSON_WStrEscaped( json_w_t *w, const char *s )
{
	JSON_WStrEscapedN( w, s, strlen( s ));
}

void JSON_WInt64( json_w_t *w, int64_t v )
{
	char tmp[24];
	int n = Q_snprintf( tmp, sizeof( tmp ), "%lld", (long long)v );
	if( n > 0 )
		JSON_WRaw( w, tmp, (size_t)n );
}

void JSON_WBool( json_w_t *w, int v )
{
	if( v )
		JSON_WRaw( w, "true", 4 );
	else
		JSON_WRaw( w, "false", 5 );
}

void JSON_WNull( json_w_t *w )
{
	JSON_WRaw( w, "null", 4 );
}
