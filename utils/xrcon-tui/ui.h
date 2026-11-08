/*
ui.h - screen rendering and key handling for xrcon-tui
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

#ifndef XRCON_TUI_UI_H
#define XRCON_TUI_UI_H

#include "edit.h"
#include "net.h"
#include "scrollback.h"
#include "sys.h"
#include "text.h"

typedef enum
{
	UI_MODE_INPUT,
	UI_MODE_FILTER
} ui_mode_t;

typedef struct
{
	const char *target; // host:port shown in the status bar
	size_t scrollback;
	qboolean colors;
} ui_config_t;

typedef struct
{
	size_t line;
	int row;
} ui_vrow_t;

typedef struct
{
	net_t *net;
	scrollback_t sb;
	history_t history;
	const char *target;

	ui_mode_t mode;
	edit_t input;
	edit_t draft; // what was typed before browsing history, also the prefix to match
	size_t hist_pos; // == history.count when not browsing
	edit_t filter;

	qboolean colors;
	qboolean show_help;
	qboolean quit;
	qboolean want_reconnect;
	qboolean reconnecting; // a reconnect is scheduled
	qboolean dirty;
	qboolean full_redraw;

	int cols;
	int rows;
	int scroll; // rows above the bottom of the output
	int unseen; // lines that arrived while scrolled up
	int input_scroll;
	int status_sig;

	ui_vrow_t *view;
	strbuf_t *prev_rows;
	strbuf_t frame;
	strbuf_t row;
	int attr;
	int col;
} ui_t;

qboolean UI_Init( ui_t *ui, net_t *net, const ui_config_t *cfg );
void UI_Shutdown( ui_t *ui );

void UI_AddText( ui_t *ui, int kind, const char *text, size_t len );
void UI_Message( ui_t *ui, int kind, const char *fmt, ... ) FORMAT_CHECK( 3 );

void UI_HandleKey( ui_t *ui, const sys_key_t *key );
void UI_Resize( ui_t *ui );
void UI_Tick( ui_t *ui );
void UI_Draw( ui_t *ui );

#endif // XRCON_TUI_UI_H
