/*
input.c - win32 input devices
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
#include "input.h"
#include "client.h"
#include "vgui_draw.h"

#define PRINTSCREEN_ID	1
#define WND_HEADSIZE	wnd_caption		// some offset
#define WND_BORDER		3			// sentinel border in pixels

HICON	in_mousecursor;
qboolean	in_mouseactive;				// false when not focus app
qboolean	in_restore_spi;
qboolean	in_mouseinitialized;
int	in_mouse_oldbuttonstate;
qboolean	in_mouse_suspended;
qboolean	in_mouse_savedpos;
int	in_mouse_buttons;
RECT	window_rect, real_rect;
POINT	in_lastvalidpos;
uint	in_mouse_wheel;
int	wnd_caption;

static byte scan_to_key[128] = 
{ 
	0,27,'1','2','3','4','5','6','7','8','9','0','-','=',K_BACKSPACE,9,
	'q','w','e','r','t','y','u','i','o','p','[',']', 13 , K_CTRL,
	'a','s','d','f','g','h','j','k','l',';','\'','`',
	K_SHIFT,'\\','z','x','c','v','b','n','m',',','.','/',K_SHIFT,
	'*',K_ALT,' ',K_CAPSLOCK,
	K_F1,K_F2,K_F3,K_F4,K_F5,K_F6,K_F7,K_F8,K_F9,K_F10,
	K_PAUSE,K_SCROLLOCK,K_HOME,K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,
	K_RIGHTARROW,K_KP_PLUS,K_END,K_DOWNARROW,K_PGDN,K_INS,K_DEL,
	0,0,0,K_F11,K_F12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// extra mouse buttons
static int mouse_buttons[] =
{
	MK_LBUTTON,
	MK_RBUTTON,
	MK_MBUTTON,
	MK_XBUTTON1,
	MK_XBUTTON2,
	MK_XBUTTON3,
	MK_XBUTTON4,
	MK_XBUTTON5
};
	
/*
=======
Host_MapKey

Map from windows to engine keynums
=======
*/
static int Host_MapKey( int key )
{
	int	result, modified;
	qboolean	is_extended = false;

	modified = ( key >> 16 ) & 255;
	if( modified > 127 ) return 0;

	if( key & ( 1 << 24 ))
		is_extended = true;

	result = scan_to_key[modified];

	if( !is_extended )
	{
		switch( result )
		{
		case K_HOME: return K_KP_HOME;
		case K_UPARROW: return K_KP_UPARROW;
		case K_PGUP: return K_KP_PGUP;
		case K_LEFTARROW: return K_KP_LEFTARROW;
		case K_RIGHTARROW: return K_KP_RIGHTARROW;
		case K_END: return K_KP_END;
		case K_DOWNARROW: return K_KP_DOWNARROW;
		case K_PGDN: return K_KP_PGDN;
		case K_INS: return K_KP_INS;
		case K_DEL: return K_KP_DEL;
		default: return result;
		}
	}
	else
	{
		switch( result )
		{
		case K_PAUSE: return K_KP_NUMLOCK;
		case 0x0D: return K_KP_ENTER;
		case 0x2F: return K_KP_SLASH;
		case 0xAF: return K_KP_PLUS;
		default: return result;
		}
	}
}

/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse( void )
{
	if( host.type == HOST_DEDICATED ) return;
	if( Sys_CheckParm( "-nomouse" )) return; 

	in_mouse_buttons = 8;
	in_mouseinitialized = true;
	in_mouse_wheel = RegisterWindowMessage( "MSWHEEL_ROLLMSG" );
}

static qboolean IN_CursorInRect( void )
{
	POINT	curpos;
	
	if( !in_mouseinitialized || !in_mouseactive )
		return false;

	// find mouse movement
	GetCursorPos( &curpos );

	if( curpos.x < real_rect.left + WND_BORDER )
		return false;
	if( curpos.x > real_rect.right - WND_BORDER * 3 )
		return false;
	if( curpos.y < real_rect.top + WND_HEADSIZE + WND_BORDER )
		return false;
	if( curpos.y > real_rect.bottom - WND_BORDER * 3 )
		return false;
	return true;
}

static void IN_ActivateCursor( void )
{
	if( cls.key_dest == key_menu )
	{
		SetCursor( in_mousecursor );
	}
}

void IN_SetCursor( HICON hCursor )
{
	in_mousecursor = hCursor;

	IN_ActivateCursor();
}

/*
===========
IN_MouseSavePos

Save mouse pos before state change e.g. changelevel
===========
*/
void IN_MouseSavePos( void )
{
	if( !in_mouseactive )
		return;

	GetCursorPos( &in_lastvalidpos );
	in_mouse_savedpos = true;
}

/*
===========
IN_MouseRestorePos

Restore right position for background
===========
*/
void IN_MouseRestorePos( void )
{
	if( !in_mouse_savedpos )
		return;

	SetCursorPos( in_lastvalidpos.x, in_lastvalidpos.y );
	in_mouse_savedpos = false;
}

/*
===========
IN_ToggleClientMouse

Called when key_dest is changed
===========
*/
void IN_ToggleClientMouse( int newstate, int oldstate )
{
	if( newstate == oldstate ) return;

	if( oldstate == key_game )
	{
		clgame.dllFuncs.IN_DeactivateMouse();
	}
	else if( newstate == key_game )
	{
		// reset mouse pos, so cancel effect in game
		SetCursorPos( host.window_center_x, host.window_center_y );	
		clgame.dllFuncs.IN_ActivateMouse();
	}

	if( newstate == key_menu && ( !CL_IsBackgroundMap() || CL_IsBackgroundDemo( )))
	{
		in_mouseactive = false;
		ClipCursor( NULL );
		ReleaseCapture();
		while( ShowCursor( true ) < 0 );
	}
}

/*
===========
IN_RecalcCenter

Recalc the center of screen
===========
*/
void IN_RecalcCenter( qboolean setpos )
{
	int	width, height;

	if( host.status != HOST_FRAME )
		return;

	width = GetSystemMetrics( SM_CXSCREEN );
	height = GetSystemMetrics( SM_CYSCREEN );
	GetWindowRect( host.hWnd, &window_rect );
	if( window_rect.left < 0 ) window_rect.left = 0;
	if( window_rect.top < 0 ) window_rect.top = 0;
	if( window_rect.right >= width ) window_rect.right = width - 1;
	if( window_rect.bottom >= height - 1 ) window_rect.bottom = height - 1;

	host.window_center_x = (window_rect.right + window_rect.left) / 2;
	host.window_center_y = (window_rect.top + window_rect.bottom) / 2;
	if( setpos ) SetCursorPos( host.window_center_x, host.window_center_y );
}

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( qboolean force )
{
	static int	oldstate;
			
	if( !in_mouseinitialized )
		return;

	if( CL_Active() && host.mouse_visible && !force )
		return;	// VGUI controls  

	if( cls.key_dest == key_menu && !Cvar_VariableInteger( "fullscreen" ))
	{
		// check for mouse leave-entering
		if( !in_mouse_suspended && !UI_MouseInRect( ))
			in_mouse_suspended = true;

		if( oldstate != in_mouse_suspended )
		{
			if( in_mouse_suspended )
			{
				ClipCursor( NULL );
				ReleaseCapture();
				while( ShowCursor( true ) < 0 );
				UI_ShowCursor( false );
			}
		}

		oldstate = in_mouse_suspended;

		if( in_mouse_suspended && IN_CursorInRect( ))
		{
			in_mouse_suspended = false;
			in_mouseactive = false; // re-initialize mouse
			UI_ShowCursor( true );
		}
	}

	if( in_mouseactive ) return;
	in_mouseactive = true;

	if( UI_IsVisible( )) return;

	if( cls.key_dest == key_game )
	{
		clgame.dllFuncs.IN_ActivateMouse();
	}

	IN_RecalcCenter( true );

	SetCapture( host.hWnd );
	ClipCursor( &window_rect );
	while( ShowCursor( false ) >= 0 );
}

/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized || !in_mouseactive )
		return;

	if( cls.key_dest == key_game )
	{
		clgame.dllFuncs.IN_DeactivateMouse();
	}

	in_mouseactive = false;
	ClipCursor( NULL );
	ReleaseCapture();
	while( ShowCursor( true ) < 0 );
}

/*
================
IN_MouseMove
================
*/
void IN_MouseMove( void )
{
	POINT	current_pos;
	
	if( !in_mouseinitialized || !in_mouseactive || !UI_IsVisible( ))
		return;

	// find mouse movement
	GetCursorPos( &current_pos );
	ScreenToClient( host.hWnd, &current_pos );

	// if the menu is visible, move the menu cursor
	UI_MouseMove( current_pos.x, current_pos.y );

	IN_ActivateCursor();
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int mstate )
{
	int	i;

	if( !in_mouseinitialized || !in_mouseactive )
		return;

	if( cls.key_dest == key_game )
	{
		clgame.dllFuncs.IN_MouseEvent( mstate );
		return;
	}

	// perform button actions
	for( i = 0; i < in_mouse_buttons; i++ )
	{
		if( FBitSet( mstate, BIT( i )) && !FBitSet( in_mouse_oldbuttonstate, BIT( i )))
		{
			Key_Event( K_MOUSE1 + i, true );
		}

		if( !FBitSet( mstate, BIT( i )) && FBitSet( in_mouse_oldbuttonstate, BIT( i )))
		{
			Key_Event( K_MOUSE1 + i, false );
		}
	}	

	in_mouse_oldbuttonstate = mstate;
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void )
{
	IN_DeactivateMouse( );
}


/*
===========
IN_Init
===========
*/
void IN_Init( void )
{
	IN_StartupMouse( );
}

/*
==================
Host_InputFrame

Called every frame, even if not generating commands
==================
*/
void Host_InputFrame( void )
{
	qboolean	shutdownMouse = false;

	Sys_SendKeyEvents ();

	if( !in_mouseinitialized )
		return;

	if( host.status != HOST_FRAME )
	{
		IN_DeactivateMouse();
		return;
	}

	// release mouse during pause or console typeing
	if( cl.paused && cls.key_dest == key_game )
		shutdownMouse = true;
	
	if( shutdownMouse && !Cvar_VariableInteger( "fullscreen" ))
	{
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse( false );
	IN_MouseMove();
}

/*
====================
IN_WndProc

main window procedure
====================
*/
LONG IN_WndProc( HWND hWnd, UINT uMsg, UINT wParam, LONG lParam )
{
	int	i, temp = 0;
	qboolean	fActivate;

	if( uMsg == in_mouse_wheel )
		uMsg = WM_MOUSEWHEEL;

	VGUI_SurfaceWndProc( hWnd, uMsg, wParam, lParam );

	switch( uMsg )
	{
	case WM_KILLFOCUS:
		if( Cvar_VariableInteger( "fullscreen" ))
			ShowWindow( host.hWnd, SW_SHOWMINNOACTIVE );
		break;
	case WM_SETCURSOR:
		IN_ActivateCursor();
		break;
	case WM_MOUSEWHEEL:
		if( !in_mouseactive )
			break;
		if(( short )HIWORD( wParam ) > 0 )
		{
			Key_Event( K_MWHEELUP, true );
			Key_Event( K_MWHEELUP, false );
		}
		else
		{
			Key_Event( K_MWHEELDOWN, true );
			Key_Event( K_MWHEELDOWN, false );
		}
		break;
	case WM_CREATE:
		host.hWnd = hWnd;
		GetWindowRect( host.hWnd, &real_rect );
		RegisterHotKey( host.hWnd, PRINTSCREEN_ID, 0, VK_SNAPSHOT );
		break;
	case WM_CLOSE:
		Sys_Quit();
		break;
	case WM_ACTIVATE:
		if( host.status == HOST_SHUTDOWN )
			break; // no need to activate
		if( HIWORD( wParam ))
			host.status = HOST_SLEEP;
		else if( LOWORD( wParam ) == WA_INACTIVE )
			host.status = HOST_NOFOCUS;
		else host.status = HOST_FRAME;
		fActivate = (host.status == HOST_FRAME) ? true : false;
		wnd_caption = GetSystemMetrics( SM_CYCAPTION ) + WND_BORDER;

		S_Activate( fActivate, host.hWnd );
		IN_ActivateMouse( fActivate );
		Key_ClearStates();
		IN_RecalcCenter( false );

		if( host.status == HOST_FRAME )
		{
			SetForegroundWindow( hWnd );
			ShowWindow( hWnd, SW_RESTORE );
		}
		else if( Cvar_VariableInteger( "fullscreen" ))
		{
			ShowWindow( hWnd, SW_MINIMIZE );
		}
		break;
	case WM_MOVE:
		if( !Cvar_VariableInteger( "fullscreen" ))
		{
			RECT	rect;
			int	xPos, yPos, style;

			xPos = (short)LOWORD( lParam );    // horizontal position 
			yPos = (short)HIWORD( lParam );    // vertical position 

			rect.left = rect.top = 0;
			rect.right = rect.bottom = 1;
			style = GetWindowLong( hWnd, GWL_STYLE );
			AdjustWindowRect( &rect, style, FALSE );

			Cvar_SetValue( "_window_xpos", xPos + rect.left );
			Cvar_SetValue( "_window_ypos", yPos + rect.top );
			GetWindowRect( host.hWnd, &real_rect );
		}
		break;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEMOVE:
		for( i = 0; i < in_mouse_buttons; i++ )
		{
			if( wParam & mouse_buttons[i] )
				temp |= (1<<i);
		}
		IN_MouseEvent( temp );
		break;
	case WM_SYSCOMMAND:
		// never turn screensaver or display off while Xash is active
		if(( wParam == SC_SCREENSAVE || wParam == SC_MONITORPOWER ) && host.status != HOST_SLEEP )
			return 0;
		break;
	case WM_SYSKEYDOWN:
		if( wParam == VK_RETURN )
		{
			// alt+enter fullscreen switch
			Cvar_SetValue( "fullscreen", !Cvar_VariableValue( "fullscreen" ));
			return 0;
		}
		// intentional fallthrough
	case WM_KEYDOWN:
		Key_Event( Host_MapKey( lParam ), true );
		if( Host_MapKey( lParam ) == K_ALT )
			return 0;	// prevent WC_SYSMENU call
		break;
	case WM_SYSKEYUP:
	case WM_KEYUP:
		Key_Event( Host_MapKey( lParam ), false );
		break;
	case WM_CHAR:
		CL_CharEvent( wParam );
		break;
	case WM_HOTKEY:
		switch( LOWORD( wParam ))
		{
		case PRINTSCREEN_ID:
			// anti FiEctro system: prevent to write snapshot without Xash version
			Q_strncpy( cls.shotname, "clipboard.bmp", sizeof( cls.shotname ));
			cls.scrshot_action = scrshot_snapshot; // build new frame for screenshot
			host.write_to_clipboard = true;
			cls.envshot_vieworg = NULL;
			break;
		}
		break;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}