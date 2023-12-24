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
double Platform_DoubleTime( void );
void Platform_Sleep( int msec );
void Platform_ShellExecute( const char *path, const char *parms );
void Platform_MessageBox( const char *title, const char *message, qboolean parentMainWindow );
qboolean Sys_DebuggerPresent( void ); // optional, see Sys_DebugBreak
void Platform_SetStatus( const char *status );

// legacy iOS port functions
#if TARGET_OS_IOS
const char *IOS_GetDocsDir( void );
#endif // TARGET_OS_IOS

#if XASH_WIN32 || XASH_LINUX
#define XASH_PLATFORM_HAVE_STATUS 1
#else
#undef XASH_PLATFORM_HAVE_STATUS
#endif

#if XASH_POSIX
void Posix_Daemonize( void );
#endif

#if XASH_SDL
void SDLash_Init( void );
void SDLash_Shutdown( void );
#endif

#if XASH_ANDROID
const char *Android_GetAndroidID( void );
const char *Android_LoadID( void );
void Android_SaveID( const char *id );
void Android_Init( void );
void *Android_GetNativeObject( const char *name );
#endif

#if XASH_WIN32
void Wcon_CreateConsole( void );
void Wcon_DestroyConsole( void );
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

#if XASH_DOS
void DOS_Init( void );
void DOS_Shutdown( void );
#endif

#if XASH_LINUX
void Linux_Init( void );
void Linux_Shutdown( void );
void Linux_SetTimer( float time );
#endif

static inline void Platform_Init( void )
{
#if XASH_POSIX
	// daemonize as early as possible, because we need to close our file descriptors
	Posix_Daemonize( );
#endif

#if XASH_SDL
	SDLash_Init( );
#endif

#if XASH_ANDROID
	Android_Init( );
#elif XASH_NSWITCH
	NSwitch_Init( );
#elif XASH_PSVITA
	PSVita_Init( );
#elif XASH_DOS
	DOS_Init( );
#elif XASH_WIN32
	Wcon_CreateConsole( );
#elif XASH_LINUX
	Linux_Init( );
#endif
}

static inline void Platform_Shutdown( void )
{
#if XASH_NSWITCH
	NSwitch_Shutdown( );
#elif XASH_PSVITA
	PSVita_Shutdown( );
#elif XASH_DOS
	DOS_Shutdown( );
#elif XASH_WIN32
	Wcon_DestroyConsole( );
#elif XASH_LINUX
	Linux_Shutdown( );
#endif

#if XASH_SDL
	SDLash_Shutdown( );
#endif
}

/*
==============================================================================

			MOBILE API

==============================================================================
*/
void Platform_Vibrate( float life, char flags );

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

static void Platform_SetTimer( float time )
{
#if XASH_LINUX
	Linux_SetTimer( time );
#endif
}

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
typedef enum window_mode_e window_mode_t;
// Window
qboolean  R_Init_Video( const int type );
void      R_Free_Video( void );
qboolean  VID_SetMode( void );
rserr_t   R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode );
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
