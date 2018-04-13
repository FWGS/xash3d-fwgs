/*
vgui_input.cpp - handle kb & mouse
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#define OEMRESOURCE		// for OCR_* cursor junk

#include "common.h"
#include "client.h"
#include "vgui_draw.h"
#include "vgui_main.h"
#include "input.h"

static KeyCode s_pVirtualKeyTrans[256];
static HICON s_pDefaultCursor[20];
static HICON s_hCurrentCursor = NULL;

void VGUI_InitCursors( void )
{
	// load up all default cursors
	s_pDefaultCursor[Cursor::dc_none]     = NULL;
	s_pDefaultCursor[Cursor::dc_arrow]    = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_NORMAL );
	s_pDefaultCursor[Cursor::dc_ibeam]    = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_IBEAM );
	s_pDefaultCursor[Cursor::dc_hourglass]= (HICON)LoadCursor( NULL, (LPCTSTR)OCR_WAIT );
	s_pDefaultCursor[Cursor::dc_crosshair]= (HICON)LoadCursor( NULL, (LPCTSTR)OCR_CROSS );
	s_pDefaultCursor[Cursor::dc_up]       = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_UP );
	s_pDefaultCursor[Cursor::dc_sizenwse] = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_SIZENWSE );
	s_pDefaultCursor[Cursor::dc_sizenesw] = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_SIZENESW );
	s_pDefaultCursor[Cursor::dc_sizewe]   = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_SIZEWE );
	s_pDefaultCursor[Cursor::dc_sizens]   = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_SIZENS );
	s_pDefaultCursor[Cursor::dc_sizeall]  = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_SIZEALL );
	s_pDefaultCursor[Cursor::dc_no]       = (HICON)LoadCursor( NULL, (LPCTSTR)OCR_NO );
	s_pDefaultCursor[Cursor::dc_hand]     = (HICON)LoadCursor( NULL, (LPCTSTR)32649 );

	s_hCurrentCursor = s_pDefaultCursor[Cursor::dc_arrow];
	host.mouse_visible = true;
}

void VGUI_CursorSelect( Cursor *cursor )
{
	Assert( cursor != NULL );

	host.mouse_visible = true;

	switch( cursor->getDefaultCursor( ))
	{
	case Cursor::dc_user:
	case Cursor::dc_none:
		host.mouse_visible = false;
		break;
	case Cursor::dc_arrow:
	case Cursor::dc_ibeam:
	case Cursor::dc_hourglass:
	case Cursor::dc_crosshair:
	case Cursor::dc_up:
	case Cursor::dc_sizenwse:
	case Cursor::dc_sizenesw:
	case Cursor::dc_sizewe:
	case Cursor::dc_sizens:
	case Cursor::dc_sizeall:
	case Cursor::dc_no:
	case Cursor::dc_hand:
		s_hCurrentCursor = s_pDefaultCursor[cursor->getDefaultCursor()];
		break;
	default:
		host.mouse_visible = false;
		Assert( 0 );
		break;
	}

	VGUI_ActivateCurrentCursor();
}

void VGUI_ActivateCurrentCursor( void )
{
	if( cls.key_dest != key_game || cl.paused )
		return;

	if( host.mouse_visible )
	{
		while( ShowCursor( true ) < 0 );
		SetCursor( s_hCurrentCursor );
	}
	else
	{
		while( ShowCursor( false ) >= 0 );
		SetCursor( NULL );
	}
}

void VGUI_InitKeyTranslationTable( void )
{
	static bool bInitted = false;

	if( bInitted ) return;
	bInitted = true;

	// set virtual key translation table
	memset( s_pVirtualKeyTrans, -1, sizeof( s_pVirtualKeyTrans ));	

	s_pVirtualKeyTrans['0'] = KEY_0;
	s_pVirtualKeyTrans['1'] = KEY_1;
	s_pVirtualKeyTrans['2'] = KEY_2;
	s_pVirtualKeyTrans['3'] = KEY_3;
	s_pVirtualKeyTrans['4'] = KEY_4;
	s_pVirtualKeyTrans['5'] = KEY_5;
	s_pVirtualKeyTrans['6'] = KEY_6;
	s_pVirtualKeyTrans['7'] = KEY_7;
	s_pVirtualKeyTrans['8'] = KEY_8;
	s_pVirtualKeyTrans['9'] = KEY_9;
	s_pVirtualKeyTrans['A'] = s_pVirtualKeyTrans['a'] = KEY_A;
	s_pVirtualKeyTrans['B'] = s_pVirtualKeyTrans['b'] = KEY_B;
	s_pVirtualKeyTrans['C'] = s_pVirtualKeyTrans['c'] = KEY_C;
	s_pVirtualKeyTrans['D'] = s_pVirtualKeyTrans['d'] = KEY_D;
	s_pVirtualKeyTrans['E'] = s_pVirtualKeyTrans['e'] = KEY_E;
	s_pVirtualKeyTrans['F'] = s_pVirtualKeyTrans['f'] = KEY_F;
	s_pVirtualKeyTrans['G'] = s_pVirtualKeyTrans['g'] = KEY_G;
	s_pVirtualKeyTrans['H'] = s_pVirtualKeyTrans['h'] = KEY_H;
	s_pVirtualKeyTrans['I'] = s_pVirtualKeyTrans['i'] = KEY_I;
	s_pVirtualKeyTrans['J'] = s_pVirtualKeyTrans['j'] = KEY_J;
	s_pVirtualKeyTrans['K'] = s_pVirtualKeyTrans['k'] = KEY_K;
	s_pVirtualKeyTrans['L'] = s_pVirtualKeyTrans['l'] = KEY_L;
	s_pVirtualKeyTrans['M'] = s_pVirtualKeyTrans['m'] = KEY_M;
	s_pVirtualKeyTrans['N'] = s_pVirtualKeyTrans['n'] = KEY_N;
	s_pVirtualKeyTrans['O'] = s_pVirtualKeyTrans['o'] = KEY_O;
	s_pVirtualKeyTrans['P'] = s_pVirtualKeyTrans['p'] = KEY_P;
	s_pVirtualKeyTrans['Q'] = s_pVirtualKeyTrans['q'] = KEY_Q;
	s_pVirtualKeyTrans['R'] = s_pVirtualKeyTrans['r'] = KEY_R;
	s_pVirtualKeyTrans['S'] = s_pVirtualKeyTrans['s'] = KEY_S;
	s_pVirtualKeyTrans['T'] = s_pVirtualKeyTrans['t'] = KEY_T;
	s_pVirtualKeyTrans['U'] = s_pVirtualKeyTrans['u'] = KEY_U;
	s_pVirtualKeyTrans['V'] = s_pVirtualKeyTrans['v'] = KEY_V;
	s_pVirtualKeyTrans['W'] = s_pVirtualKeyTrans['w'] = KEY_W;
	s_pVirtualKeyTrans['X'] = s_pVirtualKeyTrans['x'] = KEY_X;
	s_pVirtualKeyTrans['Y'] = s_pVirtualKeyTrans['y'] = KEY_Y;
	s_pVirtualKeyTrans['Z'] = s_pVirtualKeyTrans['z'] = KEY_Z;
	s_pVirtualKeyTrans[VK_NUMPAD0] = KEY_PAD_0;
	s_pVirtualKeyTrans[VK_NUMPAD1] = KEY_PAD_1;
	s_pVirtualKeyTrans[VK_NUMPAD2] = KEY_PAD_2;
	s_pVirtualKeyTrans[VK_NUMPAD3] = KEY_PAD_3;
	s_pVirtualKeyTrans[VK_NUMPAD4] = KEY_PAD_4;
	s_pVirtualKeyTrans[VK_NUMPAD5] = KEY_PAD_5;
	s_pVirtualKeyTrans[VK_NUMPAD6] = KEY_PAD_6;
	s_pVirtualKeyTrans[VK_NUMPAD7] = KEY_PAD_7;
	s_pVirtualKeyTrans[VK_NUMPAD8] = KEY_PAD_8;
	s_pVirtualKeyTrans[VK_NUMPAD9] = KEY_PAD_9;
	s_pVirtualKeyTrans[VK_DIVIDE]	= KEY_PAD_DIVIDE;
	s_pVirtualKeyTrans[VK_MULTIPLY] = KEY_PAD_MULTIPLY;
	s_pVirtualKeyTrans[VK_SUBTRACT] = KEY_PAD_MINUS;
	s_pVirtualKeyTrans[VK_ADD] = KEY_PAD_PLUS;
	s_pVirtualKeyTrans[VK_RETURN]	= KEY_PAD_ENTER;
	s_pVirtualKeyTrans[VK_DECIMAL] = KEY_PAD_DECIMAL;
	s_pVirtualKeyTrans[0xdb] = KEY_LBRACKET;
	s_pVirtualKeyTrans[0xdd] = KEY_RBRACKET;
	s_pVirtualKeyTrans[0xba] = KEY_SEMICOLON;
	s_pVirtualKeyTrans[0xde] = KEY_APOSTROPHE;
	s_pVirtualKeyTrans[0xc0] = KEY_BACKQUOTE;
	s_pVirtualKeyTrans[0xbc] = KEY_COMMA;
	s_pVirtualKeyTrans[0xbe] = KEY_PERIOD;
	s_pVirtualKeyTrans[0xbf] = KEY_SLASH;
	s_pVirtualKeyTrans[0xdc] = KEY_BACKSLASH;
	s_pVirtualKeyTrans[0xbd] = KEY_MINUS;
	s_pVirtualKeyTrans[0xbb] = KEY_EQUAL;
	s_pVirtualKeyTrans[VK_RETURN]	= KEY_ENTER;
	s_pVirtualKeyTrans[VK_SPACE] = KEY_SPACE;
	s_pVirtualKeyTrans[VK_BACK] = KEY_BACKSPACE;
	s_pVirtualKeyTrans[VK_TAB] = KEY_TAB;
	s_pVirtualKeyTrans[VK_CAPITAL] = KEY_CAPSLOCK;
	s_pVirtualKeyTrans[VK_NUMLOCK] = KEY_NUMLOCK;
	s_pVirtualKeyTrans[VK_ESCAPE]	= KEY_ESCAPE;
	s_pVirtualKeyTrans[VK_SCROLL]	= KEY_SCROLLLOCK;
	s_pVirtualKeyTrans[VK_INSERT]	= KEY_INSERT;
	s_pVirtualKeyTrans[VK_DELETE]	= KEY_DELETE;
	s_pVirtualKeyTrans[VK_HOME] = KEY_HOME;
	s_pVirtualKeyTrans[VK_END] = KEY_END;
	s_pVirtualKeyTrans[VK_PRIOR] = KEY_PAGEUP;
	s_pVirtualKeyTrans[VK_NEXT] = KEY_PAGEDOWN;
	s_pVirtualKeyTrans[VK_PAUSE] = KEY_BREAK;
	s_pVirtualKeyTrans[VK_SHIFT] = KEY_RSHIFT;
	s_pVirtualKeyTrans[VK_SHIFT] = KEY_LSHIFT;	// SHIFT -> left SHIFT
	s_pVirtualKeyTrans[VK_MENU] = KEY_RALT;
	s_pVirtualKeyTrans[VK_MENU] = KEY_LALT;		// ALT -> left ALT
	s_pVirtualKeyTrans[VK_CONTROL] = KEY_RCONTROL;
	s_pVirtualKeyTrans[VK_CONTROL] = KEY_LCONTROL;	// CTRL -> left CTRL
	s_pVirtualKeyTrans[VK_LWIN] = KEY_LWIN;
	s_pVirtualKeyTrans[VK_RWIN] = KEY_RWIN;
	s_pVirtualKeyTrans[VK_APPS] = KEY_APP;
	s_pVirtualKeyTrans[VK_UP] = KEY_UP;
	s_pVirtualKeyTrans[VK_LEFT] = KEY_LEFT;
	s_pVirtualKeyTrans[VK_DOWN] = KEY_DOWN;
	s_pVirtualKeyTrans[VK_RIGHT] = KEY_RIGHT;	
	s_pVirtualKeyTrans[VK_F1] = KEY_F1;
	s_pVirtualKeyTrans[VK_F2] = KEY_F2;
	s_pVirtualKeyTrans[VK_F3] = KEY_F3;
	s_pVirtualKeyTrans[VK_F4] = KEY_F4;
	s_pVirtualKeyTrans[VK_F5] = KEY_F5;
	s_pVirtualKeyTrans[VK_F6] = KEY_F6;
	s_pVirtualKeyTrans[VK_F7] = KEY_F7;
	s_pVirtualKeyTrans[VK_F8] = KEY_F8;
	s_pVirtualKeyTrans[VK_F9] = KEY_F9;
	s_pVirtualKeyTrans[VK_F10] = KEY_F10;
	s_pVirtualKeyTrans[VK_F11] = KEY_F11;
	s_pVirtualKeyTrans[VK_F12] = KEY_F12;
}

KeyCode VGUI_MapKey( int keyCode )
{
	VGUI_InitKeyTranslationTable();

	if( keyCode < 0 || keyCode >= ARRAYSIZE( s_pVirtualKeyTrans ))
	{
		Assert( 0 );
		return (KeyCode)-1;
	}
	else
	{
		return s_pVirtualKeyTrans[keyCode];
	}
}

LONG VGUI_SurfaceWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	if( !engSurface )
		return 0;

	switch( uMsg )
	{
	case WM_SETCURSOR:
		VGUI_ActivateCurrentCursor();
		break;
	case WM_MOUSEMOVE:
		engApp->internalCursorMoved((short)LOWORD( lParam ), (short)HIWORD( lParam ), engSurface );
		break;
	case WM_LBUTTONDOWN:
		engApp->internalMousePressed( MOUSE_LEFT, engSurface );
		break;
	case WM_RBUTTONDOWN:
		engApp->internalMousePressed( MOUSE_RIGHT, engSurface );
		break;
	case WM_MBUTTONDOWN:
		engApp->internalMousePressed( MOUSE_MIDDLE, engSurface );
		break;
	case WM_LBUTTONUP:
		engApp->internalMouseReleased( MOUSE_LEFT, engSurface );
		break;
	case WM_RBUTTONUP:
		engApp->internalMouseReleased( MOUSE_RIGHT, engSurface );
		break;
	case WM_MBUTTONUP:
		engApp->internalMouseReleased( MOUSE_MIDDLE, engSurface );
		break;
	case WM_LBUTTONDBLCLK:
		engApp->internalMouseDoublePressed( MOUSE_LEFT, engSurface );
		break;
	case WM_RBUTTONDBLCLK:
		engApp->internalMouseDoublePressed( MOUSE_RIGHT, engSurface );
		break;
	case WM_MBUTTONDBLCLK:
		engApp->internalMouseDoublePressed( MOUSE_MIDDLE, engSurface );
		break;
	case WM_MOUSEWHEEL:
		engApp->internalMouseWheeled(((short)HIWORD( wParam )) / WHEEL_DELTA, engSurface );
		break;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if( !FBitSet( lParam, BIT( 30 )))
			engApp->internalKeyPressed( VGUI_MapKey( wParam ), engSurface );
		engApp->internalKeyTyped( VGUI_MapKey( wParam ), engSurface );
		break;
	case WM_CHAR:
	case WM_SYSCHAR:
		// already handled in Key_Event
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		engApp->internalKeyReleased( VGUI_MapKey( wParam ), engSurface );
		break;
	}
	return 1;
}