/*
json.h - minimal JSON encoder and tokenising parser for xrcon-mcp
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

#ifndef XRCON_MCP_JSON_H
#define XRCON_MCP_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
	JSON_UNDEFINED = 0,
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_PRIMITIVE
} json_type_t;

typedef struct
{
	json_type_t type;
	int start;
	int end;
	int size;
	int parent;
} json_tok_t;

typedef struct
{
	const char *json;
	int len;
	json_tok_t *toks;
	int ntoks;
} json_doc_t;

#define JSON_MAX_TOKS 256

int JSON_Parse( const char *js, int len, json_tok_t *toks, int max_toks );
int JSON_ObjFind( const json_doc_t *doc, int obj_idx, const char *key );
int JSON_StringEq( const json_doc_t *doc, int tok_idx, const char *s );
int JSON_StringDup( const json_doc_t *doc, int tok_idx, char *dst, size_t dstsize );
int JSON_TokenAsInt64( const json_doc_t *doc, int tok_idx, int64_t *out );
int JSON_TokenLen( const json_doc_t *doc, int tok_idx );
const char *JSON_TokenPtr( const json_doc_t *doc, int tok_idx );

typedef struct
{
	char *buf;
	size_t len;
	size_t cap;
	int oom;
} json_w_t;

void JSON_WInit( json_w_t *w );
void JSON_WFree( json_w_t *w );
void JSON_WReserve( json_w_t *w, size_t extra );
void JSON_WPutc( json_w_t *w, char c );
void JSON_WRaw( json_w_t *w, const char *s, size_t n );
void JSON_WStr( json_w_t *w, const char *s );
void JSON_WStrN( json_w_t *w, const char *s, size_t n );
void JSON_WStrEscaped( json_w_t *w, const char *s );
void JSON_WStrEscapedN( json_w_t *w, const char *s, size_t n );
void JSON_WInt64( json_w_t *w, int64_t v );
void JSON_WBool( json_w_t *w, int v );
void JSON_WNull( json_w_t *w );

#endif // XRCON_MCP_JSON_H
