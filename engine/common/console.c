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
#include "gl_local.h"
#include "qfont.h"
#include "wadfile.h"

convar_t	*con_notifytime;
convar_t	*scr_conspeed;
convar_t	*con_fontsize;

#define CON_TIMES		4	// notify lines
#define CON_MAX_TIMES	64	// notify max lines
#define COLOR_DEFAULT	'7'
#define CON_HISTORY		64
#define MAX_DBG_NOTIFY	128
#define CON_MAXCMDS		4096	// auto-complete intermediate list
#define CON_NUMFONTS	3	// maxfonts

#define CON_LINES( i )	(con.lines[(con.lines_first + (i)) % con.maxlines])
#define CON_LINES_COUNT	con.lines_count
#define CON_LINES_LAST()	CON_LINES( CON_LINES_COUNT - 1 )

#define CON_TEXTSIZE	1048576	// max scrollback buffer characters in console (1 Mb)
#define CON_MAXLINES	16384	// max scrollback buffer lines in console

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
	string		buffer;
	int		cursor;
	int		scroll;
	int		widthInChars;
} field_t;

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
	cl_font_t		*curFont, *lastUsedFont;
	
	// console input
	field_t		input;

	// chatfiled
	field_t		chat;
	string		chat_cmd;		// can be overrieded by user

	// console history
	field_t		historyLines[CON_HISTORY];
	int		historyLine;	// the line being displayed from history buffer will be <= nextHistoryLine
	int		nextHistoryLine;	// the last line in the history buffer, not masked

	notify_t		notify[MAX_DBG_NOTIFY]; // for Con_NXPrintf
	qboolean		draw_notify;	// true if we have NXPrint message

	// console auto-complete
	string		shortestMatch;
	field_t		*completionField;	// con.input or dedicated server fake field-line
	char		*completionString;
	char		*completionBuffer;
	char		*cmds[CON_MAXCMDS];
	int		matchCount;
} console_t;

static console_t		con;

void Field_CharEvent( field_t *edit, int ch );

/*
================
Con_Clear_f
================
*/
void Con_Clear_f( void )
{
	con.lines_count = 0;
	con.backscroll = 0; // go to end
}

/*
================
Con_SetColor_f
================
*/
void Con_SetColor_f( void )
{
	vec3_t	color;

	switch( Cmd_Argc() )
	{
	case 1:
		Con_Printf( "\"con_color\" is %i %i %i\n", g_color_table[7][0], g_color_table[7][1], g_color_table[7][2] );
		break;
	case 2:
		VectorSet( color, g_color_table[7][0], g_color_table[7][1], g_color_table[7][2] );
		Q_atov( color, Cmd_Argv( 1 ), 3 );
		Con_DefaultColor( color[0], color[1], color[2] );
		break;
	case 4:
		VectorSet( color, Q_atof( Cmd_Argv( 1 )), Q_atof( Cmd_Argv( 2 )), Q_atof( Cmd_Argv( 3 )));
		Con_DefaultColor( color[0], color[1], color[2] );
		break;
	default:
		Con_Printf( S_USAGE "con_color \"r g b\"\n" );
		break;
	}
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
Con_ClearField
================
*/
void Con_ClearField( field_t *edit )
{
	memset( edit->buffer, 0, MAX_STRING );
	edit->cursor = 0;
	edit->scroll = 0;
}

/*
================
Con_ClearTyping
================
*/
void Con_ClearTyping( void )
{
	int	i;

	Con_ClearField( &con.input );
	con.input.widthInChars = con.linewidth;

	// free the old autocomplete list
	for( i = 0; i < con.matchCount; i++ )
	{
		freestring( con.cmds[i] );
	}

	con.matchCount = 0;
}

/*
============
Con_StringLength

skipped color prefixes
============
*/
int Con_StringLength( const char *string )
{
	int		len;
	const char	*p;

	if( !string ) return 0;

	len = 0;
	p = string;

	while( *p )
	{
		if( IsColorString( p ))
		{
			p += 2;
			continue;
		}
		len++;
		p++;
	}

	return len;
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f( void )
{
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
void Con_MessageMode2_f( void )
{
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
void Con_SetTimes_f( void )
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
void Con_FixTimes( void )
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
void Con_DeleteLine( void )
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
void Con_DeleteLastLine( void )
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
void Con_AddLine( const char *line, int length )
{
	byte		*putpos;
	con_lineinfo_t	*p;

	if( !con.initialized || !con.buffer )
		return;

	Con_FixTimes();
	length++;	// reserve space for term

	ASSERT( length < CON_TEXTSIZE );

	while( !( putpos = Con_BytesLeft( length )) || con.lines_count >= con.maxlines )
		Con_DeleteLine();

	memcpy( putpos, line, length );
	putpos[length - 1] = '\0';
	con.lines_count++;

	p = &CON_LINES_LAST();
	p->start = putpos;
	p->length = length;
	p->addtime = cl.time;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( void )
{
	int	charWidth = 8;
	int	i, width;

	if( con.curFont && con.curFont->hFontTexture )
		charWidth = con.curFont->charWidths['O'] - 1;

	width = ( glState.width / charWidth ) - 2;
	if( !glw_state.initialized ) width = (640 / 5);

	if( width == con.linewidth )
		return;

	Con_ClearNotify();
	con.linewidth = width;
	con.backscroll = 0;

	con.input.widthInChars = con.linewidth;

	for( i = 0; i < CON_HISTORY; i++ )
		con.historyLines[i].widthInChars = con.linewidth;
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
void Con_Top( void )
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
qboolean Con_Visible( void )
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
	if( con.curFont && con.curFont->valid && con.curFont->type == FONT_FIXED )
		return true;
	return false;
}

static qboolean Con_LoadFixedWidthFont( const char *fontname, cl_font_t *font )
{
	int	i, fontWidth;

	if( font->valid )
		return true; // already loaded

	if( !FS_FileExists( fontname, false ))
		return false;

	// keep source to print directly into conback image
	font->hFontTexture = GL_LoadTexture( fontname, NULL, 0, TF_FONT|TF_KEEP_SOURCE );
	R_GetTextureParms( &fontWidth, NULL, font->hFontTexture );

	if( font->hFontTexture && fontWidth != 0 )
	{
		font->charHeight = fontWidth / 16;
		font->type = FONT_FIXED;

		// build fixed rectangles
		for( i = 0; i < 256; i++ )
		{
			font->fontRc[i].left = (i * (fontWidth / 16)) % fontWidth;
			font->fontRc[i].right = font->fontRc[i].left + fontWidth / 16;
			font->fontRc[i].top = (i / 16) * (fontWidth / 16);
			font->fontRc[i].bottom = font->fontRc[i].top + fontWidth / 16;
			font->charWidths[i] = fontWidth / 16;
		}
		font->valid = true;
	}

	return true;
}

static qboolean Con_LoadVariableWidthFont( const char *fontname, cl_font_t *font )
{
	int	i, fontWidth;
	byte	*buffer;
	size_t	length;
	qfont_t	*src;

	if( font->valid )
		return true; // already loaded

	if( !FS_FileExists( fontname, false ))
		return false;

	font->hFontTexture = GL_LoadTexture( fontname, NULL, 0, TF_FONT|TF_NEAREST );
	R_GetTextureParms( &fontWidth, NULL, font->hFontTexture );

	// setup consolefont
	if( font->hFontTexture && fontWidth != 0 )
	{
		// half-life font with variable chars witdh
		buffer = FS_LoadFile( fontname, &length, false );

		if( buffer && length >= sizeof( qfont_t ))
		{
			src = (qfont_t *)buffer;
			font->charHeight = src->rowheight;
			font->type = FONT_VARIABLE;

			// build rectangles
			for( i = 0; i < 256; i++ )
			{
				font->fontRc[i].left = (word)src->fontinfo[i].startoffset % fontWidth;
				font->fontRc[i].right = font->fontRc[i].left + src->fontinfo[i].charwidth;
				font->fontRc[i].top = (word)src->fontinfo[i].startoffset / fontWidth;
				font->fontRc[i].bottom = font->fontRc[i].top + src->rowheight;
				font->charWidths[i] = src->fontinfo[i].charwidth;
			}
			font->valid = true;
		}
		if( buffer ) Mem_Free( buffer );
	}

	return true;
}

/*
================
Con_LoadConsoleFont

INTERNAL RESOURCE
================
*/
static void Con_LoadConsoleFont( int fontNumber, cl_font_t *font )
{
	if( font->valid ) return; // already loaded

	// loading conchars
	if( Sys_CheckParm( "-oldfont" ))
		Con_LoadVariableWidthFont( "gfx.wad/conchars.fnt", font );
	else Con_LoadVariableWidthFont( va( "fonts.wad/font%i", fontNumber ), font );

	// quake fixed font as fallback
	if( !font->valid ) Con_LoadFixedWidthFont( "gfx/conchars", font );
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
	for( i = 0; i < 3; i++ )
		Con_LoadConsoleFont( i, con.chars + i );

	// select properly fontsize
	if( glState.width <= 640 )
		fontSize = 0;
	else if( glState.width >= 1280 )
		fontSize = 2;
	else fontSize = 1;

	// sets the current font
	con.lastUsedFont = con.curFont = &con.chars[fontSize];
	
}

static void Con_DrawCharToConback( int num, byte *conchars, byte *dest )
{
	int	row, col;
	byte	*source;
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
Con_TextAdjustSize

draw charcters routine
====================
*/
static void Con_TextAdjustSize( int *x, int *y, int *w, int *h )
{
	float	xscale, yscale;

	if( !x && !y && !w && !h ) return;

	// scale for screen sizes
	xscale = (float)glState.width / (float)clgame.scrInfo.iWidth;
	yscale = (float)glState.height / (float)clgame.scrInfo.iHeight;

	if( x ) *x *= xscale;
	if( y ) *y *= yscale;
	if( w ) *w *= xscale;
	if( h ) *h *= yscale;
}

/*
====================
Con_DrawGenericChar

draw console single character
====================
*/
static int Con_DrawGenericChar( int x, int y, int number, rgba_t color )
{
	int		width, height;
	float		s1, t1, s2, t2;
	gl_texture_t	*glt;
	wrect_t		*rc;

	number &= 255;

	if( !con.curFont || !con.curFont->valid )
		return 0;

	if( y < -con.curFont->charHeight )
		return 0;

	rc = &con.curFont->fontRc[number];
	glt = R_GetTexture( con.curFont->hFontTexture );
	width = glt->srcWidth;
	height = glt->srcHeight;

	if( !width || !height )
		return con.curFont->charWidths[number];

	// don't apply color to fixed fonts it's already colored
	if( con.curFont->type != FONT_FIXED || glt->format == GL_LUMINANCE8_ALPHA8 )
		pglColor4ubv( color );
	else pglColor4ub( 255, 255, 255, color[3] );
	R_GetTextureParms( &width, &height, con.curFont->hFontTexture );

	// calc rectangle
	s1 = (float)rc->left / width;
	t1 = (float)rc->top / height;
	s2 = (float)rc->right / width;
	t2 = (float)rc->bottom / height;
	width = rc->right - rc->left;
	height = rc->bottom - rc->top;

	if( clgame.ds.adjust_size )
		Con_TextAdjustSize( &x, &y, &width, &height );
	R_DrawStretchPic( x, y, width, height, s1, t1, s2, t2, con.curFont->hFontTexture );		
	pglColor4ub( 255, 255, 255, 255 ); // don't forget reset color

	return con.curFont->charWidths[number];
}

/*
====================
Con_SetFont

choose font size
====================
*/
void Con_SetFont( int fontNum )
{
	fontNum = bound( 0, fontNum, 2 ); 
	con.curFont = &con.chars[fontNum];
}

/*
====================
Con_RestoreFont

restore auto-selected console font
(that based on screen resolution)
====================
*/
void Con_RestoreFont( void )
{
	con.curFont = con.lastUsedFont;
}

/*
====================
Con_DrawCharacter

client version of routine
====================
*/
int Con_DrawCharacter( int x, int y, int number, rgba_t color )
{
	GL_SetRenderMode( kRenderTransTexture );
	return Con_DrawGenericChar( x, y, number, color );
}

/*
====================
Con_DrawCharacterLen

returns character sizes in screen pixels
====================
*/
void Con_DrawCharacterLen( int number, int *width, int *height )
{
	if( width && con.curFont ) *width = con.curFont->charWidths[number];
	if( height && con.curFont ) *height = con.curFont->charHeight;
}

/*
====================
Con_DrawStringLen

compute string width and height in screen pixels
====================
*/
void Con_DrawStringLen( const char *pText, int *length, int *height )
{
	int	curLength = 0;

	if( !con.curFont ) return;

	if( height ) *height = con.curFont->charHeight;
	if( !length ) return;

	*length = 0;

	while( *pText )
	{
		byte	c = *pText;

		if( *pText == '\n' )
		{
			pText++;
			curLength = 0;
		}

		// skip color strings they are not drawing
		if( IsColorString( pText ))
		{
			pText += 2;
			continue;
		}

		curLength += con.curFont->charWidths[c];
		pText++;

		if( curLength > *length )
			*length = curLength;
	}
}

/*
==================
Con_DrawString

Draws a multi-colored string, optionally forcing
to a fixed color.
==================
*/
int Con_DrawGenericString( int x, int y, const char *string, rgba_t setColor, qboolean forceColor, int hideChar )
{
	rgba_t		color;
	int		drawLen = 0;
	int		numDraws = 0;
	const char	*s;

	if( !con.curFont ) return 0; // no font set

	// draw the colored text
	*(uint *)color = *(uint *)setColor;
	s = string;

	while( *s )
	{
		if( *s == '\n' )
		{
			s++;
			if( !*s ) break; // at end the string
			drawLen = 0; // begin new row
			y += con.curFont->charHeight;
		}

		if( IsColorString( s ))
		{
			if( !forceColor )
			{
				memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ));
				color[3] = setColor[3];
			}

			s += 2;
			numDraws++;
			continue;
		}

		// hide char for overstrike mode
		if( hideChar == numDraws )
			drawLen += con.curFont->charWidths[*s];
		else drawLen += Con_DrawCharacter( x + drawLen, y, *s, color );

		numDraws++;
		s++;
	}
          
	pglColor4ub( 255, 255, 255, 255 );
	return drawLen;
}

/*
====================
Con_DrawString

client version of routine
====================
*/
int Con_DrawString( int x, int y, const char *string, rgba_t setColor )
{
	return Con_DrawGenericString( x, y, string, setColor, false, -1 );
}

/*
================
Con_Init
================
*/
void Con_Init( void )
{
	int	i;

	if( host.type == HOST_DEDICATED )
		return; // dedicated server already have console

	// must be init before startup video subsystem
	scr_conspeed = Cvar_Get( "scr_conspeed", "600", FCVAR_ARCHIVE, "console moving speed" );
	con_notifytime = Cvar_Get( "con_notifytime", "3", FCVAR_ARCHIVE, "notify time to live" );
	con_fontsize = Cvar_Get( "con_fontsize", "1", FCVAR_ARCHIVE, "console font number (0, 1 or 2)" );

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

	for( i = 0; i < CON_HISTORY; i++ )
	{
		Con_ClearField( &con.historyLines[i] );
		con.historyLines[i].widthInChars = con.linewidth;
	}

	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f, "opens or closes the console" );
	Cmd_AddCommand( "con_color", Con_SetColor_f, "set a custom console color" );
	Cmd_AddCommand( "clear", Con_Clear_f, "clear console history" );
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
	static int	cr_pending = 0;
	static char	buf[MAX_PRINT_MSG];
	static qboolean	inupdate;
	static int	bufpos = 0;
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

	for( ; *txt; txt++ )
	{
		if( cr_pending )
		{
			Con_DeleteLastLine();
			cr_pending = 0;
		}

		c = *txt;

		switch( c )
		{
		case '\0':
			break;
		case '\r':
			Con_AddLine( buf, bufpos );
			cr_pending = 1;
			bufpos = 0;
			break;
		case '\n':
			Con_AddLine( buf, bufpos );
			bufpos = 0;
			break;
		default:
			buf[bufpos++] = c | mask;
			if(( bufpos >= sizeof( buf ) - 1 ) || bufpos >= ( con.linewidth - 1 ))
			{
				Con_AddLine( buf, bufpos );
				bufpos = 0;
			}
			break;
		}
	}

	if( cls.state != ca_disconnected && cls.state < ca_active && !cl.video_prepped && !cls.disable_screen )
	{
		if( !inupdate )
		{
			inupdate = true;
			SCR_UpdateScreen ();
			inupdate = false;
		}
	}
}

/*
=============
Con_Printf

=============
*/
void Con_Printf( char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( !host.allow_console )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	Sys_Print( buffer );
}

/*
=============
Con_DPrintf

=============
*/
void Con_DPrintf( char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( host_developer.value < DEV_NORMAL )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	if( buffer[0] == '0' && buffer[1] == '\n' && buffer[2] == '\0' )
		return; // hlrally spam

	Sys_Print( buffer );
}

/*
=============
Con_Reportf

=============
*/
void Con_Reportf( char *szFmt, ... )
{
	static char	buffer[MAX_PRINT_MSG];
	va_list		args;

	if( host_developer.value < DEV_EXTENDED )
		return;

	va_start( args, szFmt );
	Q_vsnprintf( buffer, sizeof( buffer ), szFmt, args );
	va_end( args );

	Sys_Print( buffer );
}

/*
================
Con_NPrint

Draw a single debug line with specified height
================
*/
void Con_NPrintf( int idx, char *fmt, ... )
{
	va_list	args;

	if( idx < 0 || idx >= MAX_DBG_NOTIFY )
		return;

	memset( con.notify[idx].szNotify, 0, MAX_STRING );

	va_start( args, fmt );
	Q_vsnprintf( con.notify[idx].szNotify, MAX_STRING, fmt, args );
	va_end( args );

	// reset values
	con.notify[idx].key_dest = key_game;
	con.notify[idx].expire = host.realtime + 4.0f;
	MakeRGBA( con.notify[idx].color, 255, 255, 255, 255 );
	con.draw_notify = true;
}

/*
================
Con_NXPrint

Draw a single debug line with specified height, color and time to live
================
*/
void Con_NXPrintf( con_nprint_t *info, char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	if( info->index < 0 || info->index >= MAX_DBG_NOTIFY )
		return;

	memset( con.notify[info->index].szNotify, 0, MAX_STRING );

	va_start( args, fmt );
	Q_vsnprintf( con.notify[info->index].szNotify, MAX_STRING, fmt, args );
	va_end( args );

	// setup values
	con.notify[info->index].key_dest = key_game;
	con.notify[info->index].expire = host.realtime + info->time_to_live;
	MakeRGBA( con.notify[info->index].color, (byte)(info->color[0] * 255), (byte)(info->color[1] * 255), (byte)(info->color[2] * 255), 255 );
	con.draw_notify = true;
}

/*
================
UI_NPrint

Draw a single debug line with specified height (menu version)
================
*/
void UI_NPrintf( int idx, char *fmt, ... )
{
	va_list	args;

	if( idx < 0 || idx >= MAX_DBG_NOTIFY )
		return;

	memset( con.notify[idx].szNotify, 0, MAX_STRING );

	va_start( args, fmt );
	Q_vsnprintf( con.notify[idx].szNotify, MAX_STRING, fmt, args );
	va_end( args );

	// reset values
	con.notify[idx].key_dest = key_menu;
	con.notify[idx].expire = host.realtime + 4.0f;
	MakeRGBA( con.notify[idx].color, 255, 255, 255, 255 );
	con.draw_notify = true;
}

/*
================
UI_NXPrint

Draw a single debug line with specified height, color and time to live (menu version)
================
*/
void UI_NXPrintf( con_nprint_t *info, char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	if( info->index < 0 || info->index >= MAX_DBG_NOTIFY )
		return;

	memset( con.notify[info->index].szNotify, 0, MAX_STRING );

	va_start( args, fmt );
	Q_vsnprintf( con.notify[info->index].szNotify, MAX_STRING, fmt, args );
	va_end( args );

	// setup values
	con.notify[info->index].key_dest = key_menu;
	con.notify[info->index].expire = host.realtime + info->time_to_live;
	MakeRGBA( con.notify[info->index].color, (byte)(info->color[0] * 255), (byte)(info->color[1] * 255), (byte)(info->color[2] * 255), 255 );
	con.draw_notify = true;
}

/*
=============================================================================

EDIT FIELDS

=============================================================================
*/
/*
===============
Con_AddCommandToList

===============
*/
static void Con_AddCommandToList( const char *s, const char *unused1, const char *unused2, void *unused3 )
{
	if( *s == '@' ) return; // never show system cvars or cmds
	if( con.matchCount >= CON_MAXCMDS ) return; // list is full

	if( Q_strnicmp( s, con.completionString, Q_strlen( con.completionString )))
		return; // no match

	con.cmds[con.matchCount++] = copystring( s );
}

/*
=================
Con_SortCmds
=================
*/
static int Con_SortCmds( const char **arg1, const char **arg2 )
{
	return Q_stricmp( *arg1, *arg2 );
}

/*
===============
Con_PrintCmdMatches
===============
*/
static void Con_PrintCmdMatches( const char *s, const char *unused1, const char *m, void *unused2 )
{
	if( !Q_strnicmp( s, con.shortestMatch, Q_strlen( con.shortestMatch )))
	{
		if( COM_CheckString( m )) Con_Printf( "    %s ^3\"%s\"\n", s, m );
		else Con_Printf( "    %s\n", s ); // variable or command without description
	}
}

/*
===============
Con_PrintCvarMatches
===============
*/
static void Con_PrintCvarMatches( const char *s, const char *value, const char *m, void *unused2 )
{
	if( !Q_strnicmp( s, con.shortestMatch, Q_strlen( con.shortestMatch )))
	{
		if( COM_CheckString( m )) Con_Printf( "    %s (%s)   ^3\"%s\"\n", s, value, m );
		else Con_Printf( "    %s  (%s)\n", s, value ); // variable or command without description
	}
}

/*
===============
Con_ConcatRemaining
===============
*/
static void Con_ConcatRemaining( const char *src, const char *start )
{
	char	*arg;
	int	i;

	arg = Q_strstr( src, start );

	if( !arg )
	{
		for( i = 1; i < Cmd_Argc(); i++ )
		{
			Q_strncat( con.completionField->buffer, " ", sizeof( con.completionField->buffer ));
			arg = Cmd_Argv( i );
			while( *arg )
			{
				if( *arg == ' ' )
				{
					Q_strncat( con.completionField->buffer, "\"", sizeof( con.completionField->buffer ));
					break;
				}
				arg++;
			}

			Q_strncat( con.completionField->buffer, Cmd_Argv( i ), sizeof( con.completionField->buffer ));
			if( *arg == ' ' ) Q_strncat( con.completionField->buffer, "\"", sizeof( con.completionField->buffer ));
		}
		return;
	}

	arg += Q_strlen( start );
	Q_strncat( con.completionField->buffer, arg, sizeof( con.completionField->buffer ));
}

/*
===============
Con_CompleteCommand

perform Tab expansion
===============
*/
void Con_CompleteCommand( field_t *field )
{
	field_t	temp;
	string	filename;
	qboolean	nextcmd;
	int	i;

	// setup the completion field
	con.completionField = field;

	// only look at the first token for completion purposes
	Cmd_TokenizeString( con.completionField->buffer );

	nextcmd = ( con.completionField->buffer[Q_strlen( con.completionField->buffer ) - 1] == ' ' ) ? true : false;

	con.completionString = Cmd_Argv( 0 );
	con.completionBuffer = Cmd_Argv( 1 );

	// skip backslash
	while( *con.completionString && ( *con.completionString == '\\' || *con.completionString == '/' ))
		con.completionString++;

	// skip backslash
	while( *con.completionBuffer && ( *con.completionBuffer == '\\' || *con.completionBuffer == '/' ))
		con.completionBuffer++;

	if( !Q_strlen( con.completionString ))
		return;

	// free the old autocomplete list
	for( i = 0; i < con.matchCount; i++ )
	{
		if( con.cmds[i] != NULL )
		{
			Mem_Free( con.cmds[i] );
			con.cmds[i] = NULL;
		}
	}

	con.matchCount = 0;
	con.shortestMatch[0] = 0;

	// find matching commands and variables
	Cmd_LookupCmds( NULL, NULL, Con_AddCommandToList );
	Cvar_LookupVars( 0, NULL, NULL, Con_AddCommandToList );

	if( !con.matchCount ) return; // no matches

	memcpy( &temp, con.completionField, sizeof( field_t ));

	// autocomplete second arg
	if(( Cmd_Argc() == 2 ) || (( Cmd_Argc() == 1 ) && nextcmd ))
	{
		if( !Q_strlen( con.completionBuffer ))
			return;

		if( Cmd_AutocompleteName( con.completionBuffer, filename, sizeof( filename )))
		{         
			Q_sprintf( con.completionField->buffer, "%s %s", Cmd_Argv( 0 ), filename ); 
			con.completionField->cursor = Q_strlen( con.completionField->buffer );
		}

		// don't adjusting cursor pos if we nothing found
		return;
	}  
	else if( Cmd_Argc() >= 3 )
	{
		// disable autocomplete for all next args
		return;
	}

	if( con.matchCount == 1 )
	{
		Q_sprintf( con.completionField->buffer, "\\%s", con.cmds[0] );
		if( Cmd_Argc() == 1 ) Q_strncat( con.completionField->buffer, " ", sizeof( con.completionField->buffer ));
		else Con_ConcatRemaining( temp.buffer, con.completionString );
		con.completionField->cursor = Q_strlen( con.completionField->buffer );
	}
	else
	{
		char	*first, *last;
		int	len = 0;

		qsort( con.cmds, con.matchCount, sizeof( char* ), Con_SortCmds );

		// find the number of matching characters between the first and
		// the last element in the list and copy it
		first = con.cmds[0];
		last = con.cmds[con.matchCount-1];

		while( *first && *last && Q_tolower( *first ) == Q_tolower( *last ))
		{
			first++;
			last++;

			con.shortestMatch[len] = con.cmds[0][len];
			len++;
		}
		con.shortestMatch[len] = 0;

		// multiple matches, complete to shortest
		Q_sprintf( con.completionField->buffer, "\\%s", con.shortestMatch );
		con.completionField->cursor = Q_strlen( con.completionField->buffer );
		Con_ConcatRemaining( temp.buffer, con.completionString );

		Con_Printf( "]%s\n", con.completionField->buffer );

		// run through again, printing matches
		Cmd_LookupCmds( NULL, NULL, Con_PrintCmdMatches );
		Cvar_LookupVars( 0, NULL, NULL, Con_PrintCvarMatches );
	}
}

/*
================
Field_Paste
================
*/
void Field_Paste( field_t *edit )
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
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
void Field_KeyDownEvent( field_t *edit, int key )
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

	if( key == K_BACKSPACE )
	{
		if( edit->cursor > 0 )
		{
			memmove( edit->buffer + edit->cursor - 1, edit->buffer + edit->cursor, len - edit->cursor + 1 );
			edit->cursor--;
			if( edit->scroll ) edit->scroll--;
		}
		return;
	}

	if( key == K_RIGHTARROW ) 
	{
		if( edit->cursor < len ) edit->cursor++;
		if( edit->cursor >= edit->scroll + edit->widthInChars && edit->cursor <= len )
			edit->scroll++;
		return;
	}

	if( key == K_LEFTARROW ) 
	{
		if( edit->cursor > 0 ) edit->cursor--;
		if( edit->cursor < edit->scroll ) edit->scroll--;
		return;
	}

	if( key == K_HOME || ( Q_tolower(key) == 'a' && Key_IsDown( K_CTRL )))
	{
		edit->cursor = 0;
		return;
	}

	if( key == K_END || ( Q_tolower(key) == 'e' && Key_IsDown( K_CTRL )))
	{
		edit->cursor = len;
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
void Field_CharEvent( field_t *edit, int ch )
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
		edit->cursor = 0;
		edit->scroll = 0;
		return;
	}

	if( ch == 'e' - 'a' + 1 )
	{
		// ctrl-e is end
		edit->cursor = len;
		edit->scroll = edit->cursor - edit->widthInChars;
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
void Field_DrawInputLine( int x, int y, field_t *edit )
{
	int	len, cursorChar;
	int	drawLen, hideChar = -1;
	int	prestep, curPos;
	char	str[MAX_SYSPATH];
	byte	*colorDefault;

	drawLen = edit->widthInChars;
	len = Q_strlen( edit->buffer ) + 1;
	colorDefault = g_color_table[ColorIndex( COLOR_DEFAULT )];

	// guarantee that cursor will be visible
	if( len <= drawLen )
	{
		prestep = 0;
	}
	else
	{
		if( edit->scroll + drawLen > len )
		{
			edit->scroll = len - drawLen;
			if( edit->scroll < 0 ) edit->scroll = 0;
		}

		prestep = edit->scroll;
	}

	if( prestep + drawLen > len )
		drawLen = len - prestep;

	// extract <drawLen> characters from the field at <prestep>
	drawLen = Q_min( drawLen, MAX_SYSPATH - 1 );

	memcpy( str, edit->buffer + prestep, drawLen );
	str[drawLen] = 0;

	// save char for overstrike
	cursorChar = str[edit->cursor - prestep];

	if( host.key_overstrike && cursorChar && !((int)( host.realtime * 4 ) & 1 ))
		hideChar = edit->cursor - prestep; // skip this char
	
	// draw it
	Con_DrawGenericString( x, y, str, colorDefault, false, hideChar );

	// draw the cursor
	if((int)( host.realtime * 4 ) & 1 ) return; // off blink

	// calc cursor position
	str[edit->cursor - prestep] = 0;
	Con_DrawStringLen( str, &curPos, NULL );

	if( host.key_overstrike && cursorChar )
	{
		// overstrike cursor
		pglEnable( GL_BLEND );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		Con_DrawGenericChar( x + curPos, y, cursorChar, colorDefault );
	}
	else Con_DrawCharacter( x + curPos, y, '_', colorDefault );
}

/*
=============================================================================

CONSOLE LINE EDITING

==============================================================================
*/
/*
====================
Key_Console

Handles history and console scrollback
====================
*/
void Key_Console( int key )
{
	// ctrl-L clears screen
	if( key == 'l' && Key_IsDown( K_CTRL ))
	{
		Cbuf_AddText( "clear\n" );
		return;
	}

	// enter finishes the line
	if( key == K_ENTER || key == K_KP_ENTER )
	{
		// if not in the game explicitly prepent a slash if needed
		if( cls.state != ca_active && con.input.buffer[0] != '\\' && con.input.buffer[0] != '/' )
		{
			char	temp[MAX_SYSPATH];

			Q_strncpy( temp, con.input.buffer, sizeof( temp ));
			Q_sprintf( con.input.buffer, "\\%s", temp );
			con.input.cursor++;
		}

		// backslash text are commands, else chat
		if( con.input.buffer[0] == '\\' || con.input.buffer[0] == '/' )
			Cbuf_AddText( con.input.buffer + 1 ); // skip backslash
		else Cbuf_AddText( con.input.buffer ); // valid command
		Cbuf_AddText( "\n" );

		// echo to console
		Con_Printf( ">%s\n", con.input.buffer );

		// copy line to history buffer
		con.historyLines[con.nextHistoryLine % CON_HISTORY] = con.input;
		con.nextHistoryLine++;
		con.historyLine = con.nextHistoryLine;

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
	if( key == K_TAB )
	{
		Con_CompleteCommand( &con.input );
		Con_Bottom();
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)
	if(( key == K_MWHEELUP && Key_IsDown( K_SHIFT )) || ( key == K_UPARROW ) || (( Q_tolower(key) == 'p' ) && Key_IsDown( K_CTRL )))
	{
		if( con.nextHistoryLine - con.historyLine < CON_HISTORY && con.historyLine > 0 )
			con.historyLine--;
		con.input = con.historyLines[con.historyLine % CON_HISTORY];
		return;
	}

	if(( key == K_MWHEELDOWN && Key_IsDown( K_SHIFT )) || ( key == K_DOWNARROW ) || (( Q_tolower(key) == 'n' ) && Key_IsDown( K_CTRL )))
	{
		if( con.historyLine == con.nextHistoryLine ) return;
		con.historyLine++;
		con.input = con.historyLines[con.historyLine % CON_HISTORY];
		return;
	}

	// console scrolling
	if( key == K_PGUP )
	{
		Con_PageUp( 1 );
		return;
	}

	if( key == K_PGDN )
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

	if( key == K_ESCAPE )
	{
		Key_SetKeyDest( key_game );
		Con_ClearField( &con.chat );
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER )
	{
		if( con.chat.buffer[0] && cls.state == ca_active )
		{
			Q_snprintf( buffer, sizeof( buffer ), "%s \"%s\"\n", con.chat_cmd, con.chat.buffer );
			Cbuf_AddText( buffer );
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
void Con_DrawInput( int lines )
{
	int	y;

	// don't draw anything (always draw if not active)
	if( cls.key_dest != key_console || !con.curFont )
		return;

	y = lines - ( con.curFont->charHeight * 2 );
	Con_DrawCharacter( 8, y, ']', g_color_table[7] );
	Field_DrawInputLine( 16, y, &con.input );
}

/*
================
Con_DrawDebugLines

Custom debug messages
================
*/
int Con_DrawDebugLines( void )
{
	int	i, count = 0;
	int	defaultX;
	int	y = 20;

	defaultX = glState.width / 4;
	
	for( i = 0; i < MAX_DBG_NOTIFY; i++ )
	{
		if( host.realtime < con.notify[i].expire && con.notify[i].key_dest == cls.key_dest )
		{
			int	x, len;
			int	fontTall;

			Con_DrawStringLen( con.notify[i].szNotify, &len, &fontTall );
			x = glState.width - Q_max( defaultX, len ) - 10;
			fontTall += 1;

			if( y + fontTall > glState.height - 20 )
				return count;

			count++;
			y = 20 + fontTall * i;
			Con_DrawString( x, y, con.notify[i].szNotify, con.notify[i].color );
		}
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

	if( scr_download->value != -1.0f )
	{
		Q_snprintf( dlstring, sizeof( dlstring ), "Downloading [%d remaining]: ^2%s^7 %5.1f%% time %.f secs",
		host.downloadcount, host.downloadfile, scr_download->value, Sys_DoubleTime() - timeStart ); 
		x = glState.width - 500;
		y = con.curFont->charHeight * 1.05f;
		Con_DrawString( x, y, dlstring, g_color_table[7] );
	}
	else
	{
		timeStart = Sys_DoubleTime();
	}

	if( !host_developer.value || Cvar_VariableInteger( "cl_background" ) || Cvar_VariableInteger( "sv_background" ))
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
void Con_DrawNotify( void )
{
	double	time = cl.time;
	int	i, x, y = 0;

	if( !con.curFont ) return;

	x = con.curFont->charWidths[' ']; // offset one space at left screen side

	if( host_developer.value && ( !Cvar_VariableInteger( "cl_background" ) && !Cvar_VariableInteger( "sv_background" )))
	{
		for( i = CON_LINES_COUNT - con.num_times; i < CON_LINES_COUNT; i++ )
		{
			con_lineinfo_t	*l = &CON_LINES( i );

			if( l->addtime < ( time - con_notifytime->value ))
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

	pglColor4ub( 255, 255, 255, 255 );
}

/*
================
Con_DrawConsoleLine

Draws a line of the console; returns its height in lines.
If alpha is 0, the line is not drawn, but still wrapped and its height
returned.
================
*/
int Con_DrawConsoleLine( int y, int lineno )
{
	con_lineinfo_t	*li = &CON_LINES( lineno );

	if( *li->start == '\1' )
		return 0;	// this string will be shown only at notify

	if( y >= con.curFont->charHeight )
		Con_DrawGenericString( con.curFont->charWidths[' '], y, li->start, g_color_table[7], false, -1 );

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
void Con_DrawSolidConsole( int lines )
{
	int	i, x, y;
	float	fraction;
	int	start;

	if( lines <= 0 ) return;

	// draw the background
	GL_SetRenderMode( kRenderNormal );
	pglColor4ub( 255, 255, 255, 255 ); // to prevent grab color from screenfade
	R_DrawStretchPic( 0, lines - glState.height, glState.width, glState.height, 0, 0, 1, 1, con.background );

	if( !con.curFont || !host.allow_console )
		return; // nothing to draw

	if( host.allow_console )
	{
		// draw current version
		int	stringLen, width = 0, charH;
		string	curbuild;
		byte	color[4];

		memcpy( color, g_color_table[7], sizeof( color ));

		Q_snprintf( curbuild, MAX_STRING, "Xash3D %i/%s (hw build %i)", PROTOCOL_VERSION, XASH_VERSION, Q_buildnum( ));
		Con_DrawStringLen( curbuild, &stringLen, &charH );
		start = glState.width - stringLen;
		stringLen = Con_StringLength( curbuild );

		fraction = lines / (float)glState.height;
		color[3] = Q_min( fraction * 2.0f, 1.0f ) * 255; // fadeout version number

		for( i = 0; i < stringLen; i++ )
			width += Con_DrawCharacter( start + width, 0, curbuild[i], color );
	}

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
				Con_DrawCharacter(( x + 1 ) * start, y, '^', g_color_table[1] );
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
	SCR_DrawFPS( max( y, 4 )); // to avoid to hide fps counter

	pglColor4ub( 255, 255, 255, 255 );
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
		if( !cl_allow_levelshots->value )
		{
			if(( Cvar_VariableInteger( "cl_background" ) || Cvar_VariableInteger( "sv_background" )) && cls.key_dest != key_console )
				con.vislines = con.showlines = 0;
			else con.vislines = con.showlines = glState.height;
		}
		else
		{
			con.showlines = 0;

			if( host_developer.value >= DEV_EXTENDED )
				Con_DrawNotify(); // draw notify lines
		}
	}

	// if disconnected, render console full screen
	switch( cls.state )
	{
	case ca_disconnected:
		if( cls.key_dest != key_menu )
		{
			Con_DrawSolidConsole( glState.height );
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
				Con_DrawSolidConsole( glState.height );
		}
		else
		{
			if( con.vislines )
				Con_DrawSolidConsole( con.vislines );
			else if( cls.state == ca_active && ( cls.key_dest == key_game || cls.key_dest == key_message ))
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
	int	i, stringLen, width = 0, charH;
	int	start, height = glState.height;
	qboolean	draw_version = false;
	string	curbuild;

	switch( cls.scrshot_action )
	{
	case scrshot_normal:
	case scrshot_snapshot:
		draw_version = true;
		break;
	}

	if( !host.force_draw_version )
	{
		if(( cls.key_dest != key_menu && !draw_version ) || CL_IsDevOverviewMode() == 2 || net_graph->value )
			return;
	}

	if( host.force_draw_version || draw_version )
		Q_snprintf( curbuild, MAX_STRING, "Xash3D v%i/%s (build %i)", PROTOCOL_VERSION, XASH_VERSION, Q_buildnum( ));
	else Q_snprintf( curbuild, MAX_STRING, "v%i/%s (build %i)", PROTOCOL_VERSION, XASH_VERSION, Q_buildnum( )); 
	Con_DrawStringLen( curbuild, &stringLen, &charH );
	start = glState.width - stringLen * 1.05f;
	stringLen = Con_StringLength( curbuild );
	height -= charH * 1.05f;

	for( i = 0; i < stringLen; i++ )
		width += Con_DrawCharacter( start + width, height, curbuild[i], color );
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

	// decide on the destination height of the console
	if( host.allow_console && cls.key_dest == key_console )
	{
		if( cls.state < ca_active || cl.first_frame )
			con.showlines = glState.height;	// full screen
		else con.showlines = (glState.height >> 1);	// half screen	
	}
	else con.showlines = 0; // none visible

	lines_per_frame = fabs( scr_conspeed->value ) * host.realframetime;

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

/*
=========
Con_VidInit

INTERNAL RESOURCE
=========
*/
void Con_VidInit( void )
{
	Con_CheckResize();

	Con_LoadConchars();

	// loading console image
	if( host.allow_console )
	{
		// trying to load truecolor image first
		if( FS_FileExists( "gfx/shell/conback.bmp", false ) || FS_FileExists( "gfx/shell/conback.tga", false ))
			con.background = GL_LoadTexture( "gfx/shell/conback", NULL, 0, TF_IMAGE );

		if( !con.background )
		{
			if( FS_FileExists( "cached/conback640", false ))
				con.background = GL_LoadTexture( "cached/conback640", NULL, 0, TF_IMAGE );
			else if( FS_FileExists( "cached/conback", false ))
				con.background = GL_LoadTexture( "cached/conback", NULL, 0, TF_IMAGE );
		}
	}
	else
	{
		// trying to load truecolor image first
		if( FS_FileExists( "gfx/shell/loading.bmp", false ) || FS_FileExists( "gfx/shell/loading.tga", false ))
			con.background = GL_LoadTexture( "gfx/shell/loading", NULL, 0, TF_IMAGE );

		if( !con.background )
		{
			if( FS_FileExists( "cached/loading640", false ))
				con.background = GL_LoadTexture( "cached/loading640", NULL, 0, TF_IMAGE );
			else if( FS_FileExists( "cached/loading", false ))
				con.background = GL_LoadTexture( "cached/loading", NULL, 0, TF_IMAGE );
		}
	}


	if( !con.background ) // last chance - quake conback image
	{
		qboolean		draw_to_console = false;
		int		length = 0;
		gl_texture_t	*chars;

		// NOTE: only these games want to draw build number into console background
		if( !Q_stricmp( FS_Gamedir(), "id1" ))
			draw_to_console = true;

		if( !Q_stricmp( FS_Gamedir(), "hipnotic" ))
			draw_to_console = true;

		if( !Q_stricmp( FS_Gamedir(), "rogue" ))
			draw_to_console = true;

		if( draw_to_console && con.curFont && ( chars = R_GetTexture( con.curFont->hFontTexture )) != NULL && chars->original )
		{
			lmp_t	*cb = (lmp_t *)FS_LoadFile( "gfx/conback.lmp", &length, false );
			char	ver[64];
			byte	*dest;
			int	x, y;

			if( cb && cb->width == 320 && cb->height == 200 )
			{
				Q_snprintf( ver, 64, "%i", Q_buildnum( )); // can store only buildnum
				dest = (byte *)(cb + 1) + 320 * 186 + 320 - 11 - 8 * Q_strlen( ver );
				y = Q_strlen( ver );
				for( x = 0; x < y; x++ )
					Con_DrawCharToConback( ver[x], chars->original->buffer, dest + (x << 3));
				con.background = GL_LoadTexture( "#gfx/conback.lmp", (byte *)cb, length, TF_IMAGE );
			}
			if( cb ) Mem_Free( cb );
		}

		if( !con.background ) // trying the load unmodified conback
			con.background = GL_LoadTexture( "gfx/conback.lmp", NULL, 0, TF_IMAGE );
	}

	// missed console image will be replaced as gray background like X-Ray or Crysis
	if( con.background == tr.defaultTexture || con.background == 0 )
		con.background = tr.grayTexture;
}

/*
=========
Con_InvalidateFonts

=========
*/
void Con_InvalidateFonts( void )
{
	memset( con.chars, 0, sizeof( con.chars ));
	con.curFont = con.lastUsedFont = NULL;
}

/*
=========
Cmd_AutoComplete

NOTE: input string must be equal or longer than MAX_STRING
=========
*/
void Cmd_AutoComplete( char *complete_string )
{
	field_t	input;

	if( !complete_string || !*complete_string )
		return;

	// setup input
	Q_strncpy( input.buffer, complete_string, sizeof( input.buffer ));
	input.cursor = input.scroll = 0;

	Con_CompleteCommand( &input );

	// setup output
	if( input.buffer[0] == '\\' || input.buffer[0] == '/' )
		Q_strncpy( complete_string, input.buffer + 1, sizeof( input.buffer ));
	else Q_strncpy( complete_string, input.buffer, sizeof( input.buffer ));
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
void Con_DefaultColor( int r, int g, int b )
{
	r = bound( 0, r, 255 );
	g = bound( 0, g, 255 );
	b = bound( 0, b, 255 );
	MakeRGBA( g_color_table[7], r, g, b, 255 );
}