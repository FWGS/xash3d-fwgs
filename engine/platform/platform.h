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
#define FSCALLBACK_OVERRIDE_MALLOC_LIKE
#include "fscallback.h"
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

typedef enum
{
	ORIENTATION_UNKNOWN = 0,
	ORIENTATION_LANDSCAPE,
	ORIENTATION_LANDSCAPE_FLIPPED,
	ORIENTATION_PORTRAIT,
	ORIENTATION_PORTRAIT_FLIPPED
} platform_orientation_t;

platform_orientation_t Platform_GetDisplayOrientation( void );

// legacy iOS port functions
#if XASH_IOS
int IOS_GetArgs( char ***argv );
const char *IOS_GetDocsDir( void );
const char *IOS_GetExecDir( void );
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
void SDLash_Init( void );
void SDLash_Shutdown( void );
void SDLash_NanoSleep( int nsec );
qboolean SDLash_GyroIsAvailable( void );
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

#if XASH_GAMECUBE
typedef enum
{
	/* Chronological boot order (monotonic GC_ReportBootPhase). */
	GC_BOOT_NONE = 0,
	GC_BOOT_EARLY,
	GC_BOOT_ENGINE,		/* server/core ready; about to CL_Init */
	GC_BOOT_RENDERER,
	GC_BOOT_SW_FB,
	GC_BOOT_MENU,		/* SCR_Init / fallback menu during VID_Init */
	GC_BOOT_CLIENT,		/* CL_Init complete */
	GC_BOOT_INTRO,
	GC_BOOT_MAP
} gc_boot_phase_t;

void GCube_EarlyInit( void );
void GC_EarlyBootSplash( void );
void GCube_Init( void );
void GCube_Shutdown( void );
qboolean GCube_GetBasePath( char *buf, size_t buflen );
qboolean GCube_GetDiscPath( char *buf, size_t buflen );
qboolean GCube_GetWritablePath( char *buf, size_t buflen );
qboolean GCube_HasWritableStorage( void );
qboolean GCube_HasPersistentWritableStorage( void );
void GCube_EnsureWritableLayout( void );
int GCube_GetArgv( int in_argc, char **in_argv, char ***out_argv );
int GC_GetVisualQuality( void );
const char *GC_GetQualityProfileName( void );
void GC_ReportQualityProfile( const char *stage );
void GC_ReportBootPhase( gc_boot_phase_t phase );
gc_boot_phase_t GC_GetBootPhase( void );
const char *GC_GetBootPhaseName( gc_boot_phase_t phase );
qboolean GC_BootDrawAllowed( void );
qboolean GC_IsFrameBudgetProbeActive( void );
qboolean GC_ShouldUseLightPresent( void );
void GC_NoteLightPresentFrame( void );
void GC_FillBudgetProbeFrameBuffer( void );
void GC_PresentBudgetProbeFrame( void );
qboolean GC_PrepareNewGameWorldPresent( void );
qboolean GC_IsNewGameWorldReady( void );
qboolean GC_IsNewGameG36Done( void );
int GC_GXDrawIntroTrain( void );
struct model_s *GC_GetWorldModel( void );
int GC_GetTramFaceCount( void ); /* G277: capture-baked *12 faces */
int GC_GetTramFaceVerts( int index, float out[][3], int maxverts );
qboolean GC_TramLightmapReady( void );
int GC_GetTramDiffuseTexnum( void );
const unsigned short *GC_GetTramLightmapAtlas( int *w, int *h );
void GC_GetTramLightmapUV( int face, float s, float t, float *out_s, float *out_t );
/* G151: Flipper GX world draw (live); soft spans remain for DumpFrames. */
qboolean GC_UseGxWorldDraw( void );
qboolean GC_UseGxRenderer( void );
void GC_MarkGxWorldEfbReady( void );
void GC_EnableGxWorldLive( void );
void *GC_GetGxVideoMode( void );
int GC_GetNewGameViewCluster( void ); /* G83: load/prepare-time PointInLeaf cluster, or -1 */
qboolean GC_HasNewGameCachedVis( void ); /* G83: prepare-time FatPVS + parent mark ready */
qboolean GC_ApplyNewGameCachedVis( int visframe ); /* stamp cached PVS without tree walks */
/* G132: stamp msurface visframe from capture-time bits (marksurfaces dangle after scratch). */
void GC_ApplyNewGameSurfVis( int surf_frame );
qboolean GC_WorldSurfacesLive( void ); /* G213: live Flipper beyond 320-cap */
int GC_GetLiveFaceCount( void );
qboolean GC_LiveFaceIsCapped( int index ); /* G214 */
int GC_GetLiveFaceVerts( int index, float out[][3], int maxverts ); /* G216 */
int GC_GetLiveFaceBakeSrc( int index ); /* G219: 1=edge 2=plane 3=tex 0=none */
int GC_GetFillFaceCount( void ); /* G222/G225: MEM1 + ARAM flat-fill beyond 320+192 */
int GC_GetFillFaceVerts( int index, float out[][3], int maxverts );
qboolean GC_FillFacePlane( int index, struct mplane_s *out, int *out_flags );
struct msurface_s *GC_GetLiveDrawSurfs( void ); /* always NULL — use Fill */
qboolean GC_FillLiveDrawSurf( int index, struct msurface_s *out, mtexinfo_t *tex_out );
int GC_GetNewGameCapFaceCount( void );
int GC_GetNewGameCapGeneration( void ); /* G179: bumps when cap faces/LMs rewrite */
int GC_GetNewGameCapBakeSrc( int slot ); /* G204/G205: 1=edge, 2=plane, 3=tex, 0=none */
struct msurface_s *GC_GetNewGameDrawSurfs( void );
const unsigned short *GC_GetNewGameCapLightmap( int slot, int *w, int *h );
const unsigned short *GC_GetNewGameCapLightmapAtlas( int *w, int *h ); /* G180 */
void GC_GetNewGameCapLightmapAtlasUV( int slot, float s, float t, float *out_s, float *out_t );
void GC_CaptureNewGamePVS( void ); /* G83: PointInLeaf+FatPVS before scratch reuse */
void GC_CaptureNewGamePVSFromModel( model_t *wmodel );
qboolean GC_UpdateNewGamePVSForOrigin( const float *org ); /* G89: select cluster row by AABB */
/* G90: one world GL_RenderFrame without R_Begin/EndFrame (V_Pre/Post own the frame). */
qboolean GC_RenderNewGameWorldPassNoFrame( qboolean draw_viewmodel );
/* G91: play one local gameplay SFX after New Game world present. */
void GC_PlayNewGameGameplaySound( void );
void GC_DrawLoadingStatus( const char *message, const char *details );
void GC_SetLoadingProgress( float progress );
float GC_GetLoadingProgress( void );
/* G92: free PVS/screens sticky flags so changelevel can re-capture. */
void GC_ResetNewGameWorldForChangelevel( void );
void GC_MarkNewGameWorldStale( void );
void GC_G94ApplyPendingRestore( void );
qboolean GC_RenderNewGameWorldFrames( int count );
/* G105: after landmark Deploy, bind + present the first-person viewmodel once. */
void GC_PresentLandmarkViewModel( void );
/* G188: landmark reposition after put-in wants a cap-face refresh + marker. */
void GC_NewGameNotifyLandmarkReposition( void );
/* G86: fill a post-G36 New Game usercmd from probe-synthetic or live PAD. */
#include "q_client.h"
qboolean GC_FillNewGameMoveUsercmd( usercmd_t *cmd, const float *cur_angles );
void GC_ArmPostMapFrameBudgetSamples( void );
void GC_RestoreVideoMemoryAfterMapLoad( void );
#endif

#if XASH_LINUX
void Linux_Init( void );
void Linux_Shutdown( void );
void Linux_SetTimer( float time );
int Linux_GetProcessID( void );
#endif

static inline void Platform_Init( qboolean con_showalways )
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
#elif XASH_GAMECUBE
	GCube_Init( );
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
#elif XASH_GAMECUBE
	GCube_Shutdown( );
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

static inline qboolean Platform_LibraryExists( const char *name, qboolean gamedironly )
{
#if XASH_ANDROID
	// when libs come from a separate APK (cs16client, tf15client, …) we can't see them
	// from the VFS; trust the launcher.
	if( !COM_StringEmptyOrNULL( getenv( "XASH3D_GAMELIBDIR" )))
		return true;
#endif
	// FIXME: use FS_FindLibrary
	return g_fsapi.FileExists( name, gamedironly );
}


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

#if XASH_SDL || XASH_DOS || XASH_GAMECUBE
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

#if XASH_SDL >= 2 || XASH_GAMECUBE
int Platform_JoyInit( void ); // returns number of connected gamepads, negative if error
void Platform_JoyShutdown( void );
#if XASH_SDL >= 2
void Platform_CalibrateGamepadGyro( void );
key_modifier_t Platform_GetKeyModifiers( void );
#else
static inline void Platform_CalibrateGamepadGyro( void ) { }
static inline key_modifier_t Platform_GetKeyModifiers( void ) { return KeyModifier_None; }
#endif
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
	rserr_invalid_context,
	rserr_unknown
} rserr_t;

struct vidmode_s;
typedef enum window_mode_e window_mode_t;
typedef enum ref_window_type_e ref_window_type_t;
typedef enum ref_graphic_apis_e ref_graphic_apis_t;

// Window
qboolean  R_Init_Video( ref_graphic_apis_t type );
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
void VID_Info_f( void );

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
				"push {r7}\n\t" \
				"mov r7,#268\n\t" \
				"mov r0,%0\n\t" \
				"mov r1,%1\n\t" \
				"mov r2,%2\n\t" \
				"svc 0\n\t" \
				"pop {r7}\n\t" \
				: \
				: "r"(raise_pid), "r"(raise_tid), "r"(raise_sig) \
				: "r0", "r1", "r2", "memory" \
			); \
		} while( 0 )
	#define INLINE_NANOSLEEP1() do \
		{ \
			struct timespec ns_t1 = {1, 0}; \
			struct timespec ns_t2 = {0, 0}; \
			__asm__ volatile ( \
				"push {r7}\n\t" \
				"mov r7,#162\n\t" \
				"mov r0,%0\n\t" \
				"mov r1,%1\n\t" \
				"svc 0\n\t" \
				"pop {r7}\n\t" \
				: \
				: "r"(&ns_t1), "r"(&ns_t2) \
				: "r0", "r1", "memory" \
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
