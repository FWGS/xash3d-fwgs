/*
in_osk.c - on-screen keyboard input
Copyright (C) 2016-2026 Alibek Omarov

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
#include "input.h"
#include "client.h"

CVAR_DEFINE_AUTO( osk_enable, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable built-in on-screen keyboard" );

/* On-screen keyboard:
 *
 * 4 lines with 13 buttons each
 * Left trigger == backspace
 * Right trigger == space
 * Any button press is button press on keyboard
 *
 * Our layout:
 *  0  1  2  3  4  5  6  7  8  9  10 11 12
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |` |1 |2 |3 |4 |5 |6 |7 |8 |9 |0 |- |= | 0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |q |w |e |r |t |y |u |i |o |p |[ |] |\ | 1
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |CL|a |s |d |f |g |h |j |k |l |; |' |BS| 2
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |SH|z |x |c |v |b |n |m |, |. |/ |SP|EN| 3
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+
 */

#define MAX_OSK_ROWS  13
#define MAX_OSK_LINES 4

enum
{
	OSK_DEFAULT = 0,
	OSK_UPPER, // on caps, shift
	/*
	OSK_RUSSIAN,
	OSK_RUSSIAN_UPPER,
	*/
	OSK_LAST
};

enum
{
	OSK_TAB = 16,
	OSK_SHIFT,
	OSK_BACKSPACE,
	OSK_ENTER,
	OSK_SPECKEY_LAST
};
static const char *osk_keylayout[][4] =
{
	{
		"`1234567890-=",             // 13
		"qwertyuiop[]\\",            // 13
		"\x10" "asdfghjkl;'" "\x12", // 11 + caps on a left, enter on a right
		"\x11" "zxcvbnm,./ " "\x13"  // 10 + esc on left + shift on a left/right
	},
	{
		"~!@#$%^&*()_+",
		"QWERTYUIOP{}|",
		"\x10" "ASDFGHJKL:\"" "\x12",
		"\x11" "ZXCVBNM<>? "  "\x13"
	}
};

static struct osk_s
{
	qboolean enable;
	int      curlayout;
	qboolean shift;
	qboolean sending;
	struct
	{
		signed char x;
		signed char y;
		char val;
	} curbutton;
} osk;

qboolean OSK_KeyEvent( int key, int down )
{
	if( !osk.enable || !osk_enable.value )
		return false;

	if( osk.sending )
	{
		osk.sending = false;
		return false;
	}

	if( osk.curbutton.val == 0 )
	{
		if( key == K_ENTER || key == K_A_BUTTON )
		{
			osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
			return true;
		}
		return false;
	}


	switch( key )
	{
	case K_A_BUTTON:
	case K_ENTER:
		switch( osk.curbutton.val )
		{
		case OSK_ENTER:
			osk.sending = true;
			Key_Event( K_ENTER, down );
			// osk_enable = false; // TODO: handle multiline
			break;
		case OSK_SHIFT:
			if( !down )
				break;

			if( osk.curlayout & 1 )
				osk.curlayout--;
			else
				osk.curlayout++;

			osk.shift = true;
			osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
			break;
		case OSK_BACKSPACE:
			Key_Event( K_BACKSPACE, down );
			break;
		case OSK_TAB:
			Key_Event( K_TAB, down );
			break;
		default:
		{
			int ch;

			if( !down )
			{
				if( osk.shift && osk.curlayout & 1 )
					osk.curlayout--;

				osk.shift = false;
				osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
				break;
			}

			ch = (byte)osk.curbutton.val;

			// do not pass UTF-8 sequence into the engine, convert it here
			if( !cls.accept_utf8 )
				ch = Con_UtfProcessCharForce( ch );

			if( !ch )
				break;

			CL_CharEvent( ch );
			break;
		}
		}
		break;
	case K_UPARROW:
		if( down && --osk.curbutton.y < 0 )
		{
			osk.curbutton.y = MAX_OSK_LINES - 1;
			osk.curbutton.val = 0;
			return true;
		}
		break;
	case K_DOWNARROW:
		if( down && ++osk.curbutton.y >= MAX_OSK_LINES )
		{
			osk.curbutton.y = 0;
			osk.curbutton.val = 0;
			return true;
		}
		break;
	case K_LEFTARROW:
		if( down && --osk.curbutton.x < 0 )
			osk.curbutton.x = MAX_OSK_ROWS - 1;
		break;
	case K_RIGHTARROW:
		if( down && ++osk.curbutton.x >= MAX_OSK_ROWS )
			osk.curbutton.x = 0;
		break;
	default:
		return false;
	}

	osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
	return true;

}

/*
=============
OSK_EnableTextInput

Enables built-in IME
=============
*/
void OSK_EnableTextInput( qboolean enable, qboolean force )
{
	qboolean old = osk.enable;

	osk.enable = enable;

	if( osk.enable && ( !old || force ))
	{
		osk.curlayout = 0;
		osk.curbutton.val = osk_keylayout[osk.curlayout][osk.curbutton.y][osk.curbutton.x];
	}
}

#define X_START 0.1347475f
#define Y_START 0.567f
#define X_STEP  0.05625
#define Y_STEP  0.0825

/*
============
OSK_DrawSymbolButton

Draw button with symbol on it
============
*/
static void OSK_DrawSymbolButton( int symb, float x, float y, float width, float height )
{
	cl_font_t *font = Con_GetCurFont();
	byte      color[] = { 255, 255, 255, 255 };
	int x1 = x * refState.width,
	    y1 = y * refState.height,
	    w = width * refState.width,
	    h = height * refState.height;

	if( symb == osk.curbutton.val )
		ref.dllFuncs.FillRGBA( kRenderTransTexture, x1, y1, w, h, 255, 160, 0, 100 );

	if( !symb || symb == ' ' || ( symb >= OSK_TAB && symb < OSK_SPECKEY_LAST ))
		return;

	CL_DrawCharacter(
		x1 + width * 0.4 * refState.width,
		y1 + height * 0.4 * refState.height,
		symb, color, font, 0 );
}

/*
=============
OSK_DrawSpecialButton

Draw special button, like shift, enter or esc
=============
*/
static void OSK_DrawSpecialButton( const char *name, float x, float y, float width, float height )
{
	byte color[] = { 0, 255, 0, 255 };

	Con_DrawString(
		x * refState.width + width * 0.4 * refState.width,
		y * refState.height + height * 0.4 * refState.height,
		name,
		color );
}

/*
=============
OSK_Draw

Draw on screen keyboard, if enabled
=============
*/
void OSK_Draw( void )
{
	const char **curlayout = osk_keylayout[osk.curlayout]; // shortcut :)
	float      x, y;
	int i, j;

	if( !osk.enable || !osk_enable.value || !osk.curbutton.val )
		return;

	// draw keyboard
	ref.dllFuncs.FillRGBA( kRenderTransTexture, X_START * refState.width, Y_START * refState.height,
			       X_STEP * MAX_OSK_ROWS * refState.width,
			       Y_STEP * MAX_OSK_LINES * refState.height, 100, 100, 100, 100 );

	OSK_DrawSpecialButton( "-]", X_START, Y_START + Y_STEP * 2, X_STEP, Y_STEP );
	OSK_DrawSpecialButton( "<-", X_START + X_STEP * 12, Y_START + Y_STEP * 2, X_STEP, Y_STEP );

	OSK_DrawSpecialButton( "sh", X_START, Y_START + Y_STEP * 3, X_STEP, Y_STEP );
	OSK_DrawSpecialButton( "en", X_START + X_STEP * 12, Y_START + Y_STEP * 3, X_STEP, Y_STEP );

	for( y = Y_START, j = 0; j < MAX_OSK_LINES; j++, y += Y_STEP )
		for( x = X_START, i = 0; i < MAX_OSK_ROWS; i++, x += X_STEP )
			OSK_DrawSymbolButton( curlayout[j][i], x, y, X_STEP, Y_STEP );
}

void OSK_Init( void )
{
	Cvar_RegisterVariable( &osk_enable );
}
