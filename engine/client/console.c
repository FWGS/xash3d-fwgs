/*
console.c - developer console
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "keydefs.h"
#include "protocol.h"		// get the protocol version
#include "con_nprint.h"
#include "qfont.h"
#include "wadfile.h"
#include "input.h"
#include "utflib.h"

static CVAR_DEFINE_AUTO( scr_conspeed, "600", FCVAR_ARCHIVE, "console moving speed" );
static CVAR_DEFINE_AUTO( con_notifytime, "3", FCVAR_ARCHIVE, "notify time to live" );
CVAR_DEFINE_AUTO( con_fontsize, "1", FCVAR_ARCHIVE, "console font number (0, 1 or 2)" );
static CVAR_DEFINE_AUTO( con_fontrender, "2", FCVAR_ARCHIVE, "console font render mode (0: additive, 1: holes, 2: trans)" );
static CVAR_DEFINE_AUTO( con_charset, "cp1251", FCVAR_ARCHIVE, "console font charset (only cp1251 supported now)" );
static CVAR_DEFINE_AUTO( con_fontscale, "1.0", FCVAR_ARCHIVE, "scale font texture" );
static CVAR_DEFINE_AUTO( con_fontnum, "-1", FCVAR_ARCHIVE, "console font number (0, 1 or 2), -1 for autoselect" );
static CVAR_DEFINE_AUTO( con_color, "240 180 24", FCVAR_ARCHIVE, "set a custom console color" );
static CVAR_DEFINE_AUTO( scr_drawversion, "1", FCVAR_ARCHIVE, "draw version in menu or screenshots, doesn't affect console" );
static CVAR_DEFINE_AUTO( con_oldfont, "0", 0, "use legacy font from gfx.wad, might be missing or broken" );

static int g_codepage = 0;

static qboolean g_messagemode_privileged = true;

#define CON_TIMES		4	// notify lines
#define CON_MAX_TIMES	64	// notify max lines
#define COLOR_DEFAULT	'7'
#define CON_HISTORY		64
#define MAX_DBG_NOTIFY	128
#if XASH_LOW_MEMORY
#define CON_NUMFONTS	1		// do not load different font textures
#define CON_TEXTSIZE	32768	// max scrollback buffer characters in console (32 kb)
#define CON_MAXLINES	2048	// max scrollback buffer lines in console
#else
#define CON_NUMFONTS	3	// maxfonts
#define CON_TEXTSIZE	1048576	// max scrollback buffer characters in console (1 Mb)
#define CON_MAXLINES	16384	// max scrollback buffer lines in console
#endif
#define CON_LINES( i )	(con.lines[(con.lines_first + (i)) % con.maxlines])
#define CON_LINES_COUNT	con.lines_count
#define CON_LINES_LAST()	CON_LINES( CON_LINES_COUNT - 1 )

// console color typeing
rgba_t g_color_table[8] =
{
{   0,   0,   0, 255 },	// black
{ 255,   0,   0, 255 },	// red
{   0, 255,   0, 255 },	// green
{ 255, 255,   0, 255 },	// yellow
{   0,   0, 255, 255 },	// blue
{   0, 255, 255, 255 },	// cyan
{ 255,   0, 255, 255 },	// magenta
{ 240, 180,  24, 255 },	// default color (can be changed by user)
};

typedef struct
{
	string		szNotify;
	float		expire;
	rgba_t		color;
	int		key_dest;
} notify_t;

typedef struct con_lineinfo_s
{
	char		*start;
	size_t		length;
	double		addtime;		// notify stuff
} con_lineinfo_t;

typedef struct history_line_s
{
	string buffer;
	int    cursor;
	int    scroll;
} history_line_t;

typedef struct con_history_s
{
	history_line_t lines[CON_HISTORY];
	history_line_t backup;
	int     line; // the line being displayed from history buffer will be <= nextHistoryLine
	int     next; // the last line in the history buffer, not masked
} con_history_t;

typedef struct
{
	qboolean		initialized;

	// conbuffer
	char		*buffer;		// common buffer for all console lines
	int		bufsize;		// CON_TEXSIZE
	con_lineinfo_t	*lines;		// console lines
	int		maxlines;		// CON_MAXLINES

	int		lines_first;	// cyclic buffer
	int		lines_count;
	int		num_times;	// overlay lines count

	// console scroll
	int		backscroll;	// lines up from bottom to display
	int 		linewidth;	// characters across screen

	// console animation
	float		showlines;	// how many lines we should display
	float		vislines;		// in scanlines

	// console images
	int		background;	// console background

	// console fonts
	cl_font_t		chars[CON_NUMFONTS];// fonts.wad/font1.fnt
	cl_font_t		*curFont;

	// console input
	field_t		input;

	// chatfiled
	field_t		chat;
	string		chat_cmd;		// can be overrieded by user

	// console history
	con_history_t	history;
	qboolean		historyLoaded;

	notify_t		notify[MAX_DBG_NOTIFY]; // for Con_NXPrintf
	qboolean		draw_notify;	// true if we have NXPrint message

	// console update
	double		lastupdate;
} console_t;

static console_t		con;

static void Con_ClearField( field_t *edit );
static void Field_CharEvent( field_t *edit, int ch );
static void Con_InvalidateFonts( void );

static void Con_LoadHistory( con_history_t *self );
static void Con_SaveHistory( con_history_t *self );

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void )
{
	con.lines_count = 0;
	con.backscroll = 0; // go to end
}

/*
================
Con_SetColor
================
*/
static void Con_SetColor( void )
{
	int r, g, b;
	int num;

	if( !FBitSet( con_color.flags, FCVAR_CHANGED ))
		return;

	num = sscanf( con_color.string, "%i %i %i", &r, &g, &b );

	switch( num )
	{
	case 1:
		Con_DefaultColor( r, r, r, false );
		break;
	case 3:
		Con_DefaultColor( r, g, b, false );
		break;
	default:
		Cvar_DirectSet( &con_color, con_color.def_string );
		break;
	}

	ClearBits( con_color.flags, FCVAR_CHANGED );
}

/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void )
{
	int	i;

	for( i = 0; i < CON_LINES_COUNT; i++ )
		CON_LINES( i ).addtime = 0.0;
}

/*
================
Con_ClearTyping
================
*/
static void Con_ClearTyping( void )
{
	Con_ClearField( &con.input );
	con.input.widthInChars = con.linewidth;

	Cmd_AutoCompleteClear();
}

/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f( void )
{
	g_messagemode_privileged = Cmd_CurrentCommandIsPrivileged();

	if( Cmd_Argc() == 2 )
		Q_strncpy( con.chat_cmd, Cmd_Argv( 1 ), sizeof( con.chat_cmd ));
	else Q_strncpy( con.chat_cmd, "say", sizeof( con.chat_cmd ));

	Key_SetKeyDest( key_message );
}

/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f( void )
{
	g_messagemode_privileged = Cmd_CurrentCommandIsPrivileged();

	Q_strncpy( con.chat_cmd, "say_team", sizeof( con.chat_cmd ));
	Key_SetKeyDest( key_message );
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void )
{
	if( !host.allow_console || UI_CreditsActive( ))
		return; // disabled

	SCR_EndLoadingPlaque();

	// show console only in game or by special call from menu
	if( cls.state != ca_active || cls.key_dest == key_menu )
		return;

	Con_ClearTyping();
	Con_ClearNotify();

	if( cls.key_dest == key_console )
	{
		if( Cvar_VariableInteger( "sv_background" ) || Cvar_VariableInteger( "cl_background" ))
			UI_SetActiveMenu( true );
		else UI_SetActiveMenu( false );
	}
	else
	{
		UI_SetActiveMenu( false );
		Key_SetKeyDest( key_console );
	}
}

/*
================
Con_SetTimes_f
================
*/
static void Con_SetTimes_f( void )
{
	int	newtimes;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "contimes <n lines>\n" );
		return;
	}

	newtimes = Q_atoi( Cmd_Argv( 1 ) );
	con.num_times = bound( CON_TIMES, newtimes, CON_MAX_TIMES );
}

/*
================
Con_FixTimes

Notifies the console code about the current time
(and shifts back times of other entries when the time
went backwards)
================
*/
static void Con_FixTimes( void )
{
	double	diff;
	int	i;

	if( con.lines_count <= 0 ) return;

	diff = cl.time - CON_LINES_LAST().addtime;
	if( diff >= 0.0 ) return; // nothing to fix

	for( i = 0; i < con.lines_count; i++ )
		CON_LINES( i ).addtime += diff;
}

/*
================
Con_DeleteLine

Deletes the first line from the console history.
================
*/
static void Con_DeleteLine( void )
{
	if( con.lines_count == 0 )
		return;
	con.lines_count--;
	con.lines_first = (con.lines_first + 1) % con.maxlines;
}

/*
================
Con_DeleteLastLine

Deletes the last line from the console history.
================
*/
static void Con_DeleteLastLine( void )
{
	if( con.lines_count == 0 )
		return;
	con.lines_count--;
}

/*
================
Con_BytesLeft

Checks if there is space for a line of the given length, and if yes, returns a
pointer to the start of such a space, and NULL otherwise.
================
*/
static char *Con_BytesLeft( int length )
{
	if( length > con.bufsize )
		return NULL;

	if( con.lines_count == 0 )
	{
		return con.buffer;
	}
	else
	{
		char	*firstline_start = con.lines[con.lines_first].start;
		char	*lastline_onepastend = CON_LINES_LAST().start + CON_LINES_LAST().length;

		// the buffer is cyclic, so we first have two cases...
		if( firstline_start < lastline_onepastend ) // buffer is contiguous
		{
			// put at end?
			if( length <= con.buffer + con.bufsize - lastline_onepastend )
				return lastline_onepastend;
			// put at beginning?
			else if( length <= firstline_start - con.buffer )
				return con.buffer;

			return NULL;
		}
		else
		{
			// buffer has a contiguous hole
			if( length <= firstline_start - lastline_onepastend )
				return lastline_onepastend;

			return NULL;
		}
	}
}

/*
================
Con_AddLine

Appends a given string as a new line to the console.
================
*/
static void Con_AddLine( const char *line, int length, qboolean newline )
{
	char		*putpos;
	con_lineinfo_t	*p;

	if( !con.initialized || !con.buffer )
		return;

	Con_FixTimes();
	length++;	// reserve space for term

	ASSERT( length < CON_TEXTSIZE );

	while( !( putpos = Con_BytesLeft( length )) || con.lines_count >= con.maxlines )
		Con_DeleteLine();

	if( newline )
	{
		memcpy( putpos, line, length );
		putpos[length - 1] = '\0';
		con.lines_count++;

		p = &CON_LINES_LAST();
		p->start = putpos;
		p->length = length;
		p->addtime = cl.time;
	}
	else
	{
		p = &CON_LINES_LAST();
		putpos = p->start + Q_strlen( p->start );
		memcpy( putpos, line, length - 1 );
		p->length = Q_strlen( p->start );
		putpos[p->length] = '\0';
		p->addtime = cl.time;
		p->length++;
	}
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
static void Con_CheckResize( void )
{
	int	charWidth = 8;
	int	i, width;

	if( con.curFont && con.curFont->hFontTexture )
		charWidth = con.curFont->charWidths['O'] - 1;

	width = ( refState.width / charWidth ) - 2;
	if( !ref.initialized ) width = (640 / 5);

	if( width == con.linewidth )
		return;

	Con_ClearNotify();
	con.linewidth = width;
	con.backscroll = 0;

	con.input.widthInChars = con.linewidth;
}

/*
================
Con_PageUp
================
*/
void Con_PageUp( int lines )
{
	con.backscroll += abs( lines );
}

/*
================
Con_PageDown
================
*/
void Con_PageDown( int lines )
{
	con.backscroll -= abs( lines );
}

/*
================
Con_Top
================
*/
static void Con_Top( void )
{
	con.backscroll = CON_MAXLINES;
}

/*
================
Con_Bottom
================
*/
void Con_Bottom( void )
{
	con.backscroll = 0;
}

/*
================
Con_Visible
================
*/
int GAME_EXPORT Con_Visible( void )
{
	return (con.vislines > 0);
}

/*
================
Con_FixedFont
================
*/
qboolean Con_FixedFont( void )
{
	return CL_FixedFont( con.curFont );
}


/*
================
Con_LoadConsoleFont

INTERNAL RESOURCE
================
*/
static void Con_LoadConsoleFont( int fontNumber, cl_font_t *font )
{
	qboolean success = false;
	float scale = con_fontscale.value;

	if( font->valid )
		return; // already loaded

	if( con_oldfont.value )
	{
		success = Con_LoadVariableWidthFont( "gfx/conchars.fnt", font, scale, &con_fontrender, TF_FONT|TF_NEAREST );
	}
	else
	{
		string path;
		dword crc = 0;

		// replace default fonts.wad textures by current charset's font
		if( !CRC32_File( &crc, "fonts.wad" ) || crc == 0x3c0a0029 )
		{
			if( Q_snprintf( path, sizeof( path ),
				"font%i_%s.fnt", fontNumber, Cvar_VariableString( "con_charset" )) > 0 )
			{
				success = Con_LoadVariableWidthFont( path, font, scale, &con_fontrender, TF_FONT|TF_NEAREST );
			}
		}

		if( !success )
		{
			Q_snprintf( path, sizeof( path ), "fonts/font%i", fontNumber );
			success = Con_LoadVariableWidthFont( path, font, scale, &con_fontrender, TF_FONT|TF_NEAREST );
		}
	}

	if( !success )
	{
		// quake fixed font as fallback
		// keep source to print directly into conback image
		if( !Con_LoadFixedWidthFont( "gfx/conchars", font, scale, &con_fontrender, TF_FONT|TF_NEAREST|TF_KEEP_SOURCE ))
			Con_DPrintf( S_ERROR "failed to load console font\n" );
	}
}

/*
================
Con_LoadConchars
================
*/
static void Con_LoadConchars( void )
{
	int	i, fontSize;

	// load all the console fonts
	for( i = 0; i < CON_NUMFONTS; i++ )
		Con_LoadConsoleFont( i, con.chars + i );

	// select properly fontsize
	if( con_fontnum.value >= 0 && con_fontnum.value <= CON_NUMFONTS - 1 )
		fontSize = con_fontnum.value;
	else if( refState.width <= 640 )
		fontSize = 0;
	else if( refState.width >= 1280 )
		fontSize = 2;
	else fontSize = 1;

	if( fontSize > CON_NUMFONTS - 1 )
		fontSize = CON_NUMFONTS - 1;

	// sets the current font
	con.curFont = &con.chars[fontSize];
}

/*
============================
Con_UtfProcessChar

Convert utf char to current font's single-byte encoding
============================
*/
int Con_UtfProcessCharForce( int in )
{
	// TODO: get rid of global state where possible
	static utfstate_t state = { 0 };

	uint32_t ch = Q_DecodeUTF8( &state, in );

	if( g_codepage == 1251 )
		return Q_UnicodeToCP1251( ch );
	if( g_codepage == 1252 )
		return Q_UnicodeToCP1252( ch );

	return '?'; // not implemented yet
}

int GAME_EXPORT Con_UtfProcessChar( int in )
{
	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return in;

	// otherwise, decode it and convert to selected codepage
	return Con_UtfProcessCharForce( in );
}

/*
=================
Con_UtfMoveLeft

get position of previous printful char
=================
*/
int Con_UtfMoveLeft( char *str, int pos )
{
	utfstate_t state = { 0 };
	int k = 0;
	int i;

	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return pos - 1;

	if( pos == 1 )
		return 0;

	for( i = 0; i < pos - 1; i++ )
	{
		if( Q_DecodeUTF8( &state, (byte)str[i] ))
			k = i + 1;
	}

	return k;
}

/*
=================
Con_UtfMoveRight

get next of previous printful char
=================
*/
int Con_UtfMoveRight( char *str, int pos, int length )
{
	utfstate_t state = { 0 };
	int i;

	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return pos + 1;

	for( i = pos; i <= length; i++ )
	{
		if( Q_DecodeUTF8( &state, (byte)str[i] ))
			return i + 1;
	}

	return pos + 1;
}

static void Con_DrawCharToConback( int num, const byte *conchars, byte *dest )
{
	int	row, col;
	const byte	*source;
	int	drawline;
	int	x;

	row = num >> 4;
	col = num & 15;
	source = conchars + (row << 10) + (col << 3);

	drawline = 8;

	while( drawline-- )
	{
		for( x = 0; x < 8; x++ )
			if( source[x] != 255 )
				dest[x] = 0x60 + source[x];
		source += 128;
		dest += 320;
	}

}

/*
====================
Con_GetFont

====================
*/
cl_font_t *Con_GetFont( int num )
{
	num = bound( 0, num, CON_NUMFONTS - 1 );
	return &con.chars[num];
}

/*
====================
Con_GetCurFont

====================
*/
cl_font_t *Con_GetCurFont( void )
{
	return con.curFont;
}

/*
====================
Con_DrawStringLen

compute string width and height in screen pixels
====================
*/
void GAME_EXPORT Con_DrawStringLen( const char *pText, int *length, int *height )
{
	CL_DrawStringLen( con.curFont, pText, length, height, FONT_DRAW_UTF8 );
}

/*
====================
Con_DrawString

client version of routine
====================
*/
int Con_DrawString( int x, int y, const char *string, const rgba_t setColor )
{
	return CL_DrawString( x, y, string, setColor, con.curFont, FONT_DRAW_UTF8 );
}

/*
================
Con_Init
================
*/
void Con_Init( void )
{
	if( host.type == HOST_DEDICATED )
		return; // dedicated server already have console

	// must be init before startup video subsystem
	Cvar_RegisterVariable( &scr_conspeed );
	Cvar_RegisterVariable( &con_notifytime );
	Cvar_RegisterVariable( &con_fontsize );
	Cvar_RegisterVariable( &con_charset );
	Cvar_RegisterVariable( &con_fontscale );
	Cvar_RegisterVariable( &con_fontrender );
	Cvar_RegisterVariable( &con_fontnum );
	Cvar_RegisterVariable( &con_color );
	Cvar_RegisterVariable( &scr_drawversion );
	Cvar_RegisterVariable( &con_oldfont );

	// init the console buffer
	con.bufsize = CON_TEXTSIZE;
	con.buffer = (char *)Z_Calloc( con.bufsize );
	con.maxlines = CON_MAXLINES;
	con.lines = (con_lineinfo_t *)Z_Calloc( con.maxlines * sizeof( *con.lines ));
	con.lines_first = con.lines_count = 0;
	con.num_times = CON_TIMES; // default as 4

	Con_CheckResize();

	Con_ClearField( &con.input );
	con.input.widthInChars = con.linewidth;

	Con_ClearField( &con.chat );
	con.chat.widthInChars = con.linewidth;

	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f, "opens or closes the console" );
	Cmd_AddRestrictedCommand( "clear", Con_Clear_f, "clear console history" );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f, "enable message mode \"say\"" );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f, "enable message mode \"say_team\"" );
	Cmd_AddCommand( "contimes", Con_SetTimes_f, "change number of console overlay lines (4-64)" );
	con.initialized = true;

	Con_Printf( "Console initialized.\n" );
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void )
{
	con.initialized = false;

	if( con.buffer )
		Mem_Free( con.buffer );

	if( con.lines )
		Mem_Free( con.lines );

	con.buffer = NULL;
	con.lines = NULL;
	Con_SaveHistory( &con.history );
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed
If no console is visible, the notify window will pop up.
================
*/
void Con_Print( const char *txt )
{
	static qboolean cr_pending = false;
	static qboolean colorstring = false;
	static char buf[MAX_PRINT_MSG];
	static int  lastlength = 0;
	static int  bufpos = 0;
	static int  charpos = 0;

	qboolean norefresh = false;
	int		c, mask = 0;

	// client not running
	if( !con.initialized || !con.buffer )
		return;

	if( txt[0] == 2 )
	{
		// go to colored text
		if( Con_FixedFont( ))
			mask = 128;
		txt++;
	}

	if( txt[0] == 3 )
	{
		norefresh = true;
		txt++;
	}

	for( ; *txt; txt++ )
	{
		if( cr_pending )
		{
			Con_DeleteLastLine();
			cr_pending = false;
		}
		c = *txt;

		switch( c )
		{
		case '\0':
			break;
		case '\r':
			if( txt[1] != '\n' )
			{
				Con_AddLine( buf, bufpos, true );
				lastlength = CON_LINES_LAST().length;
				cr_pending = true;
				bufpos = 0;
				charpos = 0;
			}
			break;
		case '\n':
			Con_AddLine( buf, bufpos, true );
			lastlength = CON_LINES_LAST().length;
			bufpos = 0;
			charpos = 0;
			break;
		default:
			buf[bufpos++] = c | mask;

			if( IsColorString( txt ))
			{
				// first color string character
				colorstring = true;
			}
			else if( colorstring )
			{
				// second color string character
				colorstring = false;
			}
			else
			{
				// not a color string, move char counter
				charpos++;
			}

			if(( bufpos >= sizeof( buf ) - 1 ) || charpos >= ( con.linewidth - 1 ))
			{
				Con_AddLine( buf, bufpos, true );
				lastlength = CON_LINES_LAST().length;
				bufpos = 0;
				charpos = 0;
			}
			break;
		}
	}

	if( norefresh ) return;

	// custom renderer cause problems while updates screen on-loading
	if( SV_Active() && cls.state < ca_active && !cl.video_prepped && !cls.disable_screen )
	{
		if( bufpos != 0 )
		{
			Con_AddLine( buf, bufpos, lastlength != 0 );
			lastlength = 0;
			bufpos = 0;
			charpos = 0;
		}

		// pump messages to avoid window hanging
		if( con.lastupdate < Sys_DoubleTime( ))
		{
			con.lastupdate = Sys_DoubleTime() + 1.0;
			Host_InputFrame();
		}

		// FIXME: disable updating screen, because when texture is bound any console print
		// can re-bound it to console font texture
#if 0
		if( !inupdate )
		{
			inupdate = true;
			SCR_UpdateScreen ();
			inupdate = false;
		}
#endif
	}
}

/*
================
Con_NXPrintfv

Draw a single debug line with specified height, color and time to live
================
*/
static void Con_NXPrintfv( keydest_t key_dest, const con_nprint_t *info, const char *fmt, va_list va )
{
	if( info->index < 0 || info->index >= ARRAYSIZE( con.notify ))
		return;

	Q_vsnprintf( con.notify[info->index].szNotify, sizeof( con.notify[info->index].szNotify ), fmt, va );

	// setup values
	con.notify[info->index].key_dest = key_dest;
	con.notify[info->index].expire = host.realtime + info->time_to_live;
	MakeRGBA( con.notify[info->index].color, (byte)(info->color[0] * 255), (byte)(info->color[1] * 255), (byte)(info->color[2] * 255), 255 );
	con.draw_notify = true;
}

/*
================
Con_NPrint

Draw a single debug line with specified height
================
*/
void GAME_EXPORT Con_NPrintf( int idx, const char *fmt, ... )
{
	va_list	args;
	con_nprint_t info =
	{
		.index = idx,
		.time_to_live = 4.0f,
		.color = { 1.0f, 1.0f, 1.0f },
	};

	va_start( args, fmt );
	Con_NXPrintfv( key_game, &info, fmt, args );
	va_end( args );
}

/*
================
Con_NXPrint

Draw a single debug line with specified height, color and time to live
================
*/
void GAME_EXPORT Con_NXPrintf( con_nprint_t *info, const char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	va_start( args, fmt );
	Con_NXPrintfv( key_game, info, fmt, args );
	va_end( args );
}

/*
================
UI_NPrint

Draw a single debug line with specified height (menu version)
================
*/
void GAME_EXPORT UI_NPrintf( int idx, const char *fmt, ... )
{
	va_list	args;
	con_nprint_t info =
	{
		.index = idx,
		.time_to_live = 4.0f,
		.color = { 1.0f, 1.0f, 1.0f },
	};

	va_start( args, fmt );
	Con_NXPrintfv( key_menu, &info, fmt, args );
	va_end( args );
}

/*
================
UI_NXPrint

Draw a single debug line with specified height, color and time to live (menu version)
================
*/
void GAME_EXPORT UI_NXPrintf( con_nprint_t *info, const char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	va_start( args, fmt );
	Con_NXPrintfv( key_menu, info, fmt, args );
	va_end( args );
}

/*
=============================================================================

EDIT FIELDS

=============================================================================
*/
/*
================
Con_ClearField
================
*/
static void Con_ClearField( field_t *edit )
{
	memset( edit->buffer, 0, sizeof( edit->buffer ));
	edit->cursor = 0;
	edit->scroll = 0;
}

/*
================
Field_Set
================
*/
static void Field_Set( field_t *f, const char *string )
{
	f->scroll = 0;
	f->cursor = Q_strncpy( f->buffer, string, sizeof( f->buffer ));
}

/*
================
Field_Paste
================
*/
static void Field_Paste( field_t *edit )
{
	char	*cbd;
	int	i, pasteLen;

	cbd = Sys_GetClipboardData();
	if( !cbd ) return;

	// send as if typed, so insert / overstrike works properly
	pasteLen = Q_strlen( cbd );
	for( i = 0; i < pasteLen; i++ )
		Field_CharEvent( edit, cbd[i] );
}

/*
=================
Field_GoTo

=================
*/
static void Field_GoTo( field_t *edit, int pos )
{
	edit->cursor = pos;
	edit->scroll = Q_max( 0, edit->cursor - edit->widthInChars );
}

/*
=================
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
static void Field_KeyDownEvent( field_t *edit, int key )
{
	int	len;

	// shift-insert is paste
	if((( key == K_INS ) || ( key == K_KP_INS )) && Key_IsDown( K_SHIFT ))
	{
		Field_Paste( edit );
		return;
	}

	len = Q_strlen( edit->buffer );

	if( key == K_DEL )
	{
		if( edit->cursor < len )
			memmove( edit->buffer + edit->cursor, edit->buffer + edit->cursor + 1, len - edit->cursor );
		return;
	}

	if( key == K_BACKSPACE || key == K_X_BUTTON )
	{
		if( edit->cursor > 0 )
		{
			int newcursor = Con_UtfMoveLeft( edit->buffer, edit->cursor );
			memmove( edit->buffer + newcursor, edit->buffer + edit->cursor, len - edit->cursor + 1 );
			edit->cursor = newcursor;
			if( edit->scroll ) edit->scroll--;
		}
		return;
	}

	if( key == K_RIGHTARROW || key == K_DPAD_RIGHT )
	{
		if( edit->cursor < len ) edit->cursor = Con_UtfMoveRight( edit->buffer, edit->cursor, edit->widthInChars );
		if( edit->cursor >= edit->scroll + edit->widthInChars && edit->cursor <= len )
			edit->scroll++;
		return;
	}

	if( key == K_LEFTARROW || key == K_DPAD_LEFT )
	{
		if( edit->cursor > 0 ) edit->cursor = Con_UtfMoveLeft( edit->buffer, edit->cursor );
		if( edit->cursor < edit->scroll ) edit->scroll--;
		return;
	}

	if( key == K_HOME || ( Q_tolower(key) == 'a' && Key_IsDown( K_CTRL )))
	{
		Field_GoTo( edit, 0 );
		return;
	}

	if( key == K_END || ( Q_tolower(key) == 'e' && Key_IsDown( K_CTRL )))
	{
		Field_GoTo( edit, len );
		return;
	}

	if( key == K_INS )
	{
		host.key_overstrike = !host.key_overstrike;
		return;
	}
}

/*
==================
Field_CharEvent
==================
*/
static void Field_CharEvent( field_t *edit, int ch )
{
	int	len;

	if( ch == 'v' - 'a' + 1 )
	{
		// ctrl-v is paste
		Field_Paste( edit );
		return;
	}

	if( ch == 'c' - 'a' + 1 )
	{
		// ctrl-c clears the field
		Con_ClearField( edit );
		return;
	}

	len = Q_strlen( edit->buffer );

	if( ch == 'a' - 'a' + 1 )
	{
		// ctrl-a is home
		Field_GoTo( edit, 0 );
		return;
	}

	if( ch == 'e' - 'a' + 1 )
	{
		// ctrl-e is end
		Field_GoTo( edit, len );
		return;
	}

	// ignore any other non printable chars
	if( ch < 32 ) return;

	if( host.key_overstrike )
	{
		if ( edit->cursor == MAX_STRING - 1 ) return;
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}
	else
	{
		// insert mode
		if ( len == MAX_STRING - 1 ) return; // all full
		memmove( edit->buffer + edit->cursor + 1, edit->buffer + edit->cursor, len + 1 - edit->cursor );
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}

	if( edit->cursor >= edit->widthInChars ) edit->scroll++;
	if( edit->cursor == len + 1 ) edit->buffer[edit->cursor] = 0;
}

/*
==================
Field_DrawInputLine
==================
*/
static void Field_DrawInputLine( int x, int y, const field_t *edit )
{
	int curPos;
	char str[MAX_SYSPATH];
	const byte *colorDefault = g_color_table[ColorIndex( COLOR_DEFAULT )];
	const int prestep = bound( 0, edit->scroll, sizeof( edit->buffer ) - 1 );
	const int drawLen = bound( 0, edit->widthInChars, sizeof( str ));
	const int cursorCharPos = bound( 0, edit->cursor - prestep, sizeof( str ));

	str[0] = 0;
	Q_strncpy( str, edit->buffer + prestep, drawLen );

	// draw it
	CL_DrawString( x, y, str, colorDefault, con.curFont, FONT_DRAW_UTF8 );

	// draw the cursor
	if((int)( host.realtime * 4 ) & 1 ) return; // off blink

	// calc cursor position
	str[cursorCharPos] = 0;
	CL_DrawStringLen( con.curFont, str, &curPos, NULL, FONT_DRAW_UTF8 );

	if( host.key_overstrike )
	{
		CL_DrawCharacter( x + curPos, y, '|', colorDefault, con.curFont, 0 );
	}
	else
	{
		CL_DrawCharacter( x + curPos, y, '_', colorDefault, con.curFont, 0 );
	}
}

/*
=============================================================================

CONSOLE HISTORY HANDLING

=============================================================================
*/
/*
===================
Con_HistoryFromField

===================
*/
static void Con_HistoryFromField( history_line_t *dst, const field_t *src )
{
	Q_strncpy( dst->buffer, src->buffer, sizeof( dst->buffer ));
	dst->cursor = src->cursor;
	dst->scroll = src->scroll;
}

/*
===================
Con_HistoryToField

===================
*/
static void Con_HistoryToField( field_t *dst, const history_line_t *src )
{
	Q_strncpy( dst->buffer, src->buffer, sizeof( dst->buffer ));
	dst->cursor = src->cursor;
	dst->scroll = src->scroll;
}

/*
===================
Con_HistoryUp

===================
*/
static void Con_HistoryUp( con_history_t *self, field_t *in )
{
	if( self->line == self->next )
		Con_HistoryFromField( &self->backup, in );
	else
		Con_HistoryFromField( &self->lines[self->line % CON_HISTORY], in );

	if(( self->next - self->line ) < CON_HISTORY )
		self->line = Q_max( 0, self->line - 1 );

	Con_HistoryToField( in, &self->lines[self->line % CON_HISTORY] );
}

/*
===================
Con_HistoryDown

===================
*/
static void Con_HistoryDown( con_history_t *self, field_t *in )
{
	Con_HistoryFromField( &self->lines[self->line % CON_HISTORY], in );

	self->line = Q_min( self->next, self->line + 1 );
	if( self->line == self->next )
		Con_HistoryToField( in, &self->backup );
	else
		Con_HistoryToField( in, &self->lines[self->line % CON_HISTORY] );
}

/*
===================
Con_HistoryAppend
===================
*/
static void Con_HistoryAppend( con_history_t *self, const field_t *from )
{
	int prevLine = Q_max( 0, self->line - 1 );
	const char *buf = from->buffer;

	// skip backslashes
	if( from->buffer[0] == '\\' || from->buffer[1] == '/' )
		buf++;

	// only if non-empty
	if( !from->buffer[0] )
		return;

	// skip empty commands
	if( Q_isspace( buf ))
		return;

	// if not copy (don't ignore backslashes)
	if( !Q_strcmp( from->buffer, self->lines[prevLine % CON_HISTORY].buffer ))
		return;

	Con_HistoryFromField( &self->lines[self->next % CON_HISTORY], from );
	self->line = ++self->next;
}

static void Con_LoadHistory( con_history_t *self )
{
	file_t *fd;
	int i;

	fd = FS_Open( "console_history.txt", "rb", true );

	if( !fd )
		return;

	while( !FS_Eof( fd ))
	{
		history_line_t *f = &self->lines[self->next % CON_HISTORY];

		FS_Gets( fd, f->buffer, sizeof( f->buffer ));
		f->cursor = Q_strlen( f->buffer );
		f->scroll = 0;

		// skip empty lines
		if( f->cursor == 0 )
			continue;

		// skip repeating lines
		if( self->next > 0 )
		{
			const history_line_t *prev = &self->lines[(self->next - 1) % CON_HISTORY];
			if( !Q_stricmp( prev->buffer, f->buffer ))
				continue;
		}

		self->next++;
	}

	FS_Close( fd );

	for( i = self->next; i < CON_HISTORY; i++ )
	{
		history_line_t *f = &self->lines[i];

		memset( f, 0, sizeof( *f ));
	}

	self->line = self->next;
}

static void Con_SaveHistory( con_history_t *self )
{
	int historyStart = self->next - CON_HISTORY, i;
	file_t *f;

	// do not save history if nothing was executed
	if( self->next == 0 )
		return;

	if( historyStart < 0 )
		historyStart = 0;

	f = FS_Open( "console_history.txt", "wb", true );

	for( i = historyStart; i < self->next; i++ )
	{
		const char *s = self->lines[i % CON_HISTORY].buffer;

		// HACKHACK: don't save lines that have something that looks like a password
		if( Q_stristr( s, "password" ) || Q_stristr( s, "_pw" ))
			continue;

		FS_Printf( f, "%s\n", s );
	}

	FS_Close( f );
}


/*
=============================================================================

CONSOLE LINE EDITING

=============================================================================
*/
/*
====================
Key_Console

Handles history and console scrollback
====================
*/
void Key_Console( int key )
{
	// exit the console by pressing MINUS on NSwitch
	// or both Back(Select)/Start buttons for everyone else
	if( key == K_BACK_BUTTON || key == K_START_BUTTON || key == K_ESCAPE )
	{
		if( cls.state == ca_active && !cl.background )
			Key_SetKeyDest( key_game );
		else UI_SetActiveMenu( true );
		return;
	}

	// ctrl-L clears screen
	if( key == 'l' && Key_IsDown( K_CTRL ))
	{
		Cbuf_AddText( "clear\n" );
		return;
	}

	// enter or A finish the line
	if( key == K_ENTER || key == K_KP_ENTER || key == K_A_BUTTON )
	{
		// backslash text are commands, else chat
		if( con.input.buffer[0] == '\\' || con.input.buffer[0] == '/' )
			Cbuf_AddText( con.input.buffer + 1 ); // skip backslash
		else Cbuf_AddText( con.input.buffer ); // valid command
		Cbuf_AddText( "\n" );

		// echo to console
		Con_Printf( ">%s\n", con.input.buffer );

		// copy line to history buffer
		// just in case, remove all CR and LF characters pushing it to the history
		// not sure how they get even added in the first place
		COM_RemoveLineFeed( con.input.buffer, sizeof( con.input.buffer ));
		Con_HistoryAppend( &con.history, &con.input );

		Con_ClearField( &con.input );
		con.input.widthInChars = con.linewidth;
		Con_Bottom();

		if( cls.state == ca_disconnected )
		{
			// force an update, because the command may take some time
			SCR_UpdateScreen ();
		}
		return;
	}

	// command completion
	if( key == K_TAB || key == K_L2_BUTTON )
	{
		Con_CompleteCommand( &con.input );
		Con_Bottom();
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)
	if(( key == K_MWHEELUP && Key_IsDown( K_SHIFT )) || ( key == K_UPARROW ) || (( Q_tolower(key) == 'p' ) && Key_IsDown( K_CTRL )))
	{
		Con_HistoryUp( &con.history, &con.input );
		return;
	}

	if(( key == K_MWHEELDOWN && Key_IsDown( K_SHIFT )) || ( key == K_DOWNARROW ) || (( Q_tolower(key) == 'n' ) && Key_IsDown( K_CTRL )))
	{
		Con_HistoryDown( &con.history, &con.input );
		return;
	}

	// console scrolling
	if( key == K_PGUP || key == K_DPAD_UP )
	{
		Con_PageUp( 1 );
		return;
	}

	if( key == K_PGDN || key == K_DPAD_DOWN )
	{
		Con_PageDown( 1 );
		return;
	}

	if( key == K_MWHEELUP )
	{
		if( Key_IsDown( K_CTRL ))
			Con_PageUp( 8 );
		else Con_PageUp( 2 );
		return;
	}

	if( key == K_MWHEELDOWN )
	{
		if( Key_IsDown( K_CTRL ))
			Con_PageDown( 8 );
		else Con_PageDown( 2 );
		return;
	}

	// ctrl-home = top of console
	if( key == K_HOME && Key_IsDown( K_CTRL ))
	{
		Con_Top();
		return;
	}

	// ctrl-end = bottom of console
	if( key == K_END && Key_IsDown( K_CTRL ))
	{
		Con_Bottom();
		return;
	}

	// enable the OSK with button press
	if( key == K_Y_BUTTON )
	{
		Key_EnableTextInput( true, true );
		return;
	}

	// pass to the normal editline routine
	Field_KeyDownEvent( &con.input, key );
}

/*
================
Key_Message

In game talk message
================
*/
void Key_Message( int key )
{
	char	buffer[MAX_SYSPATH];

	if( key == K_ESCAPE || key == K_BACK_BUTTON )
	{
		Key_SetKeyDest( key_game );
		Con_ClearField( &con.chat );
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER || key == K_A_BUTTON )
	{
		if( con.chat.buffer[0] && cls.state == ca_active )
		{
			Q_snprintf( buffer, sizeof( buffer ), "%s \"%s\"\n", con.chat_cmd, con.chat.buffer );

			if( g_messagemode_privileged )
				Cbuf_AddText( buffer );
			else Cbuf_AddFilteredText( buffer );
		}

		Key_SetKeyDest( key_game );
		Con_ClearField( &con.chat );
		return;
	}

	Field_KeyDownEvent( &con.chat, key );
}

/*
==============================================================================

DRAWING

==============================================================================
*/
/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
static void Con_DrawInput( int lines )
{
	int	y;

	// don't draw anything (always draw if not active)
	if( cls.key_dest != key_console || !con.curFont )
		return;

	y = lines - ( con.curFont->charHeight * 2 );
	CL_DrawCharacter( con.curFont->charWidths[' '], y, ']', g_color_table[7], con.curFont, 0 );
	Field_DrawInputLine(  con.curFont->charWidths[' ']*2, y, &con.input );
}

/*
================
Con_DrawDebugLines

Custom debug messages
================
*/
static int Con_DrawDebugLines( void )
{
	notify_t *notify = con.notify;
	int	i, count = 0;
	int	defaultX;
	int	y = 20;
	int	fontTall;

	if( !con.curFont || !con.curFont->valid )
		return 0;

	defaultX = refState.width / 4;
	fontTall = con.curFont->charHeight + 1;

	for( i = 0; i < ARRAYSIZE( con.notify ); i++, notify++ )
	{
		int x, len;

		if( host.realtime > notify->expire )
			continue;

		if( notify->key_dest != cls.key_dest )
			continue;

		Con_DrawStringLen( notify->szNotify, &len, NULL );
		x = refState.width - Q_max( defaultX, len ) - 10;

		if( y + fontTall > refState.height - 20 )
			return count;

		count++;
		y += fontTall;
		CL_DrawString( x, y, notify->szNotify, notify->color, con.curFont, FONT_DRAW_UTF8 | FONT_DRAW_NOLF );
	}

	return count;
}

/*
================
Con_DrawDebug

Draws the debug messages (not passed to console history)
================
*/
void Con_DrawDebug( void )
{
	static double	timeStart;
	string		dlstring;
	int		x, y;

	if( scr_download.value != -1.0f )
	{
		int length;
		Q_snprintf( dlstring, sizeof( dlstring ), "Downloading [%d remaining]: ^2%s^7 %5.1f%% time %.f secs",
			host.downloadcount, host.downloadfile, scr_download.value, Sys_DoubleTime() - timeStart );

		Con_DrawStringLen( dlstring, &length, NULL );
		length = Q_max( length, 300 );
		x = refState.width - length * 1.05f;
		y = con.curFont->charHeight * 1.05f;
		Con_DrawString( x, y, dlstring, g_color_table[7] );
	}
	else
	{
		timeStart = host.realtime;
	}

	if( !host.allow_console || Cvar_VariableInteger( "cl_background" ) || Cvar_VariableInteger( "sv_background" ))
		return;

	if( con.draw_notify && !Con_Visible( ))
	{
		if( Con_DrawDebugLines() == 0 )
			con.draw_notify = false;
	}
}

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify( void )
{
	double	time = cl.time;
	int	i, x, y = 0;

	if( !con.curFont ) return;

	x = con.curFont->charWidths[' ']; // offset one space at left screen side

	if( host.allow_console && ( !Cvar_VariableInteger( "cl_background" ) && !Cvar_VariableInteger( "sv_background" )))
	{
		for( i = Q_max( 0, CON_LINES_COUNT - con.num_times ); i < CON_LINES_COUNT; i++ )
		{
			con_lineinfo_t	*l = &CON_LINES( i );

			if( l->addtime < ( time - con_notifytime.value ))
				continue;

			Con_DrawString( x, y, l->start, g_color_table[7] );
			y += con.curFont->charHeight;
		}
	}

	if( cls.key_dest == key_message )
	{
		string	buf;
		int	len;

		// update chatline position from client.dll
		if( clgame.dllFuncs.pfnChatInputPosition )
			clgame.dllFuncs.pfnChatInputPosition( &x, &y );

		Q_snprintf( buf, sizeof( buf ), "%s: ", con.chat_cmd );

		Con_DrawStringLen( buf, &len, NULL );
		Con_DrawString( x, y, buf, g_color_table[7] );

		Field_DrawInputLine( x + len, y, &con.chat );
	}

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
================
Con_DrawConsoleLine

Draws a line of the console; returns its height in lines.
If alpha is 0, the line is not drawn, but still wrapped and its height
returned.
================
*/
static int Con_DrawConsoleLine( int y, int lineno )
{
	con_lineinfo_t	*li = &CON_LINES( lineno );

	if( !li || !li->start || *li->start == '\1' )
		return 0;	// this string will be shown only at notify

	if( y >= con.curFont->charHeight )
	{
		float x = con.curFont->charWidths[' '];

		CL_DrawString( x, y, li->start, g_color_table[7], con.curFont, FONT_DRAW_UTF8 );
	}

	return con.curFont->charHeight;
}

/*
================
Con_LastVisibleLine

Calculates the last visible line index and how much to show
of it based on con.backscroll.
================
*/
static void Con_LastVisibleLine( int *lastline )
{
	int	i, lines_seen = 0;

	con.backscroll = Q_max( 0, con.backscroll );
	*lastline = 0;

	// now count until we saw con_backscroll actual lines
	for( i = CON_LINES_COUNT - 1; i >= 0; i-- )
	{
		// line is the last visible line?
		*lastline = i;

		if( lines_seen + 1 > con.backscroll && lines_seen <= con.backscroll )
			return;

		lines_seen += 1;
	}

	// if we get here, no line was on screen - scroll so that one line is visible then.
	con.backscroll = lines_seen - 1;
}

/*
================
Con_DrawConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole( int lines )
{
	int	i, x, y;
	float	fraction;
	int	start;
	int	stringLen, width = 0, charH;
	string	curbuild;
	byte	color[4];

	if( lines <= 0 ) return;

	// draw the background
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 ); // to prevent grab color from screenfade
	if( refState.width * 3 / 4 < refState.height && lines >= refState.height )
		ref.dllFuncs.R_DrawStretchPic( 0, lines - refState.height, refState.width, refState.height - refState.width * 3 / 4, 0, 0, 1, 1, R_GetBuiltinTexture( REF_BLACK_TEXTURE) );
	ref.dllFuncs.R_DrawStretchPic( 0, lines - refState.width * 3 / 4, refState.width, refState.width * 3 / 4, 0, 0, 1, 1, con.background );

	if( !con.curFont || !host.allow_console )
		return; // nothing to draw

	// draw current version
	memcpy( color, g_color_table[7], sizeof( color ));

	Q_snprintf( curbuild, MAX_STRING, XASH_ENGINE_NAME " %i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));

	Con_DrawStringLen( curbuild, &stringLen, &charH );

	start = refState.width - stringLen;
	fraction = lines / (float)refState.height;
	color[3] = Q_min( fraction * 2.0f, 1.0f ) * 255; // fadeout version number

	Con_DrawString( start, 0, curbuild, color );

	// draw the text
	if( CON_LINES_COUNT > 0 )
	{
		int	ymax = lines - (con.curFont->charHeight * 2.0f);
		int	lastline;

		Con_LastVisibleLine( &lastline );
		y = ymax - con.curFont->charHeight;

		if( con.backscroll )
		{
			start = con.curFont->charWidths[' ']; // offset one space at left screen side

			// draw red arrows to show the buffer is backscrolled
			for( x = 0; x < con.linewidth; x += 4 )
				CL_DrawCharacter( ( x + 1 ) * start, y, '^', g_color_table[1], con.curFont, 0 );
			y -= con.curFont->charHeight;
		}
		x = lastline;

		while( 1 )
		{
			y -= Con_DrawConsoleLine( y, x );

			// top of console buffer or console window
			if( x == 0 || y < con.curFont->charHeight )
				break;
			x--;
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( lines );

	y = lines - ( con.curFont->charHeight * 1.2f );
	SCR_DrawFPS( Q_max( y, 4 )); // to avoid to hide fps counter

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void )
{
	// never draw console when changelevel in-progress
	if( cls.state != ca_disconnected && ( cls.changelevel || cls.changedemo ))
		return;

	// check for console width changes from a vid mode change
	Con_CheckResize ();

	if( cls.state == ca_connecting || cls.state == ca_connected )
	{
		if( !cl_allow_levelshots.value && !cls.timedemo )
		{
			if( cls.key_dest != key_console && ( Cvar_VariableInteger( "cl_background" ) || Cvar_VariableInteger( "sv_background" )))
				con.vislines = con.showlines = 0;
			else con.vislines = con.showlines = refState.height;
		}
		else
		{
			con.showlines = 0;

			if( host_developer.value >= DEV_EXTENDED && !cls.timedemo )
				Con_DrawNotify(); // draw notify lines
		}
	}

	// if disconnected, render console full screen
	switch( cls.state )
	{
	case ca_disconnected:
		if( cls.key_dest != key_menu )
		{
			Con_DrawSolidConsole( refState.height );
			Key_SetKeyDest( key_console );
		}
		break;
	case ca_connecting:
	case ca_connected:
	case ca_validate:
		// force to show console always for -dev 3 and higher
		Con_DrawSolidConsole( con.vislines );
		break;
	case ca_active:
	case ca_cinematic:
		if( Cvar_VariableInteger( "cl_background" ) || Cvar_VariableInteger( "sv_background" ))
		{
			if( cls.key_dest == key_console )
				Con_DrawSolidConsole( refState.height );
		}
		else
		{
			if( con.vislines )
				Con_DrawSolidConsole( con.vislines );
			else if( cls.state == ca_active && ( cls.key_dest == key_game || cls.key_dest == key_message ) && !cls.timedemo )
				Con_DrawNotify(); // draw notify lines
		}
		break;
	}

	if( !Con_Visible( )) SCR_DrawFPS( 4 );
}

/*
==================
Con_DrawVersion

Used by menu
==================
*/
void Con_DrawVersion( void )
{
	// draws the current build
	byte	*color = g_color_table[7];
	int	stringLen, charH = 0;
	int	start, height = refState.height;
	string	curbuild;

	if( !scr_drawversion.value )
		return;

	if( cls.key_dest == key_menu )
	{
		Q_snprintf( curbuild, sizeof( curbuild ),
			"v%i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
	}
	else
	{
		qboolean draw_version;

		if( CL_IsDevOverviewMode() == 2 || net_graph.value )
			return;

		draw_version = cls.scrshot_action == scrshot_normal
			|| cls.scrshot_action == scrshot_snapshot
			|| host.force_draw_version_time > host.realtime;

		if( !draw_version )
			return;

		Q_snprintf( curbuild, sizeof( curbuild ),
			XASH_ENGINE_NAME " v%i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
	}

	Con_DrawStringLen( curbuild, &stringLen, &charH );
	start = refState.width - stringLen * 1.05f;
	height -= charH * 1.05f;

	Con_DrawString( start, height, curbuild, color );
}

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void )
{
	float	lines_per_frame;

	Con_SetColor( );

	// decide on the destination height of the console
	if( host.allow_console && cls.key_dest == key_console )
	{
#if XASH_MOBILE_PLATFORM
		con.showlines = refState.height; // always full screen on mobile devices
#else
		if( cls.state < ca_active || cl.first_frame )
			con.showlines = refState.height;	// full screen
		else con.showlines = (refState.height >> 1);	// half screen
#endif
	}
	else con.showlines = 0; // none visible

	lines_per_frame = fabs( scr_conspeed.value ) * host.realframetime;

	if( con.showlines < con.vislines )
	{
		con.vislines -= lines_per_frame;
		if( con.showlines > con.vislines )
			con.vislines = con.showlines;
	}
	else if( con.showlines > con.vislines )
	{
		con.vislines += lines_per_frame;
		if( con.showlines < con.vislines )
			con.vislines = con.showlines;
	}

	if( FBitSet( con_charset.flags|con_fontscale.flags|con_fontnum.flags|cl_charset.flags|con_oldfont.flags,  FCVAR_CHANGED ))
	{
		// update codepage parameters
		if( !Q_stricmp( con_charset.string, "cp1251" ))
		{
			g_codepage = 1251;
		}
		else if( !Q_stricmp( con_charset.string, "cp1252" ))
		{
			g_codepage = 1252;
		}
		else
		{
			g_codepage = 0;
		}

		cls.accept_utf8 = !Q_stricmp( cl_charset.string, "utf-8" );
		Con_InvalidateFonts();
		Con_LoadConchars();
		ClearBits( con_charset.flags,   FCVAR_CHANGED );
		ClearBits( con_fontnum.flags,   FCVAR_CHANGED );
		ClearBits( con_fontscale.flags, FCVAR_CHANGED );
		ClearBits( cl_charset.flags,    FCVAR_CHANGED );
		ClearBits( con_oldfont.flags,   FCVAR_CHANGED );
	}
}

/*
==============================================================================

CONSOLE INTERFACE

==============================================================================
*/
/*
================
Con_CharEvent

Console input
================
*/
void Con_CharEvent( int key )
{
	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_console )
	{
		Field_CharEvent( &con.input, key );
	}
	else if( cls.key_dest == key_message )
	{
		Field_CharEvent( &con.chat, key );
	}
}

static int Con_LoadSimpleConback( const char *name, int flags )
{
	int i;

	for( i = 0; i < 5; i++ )
	{
		string path;

		switch( i )
		{
		case 0:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.dds", name );
			break;
		case 1:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.bmp", name );
			break;
		case 2:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.tga", name );
			break;
		case 3:
			Q_snprintf( path, sizeof( path ), "cached/%s640", name );
			break;
		case 4:
			Q_snprintf( path, sizeof( path ), "cached/%s", name );
			break;
		}

		if( g_fsapi.FileExists( path, false ))
		{
			int gl_texturenum = ref.dllFuncs.GL_LoadTexture( path, NULL, 0, flags );

			if( gl_texturenum )
				return gl_texturenum;
		}
	}

	return 0;
}

/*
=========
Con_VidInit

INTERNAL RESOURCE
=========
*/
void Con_VidInit( void )
{
	const uint flags = TF_IMAGE|TF_ALLOW_NEAREST;

	if( !con.historyLoaded )
	{
		Con_LoadHistory( &con.history );
		con.historyLoaded = true;
	}

	if( Sys_CheckParm( "-oldfont" ))
		Cvar_DirectSet( &con_oldfont, "1" );

	Con_LoadConchars();
	Con_CheckResize();

#if XASH_LOW_MEMORY
	con.background = R_GetBuiltinTexture( REF_GRAY_TEXTURE );
#else
	// loading console image
	con.background = Con_LoadSimpleConback( host.allow_console ? "conback" : "loading", flags );

	if( !con.background ) // last chance - quake conback image
	{
		qboolean		draw_to_console = false;
		fs_offset_t		length = 0;
		const byte *buf;

		// NOTE: only these games want to draw build number into console background
		if( !Q_stricmp( FS_Gamedir(), "id1" ))
			draw_to_console = true;

		if( !Q_stricmp( FS_Gamedir(), "hipnotic" ))
			draw_to_console = true;

		if( !Q_stricmp( FS_Gamedir(), "rogue" ))
			draw_to_console = true;

		if( draw_to_console && con.curFont &&
			( buf = ref.dllFuncs.R_GetTextureOriginalBuffer( con.curFont->hFontTexture )) != NULL )
		{
			lmp_t	*cb = (lmp_t *)FS_LoadFile( "gfx/conback.lmp", &length, false );
			char	ver[64];
			byte	*dest;
			int	x, y, len;

			if( cb && cb->width == 320 && cb->height == 200 )
			{
				len = Q_snprintf( ver, 64, "%i", Q_buildnum( )); // can store only buildnum
				dest = (byte *)(cb + 1) + 320 * 186 + 320 - 11 - 8 * len;
				y = len;
				for( x = 0; x < y; x++ )
					Con_DrawCharToConback( ver[x], buf, dest + (x << 3));
				con.background = ref.dllFuncs.GL_LoadTexture( "#gfx/conback.lmp", (byte *)cb, length, TF_IMAGE );
			}
			if( cb ) Mem_Free( cb );
		}

		if( !con.background ) // trying the load unmodified conback
			con.background = ref.dllFuncs.GL_LoadTexture( "gfx/conback.lmp", NULL, 0, TF_IMAGE );
	}

	// missed console image will be replaced as gray background like X-Ray or Crysis
	if( con.background == R_GetBuiltinTexture( REF_DEFAULT_TEXTURE ) || con.background == 0 )
		con.background = R_GetBuiltinTexture( REF_GRAY_TEXTURE );
#endif
}

/*
=========
Con_InvalidateFonts

=========
*/
void Con_InvalidateFonts( void )
{
	int i;
	for( i = 0; i < ARRAYSIZE( con.chars ); i++ )
		CL_FreeFont( &con.chars[i] );
	con.curFont = NULL;
}

/*
=========
Con_FastClose

immediately close the console
=========
*/
void Con_FastClose( void )
{
	Con_ClearField( &con.input );
	Con_ClearNotify();
	con.showlines = 0;
	con.vislines = 0;
}

/*
=========
Con_DefaultColor

called from MainUI
=========
*/
void Con_DefaultColor( int r, int g, int b, qboolean gameui )
{
	r = bound( 0, r, 255 );
	g = bound( 0, g, 255 );
	b = bound( 0, b, 255 );

	// gameui wants to override console color... check if it's not default
	if( gameui && ( g_color_table[7][0] != r || g_color_table[7][1] != g || g_color_table[7][2] != b ))
	{
		// yes, different from default orange, disable con_color
		SetBits( con_color.flags, FCVAR_READ_ONLY );
		ClearBits( con_color.flags, FCVAR_CHANGED );
	}

	MakeRGBA( g_color_table[7], r, g, b, 255 );
}

#if XASH_ENGINE_TESTS
#include "tests.h"

static void Test_RunConHistory( void )
{
	con_history_t hist = { 0 };
	field_t input = { 0 };
	const char *strs1[] = { "map t0a0", "quit", "wtf", "wtf", "", "nyan" };
	const char *strs2[] = { "nyan", "wtf", "quit", "map t0a0" };
	const char *testbackup = "unfinished_edit";
	int i;

	for( i = 0; i < ARRAYSIZE( strs1 ); i++ )
	{
		Field_Set( &input, strs1[i] );
		Con_HistoryAppend( &hist, &input );
	}

	Field_Set( &input, testbackup );

	for( i = 0; i < ARRAYSIZE( strs2 ); i++ )
	{
		Con_HistoryUp( &hist, &input );
		TASSERT_STR( input.buffer, strs2[i] );
	}

	// check for overrun
	Con_HistoryUp( &hist, &input );

	for( i = ARRAYSIZE( strs2 ) - 1; i >= 0; i-- )
	{
		TASSERT_STR( input.buffer, strs2[i] );
		Con_HistoryDown( &hist, &input );
	}

	TASSERT_STR( input.buffer, testbackup );
}

void Test_RunCon( void )
{
	TRUN( Test_RunConHistory() );
}

#endif /* XASH_ENGINE_TESTS */
