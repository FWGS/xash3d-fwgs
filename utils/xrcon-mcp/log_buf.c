/*
log_buf.c - bounded line-oriented ring buffer for xrcon-mcp
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

#include "crtlib.h"
#include "log_buf.h"

int LogBuf_Init( log_buf_t *lb, size_t cap )
{
	memset( lb, 0, sizeof( *lb ));
	if( cap == 0 )
		cap = 65536;
	lb->data = (char *)malloc( cap );
	if( !lb->data )
		return -1;
	lb->cap = cap;
	return 0;
}

void LogBuf_Free( log_buf_t *lb )
{
	free( lb->data );
	memset( lb, 0, sizeof( *lb ));
}

static void LogBuf_Drop( log_buf_t *lb, size_t bytes )
{
	if( bytes >= lb->len )
	{
		lb->total_evicted += lb->len;
		lb->len = 0;
		return;
	}
	memmove( lb->data, lb->data + bytes, lb->len - bytes );
	lb->len -= bytes;
	lb->total_evicted += bytes;
}

void LogBuf_Append( log_buf_t *lb, const char *s, size_t n )
{
	if( !lb->data || n == 0 )
		return;

	if( n >= lb->cap )
	{
		// payload alone exceeds capacity — keep only the tail
		lb->total_evicted += lb->len;
		lb->len = 0;
		s += ( n - lb->cap ) + 1;
		n = lb->cap - 1;
	}

	if( lb->len + n > lb->cap )
	{
		size_t need = lb->len + n - lb->cap;
		// prefer to drop whole lines so the surviving buffer stays line-aligned
		size_t drop = 0;
		for( size_t i = 0; i < lb->len && drop < need; i++ )
		{
			if( lb->data[i] == '\n' )
				drop = i + 1;
		}
		if( drop < need )
			drop = need;
		LogBuf_Drop( lb, drop );
	}

	memcpy( lb->data + lb->len, s, n );
	lb->len += n;
	lb->total_appended += n;
}

void LogBuf_AppendStr( log_buf_t *lb, const char *s )
{
	LogBuf_Append( lb, s, Q_strlen( s ));
}
