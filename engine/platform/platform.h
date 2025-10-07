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

#include "common.h"
#include "system.h"
#include "defaults.h"
#include "cursor_type.h"
#include "key_modifiers.h"

/*
==============================================================================

                       SYSTEM UTILS

==============================================================================
*/

void Platform_Init( void );
void Platform_Shutdown( void );
double Platform_DoubleTime( void );
void Platform_Sleep( int msec );
void Platform_ShellExecute( const char *path, const char *parms );
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow );
qboolean Sys_DebuggerPresent( void ); // optional, see Sys_DebugBreak

#if XASH_ANDROID
const char *Android_GetAndroidID( void );
const char *Android_LoadID( void );
void Android_SaveID( const char *id );
void Android_Init( void );
void *Android_GetNativeObject( const char *name );
int Android_GetKeyboardHeight( void );
#endif

#if XASH_WIN32
void Platform_UpdateStatusLine( void );
#else
static inline void Platform_UpdateStatusLine( void ) { }
#endif

#if XASH_NSWITCH
void NSwitch_Init( void );
void NSwitch_Shutdown( void );
#endif

#if XASH_PSVITA
void PSVita_Init( void );
void PSVita_Shutdown( void );
qboolean PSVita_GetBasePath( char *buf, const size_t buflen );
int PSVita_GetArgv( int in_argc, char **in_argv, char ***out_argv );
void PSVita_InputUpdate( void );
#endif

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
key_modifier_t Platform_GetKeyModifiers( void );
// System events
void Platform_RunEvents( void );
// Mouse
void Platform_GetMousePos( int *x, int *y );
void Platform_SetMousePos( int x, int y );
void Platform_PreCreateMove( void );
void Platform_MouseMove( float *x, float *y );
void Platform_SetCursorType( VGUI_DefaultCursor type );
// Clipboard
int Platform_GetClipboardText( char *buffer, size_t size );
void Platform_SetClipboardText( const char *buffer );

#if XASH_SDL == 12
#define SDL_SetWindowGrab( wnd, state ) SDL_WM_GrabInput( (state) )
#define SDL_MinimizeWindow( wnd ) SDL_WM_IconifyWindow()
#define SDL_IsTextInputActive() host.textmode
#endif

#if !XASH_SDL
#define SDL_VERSION_ATLEAST( x, y, z ) 0
#endif

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

struct vidmode_s;
// Window
qboolean  R_Init_Video( const int type );
void      R_Free_Video( void );
qboolean  VID_SetMode( void );
rserr_t   R_ChangeDisplaySettings( int width, int height, qboolean fullscreen );
int       R_MaxVideoModes( void );
struct vidmode_s *R_GetVideoMode( int num );
void*     GL_GetProcAddress( const char *name ); // RenderAPI requirement
void      GL_UpdateSwapInterval( void );
int GL_SetAttribute( int attr, int val );
int GL_GetAttribute( int attr, int *val );
void GL_SwapBuffers( void );
void *SW_LockBuffer( void );
void SW_UnlockBuffer( void );
qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b );


//
// in_evdev.c
//
#if XASH_USE_EVDEV
void Evdev_SetGrab( qboolean grab );
void Evdev_Shutdown( void );
void Evdev_Init( void );
void IN_EvdevMove( float *yaw, float *pitch );
void IN_EvdevFrame ( void );
#endif // XASH_USE_EVDEV
/*
==============================================================================

			AUDIO INPUT/OUTPUT

==============================================================================
*/
// initializes cycling through a DMA buffer and returns information on it
qboolean SNDDMA_Init( void );
void SNDDMA_Shutdown( void );
void SNDDMA_BeginPainting( void );
void SNDDMA_Submit( void );
void SNDDMA_Activate( qboolean active ); // pause audio
// void SNDDMA_PrintDeviceName( void ); // unused
// void SNDDMA_LockSound( void ); // unused
// void SNDDMA_UnlockSound( void ); // unused

qboolean VoiceCapture_Init( void );
void VoiceCapture_Shutdown( void );
qboolean VoiceCapture_Activate( qboolean activate );
qboolean VoiceCapture_Lock( qboolean lock );

#endif // PLATFORM_H
