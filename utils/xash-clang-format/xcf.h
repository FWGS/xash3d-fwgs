/*
xcf.h - clang-format wrapper internals
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
#ifndef XCF_H
#define XCF_H

#include <stddef.h>
#include "xash3d_types.h"

//
// transform.c
//
typedef struct
{
	char *data;
	size_t len;
	size_t cap;
} xcf_buf_t;

void BufFree( xcf_buf_t *b );
qboolean BufPutChar( xcf_buf_t *b, char c );
qboolean BufPutMem( xcf_buf_t *b, const char *src, size_t n );
qboolean Transform( const char *in, size_t in_len, xcf_buf_t *out );

//
// config.c
//
typedef struct
{
	qboolean collapse_parens; // default: false
} xcf_config_t;

qboolean ParseConfigText( const char *text, xcf_config_t *cfg );

//
// version.c
//
qboolean ParseVersion( const char *s, int *major );
qboolean ClangFormatVersion( const char *exe, int *major );

//
// proc.c
//
int RunCapture( char *const argv[], xcf_buf_t *out, qboolean silent_stderr );

#endif // XCF_H
