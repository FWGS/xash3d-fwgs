/*
vgui_api.h - vgui_support library interface
Copyright (C) 2015 Mittorn

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
#ifndef VGUI_API_H
#define VGUI_API_H

#include <stddef.h>
#include "key_modifiers.h"
#include "cursor_type.h"

// VGUI generic vertex

typedef struct
{
	float	point[2];
	float	coord[2];
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

// legacy VGUI support API, a single structure shared by both sides
// only kept for compatibility with existing support libraries and client libraries,
// new code should use vgui_support_api_t and vgui_support_interface_t below
typedef struct legacy_vguiapi_s
{
	int	initialized;
	// called from vgui_support
	void	(*DrawInit)( void );
	void	(*DrawShutdown)( void );
	void	(*SetupDrawingText)( int *pColor );
	void	(*SetupDrawingRect)( int *pColor );
	void	(*SetupDrawingImage)( int *pColor );
	void	(*BindTexture)( int id );
	void	(*EnableTexture)( int enable );
	void	(*Reserved0)( int id, int width, int height );
	void	(*UploadTexture)( int id, const char *buffer, int width, int height );
	void	(*Reserved1)( int id, int drawX, int drawY, const unsigned char *rgba, int blockWidth, int blockHeight );
	void	(*DrawQuad)( const vpoint_t *ul, const vpoint_t *lr );
	void	(*GetTextureSizes)( int *width, int *height );
	int		(*GenerateTexture)( void );
	void	*(*EngineMalloc)( size_t size );
	void	(*CursorSelect)( VGUI_DefaultCursor cursor );
	unsigned char	(*GetColor)( int i, int j );
	int		(*IsInGame)( void );
	void	(*EnableTextInput)( int enable, int force );
	void	(*GetCursorPos)( int *x, int *y );
	int		(*ProcessUtfChar)( int ch );
	int		(*GetClipboardText)( char *buffer, size_t bufferSize );
	void	(*SetClipboardText)( const char *text );
	key_modifier_t (*GetKeyModifiers)( void );
	// called from engine side
	void	(*Startup)( int width, int height );
	void	(*Shutdown)( void );
	void	*(*GetPanel)( void );
	void	(*Paint)( void );
	void	(*Mouse)( enum VGUI_MouseAction action, int code );
	void	(*Key)( enum VGUI_KeyAction action, enum VGUI_KeyCode code );
	void	(*MouseMove)( int x, int y );
	void	(*TextInput)( const char *text );

	// called from vgui_support, appended later so older support libraries keep working
	void	(*SetPaintOffset)( int x, int y );	// translates 2D drawing for the everything that's not VGUI, used to simulate GoldSrc behavior which installs matrix in push/popMakeCurrent
} legacy_vguiapi_t;

// keep old name for existing support libraries source code
#define vguiapi_t legacy_vguiapi_t

typedef void (*LEGACY_VGUISUPPORTAPI)( legacy_vguiapi_t *api );
#define LEGACY_GET_VGUI_SUPPORT_API        "InitAPI"             // exported by vgui_support library
#define LEGACY_CLIENT_GET_VGUI_SUPPORT_API "InitVGUISupportAPI"  // exported by client library

// VGUI support API changelog:
// 1. Initial revision
#define VGUI_SUPPORT_API_VERSION 1

// VGUI support API versioning rules:
// * engine calls GET_VGUI_SUPPORT_API export with the highest version it supports
// * support library returns the version it's going to use, which must not be higher than
//   the one engine passed, or 0 if it can't work with this engine at all
// * engine zeroes the interface table before the call, support library fills only fields
//   that are present in the negotiated version, engine never calls NULL functions
// * engine functions table is owned by the engine and stays valid until the library is unloaded,
//   support library must not use the fields that aren't present in the negotiated version
// * both structures are append only, never remove or reorder fields, never change their signature

// engine functions, called from support library
typedef struct vgui_support_api_s
{
	// version 1
	void	(*DrawInit)( void );
	void	(*DrawShutdown)( void );
	void	(*SetupDrawingText)( int *pColor );
	void	(*SetupDrawingRect)( int *pColor );
	void	(*SetupDrawingImage)( int *pColor );
	void	(*BindTexture)( int id );
	void	(*EnableTexture)( int enable );
	void	(*UploadTexture)( int id, const char *buffer, int width, int height );
	void	(*DrawQuad)( const vpoint_t *ul, const vpoint_t *lr );
	void	(*GetTextureSizes)( int *width, int *height );
	int		(*GenerateTexture)( void );
	void	*(*EngineMalloc)( size_t size );
	void	(*CursorSelect)( VGUI_DefaultCursor cursor );
	unsigned char	(*GetColor)( int i, int j );
	int		(*IsInGame)( void );
	void	(*EnableTextInput)( int enable, int force );
	void	(*GetCursorPos)( int *x, int *y );
	int		(*ProcessUtfChar)( int ch );
	int		(*GetClipboardText)( char *buffer, size_t bufferSize );
	void	(*SetClipboardText)( const char *text );
	key_modifier_t (*GetKeyModifiers)( void );
	void	(*SetPaintOffset)( int x, int y ); // translates 2D drawing for the everything that's not VGUI, used to simulate GoldSrc behavior which installs matrix in push/popMakeCurrent
	void	*(*LoadLib)( const char *dllname, int build_ordinals_table, int directpath );
	void	(*FreeLib)( void *hInstance );
	void	*(*GetProcAddr)( void *hInstance, const char *name );
} vgui_support_api_t;

// support library functions, called from engine
typedef struct vgui_support_interface_s
{
	// version 1
	void	(*Startup)( int width, int height ); // called on startup and on every resolution change
	void	(*Shutdown)( void );
	void	*(*GetPanel)( void );
	void	(*Paint)( void );
	void	(*Mouse)( enum VGUI_MouseAction action, int code );
	void	(*Key)( enum VGUI_KeyAction action, enum VGUI_KeyCode code );
	void	(*MouseMove)( int x, int y );
	void	(*TextInput)( const char *text );
	void	(*ClientStartup)( void *clientInstance, int width, int height ); // called once after client library has been loaded
} vgui_support_interface_t;

typedef int (*VGUISUPPORTAPI)( int version, vgui_support_interface_t *pFunctionTable, const vgui_support_api_t *engfuncs );
#define GET_VGUI_SUPPORT_API "GetVGUISupportAPI" // exported by vgui_support library or client library

#endif // VGUI_API_H
