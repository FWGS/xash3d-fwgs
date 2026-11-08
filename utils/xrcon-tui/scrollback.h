/*
scrollback.h - bounded ring of console lines for xrcon-tui
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

#ifndef XRCON_TUI_SCROLLBACK_H
#define XRCON_TUI_SCROLLBACK_H

#include <stddef.h>
#include <stdint.h>

#include "xash3d_types.h"

enum
{
	SB_LINE_SERVER, // received from the engine
	SB_LINE_INFO,   // our own status messages
	SB_LINE_ERROR,
};

typedef struct
{
	char *text; // sanitized UTF-8
	uint32_t len;
	int kind;
	char stamp[9]; // HH:MM:SS when the line arrived

	// row count for the last wrap width, recomputed when the width changes
	int wrap_width;
	int wrap_rows;
} sb_line_t;

typedef struct
{
	sb_line_t *lines;
	size_t cap;
	size_t first;
	size_t count;
} scrollback_t;

qboolean SB_Init( scrollback_t *sb, size_t cap );
void SB_Free( scrollback_t *sb );
void SB_Clear( scrollback_t *sb );

// evicts the oldest line when full
sb_line_t *SB_Push( scrollback_t *sb, int kind, const char *stamp, const char *text, size_t len );

// 0 is the oldest line
sb_line_t *SB_Line( scrollback_t *sb, size_t i );

#endif // XRCON_TUI_SCROLLBACK_H
