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

#define VGUI_MAX_TEXTURES 1024

typedef struct vgui_reusable_texture_s
{
	int gl_texturenum;
	byte hash[16];
} vgui_reusable_texture_t;

typedef struct vgui_static_s
{
	qboolean initialized;
	VGUI_DefaultCursor cursor;
	vguiapi_t dllFuncs;

	vgui_reusable_texture_t *textures;
	int texture_id;
	int max_textures;
	int bound_texture;
	byte color[4];
	qboolean enable_texture;

	HINSTANCE hInstance;

	poolhandle_t mempool;

	enum VGUI_KeyCode virtualKeyTrans[256];
} vgui_static_t;

static vgui_static_t vgui = {
	false, -1
};
static CVAR_DEFINE_AUTO( vgui_utf8, "0", FCVAR_ARCHIVE, "enable utf-8 support for vgui text" );

static void GAME_EXPORT VGUI_DrawInit( void )
{
	if( vgui.mempool )
		Mem_EmptyPool( vgui.mempool );
	else vgui.mempool = Mem_AllocPool( "VGui Support Pool" );

	vgui.textures = NULL;

	memset( vgui.color, 0, sizeof( vgui.color ));
	vgui.texture_id = 0;
	vgui.bound_texture = 0;
	vgui.max_textures = 0;
	vgui.enable_texture = true;
}

static void GAME_EXPORT VGUI_DrawShutdown( void )
{
	int i;

	for( i = 1; i < vgui.texture_id; i++ )
		ref.dllFuncs.GL_FreeTexture( vgui.textures[i].gl_texturenum );

	Mem_FreePool( &vgui.mempool );
	vgui.textures = NULL;

	memset( vgui.color, 0, sizeof( vgui.color ));
	vgui.texture_id = 0;
	vgui.bound_texture = 0;
	vgui.max_textures = 0;
}

static int GAME_EXPORT VGUI_GenerateTexture( void )
{
	// allocate new
	if( vgui.texture_id + 1 >= vgui.max_textures )
	{
		if( vgui.max_textures + VGUI_MAX_TEXTURES >= VGUI_MAX_TEXTURES * VGUI_MAX_TEXTURES )
		{
			// in theory it might look up texture that hasn't been bound for a while and
			// reuse that but it will eventually overwrite some important textures anyway
			Con_Printf( S_ERROR "%s: Refusing resizing VGUI textures array due to memory leak\n", __func__ );
			return vgui.texture_id;
		}

		vgui.max_textures += VGUI_MAX_TEXTURES;

		// this potentially might leak memory if VGUI is used incorrectly!
		// (like in Cry of Fear)
		vgui.textures = Mem_Realloc( vgui.mempool, vgui.textures, sizeof( *vgui.textures ) * vgui.max_textures );

		// warn mod developer
		if( vgui.max_textures >= VGUI_MAX_TEXTURES * 4 )
			Con_Printf( S_ERROR "%s: Potential memory leak in VGUI code is detected!\n", __func__ );
	}

	return ++vgui.texture_id;
}

static void GAME_EXPORT VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	rgbdata_t r_image = { 0 };
	char texName[32];
	MD5Context_t ctx;
	byte hash[16];

	if( id <= 0 || id >= vgui.max_textures || width <= 0 || height <= 0 )
	{
		Con_DPrintf( S_ERROR "%s: bad texture %i. Ignored\n", __func__, id );
		return;
	}

	// need to do this as some mods tend to upload same texture over and over
	// exhausing engine-wide limit on textures and leaking vram
	MD5Init( &ctx );
	MD5Update( &ctx, buffer, width * height * 4 );
	MD5Final( hash, &ctx );

	// it's a new texture, try to find a copy
	if( vgui.textures[id].gl_texturenum == 0 )
	{
		int i;

		for( i = 1; i < vgui.texture_id; i++ )
		{
			if( vgui.textures[i].gl_texturenum != 0 && !memcmp( vgui.textures[i].hash, hash, sizeof( hash )))
			{
				// copy data to new texture id
				vgui.textures[id] = vgui.textures[i];
				return;
			}
		}
	}

	Q_snprintf( texName, sizeof( texName ), "*vgui%i", id );

	r_image.width = width;
	r_image.height = height;
	r_image.type = PF_RGBA_32;
	r_image.size = width * height * 4;
	r_image.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;
	r_image.buffer = (byte*)buffer;

	vgui.textures[id].gl_texturenum = GL_LoadTextureInternal( texName, &r_image, TF_IMAGE );
	memcpy( vgui.textures[id].hash, hash, sizeof( hash ));
}

static void GAME_EXPORT VGUI_CreateTexture( int id, int width, int height )
{
	// nothing uses it, it can be removed
	Host_Error( "%s: deprecated\n", __func__ );

}

static void GAME_EXPORT VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	// nothing uses it, it can be removed
	Host_Error( "%s: deprecated\n", __func__ );
}

static void GAME_EXPORT VGUI_BindTexture( int id )
{
	if( id <= 0 || id >= vgui.max_textures || !vgui.textures[id].gl_texturenum )
		id = 1; // NOTE: same as bogus index 2700 in GoldSrc

	ref.dllFuncs.GL_Bind( XASH_TEXTURE0, vgui.textures[id].gl_texturenum );
	vgui.bound_texture = id;
}

static void GAME_EXPORT VGUI_GetTextureSizes( int *w, int *h )
{
	int texnum;

	if( vgui.bound_texture )
		texnum = vgui.textures[vgui.bound_texture].gl_texturenum;
	else
		texnum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );

	R_GetTextureParms( w, h, texnum );
}

static void GAME_EXPORT VGUI_SetupDrawingRect( int *pColor )
{
	ref.dllFuncs.VGUI_SetupDrawing( true );
	Vector4Set( vgui.color, pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

static void GAME_EXPORT VGUI_SetupDrawingText( int *pColor )
{
	ref.dllFuncs.VGUI_SetupDrawing( false );
	Vector4Set( vgui.color, pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

static void GAME_EXPORT VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	float x, y, w, h;

	if( !ul || !lr )
		return;

	x = ul->point[0];
	y = ul->point[1];
	w = lr->point[0] - x;
	h = lr->point[1] - y;

	SPR_AdjustSize( &x, &y, &w, &h );

	if( vgui.enable_texture )
	{
		float s1, s2, t1, t2;

		s1 = ul->coord[0];
		t1 = ul->coord[1];
		s2 = lr->coord[0];
		t2 = lr->coord[1];

		ref.dllFuncs.Color4ub( vgui.color[0], vgui.color[1], vgui.color[2], vgui.color[3] );
		ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, vgui.textures[vgui.bound_texture].gl_texturenum );
	}
	else
	{
		ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, vgui.color[0], vgui.color[1], vgui.color[2], vgui.color[3] );
	}
}

static void GAME_EXPORT VGUI_EnableTexture( qboolean enable )
{
	vgui.enable_texture = enable;
}

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

void VGui_RegisterCvars( void )
{
	Cvar_RegisterVariable( &vgui_utf8 );
}

static const vguiapi_t gEngfuncs =
{
	false, // Not initialized yet
	VGUI_DrawInit, // VGUI_DrawInit,
	VGUI_DrawShutdown, // VGUI_DrawShutdown,
	VGUI_SetupDrawingText, // VGUI_SetupDrawingText,
	VGUI_SetupDrawingRect, // VGUI_SetupDrawingRect,
	VGUI_SetupDrawingText, // VGUI_SetupDrawingImage, (same as text)
	VGUI_BindTexture, // VGUI_BindTexture,
	VGUI_EnableTexture, // VGUI_EnableTexture,
	VGUI_CreateTexture, // VGUI_CreateTexture,
	VGUI_UploadTexture, // VGUI_UploadTexture,
	VGUI_UploadTextureBlock, // VGUI_UploadTextureBlock,
	VGUI_DrawQuad, // VGUI_DrawQuad,
	VGUI_GetTextureSizes, // VGUI_GetTextureSizes,
	VGUI_GenerateTexture, // VGUI_GenerateTexture,
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
};

qboolean VGui_LoadProgs( HINSTANCE hInstance )
{
	void (*F)( vguiapi_t* );
	qboolean client = hInstance != NULL;

	vgui.dllFuncs = gEngfuncs;

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
			else Con_Reportf( "%s: not found\n", __func__ );

			return false;
		}
	}

	// try legacy API first
	F = COM_GetProcAddress( hInstance, client ? "InitVGUISupportAPI" : "InitAPI" );

	if( F )
	{
		F( &vgui.dllFuncs );

		vgui.initialized = vgui.dllFuncs.initialized = true;
		Con_Reportf( "%s: initialized legacy API in %s module\n", __func__, client ? "client" : "support" );

		return true;
	}

	Con_Reportf( S_ERROR "%s: Failed to find VGUI support API entry point in %s module\n", __func__, client ? "client" : "support" );
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

	// drop pointers to now unloaded vgui_support
	vgui.dllFuncs = gEngfuncs;
	vgui.hInstance = NULL;
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

