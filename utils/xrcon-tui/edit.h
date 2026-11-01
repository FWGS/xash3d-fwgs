/*
edit.h - line editor buffer and command history for xrcon-tui
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

#ifndef XRCON_TUI_EDIT_H
#define XRCON_TUI_EDIT_H

#include <stddef.h>

#include "xash3d_types.h"
#include "xrcon_protocol.h"

// a command line must fit into a single CMND frame
#define EDIT_MAX_LEN ( XRCON_MAX_FRAME_SIZE - XRCON_HEADER_SIZE - 1 )

typedef struct
{
	char buf[EDIT_MAX_LEN + 1];
	size_t len;
	size_t pos; // cursor, in bytes
} edit_t;

typedef struct
{
	char **items; // oldest first
	size_t count;
	size_t max;
} history_t;

void Edit_Clear( edit_t *e );
void Edit_Set( edit_t *e, const char *s, size_t len );
qboolean Edit_Insert( edit_t *e, const char *s, size_t len );
void Edit_Delete( edit_t *e, size_t from, size_t to );
size_t Edit_WordLeft( const edit_t *e, size_t pos );
size_t Edit_WordRight( const edit_t *e, size_t pos );

qboolean History_Init( history_t *h, size_t max );
void History_Free( history_t *h );
void History_Add( history_t *h, const char *s, size_t len );

#endif // XRCON_TUI_EDIT_H
