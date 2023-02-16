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
#include "platform/platform.h"

CVAR_DEFINE_AUTO( vgui_utf8, "0", FCVAR_ARCHIVE, "enable utf-8 support for vgui text" );

static void GAME_EXPORT *VGUI_EngineMalloc( size_t size );
static void GAME_EXPORT VGUI_GetMousePos( int *, int * );
static void GAME_EXPORT VGUI_CursorSelect( VGUI_DefaultCursor );
static byte GAME_EXPORT VGUI_GetColor( int, int );
static int GAME_EXPORT VGUI_UtfProcessChar( int in );
static qboolean GAME_EXPORT VGUI_IsInGame( void );

static struct
{
	qboolean initialized;
	vguiapi_t dllFuncs;
	VGUI_DefaultCursor cursor;

	HINSTANCE hInstance;

	enum VGUI_KeyCode virtualKeyTrans[256];
} vgui =
{
	false,
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
		VGUI_CursorSelect,
		VGUI_GetColor,
		VGUI_IsInGame,
		Key_EnableTextInput,
		VGUI_GetMousePos,
		VGUI_UtfProcessChar,
		Platform_GetClipboardText,
		Platform_SetClipboardText,
		Platform_GetKeyModifiers,
	},
	-1
};

static void GAME_EXPORT *VGUI_EngineMalloc( size_t size )
{
	return Z_Malloc( size );
}

static qboolean GAME_EXPORT VGUI_IsInGame( void )
{
	return cls.state == ca_active && cls.key_dest == key_game;
}

static void GAME_EXPORT VGUI_GetMousePos( int *_x, int *_y )
{
	float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth;
	float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight;
	int x, y;

	Platform_GetMousePos( &x, &y );
	*_x = x / xscale;
	*_y = y / yscale;
}

static void GAME_EXPORT VGUI_CursorSelect( VGUI_DefaultCursor cursor )
{
	if( vgui.cursor != cursor )
		Platform_SetCursorType( cursor );
}

static byte GAME_EXPORT VGUI_GetColor( int i, int j )
{
	return g_color_table[i][j];
}

static int GAME_EXPORT VGUI_UtfProcessChar( int in )
{
	if( vgui_utf8.value )
		return Con_UtfProcessCharForce( in );
	return in;
}

qboolean VGui_IsActive( void )
{
	return vgui.initialized;
}

static void VGui_FillAPIFromRef( vguiapi_t *to, const ref_interface_t *from )
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

void VGui_RegisterCvars( void )
{
	Cvar_RegisterVariable( &vgui_utf8 );
}

qboolean VGui_LoadProgs( HINSTANCE hInstance )
{
	void (*F)( vguiapi_t* );
	qboolean client = hInstance != NULL;

	// not loading interface from client.dll, load vgui_support.dll instead
	if( !client )
	{
		string vguiloader, vguilib;

		// HACKHACK: try to load path from custom path
		// to support having different versions of VGUI
		if( Sys_GetParmFromCmdLine( "-vguilib", vguilib ) && !COM_LoadLibrary( vguilib, false, false ))
		{
			Con_Reportf( S_WARN "VGUI preloading failed. Default library will be used! Reason: %s", COM_GetLibraryError());
		}

		if( !Sys_GetParmFromCmdLine( "-vguiloader", vguiloader ))
		{
			Q_strncpy( vguiloader, VGUI_SUPPORT_DLL, sizeof( vguiloader ));
		}

		hInstance = vgui.hInstance = COM_LoadLibrary( vguiloader, false, false );

		if( !vgui.hInstance )
		{
			if( FS_FileExists( vguiloader, false ))
				Con_Reportf( S_ERROR "Failed to load vgui_support library: %s\n", COM_GetLibraryError() );
			else Con_Reportf( "vgui_support: not found\n" );

			return false;
		}
	}

	// try legacy API first
	F = COM_GetProcAddress( hInstance, client ? "InitVGUISupportAPI" : "InitAPI" );

	if( F )
	{
		VGui_FillAPIFromRef( &vgui.dllFuncs, &ref.dllFuncs );
		F( &vgui.dllFuncs );

		vgui.initialized = vgui.dllFuncs.initialized = true;
		Con_Reportf( "vgui_support: initialized legacy API in %s module\n", client ? "client" : "support" );

		return true;
	}

	Con_Reportf( S_ERROR "Failed to find VGUI support API entry point in %s module\n", client ? "client" : "support" );
	return false;
}

/*
================
VGui_Startup

================
*/
void VGui_Startup( int width, int height )
{
	// vgui not initialized from both support and client modules, skip
	if( !vgui.initialized )
		return;

	height = Q_max( 480, height );

	if( width <= 640 ) width = 640;
	else if( width <= 800 ) width = 800;
	else if( width <= 1024 ) width = 1024;
	else if( width <= 1152 ) width = 1152;
	else if( width <= 1280 ) width = 1280;
	else if( width <= 1600 ) width = 1600;

	if( vgui.dllFuncs.Startup )
		vgui.dllFuncs.Startup( width, height );
}



/*
================
VGui_Shutdown

Unload vgui_support library and call VGui_Shutdown
================
*/
void VGui_Shutdown( void )
{
	if( vgui.dllFuncs.Shutdown )
		vgui.dllFuncs.Shutdown();

	if( vgui.hInstance )
		COM_FreeLibrary( vgui.hInstance );

	vgui.hInstance = NULL;
	vgui.initialized = false;
}


static void VGUI_InitKeyTranslationTable( void )
{
	static qboolean initialized = false;

	if( initialized ) return;

	initialized = true;

	// set virtual key translation table
	memset( vgui.virtualKeyTrans, -1, sizeof( vgui.virtualKeyTrans ) );

	// TODO: engine keys are not enough here!
	// make crossplatform way to pass SDL keys here

	vgui.virtualKeyTrans['0'] = KEY_0;
	vgui.virtualKeyTrans['1'] = KEY_1;
	vgui.virtualKeyTrans['2'] = KEY_2;
	vgui.virtualKeyTrans['3'] = KEY_3;
	vgui.virtualKeyTrans['4'] = KEY_4;
	vgui.virtualKeyTrans['5'] = KEY_5;
	vgui.virtualKeyTrans['6'] = KEY_6;
	vgui.virtualKeyTrans['7'] = KEY_7;
	vgui.virtualKeyTrans['8'] = KEY_8;
	vgui.virtualKeyTrans['9'] = KEY_9;
	vgui.virtualKeyTrans['A'] = vgui.virtualKeyTrans['a'] = KEY_A;
	vgui.virtualKeyTrans['B'] = vgui.virtualKeyTrans['b'] = KEY_B;
	vgui.virtualKeyTrans['C'] = vgui.virtualKeyTrans['c'] = KEY_C;
	vgui.virtualKeyTrans['D'] = vgui.virtualKeyTrans['d'] = KEY_D;
	vgui.virtualKeyTrans['E'] = vgui.virtualKeyTrans['e'] = KEY_E;
	vgui.virtualKeyTrans['F'] = vgui.virtualKeyTrans['f'] = KEY_F;
	vgui.virtualKeyTrans['G'] = vgui.virtualKeyTrans['g'] = KEY_G;
	vgui.virtualKeyTrans['H'] = vgui.virtualKeyTrans['h'] = KEY_H;
	vgui.virtualKeyTrans['I'] = vgui.virtualKeyTrans['i'] = KEY_I;
	vgui.virtualKeyTrans['J'] = vgui.virtualKeyTrans['j'] = KEY_J;
	vgui.virtualKeyTrans['K'] = vgui.virtualKeyTrans['k'] = KEY_K;
	vgui.virtualKeyTrans['L'] = vgui.virtualKeyTrans['l'] = KEY_L;
	vgui.virtualKeyTrans['M'] = vgui.virtualKeyTrans['m'] = KEY_M;
	vgui.virtualKeyTrans['N'] = vgui.virtualKeyTrans['n'] = KEY_N;
	vgui.virtualKeyTrans['O'] = vgui.virtualKeyTrans['o'] = KEY_O;
	vgui.virtualKeyTrans['P'] = vgui.virtualKeyTrans['p'] = KEY_P;
	vgui.virtualKeyTrans['Q'] = vgui.virtualKeyTrans['q'] = KEY_Q;
	vgui.virtualKeyTrans['R'] = vgui.virtualKeyTrans['r'] = KEY_R;
	vgui.virtualKeyTrans['S'] = vgui.virtualKeyTrans['s'] = KEY_S;
	vgui.virtualKeyTrans['T'] = vgui.virtualKeyTrans['t'] = KEY_T;
	vgui.virtualKeyTrans['U'] = vgui.virtualKeyTrans['u'] = KEY_U;
	vgui.virtualKeyTrans['V'] = vgui.virtualKeyTrans['v'] = KEY_V;
	vgui.virtualKeyTrans['W'] = vgui.virtualKeyTrans['w'] = KEY_W;
	vgui.virtualKeyTrans['X'] = vgui.virtualKeyTrans['x'] = KEY_X;
	vgui.virtualKeyTrans['Y'] = vgui.virtualKeyTrans['y'] = KEY_Y;
	vgui.virtualKeyTrans['Z'] = vgui.virtualKeyTrans['z'] = KEY_Z;

	vgui.virtualKeyTrans[K_KP_5 - 5] = KEY_PAD_0;
	vgui.virtualKeyTrans[K_KP_5 - 4] = KEY_PAD_1;
	vgui.virtualKeyTrans[K_KP_5 - 3] = KEY_PAD_2;
	vgui.virtualKeyTrans[K_KP_5 - 2] = KEY_PAD_3;
	vgui.virtualKeyTrans[K_KP_5 - 1] = KEY_PAD_4;
	vgui.virtualKeyTrans[K_KP_5 - 0] = KEY_PAD_5;
	vgui.virtualKeyTrans[K_KP_5 + 1] = KEY_PAD_6;
	vgui.virtualKeyTrans[K_KP_5 + 2] = KEY_PAD_7;
	vgui.virtualKeyTrans[K_KP_5 + 3] = KEY_PAD_8;
	vgui.virtualKeyTrans[K_KP_5 + 4] = KEY_PAD_9;
	vgui.virtualKeyTrans[K_KP_SLASH] = KEY_PAD_DIVIDE;
	vgui.virtualKeyTrans['*']        = KEY_PAD_MULTIPLY;
	vgui.virtualKeyTrans[K_KP_MINUS] = KEY_PAD_MINUS;
	vgui.virtualKeyTrans[K_KP_PLUS]  = KEY_PAD_PLUS;
	vgui.virtualKeyTrans[K_KP_ENTER] = KEY_PAD_ENTER;
	vgui.virtualKeyTrans[K_KP_NUMLOCK] = KEY_NUMLOCK;
	vgui.virtualKeyTrans['['] = KEY_LBRACKET;
	vgui.virtualKeyTrans[']'] = KEY_RBRACKET;
	vgui.virtualKeyTrans[';'] = KEY_SEMICOLON;
	vgui.virtualKeyTrans['`'] = KEY_BACKQUOTE;
	vgui.virtualKeyTrans[','] = KEY_COMMA;
	vgui.virtualKeyTrans['.'] = KEY_PERIOD;
	vgui.virtualKeyTrans['-'] = KEY_MINUS;
	vgui.virtualKeyTrans['='] = KEY_EQUAL;
	vgui.virtualKeyTrans['/'] = KEY_SLASH;
	vgui.virtualKeyTrans['\\'] = KEY_BACKSLASH;
	vgui.virtualKeyTrans['\''] = KEY_APOSTROPHE;
	vgui.virtualKeyTrans[K_TAB] = KEY_TAB;
	vgui.virtualKeyTrans[K_ENTER] = KEY_ENTER;
	vgui.virtualKeyTrans[K_SPACE] = KEY_SPACE;
	vgui.virtualKeyTrans[K_CAPSLOCK] = KEY_CAPSLOCK;
	vgui.virtualKeyTrans[K_BACKSPACE] = KEY_BACKSPACE;
	vgui.virtualKeyTrans[K_ESCAPE]	= KEY_ESCAPE;
	vgui.virtualKeyTrans[K_INS] = KEY_INSERT;
	vgui.virtualKeyTrans[K_DEL] = KEY_DELETE;
	vgui.virtualKeyTrans[K_HOME] = KEY_HOME;
	vgui.virtualKeyTrans[K_END] = KEY_END;
	vgui.virtualKeyTrans[K_PGUP] = KEY_PAGEUP;
	vgui.virtualKeyTrans[K_PGDN] = KEY_PAGEDOWN;
	vgui.virtualKeyTrans[K_PAUSE] = KEY_BREAK;
	vgui.virtualKeyTrans[K_SHIFT] = KEY_LSHIFT;	// SHIFT -> left SHIFT
	vgui.virtualKeyTrans[K_ALT] = KEY_LALT;		// ALT -> left ALT
	vgui.virtualKeyTrans[K_CTRL] = KEY_LCONTROL;	// CTRL -> left CTRL
	vgui.virtualKeyTrans[K_WIN] = KEY_LWIN;
	vgui.virtualKeyTrans[K_UPARROW] = KEY_UP;
	vgui.virtualKeyTrans[K_LEFTARROW] = KEY_LEFT;
	vgui.virtualKeyTrans[K_DOWNARROW] = KEY_DOWN;
	vgui.virtualKeyTrans[K_RIGHTARROW] = KEY_RIGHT;
	vgui.virtualKeyTrans[K_F1] = KEY_F1;
	vgui.virtualKeyTrans[K_F2] = KEY_F2;
	vgui.virtualKeyTrans[K_F3] = KEY_F3;
	vgui.virtualKeyTrans[K_F4] = KEY_F4;
	vgui.virtualKeyTrans[K_F5] = KEY_F5;
	vgui.virtualKeyTrans[K_F6] = KEY_F6;
	vgui.virtualKeyTrans[K_F7] = KEY_F7;
	vgui.virtualKeyTrans[K_F8] = KEY_F8;
	vgui.virtualKeyTrans[K_F9] = KEY_F9;
	vgui.virtualKeyTrans[K_F10] = KEY_F10;
	vgui.virtualKeyTrans[K_F11] = KEY_F11;
	vgui.virtualKeyTrans[K_F12] = KEY_F12;
}

static enum VGUI_KeyCode VGUI_MapKey( int keyCode )
{
	VGUI_InitKeyTranslationTable();

	if( keyCode >= 0 && keyCode < ARRAYSIZE( vgui.virtualKeyTrans ))
		return vgui.virtualKeyTrans[keyCode];

	return (enum VGUI_KeyCode)-1;
}

void VGui_MouseEvent( int key, int clicks )
{
	enum VGUI_MouseAction mact;
	enum VGUI_MouseCode   code;

	if( !vgui.dllFuncs.Mouse )
		return;

	switch( key )
	{
	case K_MOUSE1: code = MOUSE_LEFT; break;
	case K_MOUSE2: code = MOUSE_RIGHT; break;
	case K_MOUSE3: code = MOUSE_MIDDLE; break;
	default: return;
	}

	if( clicks >= 2 )
		mact = MA_DOUBLE;
	else if( clicks == 1 )
		mact = MA_PRESSED;
	else
		mact = MA_RELEASED;

	vgui.dllFuncs.Mouse( mact, code );
}

void VGui_MWheelEvent( int y )
{
	if( !vgui.dllFuncs.Mouse )
		return;

	vgui.dllFuncs.Mouse( MA_WHEEL, y );
}

void VGui_KeyEvent( int key, int down )
{
	enum VGUI_KeyCode code;

	if( !vgui.dllFuncs.Key )
		return;

	if(( code = VGUI_MapKey( key )) < 0 )
		return;

	if( down )
	{
		vgui.dllFuncs.Key( KA_PRESSED, code );
		vgui.dllFuncs.Key( KA_TYPED, code );
	}
	else vgui.dllFuncs.Key( KA_RELEASED, code );
}

void VGui_MouseMove( int x, int y )
{
	if( vgui.dllFuncs.MouseMove )
	{
		float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth;
		float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight;
		vgui.dllFuncs.MouseMove( x / xscale, y / yscale );
	}
}

void VGui_Paint( void )
{
	if( vgui.dllFuncs.Paint )
		vgui.dllFuncs.Paint();
}

void VGui_UpdateInternalCursorState( VGUI_DefaultCursor cursorType )
{
	vgui.cursor = cursorType;
}

void *GAME_EXPORT VGui_GetPanel( void )
{
	if( vgui.dllFuncs.GetPanel )
		return vgui.dllFuncs.GetPanel();
	return NULL;
}

void VGui_ReportTextInput( const char *text )
{
	if( vgui.dllFuncs.TextInput )
		vgui.dllFuncs.TextInput( text );
}

