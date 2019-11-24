/*
vgui_draw.c - vgui draw methods
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
#include <string.h>
#include "common.h"
#include "client.h"
#include "vgui_draw.h"
#include "vgui_api.h"
#include "library.h"
#include "keydefs.h"
#include "ref_common.h"
#include "input.h"
#ifdef XASH_SDL
#include <SDL.h>
static SDL_Cursor* s_pDefaultCursor[20];
#endif
#include "platform/platform.h"

static enum VGUI_KeyCode s_pVirtualKeyTrans[256];
static enum VGUI_DefaultCursor s_currentCursor;
static HINSTANCE s_pVGuiSupport; // vgui_support library
static convar_t	*vgui_utf8 = NULL;

// Helper functions for vgui backend

/*void VGUI_HideCursor( void )
{
	host.mouse_visible = false;
	SDL_HideCursor();
}

void VGUI_ShowCursor( void )
{
	host.mouse_visible = true;
	SDL_ShowCursor();
}*/

void GAME_EXPORT *VGUI_EngineMalloc(size_t size)
{
	return Z_Malloc( size );
}

qboolean GAME_EXPORT VGUI_IsInGame( void )
{
	return cls.state == ca_active && cls.key_dest == key_game;
}

void GAME_EXPORT VGUI_GetMousePos( int *_x, int *_y )
{
	float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth;
	float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight;
	int x, y;

	Platform_GetMousePos( &x, &y );
	*_x = x / xscale, *_y = y / yscale;
}

void VGUI_InitCursors( void )
{
	// load up all default cursors
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	s_pDefaultCursor[dc_none] = NULL;
	s_pDefaultCursor[dc_arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	s_pDefaultCursor[dc_ibeam] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	s_pDefaultCursor[dc_hourglass]= SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
	s_pDefaultCursor[dc_crosshair]= SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
	s_pDefaultCursor[dc_up] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	s_pDefaultCursor[dc_sizenwse] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
	s_pDefaultCursor[dc_sizenesw] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
	s_pDefaultCursor[dc_sizewe] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
	s_pDefaultCursor[dc_sizens] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
	s_pDefaultCursor[dc_sizeall] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	s_pDefaultCursor[dc_no] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NO);
	s_pDefaultCursor[dc_hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
	//host.mouse_visible = true;
	SDL_SetCursor( s_pDefaultCursor[dc_arrow] );
#endif
}

void GAME_EXPORT VGUI_CursorSelect(enum VGUI_DefaultCursor cursor )
{
	qboolean visible;
	if( cls.key_dest != key_game || cl.paused )
		return;
	
	switch( cursor )
	{
		case dc_user:
		case dc_none:
			visible = false;
			break;
		default:
		visible = true;
		break;
	}

#if SDL_VERSION_ATLEAST( 2, 0, 0 )
	/// TODO: platform cursors

	if( CVAR_TO_BOOL( touch_emulate ) )
		return;
	if( host.mouse_visible )
	{
		SDL_SetRelativeMouseMode( SDL_FALSE );
		SDL_SetCursor( s_pDefaultCursor[cursor] );
		SDL_ShowCursor( true );
	}
	else
	{
		SDL_ShowCursor( false );
		if( host.mouse_visible )
			SDL_GetRelativeMouseState( NULL, NULL );
	}
	//SDL_SetRelativeMouseMode(false);
#endif
	if( s_currentCursor == cursor )
		return;

	s_currentCursor = cursor;
	host.mouse_visible = visible;
}

byte GAME_EXPORT VGUI_GetColor( int i, int j)
{
	return g_color_table[i][j];
}

// Define and initialize vgui API

void GAME_EXPORT VGUI_SetVisible( qboolean state )
{
	host.mouse_visible=state;
#ifdef XASH_SDL
	SDL_ShowCursor( state );
	if( !state )
		SDL_GetRelativeMouseState( NULL, NULL );
#endif
	Key_EnableTextInput( state, true );
}

int GAME_EXPORT VGUI_UtfProcessChar( int in )
{
	if( CVAR_TO_BOOL( vgui_utf8 ) )
		return Con_UtfProcessCharForce( in );
	else
		return in;
}

vguiapi_t vgui =
{
	false, // Not initialized yet
	NULL, // VGUI_DrawInit,
	NULL, // VGUI_DrawShutdown,
	NULL, // VGUI_SetupDrawingText,
	NULL, // VGUI_SetupDrawingRect,
	NULL, // VGUI_SetupDrawingImage,
	NULL, // VGUI_BindTexture,
	NULL, // VGUI_EnableTexture,
	NULL, // VGUI_CreateTexture,
	NULL, // VGUI_UploadTexture,
	NULL, // VGUI_UploadTextureBlock,
	NULL, // VGUI_DrawQuad,
	NULL, // VGUI_GetTextureSizes,
	NULL, // VGUI_GenerateTexture,
	VGUI_EngineMalloc,
/*	VGUI_ShowCursor,
	VGUI_HideCursor,*/
	VGUI_CursorSelect,
	VGUI_GetColor,
	VGUI_IsInGame,
	VGUI_SetVisible,
	VGUI_GetMousePos,
	VGUI_UtfProcessChar,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

qboolean VGui_IsActive( void )
{
	return vgui.initialized;
}

void VGui_FillAPIFromRef( vguiapi_t *to, const ref_interface_t *from )
{
	to->DrawInit = from->VGUI_DrawInit;
	to->DrawShutdown = from->VGUI_DrawShutdown;
	to->SetupDrawingText = from->VGUI_SetupDrawingText;
	to->SetupDrawingRect = from->VGUI_SetupDrawingRect;
	to->SetupDrawingImage = from->VGUI_SetupDrawingImage;
	to->BindTexture = from->VGUI_BindTexture;
	to->EnableTexture = from->VGUI_EnableTexture;
	to->CreateTexture = from->VGUI_CreateTexture;
	to->UploadTexture = from->VGUI_UploadTexture;
	to->UploadTextureBlock = from->VGUI_UploadTextureBlock;
	to->DrawQuad = from->VGUI_DrawQuad;
	to->GetTextureSizes = from->VGUI_GetTextureSizes;
	to->GenerateTexture = from->VGUI_GenerateTexture;
}

/*
================
VGui_Startup

Load vgui_support library and call VGui_Startup
================
*/
void VGui_Startup( const char *clientlib, int width, int height )
{
	static qboolean failed = false;

	void (*F) ( vguiapi_t * );
	char vguiloader[256];
	char vguilib[256];

	vguiloader[0] = vguilib[0] = '\0';

	if( failed )
		return;

	if( !vgui.initialized )
	{
		vgui_utf8 = Cvar_Get( "vgui_utf8", "0", FCVAR_ARCHIVE, "enable utf-8 support for vgui text" );

		VGui_FillAPIFromRef( &vgui, &ref.dllFuncs );

#ifdef XASH_INTERNAL_GAMELIBS
		s_pVGuiSupport = COM_LoadLibrary( clientlib, false, false );

		if( s_pVGuiSupport )
		{
			F = COM_GetProcAddress( s_pVGuiSupport, "InitVGUISupportAPI" );
			if( F )
			{
				F( &vgui );
				vgui.initialized = true;
				VGUI_InitCursors();
				Con_Reportf( "vgui_support: found interal client support\n" );
			}
		}
#endif // XASH_INTERNAL_GAMELIBS

		// HACKHACK: load vgui with correct path first if specified.
		// it will be reused while resolving vgui support and client deps
		if( Sys_GetParmFromCmdLine( "-vguilib", vguilib ) )
		{
			if( Q_strstr( vguilib, ".dll") )
				Q_strncpy( vguiloader, "vgui_support.dll", 256 );
			else
				Q_strncpy( vguiloader, VGUI_SUPPORT_DLL, 256 );

			if( !COM_LoadLibrary( vguilib, false, false ) )
				Con_Reportf( S_WARN "VGUI preloading failed. Default library will be used! Reason: %s\n", COM_GetLibraryError());
		}

		if( Q_strstr( clientlib, ".dll" ) )
			Q_strncpy( vguiloader, "vgui_support.dll", 256 );

		if( !vguiloader[0] && !Sys_GetParmFromCmdLine( "-vguiloader", vguiloader ) )
			Q_strncpy( vguiloader, VGUI_SUPPORT_DLL, 256 );

		s_pVGuiSupport = COM_LoadLibrary( vguiloader, false, false );

		if( !s_pVGuiSupport )
		{
			s_pVGuiSupport = COM_LoadLibrary( va( "../%s", vguiloader ), false, false );
		}

		if( !s_pVGuiSupport )
		{
			if( FS_FileExists( vguiloader, false ) )
				Con_Reportf( S_ERROR  "Failed to load vgui_support library: %s", COM_GetLibraryError() );
			else
				Con_Reportf( "vgui_support: not found\n" );
		}
		else
		{
			F = COM_GetProcAddress( s_pVGuiSupport, "InitAPI" );
			if( F )
			{
				F( &vgui );
				vgui.initialized = true;
				VGUI_InitCursors();
			}
			else
				Con_Reportf( S_ERROR  "Failed to find vgui_support library entry point!\n" );
		}

	}

	if( height < 480 )
		height = 480;

	if( width <= 640 )
		width = 640;
	else if( width <= 800 )
		width = 800;
	else if( width <= 1024 )
		width = 1024;
	else if( width <= 1152 )
		width = 1152;
	else if( width <= 1280 )
		width = 1280;
	else if( width <= 1600 )
		width = 1600;
#ifdef DLL_LOADER
	else if ( Q_strstr( vguiloader, ".dll" ) )
		width = 1600;
#endif


	if( vgui.initialized )
	{
		//host.mouse_visible = true;
		vgui.Startup( width, height );
	}
	else failed = true;
}



/*
================
VGui_Shutdown

Unload vgui_support library and call VGui_Shutdown
================
*/
void VGui_Shutdown( void )
{
	if( vgui.Shutdown )
		vgui.Shutdown();

	if( s_pVGuiSupport )
		COM_FreeLibrary( s_pVGuiSupport );
	s_pVGuiSupport = NULL;

	vgui.initialized = false;
}


void VGUI_InitKeyTranslationTable( void )
{
	static qboolean bInitted = false;

	if( bInitted )
		return;
	bInitted = true;

	// set virtual key translation table
	memset( s_pVirtualKeyTrans, -1, sizeof( s_pVirtualKeyTrans ) );

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

	s_pVirtualKeyTrans[K_KP_5 - 5] = KEY_PAD_0;
	s_pVirtualKeyTrans[K_KP_5 - 4] = KEY_PAD_1;
	s_pVirtualKeyTrans[K_KP_5 - 3] = KEY_PAD_2;
	s_pVirtualKeyTrans[K_KP_5 - 2] = KEY_PAD_3;
	s_pVirtualKeyTrans[K_KP_5 - 1] = KEY_PAD_4;
	s_pVirtualKeyTrans[K_KP_5 - 0] = KEY_PAD_5;
	s_pVirtualKeyTrans[K_KP_5 + 1] = KEY_PAD_6;
	s_pVirtualKeyTrans[K_KP_5 + 2] = KEY_PAD_7;
	s_pVirtualKeyTrans[K_KP_5 + 3] = KEY_PAD_8;
	s_pVirtualKeyTrans[K_KP_5 + 4] = KEY_PAD_9;
	s_pVirtualKeyTrans[K_KP_SLASH]	= KEY_PAD_DIVIDE;
	s_pVirtualKeyTrans['*'] = KEY_PAD_MULTIPLY;
	s_pVirtualKeyTrans[K_KP_MINUS] = KEY_PAD_MINUS;
	s_pVirtualKeyTrans[K_KP_PLUS] = KEY_PAD_PLUS;
	s_pVirtualKeyTrans[K_KP_ENTER]	= KEY_PAD_ENTER;
	//s_pVirtualKeyTrans[K_KP_DECIMAL] = KEY_PAD_DECIMAL;
	s_pVirtualKeyTrans['['] = KEY_LBRACKET;
	s_pVirtualKeyTrans[']'] = KEY_RBRACKET;
	s_pVirtualKeyTrans[';'] = KEY_SEMICOLON;
	s_pVirtualKeyTrans['\''] = KEY_APOSTROPHE;
	s_pVirtualKeyTrans['`'] = KEY_BACKQUOTE;
	s_pVirtualKeyTrans[','] = KEY_COMMA;
	s_pVirtualKeyTrans['.'] = KEY_PERIOD;
	s_pVirtualKeyTrans[K_KP_SLASH] = KEY_SLASH;
	s_pVirtualKeyTrans['\\'] = KEY_BACKSLASH;
	s_pVirtualKeyTrans['-'] = KEY_MINUS;
	s_pVirtualKeyTrans['='] = KEY_EQUAL;
	s_pVirtualKeyTrans[K_ENTER]	= KEY_ENTER;
	s_pVirtualKeyTrans[K_SPACE] = KEY_SPACE;
	s_pVirtualKeyTrans[K_BACKSPACE] = KEY_BACKSPACE;
	s_pVirtualKeyTrans[K_TAB] = KEY_TAB;
	s_pVirtualKeyTrans[K_CAPSLOCK] = KEY_CAPSLOCK;
	s_pVirtualKeyTrans[K_KP_NUMLOCK] = KEY_NUMLOCK;
	s_pVirtualKeyTrans[K_ESCAPE]	= KEY_ESCAPE;
	//s_pVirtualKeyTrans[K_KP_SCROLLLOCK]	= KEY_SCROLLLOCK;
	s_pVirtualKeyTrans[K_INS]	= KEY_INSERT;
	s_pVirtualKeyTrans[K_DEL]	= KEY_DELETE;
	s_pVirtualKeyTrans[K_HOME] = KEY_HOME;
	s_pVirtualKeyTrans[K_END] = KEY_END;
	s_pVirtualKeyTrans[K_PGUP] = KEY_PAGEUP;
	s_pVirtualKeyTrans[K_PGDN] = KEY_PAGEDOWN;
	s_pVirtualKeyTrans[K_PAUSE] = KEY_BREAK;
	//s_pVirtualKeyTrans[K_SHIFT] = KEY_RSHIFT;
	s_pVirtualKeyTrans[K_SHIFT] = KEY_LSHIFT;	// SHIFT -> left SHIFT
	//s_pVirtualKeyTrans[SDLK_RALT] = KEY_RALT;
	s_pVirtualKeyTrans[K_ALT] = KEY_LALT;		// ALT -> left ALT
	//s_pVirtualKeyTrans[SDLK_RCTRL] = KEY_RCONTROL;
	s_pVirtualKeyTrans[K_CTRL] = KEY_LCONTROL;	// CTRL -> left CTRL
	s_pVirtualKeyTrans[K_WIN] = KEY_LWIN;
	//s_pVirtualKeyTrans[SDLK_APPLICATION] = KEY_RWIN;
	//s_pVirtualKeyTrans[K_WIN] = KEY_APP;
	s_pVirtualKeyTrans[K_UPARROW] = KEY_UP;
	s_pVirtualKeyTrans[K_LEFTARROW] = KEY_LEFT;
	s_pVirtualKeyTrans[K_DOWNARROW] = KEY_DOWN;
	s_pVirtualKeyTrans[K_RIGHTARROW] = KEY_RIGHT;
	s_pVirtualKeyTrans[K_F1] = KEY_F1;
	s_pVirtualKeyTrans[K_F2] = KEY_F2;
	s_pVirtualKeyTrans[K_F3] = KEY_F3;
	s_pVirtualKeyTrans[K_F4] = KEY_F4;
	s_pVirtualKeyTrans[K_F5] = KEY_F5;
	s_pVirtualKeyTrans[K_F6] = KEY_F6;
	s_pVirtualKeyTrans[K_F7] = KEY_F7;
	s_pVirtualKeyTrans[K_F8] = KEY_F8;
	s_pVirtualKeyTrans[K_F9] = KEY_F9;
	s_pVirtualKeyTrans[K_F10] = KEY_F10;
	s_pVirtualKeyTrans[K_F11] = KEY_F11;
	s_pVirtualKeyTrans[K_F12] = KEY_F12;
}

enum VGUI_KeyCode VGUI_MapKey( int keyCode )
{
	VGUI_InitKeyTranslationTable();

	if( keyCode < 0 || keyCode >= (int)sizeof( s_pVirtualKeyTrans ) / (int)sizeof( s_pVirtualKeyTrans[0] ))
	{
		//Assert( false );
		return (enum VGUI_KeyCode)-1;
	}
	else
	{
		return s_pVirtualKeyTrans[keyCode];
	}
}

void VGui_KeyEvent( int key, int down )
{
	if( !vgui.initialized )
		return;

	if( host.mouse_visible )
		Key_EnableTextInput( true, false );

	switch( key )
	{
	case K_MOUSE1:
		vgui.Mouse( down ? MA_PRESSED : MA_RELEASED, MOUSE_LEFT );
		return;
	case K_MOUSE2:
		vgui.Mouse( down ? MA_PRESSED : MA_RELEASED, MOUSE_RIGHT );
		return;
	case K_MOUSE3:
		vgui.Mouse( down ? MA_PRESSED : MA_RELEASED, MOUSE_MIDDLE );
		return;
	case K_MWHEELDOWN:
		vgui.Mouse( MA_WHEEL, 1 );
		return;
	case K_MWHEELUP:
		vgui.Mouse( MA_WHEEL, -1 );
		return;
	default:
		break;
	}

	if( down == 2 )
		vgui.Key( KA_TYPED, VGUI_MapKey( key ) );
	else
		vgui.Key( down?KA_PRESSED:KA_RELEASED, VGUI_MapKey( key ) );
	//Msg("VGui_KeyEvent %d %d %d\n", key, VGUI_MapKey( key ), down );
}

void VGui_MouseMove( int x, int y )
{
	float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth;
	float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight;
	if( vgui.initialized )
		vgui.MouseMove( x / xscale, y / yscale );
}

void VGui_Paint( void )
{
	if(vgui.initialized)
		vgui.Paint();
}

void VGui_RunFrame( void )
{
	//stub
}


void *GAME_EXPORT VGui_GetPanel( void )
{
	if( vgui.initialized )
		return vgui.GetPanel();
	return NULL;
}
