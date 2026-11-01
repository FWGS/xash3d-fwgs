/*
scrollback.c - bounded ring of console lines for xrcon-tui
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
#include "scrollback.h"

qboolean SB_Init( scrollback_t *sb, size_t cap )
{
	memset( sb, 0, sizeof( *sb ));

	sb->lines = (sb_line_t *)calloc( cap, sizeof( *sb->lines ));
	if( !sb->lines )
		return false;

	sb->cap = cap;
	return true;
}

void SB_Clear( scrollback_t *sb )
{
	for( size_t i = 0; i < sb->count; i++ )
	{
		sb_line_t *line = SB_Line( sb, i );

		free( line->text );
		line->text = NULL;
	}

	sb->first = 0;
	sb->count = 0;
}

void SB_Free( scrollback_t *sb )
{
	SB_Clear( sb );
	free( sb->lines );
	memset( sb, 0, sizeof( *sb ));
}

sb_line_t *SB_Line( scrollback_t *sb, size_t i )
{
	return &sb->lines[( sb->first + i ) % sb->cap];
}

sb_line_t *SB_Push( scrollback_t *sb, int kind, const char *stamp, const char *text, size_t len )
{
	sb_line_t *line;
	char *copy = (char *)malloc( len + 1 );

	if( !copy )
		return NULL;

	memcpy( copy, text, len );
	copy[len] = '\0';

	if( sb->count == sb->cap )
	{
		line = &sb->lines[sb->first];
		free( line->text );
		sb->first = ( sb->first + 1 ) % sb->cap;
	}
	else
	{
		line = &sb->lines[( sb->first + sb->count ) % sb->cap];
		sb->count++;
	}

	line->text = copy;
	line->len = (uint32_t)len;
	line->kind = kind;
	line->wrap_width = 0;
	line->wrap_rows = 1;
	Q_strncpy( line->stamp, stamp, sizeof( line->stamp ));

	return line;
}
