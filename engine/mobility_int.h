/*
mobility_int.h - interface between engine and client for mobile platforms
Copyright (C) 2015 a1batross

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

#pragma once
#ifndef MOBILITY_INT_H
#define MOBILITY_INT_H
#ifdef __cplusplus
extern "C" {
#endif

#define MOBILITY_API_VERSION 2
#define MOBILITY_CLIENT_EXPORT "HUD_MobilityInterface"

#define VIBRATE_NORMAL (1U << 0) // just vibrate for given "life"

#define TOUCH_FL_HIDE			(1U << 0)
#define TOUCH_FL_NOEDIT			(1U << 1)
#define TOUCH_FL_CLIENT			(1U << 2)
#define TOUCH_FL_MP				(1U << 3)
#define TOUCH_FL_SP				(1U << 4)
#define TOUCH_FL_DEF_SHOW		(1U << 5)
#define TOUCH_FL_DEF_HIDE		(1U << 6)
#define TOUCH_FL_DRAW_ADDITIVE	(1U << 7)
#define TOUCH_FL_STROKE			(1U << 8)
#define TOUCH_FL_PRECISION		(1U << 9)

// flags for COM_ParseFileSafe
#define PFILE_IGNOREBRACKET (1<<0)
#define PFILE_HANDLECOLON   (1<<1)
#define PFILE_IGNOREHASHCMT (1<<2)

typedef struct mobile_engfuncs_s
{
	// indicates version of API. Should be equal to MOBILITY_API_VERSION
	// version changes when existing functions are changes
	int version;

	// vibration control
	// life -- time to vibrate in ms
	void (*pfnVibrate)( float life, char flags );

	// enable text input
	void (*pfnEnableTextInput)( int enable );

	// add temporaty button, edit will be disabled
	void (*pfnTouchAddClientButton)( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, unsigned char *color, int round, float aspect, int flags );

	// add button to defaults list. Will be loaded on config generation
	void (*pfnTouchAddDefaultButton)( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, unsigned char *color, int round, float aspect, int flags );

	// hide/show buttons by pattern
	void (*pfnTouchHideButtons)( const char *name, unsigned char hide );

	// remove button with given name
	void (*pfnTouchRemoveButton)( const char *name );

	// when enabled, only client buttons shown
	void (*pfnTouchSetClientOnly)( unsigned char state );

	// Clean defaults list
	void (*pfnTouchResetDefaultButtons)( void );

	// Draw scaled font for client
	int (*pfnDrawScaledCharacter)( int x, int y, int number, int r, int g, int b, float scale );

	void (*pfnSys_Warn)( const char *format, ... );

	// Get native object for current platform.
	// Pass NULL to arguments to receive an array of available objects or NULL if nothing
	void *(*pfnGetNativeObject)( const char *obj );

	void (*pfnSetCustomClientID)( const char *id );

	// COM_ParseFile but with buffer size limit, len reports written size or -1 on overflow
	char* (*pfnParseFile)( char *data, char *buf, const int size, unsigned int flags, int *len );
	// To be continued...
} mobile_engfuncs_t;

// function exported from client
// returns 0 on no error otherwise error
typedef int (*pfnMobilityInterface)( mobile_engfuncs_t *gMobileEngfuncs );

#ifdef __cplusplus
}
#endif
#endif
