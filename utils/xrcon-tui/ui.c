/*
ui.c - screen rendering and key handling for xrcon-tui
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "crtlib.h"
#include "utflib.h"
#include "xash3d_mathlib.h"
#include "ui.h"

#define UI_HISTORY_MAX  1000
#define UI_WHEEL_LINES  3
#define UI_STAMP_WIDTH  11 // "[HH:MM:SS] ", wrapped rows are indented by as much
#define UI_LINE_MAX     16384
#define UI_MIN_COLS     20
#define UI_MIN_ROWS     4
#define UI_COMMAND_ECHO "Note: XRcon command: " // the engine prints this for every command it receives

enum
{
	INK_DEFAULT = 0,
	INK_BLACK,
	INK_RED,
	INK_GREEN,
	INK_YELLOW,
	INK_BLUE,
	INK_MAGENTA,
	INK_CYAN,
	INK_WHITE,
	INK_GRAY,
};

#define ATTR_FG( c )  ( c )
#define ATTR_BG( c )  (( c ) << 4 )
#define ATTR_BOLD     BIT( 8 )
#define ATTR_DIM      BIT( 9 )
#define ATTR_REVERSE  BIT( 10 )

// stock 16 color palette, so the user's terminal theme applies
static const int ink_fg[] = { 39, 30, 31, 32, 33, 34, 35, 36, 37, 90 };
static const int ink_bg[] = { 49, 40, 41, 42, 43, 44, 45, 46, 47, 100 };

// same colors the engine uses for these prefixes, it strips color codes before sending
static const struct
{
	const char *text;
	int attr;
} engine_prefixes[] = {
	{ "Error:", ATTR_FG( INK_RED ) | ATTR_BOLD },
	{ "Warning:", ATTR_FG( INK_YELLOW ) | ATTR_BOLD },
	{ "Note:", ATTR_FG( INK_GREEN ) },
};

static const struct
{
	const char *keys;
	const char *action;
} help_keys[] = {
	{ "Enter", "send the command" },
	{ "Up, Down", "browse history, matching what is already typed" },
	{ "Left, Right", "move the cursor" },
	{ "Ctrl-Left, Ctrl-Right", "move by word, also Alt-B and Alt-F" },
	{ "Home, End", "line start or end, also Ctrl-A and Ctrl-E" },
	{ "Ctrl-W, Alt-Backspace", "delete the previous word" },
	{ "Alt-D", "delete the next word" },
	{ "Ctrl-U, Ctrl-K", "delete to line start or end" },
	{ "PgUp, PgDn", "scroll output by page" },
	{ "Shift-Up, Shift-Down", "scroll output by line, the mouse wheel works too" },
	{ "Ctrl-Home, Ctrl-End", "jump to the oldest or newest output" },
	{ "Ctrl-F", "filter output, Esc clears the filter" },
	{ "Ctrl-L", "clear output" },
	{ "F5", "reconnect now" },
	{ "Ctrl-C, Ctrl-D", "clear the line, quit when it is empty" },
};

typedef struct
{
	int base;
	size_t prefix_len;
	int prefix_attr;
} ui_style_t;

typedef enum
{
	EDIT_IGNORED,
	EDIT_MOVED,
	EDIT_CHANGED
} edit_result_t;

static void UI_Attr( ui_t *ui, int attr )
{
	if( ui->attr == attr )
		return;

	ui->attr = attr;
	Buf_AppendStr( &ui->row, "\x1b[0" );

	if( FBitSet( attr, ATTR_BOLD ))
		Buf_AppendStr( &ui->row, ";1" );

	if( FBitSet( attr, ATTR_DIM ))
		Buf_AppendStr( &ui->row, ";2" );

	if( FBitSet( attr, ATTR_REVERSE ))
		Buf_AppendStr( &ui->row, ";7" );

	if( ui->colors )
	{
		int fg = attr & 0x0f;
		int bg = ( attr >> 4 ) & 0x0f;

		if( fg != INK_DEFAULT )
			Buf_Printf( &ui->row, ";%d", ink_fg[fg] );

		if( bg != INK_DEFAULT )
			Buf_Printf( &ui->row, ";%d", ink_bg[bg] );
	}

	Buf_AppendStr( &ui->row, "m" );
}

// appends text, skipping unprintable characters and clipping at the right edge
static void UI_Text( ui_t *ui, const char *s, size_t len )
{
	size_t i = 0;

	while( i < len )
	{
		uint32_t uc;
		size_t n = Q_DecodeUTF8Char( s + i, len - i, &uc );
		int w = Text_CharWidth( uc );

		i += n;

		if( !Text_IsPrintable( uc ))
			continue;

		if( ui->col + w > ui->cols )
			break;

		Buf_AppendChar( &ui->row, uc );
		ui->col += w;
	}
}

static void UI_Spaces( ui_t *ui, int count )
{
	count = Q_min( count, ui->cols - ui->col );
	if( count <= 0 )
		return;

	Buf_Repeat( &ui->row, ' ', count );
	ui->col += count;
}

static void UI_BeginRow( ui_t *ui )
{
	Buf_Reset( &ui->row );
	ui->attr = 0;
	ui->col = 0;
}

// emits the row only if it differs from what is already on the screen
static void UI_EndRow( ui_t *ui, int r )
{
	strbuf_t *prev = &ui->prev_rows[r];

	UI_Attr( ui, 0 );

	// erasing from the last column would wipe the character under the cursor
	if( ui->col < ui->cols )
		Buf_AppendStr( &ui->row, "\x1b[K" );

	if( !ui->full_redraw && prev->len == ui->row.len && !memcmp( prev->data, ui->row.data, ui->row.len ))
		return;

	Buf_Printf( &ui->frame, "\x1b[%d;1H", r + 1 );
	Buf_Append( &ui->frame, ui->row.data, ui->row.len );
	Buf_Reset( prev );
	Buf_Append( prev, ui->row.data, ui->row.len );
}

// splits text into rows of at most width cells, when want is a valid row
// its byte range is stored to [*start, *end) and the counting stops there
static int UI_Wrap( const char *text, size_t len, int width, int want, size_t *start, size_t *end )
{
	size_t row_start = 0;
	size_t i = 0;
	int row = 0;
	int col = 0;

	while( i < len )
	{
		uint32_t uc;
		size_t n = Q_DecodeUTF8Char( text + i, len - i, &uc );
		int w = Text_CharWidth( uc );

		if( col + w > width && col > 0 )
		{
			if( row == want )
				break;

			row++;
			col = 0;
			row_start = i;
		}

		col += w;
		i += n;
	}

	if( row == want )
	{
		*start = row_start;
		*end = i;
	}

	return row + 1;
}

static int UI_WrapWidth( const ui_t *ui )
{
	return ui->cols - UI_STAMP_WIDTH;
}

static int UI_LogHeight( const ui_t *ui )
{
	return ui->rows - 2;
}

static int UI_LineRows( const ui_t *ui, sb_line_t *line )
{
	int width = UI_WrapWidth( ui );

	if( line->wrap_width != width )
	{
		line->wrap_width = width;
		line->wrap_rows = UI_Wrap( line->text, line->len, width, -1, NULL, NULL );
	}

	return line->wrap_rows;
}

static qboolean UI_LineVisible( const ui_t *ui, const sb_line_t *line )
{
	return ui->filter.len == 0 || Q_stristr( line->text, ui->filter.buf ) != NULL;
}

static int UI_TotalRows( ui_t *ui )
{
	int total = 0;

	for( size_t i = 0; i < ui->sb.count; i++ )
	{
		sb_line_t *line = SB_Line( &ui->sb, i );

		if( UI_LineVisible( ui, line ))
			total += UI_LineRows( ui, line );
	}

	return total;
}

static void UI_ClampScroll( ui_t *ui )
{
	if( ui->scroll > 0 )
	{
		int max = UI_TotalRows( ui ) - UI_LogHeight( ui );

		if( ui->scroll > max )
			ui->scroll = max;
	}

	if( ui->scroll <= 0 )
	{
		ui->scroll = 0;
		ui->unseen = 0;
	}
}

static void UI_ScrollBy( ui_t *ui, int delta )
{
	ui->scroll += delta;
	UI_ClampScroll( ui );
}

static void UI_ScrollToBottom( ui_t *ui )
{
	ui->scroll = 0;
	ui->unseen = 0;
}

static void UI_AddLine( ui_t *ui, int kind, const char *text, size_t len )
{
	char clean[UI_LINE_MAX];
	char stamp[16] = "";
	time_t t = time( NULL );
	const struct tm *tm = localtime( &t );

	if( tm )
		strftime( stamp, sizeof( stamp ), "%H:%M:%S", tm );

	size_t n = Text_Sanitize( clean, sizeof( clean ), text, len );
	sb_line_t *line = SB_Push( &ui->sb, kind, stamp, clean, n );

	if( !line )
		return;

	// keep the view still while the user reads older output
	if( ui->scroll > 0 && UI_LineVisible( ui, line ))
	{
		ui->scroll += UI_LineRows( ui, line );
		ui->unseen++;
	}

	ui->dirty = true;
}

void UI_AddText( ui_t *ui, int kind, const char *text, size_t len )
{
	size_t start = 0;

	while( start < len )
	{
		const char *nl = (const char *)memchr( text + start, '\n', len - start );
		size_t end = nl ? (size_t)( nl - text ) : len;

		UI_AddLine( ui, kind, text + start, end - start );
		start = end + 1;
	}
}

void UI_Message( ui_t *ui, int kind, const char *fmt, ... )
{
	char msg[1024] = "-- ";
	va_list ap;

	va_start( ap, fmt );
	Q_vsnprintf( msg + 3, sizeof( msg ) - 3, fmt, ap );
	va_end( ap );

	UI_AddText( ui, kind, msg, Q_strlen( msg ));
}

qboolean UI_Init( ui_t *ui, net_t *net, const ui_config_t *cfg )
{
	memset( ui, 0, sizeof( *ui ));
	ui->net = net;
	ui->target = cfg->target;
	ui->colors = cfg->colors;
	ui->status_sig = -1;
	ui->dirty = true;

	if( !SB_Init( &ui->sb, cfg->scrollback ) || !History_Init( &ui->history, UI_HISTORY_MAX ))
	{
		fprintf( stderr, "xrcon-tui: out of memory\n" );
		return false;
	}

	return true;
}

void UI_Shutdown( ui_t *ui )
{
	if( ui->prev_rows )
	{
		for( int i = 0; i < ui->rows; i++ )
			Buf_Free( &ui->prev_rows[i] );
	}

	free( ui->prev_rows );
	free( ui->view );
	Buf_Free( &ui->frame );
	Buf_Free( &ui->row );
	History_Free( &ui->history );
	SB_Free( &ui->sb );
	ui->prev_rows = NULL;
	ui->view = NULL;
}

static void UI_LineStyle( const sb_line_t *line, ui_style_t *st )
{
	st->base = 0;
	st->prefix_len = 0;
	st->prefix_attr = 0;

	if( line->kind == SB_LINE_INFO )
	{
		st->base = ATTR_FG( INK_CYAN );
	}
	else if( line->kind == SB_LINE_ERROR )
	{
		st->base = ATTR_FG( INK_RED ) | ATTR_BOLD;
	}
	else
	{
		for( size_t i = 0; i < ARRAYSIZE( engine_prefixes ); i++ )
		{
			size_t len = Q_strlen( engine_prefixes[i].text );

			if( !Q_strncmp( line->text, engine_prefixes[i].text, len ))
			{
				st->prefix_len = len;
				st->prefix_attr = engine_prefixes[i].attr;
				break;
			}
		}

		if( !Q_strncmp( line->text, UI_COMMAND_ECHO, sizeof( UI_COMMAND_ECHO ) - 1 ))
			st->base = ATTR_BOLD;
	}
}

static void UI_DrawLogRow( ui_t *ui, sb_line_t *line, int row )
{
	ui_style_t st;
	size_t start = 0, end = 0;

	UI_LineStyle( line, &st );
	UI_Wrap( line->text, line->len, UI_WrapWidth( ui ), row, &start, &end );

	if( row == 0 )
	{
		char stamp[16];

		Q_snprintf( stamp, sizeof( stamp ), "[%s] ", line->stamp );
		UI_Attr( ui, ATTR_FG( INK_GRAY ));
		UI_Text( ui, stamp, Q_strlen( stamp ));
	}
	else
	{
		UI_Spaces( ui, UI_STAMP_WIDTH );
	}

	if( start < st.prefix_len )
	{
		size_t prefix_end = Q_min( st.prefix_len, end );

		UI_Attr( ui, st.prefix_attr );
		UI_Text( ui, line->text + start, prefix_end - start );
		start = prefix_end;
	}

	UI_Attr( ui, st.base );
	UI_Text( ui, line->text + start, end - start );
}

// fills the bottom of ui->view with the rows to show, returns their count
static int UI_CollectView( ui_t *ui, int height )
{
	int skip = ui->scroll;
	int filled = 0;

	for( size_t i = ui->sb.count; i-- > 0 && filled < height; )
	{
		sb_line_t *line = SB_Line( &ui->sb, i );

		if( !UI_LineVisible( ui, line ))
			continue;

		int rows = UI_LineRows( ui, line );

		if( skip >= rows )
		{
			skip -= rows;
			continue;
		}

		for( int r = rows - skip; r-- > 0 && filled < height; )
		{
			filled++;
			ui->view[height - filled].line = i;
			ui->view[height - filled].row = r;
		}

		skip = 0;
	}

	return filled;
}

static void UI_DrawLog( ui_t *ui, int height )
{
	int filled = UI_CollectView( ui, height );

	// old lines were evicted or the window grew while scrolled up
	if( filled < height && ui->scroll > 0 )
	{
		ui->scroll = Q_max( ui->scroll - ( height - filled ), 0 );
		filled = UI_CollectView( ui, height );
	}

	if( ui->scroll == 0 )
		ui->unseen = 0;

	for( int r = 0; r < height; r++ )
	{
		UI_BeginRow( ui );

		if( r >= height - filled )
			UI_DrawLogRow( ui, SB_Line( &ui->sb, ui->view[r].line ), ui->view[r].row );

		UI_EndRow( ui, r );
	}
}

static void UI_DrawHelp( ui_t *ui, int height )
{
	const int count = (int)ARRAYSIZE( help_keys );
	int keys_width = 0;

	for( int i = 0; i < count; i++ )
	{
		int w = Text_Width( help_keys[i].keys, Q_strlen( help_keys[i].keys ));

		if( w > keys_width )
			keys_width = w;
	}

	for( int r = 0; r < height; r++ )
	{
		int i = r - 2;

		UI_BeginRow( ui );

		if( r == 0 )
		{
			UI_Attr( ui, ATTR_BOLD );
			UI_Text( ui, " xrcon-tui key bindings", Q_strlen( " xrcon-tui key bindings" ));
		}
		else if( i >= 0 && i < count )
		{
			UI_Spaces( ui, 2 );
			UI_Attr( ui, ATTR_FG( INK_CYAN ) | ATTR_BOLD );
			UI_Text( ui, help_keys[i].keys, Q_strlen( help_keys[i].keys ));
			UI_Attr( ui, 0 );
			UI_Spaces( ui, keys_width + 6 - ui->col );
			UI_Text( ui, help_keys[i].action, Q_strlen( help_keys[i].action ));
		}
		else if( i == count + 1 )
		{
			UI_Attr( ui, ATTR_DIM );
			UI_Text( ui, " Press any key to close", Q_strlen( " Press any key to close" ));
		}

		UI_EndRow( ui, r );
	}
}

static void UI_Append( char *buf, size_t size, const char *fmt, ... ) FORMAT_CHECK( 3 );

static void UI_Append( char *buf, size_t size, const char *fmt, ... )
{
	size_t len = Q_strlen( buf );
	va_list ap;

	if( len + 1 >= size )
		return;

	va_start( ap, fmt );
	Q_vsnprintf( buf + len, size - len, fmt, ap );
	va_end( ap );
}

static void UI_DrawStatus( ui_t *ui, int r )
{
	const net_t *net = ui->net;
	char left[512] = "";
	char right[256] = "";
	const char *badge;
	int badge_attr;

	switch( net->state )
	{
	case NET_ONLINE:
		badge = " ONLINE ";
		badge_attr = ATTR_FG( INK_BLACK ) | ATTR_BG( INK_GREEN );
		break;
	case NET_CONNECTED:
		badge = " WAITING ";
		badge_attr = ATTR_FG( INK_BLACK ) | ATTR_BG( INK_YELLOW );
		break;
	case NET_CONNECTING:
		badge = " CONNECTING ";
		badge_attr = ATTR_FG( INK_BLACK ) | ATTR_BG( INK_YELLOW );
		break;
	default:
		badge = " OFFLINE ";
		badge_attr = ATTR_FG( INK_WHITE ) | ATTR_BG( INK_RED );
		break;
	}

	UI_Append( left, sizeof( left ), " %s", ui->target );

	if( net->state == NET_OFFLINE )
	{
		if( net->last_error[0] )
			UI_Append( left, sizeof( left ), " · %s", net->last_error );

		if( ui->reconnecting )
			UI_Append( left, sizeof( left ), " · retrying" );
		else
			UI_Append( left, sizeof( left ), " · F5 to connect" );
	}

	if( ui->filter.len > 0 )
		UI_Append( right, sizeof( right ), "filter: %s  ", ui->filter.buf );

	if( ui->scroll > 0 )
	{
		if( ui->unseen > 0 )
			UI_Append( right, sizeof( right ), "↓ %d new  ", ui->unseen );
		else
			UI_Append( right, sizeof( right ), "scrolled  " );
	}

	UI_Append( right, sizeof( right ), "F1 help " );

	UI_BeginRow( ui );
	UI_Attr( ui, ui->colors ? ( badge_attr | ATTR_BOLD ) : ATTR_BOLD );
	UI_Text( ui, badge, Q_strlen( badge ));
	UI_Attr( ui, ATTR_REVERSE );

	int right_width = Text_Width( right, Q_strlen( right ));
	int room = ui->cols - ui->col - right_width;

	if( room < 0 )
	{
		right[0] = '\0';
		right_width = 0;
		room = ui->cols - ui->col;
	}

	UI_Text( ui, left, Text_FitWidth( left, Q_strlen( left ), room ));
	UI_Spaces( ui, ui->cols - ui->col - right_width );
	UI_Text( ui, right, Q_strlen( right ));
	UI_EndRow( ui, r );
}

// newlines from pasted text are shown as one cell
static int UI_EditWidth( const char *s, size_t len )
{
	int width = 0;
	size_t i = 0;

	while( i < len )
	{
		uint32_t uc;

		i += Q_DecodeUTF8Char( s + i, len - i, &uc );
		width += uc == '\n' ? 1 : Text_CharWidth( uc );
	}

	return width;
}

// returns the cursor column
static int UI_DrawInput( ui_t *ui, int r )
{
	const edit_t *e = &ui->input;
	const char *prompt = "] ";
	int prompt_attr = ATTR_BOLD;

	if( ui->mode == UI_MODE_FILTER )
	{
		e = &ui->filter;
		prompt = "filter: ";
		prompt_attr = ATTR_FG( INK_YELLOW ) | ATTR_BOLD;
	}

	UI_BeginRow( ui );
	UI_Attr( ui, prompt_attr );
	UI_Text( ui, prompt, Q_strlen( prompt ));
	UI_Attr( ui, 0 );

	// the last column stays free for the cursor
	int start_col = ui->col;
	int room = ui->cols - start_col - 1;
	int cursor = UI_EditWidth( e->buf, e->pos );

	if( cursor < ui->input_scroll )
		ui->input_scroll = cursor;
	else if( cursor >= ui->input_scroll + room )
		ui->input_scroll = cursor - room + 1;

	size_t i = 0;
	int col = 0;

	while( i < e->len )
	{
		uint32_t uc;
		size_t n = Q_DecodeUTF8Char( e->buf + i, e->len - i, &uc );
		int w = uc == '\n' ? 1 : Text_CharWidth( uc );

		if( col + w > ui->input_scroll + room )
			break;

		if( col >= ui->input_scroll )
		{
			if( uc == '\n' )
			{
				UI_Attr( ui, ATTR_DIM );
				UI_Text( ui, "↵", Q_strlen( "↵" ));
				UI_Attr( ui, 0 );
			}
			else
			{
				UI_Text( ui, e->buf + i, n );
			}
		}

		col += w;
		i += n;
	}

	UI_EndRow( ui, r );
	return start_col + cursor - ui->input_scroll;
}

void UI_Draw( ui_t *ui )
{
	strbuf_t *f = &ui->frame;

	Buf_Reset( f );

	// synchronized update, hidden cursor and default attributes for the whole frame
	Buf_AppendStr( f, "\x1b[?2026h\x1b[?25l\x1b[0m" );

	if( ui->cols < UI_MIN_COLS || ui->rows < UI_MIN_ROWS )
	{
		const char *msg = "too small";
		size_t len = Q_strlen( msg );

		if( len > (size_t)ui->cols )
			len = ui->cols;

		Buf_AppendStr( f, "\x1b[2J\x1b[H" );
		Buf_Append( f, msg, len );
		Buf_AppendStr( f, "\x1b[?2026l" );
		Sys_Write( f->data, f->len );
		ui->full_redraw = true;
		ui->dirty = false;
		return;
	}

	int log_height = UI_LogHeight( ui );

	if( ui->show_help )
		UI_DrawHelp( ui, log_height );
	else
		UI_DrawLog( ui, log_height );

	UI_DrawStatus( ui, log_height );
	int cursor = UI_DrawInput( ui, log_height + 1 );

	Buf_Printf( f, "\x1b[%d;%dH\x1b[?25h\x1b[?2026l", ui->rows, cursor + 1 );
	Sys_Write( f->data, f->len );
	ui->full_redraw = false;
	ui->dirty = false;
}

void UI_Resize( ui_t *ui )
{
	int cols, rows;

	Sys_GetSize( &cols, &rows );

	if( rows != ui->rows || !ui->prev_rows )
	{
		if( ui->prev_rows )
		{
			for( int i = 0; i < ui->rows; i++ )
				Buf_Free( &ui->prev_rows[i] );
		}

		free( ui->prev_rows );
		free( ui->view );
		ui->prev_rows = (strbuf_t *)calloc( rows, sizeof( *ui->prev_rows ));
		ui->view = (ui_vrow_t *)calloc( rows, sizeof( *ui->view ));

		if( !ui->prev_rows || !ui->view )
		{
			free( ui->prev_rows );
			free( ui->view );
			ui->prev_rows = NULL;
			ui->view = NULL;
			rows = 0;
		}
	}

	ui->cols = cols;
	ui->rows = rows;
	ui->full_redraw = true;
	ui->dirty = true;
	UI_ClampScroll( ui );
}

void UI_Tick( ui_t *ui )
{
	// redraw the status bar when the connection state changes
	int sig = (int)ui->net->state | ( ui->reconnecting ? 8 : 0 );

	if( sig != ui->status_sig )
	{
		ui->status_sig = sig;
		ui->dirty = true;
	}
}

static void UI_SetMode( ui_t *ui, ui_mode_t mode )
{
	ui->mode = mode;
	ui->input_scroll = 0;
}

static qboolean UI_IsCtrl( const sys_key_t *k, char letter )
{
	return k->key == TK_CHAR && FBitSet( k->mods, TK_MOD_CTRL ) && k->ch == (uint32_t)letter;
}

static void UI_EditInsert( edit_t *e, uint32_t ch, qboolean multiline )
{
	char enc[4];

	if( ch == '\t' || ( ch == '\n' && !multiline ))
		ch = ' ';
	else if( ch != '\n' && !Text_IsPrintable( ch ))
		return;

	size_t len = Q_EncodeUTF8( enc, ch );
	Edit_Insert( e, enc, len );
}

static edit_result_t UI_EditCtrlKey( edit_t *e, uint32_t ch )
{
	switch( ch )
	{
	case 'a':
		e->pos = 0;
		return EDIT_MOVED;
	case 'e':
		e->pos = e->len;
		return EDIT_MOVED;
	case 'd':
		if( e->pos >= e->len )
			return EDIT_IGNORED;
		Edit_Delete( e, e->pos, Q_UTF8NextChar( e->buf, e->len, e->pos ));
		return EDIT_CHANGED;
	case 'w':
		Edit_Delete( e, Edit_WordLeft( e, e->pos ), e->pos );
		return EDIT_CHANGED;
	case 'u':
		Edit_Delete( e, 0, e->pos );
		return EDIT_CHANGED;
	case 'k':
		Edit_Delete( e, e->pos, e->len );
		return EDIT_CHANGED;
	}

	return EDIT_IGNORED;
}

static edit_result_t UI_EditAltKey( edit_t *e, uint32_t ch )
{
	switch( ch )
	{
	case 'b':
		e->pos = Edit_WordLeft( e, e->pos );
		return EDIT_MOVED;
	case 'f':
		e->pos = Edit_WordRight( e, e->pos );
		return EDIT_MOVED;
	case 'd':
		Edit_Delete( e, e->pos, Edit_WordRight( e, e->pos ));
		return EDIT_CHANGED;
	}

	return EDIT_IGNORED;
}

static edit_result_t UI_EditKey( edit_t *e, const sys_key_t *k, qboolean multiline )
{
	qboolean by_word = FBitSet( k->mods, TK_MOD_CTRL | TK_MOD_ALT ) != 0;

	switch( k->key )
	{
	case TK_CHAR:
		break;
	case TK_LEFT:
		e->pos = by_word ? Edit_WordLeft( e, e->pos ) : Q_UTF8PrevChar( e->buf, e->pos );
		return EDIT_MOVED;
	case TK_RIGHT:
		e->pos = by_word ? Edit_WordRight( e, e->pos ) : Q_UTF8NextChar( e->buf, e->len, e->pos );
		return EDIT_MOVED;
	case TK_HOME:
		e->pos = 0;
		return EDIT_MOVED;
	case TK_END:
		e->pos = e->len;
		return EDIT_MOVED;
	case TK_BACKSPACE:
		if( by_word )
			Edit_Delete( e, Edit_WordLeft( e, e->pos ), e->pos );
		else
			Edit_Delete( e, Q_UTF8PrevChar( e->buf, e->pos ), e->pos );
		return EDIT_CHANGED;
	case TK_DELETE:
		if( by_word )
			Edit_Delete( e, e->pos, Edit_WordRight( e, e->pos ));
		else
			Edit_Delete( e, e->pos, Q_UTF8NextChar( e->buf, e->len, e->pos ));
		return EDIT_CHANGED;
	default:
		return EDIT_IGNORED;
	}

	if( FBitSet( k->mods, TK_MOD_CTRL ))
		return UI_EditCtrlKey( e, k->ch );

	if( FBitSet( k->mods, TK_MOD_ALT ))
		return UI_EditAltKey( e, k->ch );

	UI_EditInsert( e, k->ch, multiline );
	return EDIT_CHANGED;
}

static qboolean UI_HistoryMatch( const ui_t *ui, size_t i )
{
	const char *item = ui->history.items[i];

	return !Q_strncmp( item, ui->draft.buf, ui->draft.len ) && Q_strcmp( item, ui->input.buf );
}

static void UI_HistoryPrev( ui_t *ui )
{
	// start browsing, what's typed so far is the prefix to match
	if( ui->hist_pos == ui->history.count )
		Edit_Set( &ui->draft, ui->input.buf, ui->input.len );

	for( size_t i = ui->hist_pos; i-- > 0; )
	{
		if( UI_HistoryMatch( ui, i ))
		{
			ui->hist_pos = i;
			Edit_Set( &ui->input, ui->history.items[i], Q_strlen( ui->history.items[i] ));
			return;
		}
	}
}

static void UI_HistoryNext( ui_t *ui )
{
	if( ui->hist_pos == ui->history.count )
		return;

	for( size_t i = ui->hist_pos + 1; i < ui->history.count; i++ )
	{
		if( UI_HistoryMatch( ui, i ))
		{
			ui->hist_pos = i;
			Edit_Set( &ui->input, ui->history.items[i], Q_strlen( ui->history.items[i] ));
			return;
		}
	}

	ui->hist_pos = ui->history.count;
	Edit_Set( &ui->input, ui->draft.buf, ui->draft.len );
}

static void UI_Submit( ui_t *ui )
{
	edit_t *e = &ui->input;

	UI_ScrollToBottom( ui );

	if( e->len == 0 )
		return;

	if( !Net_IsUp( ui->net ))
	{
		UI_Message( ui, SB_LINE_ERROR, "not connected, the command stays in the input line" );
		return;
	}

	if( !Net_Send( ui->net, e->buf, e->len ))
	{
		UI_Message( ui, SB_LINE_ERROR, "send buffer is full, try again" );
		return;
	}

	History_Add( &ui->history, e->buf, e->len );
	ui->hist_pos = ui->history.count;
	Edit_Clear( e );
}

static void UI_InputKey( ui_t *ui, const sys_key_t *k )
{
	edit_t *e = &ui->input;

	if( k->key == TK_ENTER )
	{
		UI_Submit( ui );
	}
	else if( k->key == TK_UP || UI_IsCtrl( k, 'p' ))
	{
		UI_HistoryPrev( ui );
	}
	else if( k->key == TK_DOWN || UI_IsCtrl( k, 'n' ))
	{
		UI_HistoryNext( ui );
	}
	else if( UI_IsCtrl( k, 'c' ) || ( UI_IsCtrl( k, 'd' ) && e->len == 0 ))
	{
		if( e->len == 0 )
			ui->quit = true;

		Edit_Clear( e );
		ui->hist_pos = ui->history.count;
	}
	else if( k->key == TK_ESCAPE )
	{
		Edit_Clear( &ui->filter );
		UI_ScrollToBottom( ui );
	}
	else if( UI_EditKey( e, k, true ) == EDIT_CHANGED )
	{
		ui->hist_pos = ui->history.count;
	}
}

static void UI_FilterKey( ui_t *ui, const sys_key_t *k )
{
	if( k->key == TK_ENTER )
	{
		UI_SetMode( ui, UI_MODE_INPUT );
	}
	else if( k->key == TK_ESCAPE || UI_IsCtrl( k, 'c' ))
	{
		Edit_Clear( &ui->filter );
		UI_ScrollToBottom( ui );
		UI_SetMode( ui, UI_MODE_INPUT );
	}
	else if( UI_EditKey( &ui->filter, k, false ) == EDIT_CHANGED )
	{
		UI_ScrollToBottom( ui );
	}
}

static qboolean UI_GlobalCtrlKey( ui_t *ui, uint32_t ch )
{
	switch( ch )
	{
	case 'f':
		UI_SetMode( ui, ui->mode == UI_MODE_FILTER ? UI_MODE_INPUT : UI_MODE_FILTER );
		return true;
	case 'l':
		SB_Clear( &ui->sb );
		UI_ScrollToBottom( ui );
		ui->full_redraw = true;
		return true;
	}

	return false;
}

static qboolean UI_GlobalKey( ui_t *ui, const sys_key_t *k )
{
	int page = Q_max( UI_LogHeight( ui ) - 1, 1 );

	switch( k->key )
	{
	case TK_F1:
		ui->show_help = true;
		return true;
	case TK_F1 + 4:
		ui->want_reconnect = true;
		return true;
	case TK_PGUP:
		UI_ScrollBy( ui, page );
		return true;
	case TK_PGDN:
		UI_ScrollBy( ui, -page );
		return true;
	case TK_WHEEL_UP:
		UI_ScrollBy( ui, UI_WHEEL_LINES );
		return true;
	case TK_WHEEL_DOWN:
		UI_ScrollBy( ui, -UI_WHEEL_LINES );
		return true;
	case TK_UP:
	case TK_DOWN:
		if( !FBitSet( k->mods, TK_MOD_SHIFT | TK_MOD_ALT | TK_MOD_CTRL ))
			return false;
		UI_ScrollBy( ui, k->key == TK_UP ? 1 : -1 );
		return true;
	case TK_HOME:
	case TK_END:
		if( !FBitSet( k->mods, TK_MOD_CTRL ))
			return false;
		UI_ScrollBy( ui, k->key == TK_HOME ? INT_MAX / 2 : INT_MIN / 2 );
		return true;
	case TK_CHAR:
		if( !FBitSet( k->mods, TK_MOD_CTRL ))
			return false;
		return UI_GlobalCtrlKey( ui, k->ch );
	}

	return false;
}

void UI_HandleKey( ui_t *ui, const sys_key_t *k )
{
	ui->dirty = true;

	if( ui->show_help )
	{
		ui->show_help = false;
		return;
	}

	if( UI_GlobalKey( ui, k ))
		return;

	if( ui->mode == UI_MODE_FILTER )
		UI_FilterKey( ui, k );
	else
		UI_InputKey( ui, k );
}
