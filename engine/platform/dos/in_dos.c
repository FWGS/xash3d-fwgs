/*
in_dos.c - DOS input
Copyright (C) 2020 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform/platform.h"
#include "input.h"
#include <dos.h>

static struct key_s
{
byte buf[256];
byte buf_head;
qboolean shift;
qboolean chars;
} keystate;

//=============================================================================

byte        scantokey[128] =
					{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*',
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,0  ,    0  , K_HOME,
	K_UPARROW,K_PGUP,'-',K_LEFTARROW,'5',K_RIGHTARROW,'+',K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11,
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7
					};


// will be implemented later
void Platform_RunEvents( void )
{
	int i;

	for( i = 0; i < keystate.buf_head;i++ )
	{
		int k = keystate.buf[i];
		int key = scantokey[k&0x7f];
		int value = !(k & 0x80);

		Key_Event( key , value );

		if( keystate.chars && value )
		{
			if( key >= 32 && key < 127 )
			{
				if( keystate.shift )
				{
					key = Key_ToUpper( key );
				}
				CL_CharEvent( key );
			}
		}
		if( key == K_SHIFT )
			keystate.shift = value;
	}
	keystate.buf_head = 0;

}

void Platform_EnableTextInput( qboolean enable )
{
	keystate.chars = enable;
}

void Platform_MouseMove( float *x, float *y )
{
	static int lx,ly;
	union REGS regs;

	regs.w.ax = regs.w.bx = regs.w.cx = regs.w.dx = 0;

	regs.w.ax = 11;
	int386(0x33,&regs,&regs);
	*x = (short)regs.w.cx, *y = (short)regs.w.dx;
	regs.w.ax = regs.w.bx = regs.w.cx = regs.w.dx = 0;
	int386(0x33,&regs,&regs);// reset
}

void __interrupt __far keyhandler( void )
{
	keystate.buf[keystate.buf_head++] = inp(0x60);
	outp(0x20, 0x20);
}
