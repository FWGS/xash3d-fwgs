/*
edit.c - line editor buffer and command history for xrcon-tui
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
#include "edit.h"

void Edit_Clear( edit_t *e )
{
	e->len = 0;
	e->pos = 0;
	e->buf[0] = '\0';
}

void Edit_Set( edit_t *e, const char *s, size_t len )
{
	if( len > EDIT_MAX_LEN )
		len = EDIT_MAX_LEN;

	memmove( e->buf, s, len );
	e->buf[len] = '\0';
	e->len = len;
	e->pos = len;
}

qboolean Edit_Insert( edit_t *e, const char *s, size_t len )
{
	if( e->len + len > EDIT_MAX_LEN )
		return false;

	memmove( e->buf + e->pos + len, e->buf + e->pos, e->len - e->pos + 1 );
	memcpy( e->buf + e->pos, s, len );
	e->len += len;
	e->pos += len;
	return true;
}

void Edit_Delete( edit_t *e, size_t from, size_t to )
{
	if( from >= to || to > e->len )
		return;

	memmove( e->buf + from, e->buf + to, e->len - to + 1 );
	e->len -= to - from;

	if( e->pos >= to )
		e->pos -= to - from;
	else if( e->pos > from )
		e->pos = from;
}

static qboolean Edit_IsSeparator( char c )
{
	return c == ' ' || c == '\n' || c == ';';
}

size_t Edit_WordLeft( const edit_t *e, size_t pos )
{
	while( pos > 0 && Edit_IsSeparator( e->buf[pos - 1] ))
		pos--;

	while( pos > 0 && !Edit_IsSeparator( e->buf[pos - 1] ))
		pos--;

	return pos;
}

size_t Edit_WordRight( const edit_t *e, size_t pos )
{
	while( pos < e->len && Edit_IsSeparator( e->buf[pos] ))
		pos++;

	while( pos < e->len && !Edit_IsSeparator( e->buf[pos] ))
		pos++;

	return pos;
}

qboolean History_Init( history_t *h, size_t max )
{
	memset( h, 0, sizeof( *h ));

	h->items = (char **)calloc( max, sizeof( *h->items ));
	if( !h->items )
		return false;

	h->max = max;
	return true;
}

void History_Free( history_t *h )
{
	for( size_t i = 0; i < h->count; i++ )
		free( h->items[i] );

	free( h->items );
	memset( h, 0, sizeof( *h ));
}

void History_Add( history_t *h, const char *s, size_t len )
{
	if( len == 0 )
		return;

	if( h->count > 0 )
	{
		const char *last = h->items[h->count - 1];

		if( Q_strlen( last ) == len && !memcmp( last, s, len ))
			return;
	}

	char *copy = (char *)malloc( len + 1 );
	if( !copy )
		return;

	memcpy( copy, s, len );
	copy[len] = '\0';

	if( h->count == h->max )
	{
		free( h->items[0] );
		memmove( h->items, h->items + 1, ( h->max - 1 ) * sizeof( *h->items ));
		h->count--;
	}

	h->items[h->count++] = copy;
}
