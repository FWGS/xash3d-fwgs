/*
platform.h - common platform-dependent function defines
Copyright (C) 2018 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#pragma once
#ifndef PLATFORM_H
#define PLATFORM_H

/* 
============================================================================== 
 
			MOBILE API 
 
============================================================================== 
*/
void Platform_Vibrate( float life, char flags );
void*Platform_GetNativeObject( const char *name );

/* 
============================================================================== 
 
			INPUT 
 
============================================================================== 
*/
// Gamepad support
int Platform_JoyInit( int numjoy ); // returns number of connected gamepads, negative if error
// Text input
void Platform_EnableTextInput( qboolean enable );
// System events
void Platform_RunEvents( void );
// Mouse
void Platform_GetMousePos( int *x, int *y );
void Platform_SetMousePos( int x, int y );
// Clipboard
void Platform_GetClipboardText( char *buffer, size_t size );
void Platform_SetClipboardText( char *buffer, size_t size );

/* 
============================================================================== 
 
			WINDOW MANAGEMENT 
 
============================================================================== 
*/
typedef enum
{
	rserr_ok,
	rserr_invalid_fullscreen,
	rserr_invalid_mode,
	rserr_unknown
} rserr_t;

typedef struct vidmode_s vidmode_t;

// Window
qboolean  R_Init_Video( void );
void      R_Free_Video( void );
qboolean  VID_SetMode( void );
rserr_t   R_ChangeDisplaySettings( int width, int height, qboolean fullscreen );
int       R_MaxVideoModes();
vidmode_t*R_GetVideoMode( int num );
void*     GL_GetProcAddress( const char *name ); // RenderAPI requirement



#endif // PLATFORM_H
