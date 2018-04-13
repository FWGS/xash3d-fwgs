/*
vgui_api.h - vgui_support library interface
Copyright (C) 2015 Mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef VGUI_API_H
#define VGUI_API_H

#include "xash3d_types.h"

// VGUI generic vertex

typedef struct
{
	vec2_t	point;
	vec2_t	coord;
} vpoint_t;

// C-Style VGUI enums

enum VGUI_MouseCode
{
	MOUSE_LEFT=0,
	MOUSE_RIGHT,
	MOUSE_MIDDLE,
	MOUSE_LAST
};

enum VGUI_KeyCode
{
	KEY_0=0,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_A,
	KEY_B,
	KEY_C,
	KEY_D,
	KEY_E,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_I,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_M,
	KEY_N,
	KEY_O,
	KEY_P,
	KEY_Q,
	KEY_R,
	KEY_S,
	KEY_T,
	KEY_U,
	KEY_V,
	KEY_W,
	KEY_X,
	KEY_Y,
	KEY_Z,
	KEY_PAD_0,
	KEY_PAD_1,
	KEY_PAD_2,
	KEY_PAD_3,
	KEY_PAD_4,
	KEY_PAD_5,
	KEY_PAD_6,
	KEY_PAD_7,
	KEY_PAD_8,
	KEY_PAD_9,
	KEY_PAD_DIVIDE,
	KEY_PAD_MULTIPLY,
	KEY_PAD_MINUS,
	KEY_PAD_PLUS,
	KEY_PAD_ENTER,
	KEY_PAD_DECIMAL,
	KEY_LBRACKET,
	KEY_RBRACKET,
	KEY_SEMICOLON,
	KEY_APOSTROPHE,
	KEY_BACKQUOTE,
	KEY_COMMA,
	KEY_PERIOD,
	KEY_SLASH,
	KEY_BACKSLASH,
	KEY_MINUS,
	KEY_EQUAL,
	KEY_ENTER,
	KEY_SPACE,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_CAPSLOCK,
	KEY_NUMLOCK,
	KEY_ESCAPE,
	KEY_SCROLLLOCK,
	KEY_INSERT,
	KEY_DELETE,
	KEY_HOME,
	KEY_END,
	KEY_PAGEUP,
	KEY_PAGEDOWN,
	KEY_BREAK,
	KEY_LSHIFT,
	KEY_RSHIFT,
	KEY_LALT,
	KEY_RALT,
	KEY_LCONTROL,
	KEY_RCONTROL,
	KEY_LWIN,
	KEY_RWIN,
	KEY_APP,
	KEY_UP,
	KEY_LEFT,
	KEY_DOWN,
	KEY_RIGHT,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,
	KEY_F7,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_F11,
	KEY_F12,
	KEY_LAST
};

enum VGUI_KeyAction
{
	KA_TYPED=0,
	KA_PRESSED,
	KA_RELEASED
};
enum VGUI_MouseAction
{
	MA_PRESSED=0,
	MA_RELEASED,
	MA_DOUBLE,
	MA_WHEEL
};

enum VGUI_DefaultCursor
{
	dc_user,
	dc_none,
	dc_arrow,
	dc_ibeam,
	dc_hourglass,
	dc_crosshair,
	dc_up,
	dc_sizenwse,
	dc_sizenesw,
	dc_sizewe,
	dc_sizens,
	dc_sizeall,
	dc_no,
	dc_hand,
	dc_last
};






typedef struct  vguiapi_s
{
	qboolean initialized;
	void	(*DrawInit)( void );
	void	(*DrawShutdown)( void );
	void	(*SetupDrawingText)( int *pColor );
	void	(*SetupDrawingRect)( int *pColor );
	void	(*SetupDrawingImage)( int *pColor );
	void	(*BindTexture)( int id );
	void	(*EnableTexture)( qboolean enable );
	void	(*CreateTexture)( int id, int width, int height );
	void	(*UploadTexture)( int id, const char *buffer, int width, int height );
	void	(*UploadTextureBlock)( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight );
	void	(*DrawQuad)( const vpoint_t *ul, const vpoint_t *lr );
	void	(*GetTextureSizes)( int *width, int *height );
	int		(*GenerateTexture)( void );
	void	*(*EngineMalloc)( size_t size );
	void	(*CursorSelect)( enum VGUI_DefaultCursor cursor );
	byte		(*GetColor)( int i, int j );
	qboolean	(*IsInGame)( void );
	void	(*SetVisible)( qboolean state );
	void	(*GetCursorPos)( int *x, int *y );
	int		(*ProcessUtfChar)( int ch );
	void	(*Startup)( int width, int height );
	void	(*Shutdown)( void );
	void	*(*GetPanel)( void );
	void	(*Paint)( void );
	void	(*Mouse)(enum VGUI_MouseAction action, int code );
	void	(*Key)(enum VGUI_KeyAction action,enum VGUI_KeyCode code );
	void	(*MouseMove)( int x, int y );
} vguiapi_t;
#endif // VGUI_API_H
