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

#include <errno.h>
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
void Platform_SetStatus( const char *status );
qboolean Platform_DebuggerPresent( void );

// legacy iOS port functions
#if TARGET_OS_IOS
const char *IOS_GetDocsDir( void );
void IOS_LaunchDialog( void );
#endif // TARGET_OS_IOS

#if XASH_WIN32 || XASH_LINUX
#define XASH_PLATFORM_HAVE_STATUS 1
#else
#undef XASH_PLATFORM_HAVE_STATUS
#endif

#if XASH_POSIX
void Posix_Daemonize( void );
void Posix_SetupSigtermHandling( void );
char *Posix_Input( void );
#endif

#if XASH_SDL
void SDLash_Init( const char *basedir );
void SDLash_Shutdown( void );
void SDLash_NanoSleep( int nsec );
#endif

#if XASH_ANDROID
const char *Android_GetAndroidID( void );
const char *Android_LoadID( void );
void Android_SaveID( const char *id );
void Android_Init( void );
void *Android_GetNativeObject( const char *name );
int Android_GetKeyboardHeight( void );
void Android_Shutdown( void );
#endif

#if XASH_WIN32
void Win32_Init( qboolean con_showalways );
void Win32_Shutdown( void );
qboolean Win32_NanoSleep( int nsec );
void Wcon_CreateConsole( qboolean con_showalways );
void Wcon_DestroyConsole( void );
void Wcon_InitConsoleCommands( void );
void Wcon_ShowConsole( qboolean show );
void Wcon_DisableInput( void );
char *Wcon_Input( void );
void Wcon_WinPrint( const char *pMsg );
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
int Linux_GetProcessID( void );
#endif

static inline void Platform_Init( qboolean con_showalways, const char *basedir )
{
#if XASH_POSIX
	// daemonize as early as possible, because we need to close our file descriptors
	Posix_Daemonize( );
#endif

#if XASH_SDL
	SDLash_Init( basedir );
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
	Win32_Init( con_showalways );
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
	Win32_Shutdown( );
#elif XASH_LINUX
	Linux_Shutdown( );
#endif

#if XASH_SDL
	SDLash_Shutdown( );
#endif
}

static inline qboolean Sys_DebuggerPresent( void )
{
#if XASH_LINUX || XASH_WIN32
	return Platform_DebuggerPresent();
#else
	return false;
#endif
}

static inline void Platform_SetupSigtermHandling( void )
{
#if XASH_POSIX
	Posix_SetupSigtermHandling( );
#endif
}

static inline qboolean Platform_NanoSleep( int nsec )
{
#if XASH_SDL == 3
	SDLash_NanoSleep( nsec );
	return true;
	// SDL2 doesn't have nanosleep, so use low-level functions here
#elif XASH_POSIX
	struct timespec ts = {
		.tv_sec = 0,
		.tv_nsec = nsec, // just don't put large numbers here
	};
	int ret = nanosleep( &ts, NULL );
	if( ret < 0 )
		return errno == EINTR; // ignore EINTR error, it just means sleep was interrupted
	return true;
#elif XASH_WIN32
	return Win32_NanoSleep( nsec );
#else
	return false;
#endif
}

#if XASH_WIN32 || XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX || XASH_APPLE
void Sys_SetupCrashHandler( const char *argv0 );
void Sys_RestoreCrashHandler( void );
#else
static inline void Sys_SetupCrashHandler( const char *argv0 )
{
}

static inline void Sys_RestoreCrashHandler( void )
{
}
#endif


/*
==============================================================================

			MOBILE API

==============================================================================
*/
#if XASH_SDL >= 2
void Platform_Vibrate( float life, char flags ); // left for compatibility
void Platform_Vibrate2( float time, int low_freq, int high_freq, uint flags );
#else
static inline void Platform_Vibrate( float life, char flags ) {}
static inline void Platform_Vibrate2( float time, int low_freq, int high_freq, uint flags ) {}
#endif

/*
==============================================================================

			INPUT

==============================================================================
*/
#if XASH_SDL // only SDL based backends implements these functions
void Platform_PreCreateMove( void );
void GAME_EXPORT Platform_GetMousePos( int *x, int *y );
void GAME_EXPORT Platform_SetMousePos( int x, int y );
qboolean Platform_GetMouseGrab( void );
void Platform_SetMouseGrab( qboolean enable );
void Platform_SetCursorType( VGUI_DefaultCursor type );
int Platform_GetClipboardText( char *buffer, size_t size );
void Platform_SetClipboardText( const char *buffer );
#else
static inline void Platform_PreCreateMove( void ) { }
static inline void GAME_EXPORT Platform_SetMousePos( int x, int y ) { }
static inline void Platform_SetMouseGrab( qboolean enable ) { }
static inline void Platform_SetCursorType( VGUI_DefaultCursor type ) { }
static inline int Platform_GetClipboardText( char *buffer, size_t size ) { return 0; }
static inline void Platform_SetClipboardText( const char *buffer ) { }
static inline qboolean Platform_GetMouseGrab( void ) { return false; }
static inline void GAME_EXPORT Platform_GetMousePos( int *x, int *y )
{
	if( x ) *x = 0;
	if( y ) *y = 0;
}
#endif

#if XASH_SDL || XASH_DOS
void Platform_RunEvents( void );
void Platform_MouseMove( float *x, float *y );
#else
static inline void Platform_RunEvents( void ) { }
static inline void Platform_MouseMove( float *x, float *y )
{
	if( x ) *x = 0.0f;
	if( y ) *y = 0.0f;
}
#endif

#if XASH_SDL >= 2 || XASH_PSVITA || XASH_DOS || XASH_USE_EVDEV
void Platform_EnableTextInput( qboolean enable );
#else
static inline void Platform_EnableTextInput( qboolean enable ) { }
#endif

#if XASH_SDL >= 2
int Platform_JoyInit( void ); // returns number of connected gamepads, negative if error
void Platform_JoyShutdown( void );
void Platform_CalibrateGamepadGyro( void );
key_modifier_t Platform_GetKeyModifiers( void );
#else
static inline int Platform_JoyInit( void ) { return 0; }
static inline void Platform_JoyShutdown( void ) { }
static inline void Platform_CalibrateGamepadGyro( void ) { }
static inline key_modifier_t Platform_GetKeyModifiers( void ) { return KeyModifier_None; }
#endif

static inline void Platform_SetTimer( float time )
{
#if XASH_LINUX
	Linux_SetTimer( time );
#endif
}

static inline char *Platform_Input( void )
{
#if XASH_WIN32
	return Wcon_Input();
#elif XASH_POSIX && !XASH_MOBILE_PLATFORM && !XASH_LOW_MEMORY
	return Posix_Input();
#else
	return NULL;
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
typedef enum ref_window_type_e ref_window_type_t;

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
void Platform_Minimize_f( void );
ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type );

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

// this allows to make break in current line, without entering libc code
// libc built with -fomit-frame-pointer may just eat stack frame (hello, glibc), making entering libc even more useless
// calling syscalls directly allows to make break like if it was asm("int $3") on x86
#if XASH_LINUX && XASH_X86
	#define INLINE_RAISE(x) asm volatile( "int $3;" );
	#define INLINE_NANOSLEEP1() // nothing!
#elif XASH_LINUX && XASH_ARM && !XASH_64BIT
	#include <sys/syscall.h>
	#include <sys/types.h>
	#define INLINE_RAISE(x) do \
		{ \
			int raise_pid = getpid(); \
			pid_t raise_tid = Linux_GetProcessID(); \
			int raise_sig = (x); \
			__asm__ volatile (  \
				"mov r7,#268\n\t" \
				"mov r0,%0\n\t" \
				"mov r1,%1\n\t" \
				"mov r2,%2\n\t" \
				"svc 0\n\t" \
				: \
				: "r"(raise_pid), "r"(raise_tid), "r"(raise_sig) \
				: "r0", "r1", "r2", "r7", "memory" \
			); \
		} while( 0 )
	#define INLINE_NANOSLEEP1() do \
		{ \
			struct timespec ns_t1 = {1, 0}; \
			struct timespec ns_t2 = {0, 0}; \
			__asm__ volatile ( \
				"mov r7,#162\n\t" \
				"mov r0,%0\n\t" \
				"mov r1,%1\n\t" \
				"svc 0\n\t" \
				: \
				: "r"(&ns_t1), "r"(&ns_t2) \
				: "r0", "r1", "r7", "memory" \
			); \
		} while( 0 )
#elif XASH_LINUX && XASH_ARM && XASH_64BIT
	#include <sys/syscall.h>
	#include <sys/types.h>
	#define INLINE_RAISE(x) do \
		{ \
			int raise_pid = getpid(); \
			pid_t raise_tid = Linux_GetProcessID(); \
			int raise_sig = (x); \
			__asm__ volatile ( \
				"mov x8,#131\n\t" \
				"mov x0,%0\n\t" \
				"mov x1,%1\n\t" \
				"mov x2,%2\n\t" \
				"svc 0\n\t" \
				: \
				: "r"(raise_pid), "r"(raise_tid), "r"(raise_sig) \
				: "x0", "x1", "x2", "x8", "memory", "cc" \
			); \
		} while( 0 )
	#define INLINE_NANOSLEEP1() do \
		{ \
			struct timespec ns_t1 = {1, 0}; \
			struct timespec ns_t2 = {0, 0}; \
			__asm__ volatile ( \
				"mov x8,#101\n\t" \
				"mov x0,%0\n\t" \
				"mov x1,%1\n\t" \
				"svc 0\n\t" \
				: \
				: "r"(&ns_t1), "r"(&ns_t2) \
				: "x0", "x1", "x8", "memory", "cc" \
			); \
		} while( 0 )
#elif XASH_LINUX
	#if defined( __NR_tgkill )
		#define INLINE_RAISE(x) syscall( __NR_tgkill, getpid(), Linux_GetProcessID(), x )
	#else // __NR_tgkill
		#define INLINE_RAISE(x) raise(x)
	#endif // __NR_tgkill
	#define INLINE_NANOSLEEP1() do \
		{ \
			struct timespec ns_t1 = {1, 0}; \
			struct timespec ns_t2 = {0, 0}; \
			nanosleep( &ns_t1, &ns_t2 ); \
		} while( 0 )
#else // generic
	#define INLINE_RAISE(x) raise(x)
	#define INLINE_NANOSLEEP1() sleep(1)
#endif // generic

#endif // PLATFORM_H
