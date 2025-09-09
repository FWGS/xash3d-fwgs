/*
input.c - win32 input devices
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if XASH_SDL == 2
#include <SDL.h>
#elif XASH_SDL == 3
#include <SDL3/SDL.h>
#endif

#include "common.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "cursor_type.h"
#include "platform/platform.h"

void*		in_mousecursor;
qboolean	in_mouseactive;				// false when not focus app
qboolean	in_mouseinitialized;
qboolean	in_mouse_suspended;
POINT		in_lastvalidpos;
qboolean	in_mouse_savedpos;
static int in_mstate = 0;
static struct inputstate_s
{
	float lastpitch, lastyaw;
} inputstate;

CVAR_DEFINE_AUTO( m_pitch, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse pitch value" );
CVAR_DEFINE_AUTO( m_yaw, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse yaw value" );
CVAR_DEFINE_AUTO( m_ignore, DEFAULT_M_IGNORE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "ignore mouse events" );
static CVAR_DEFINE_AUTO( look_filter, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "filter look events making it smoother" );
static CVAR_DEFINE_AUTO( m_rawinput, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable mouse raw input" );

static CVAR_DEFINE_AUTO( cl_forwardspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default forward move speed" );
static CVAR_DEFINE_AUTO( cl_backspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default back move speed"  );
static CVAR_DEFINE_AUTO( cl_sidespeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default side move speed"  );

static CVAR_DEFINE_AUTO( m_grab_debug, "0", FCVAR_PRIVILEGED, "show debug messages on mouse state change" );

/*
================
IN_CollectInputDevices

Returns a bit mask representing connected devices or, at least, enabled
================
*/
uint IN_CollectInputDevices( void )
{
	uint ret = 0;

	if( !m_ignore.value ) // no way to check is mouse connected, so use cvar only
		ret |= INPUT_DEVICE_MOUSE;

	if( touch_enable.value )
		ret |= INPUT_DEVICE_TOUCH;

	if( Joy_IsActive() ) // connected or enabled
		ret |= INPUT_DEVICE_JOYSTICK;

	Con_Reportf( "Connected devices: %s%s%s%s\n",
		FBitSet( ret, INPUT_DEVICE_MOUSE )    ? "mouse " : "",
		FBitSet( ret, INPUT_DEVICE_TOUCH )    ? "touch " : "",
		FBitSet( ret, INPUT_DEVICE_JOYSTICK ) ? "joy " : "",
		FBitSet( ret, INPUT_DEVICE_VR )       ? "vr " : "");

	return ret;
}

/*
=================
IN_LockInputDevices

tries to lock any possibilty to connect another input device after
player is connected to the server
=================
*/
void IN_LockInputDevices( qboolean lock )
{
	extern convar_t joy_enable; // private to input system

	if( lock )
	{
		SetBits( m_ignore.flags, FCVAR_READ_ONLY );
		SetBits( joy_enable.flags, FCVAR_READ_ONLY );
		SetBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
	else
	{
		ClearBits( m_ignore.flags, FCVAR_READ_ONLY );
		ClearBits( joy_enable.flags, FCVAR_READ_ONLY );
		ClearBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
}


/*
===========
IN_StartupMouse
===========
*/
static void IN_StartupMouse( void )
{
	Cvar_RegisterVariable( &m_ignore );

	Cvar_RegisterVariable( &m_pitch );
	Cvar_RegisterVariable( &m_yaw );
	Cvar_RegisterVariable( &look_filter );
	Cvar_RegisterVariable( &m_rawinput );
	Cvar_RegisterVariable( &m_grab_debug );

	// You can use -nomouse argument to prevent using mouse from client
	// -noenginemouse will disable all mouse input
	if( Sys_CheckParm(  "-noenginemouse" )) return;

	in_mouseinitialized = true;
}

/*
===========
IN_MouseSavePos

Save mouse pos before state change e.g. changelevel
===========
*/
void IN_MouseSavePos( void )
{
	if( !in_mouseactive )
		return;

	Platform_GetMousePos( &in_lastvalidpos.x, &in_lastvalidpos.y );
	in_mouse_savedpos = true;
}

/*
===========
IN_MouseRestorePos

Restore right position for background
===========
*/
void IN_MouseRestorePos( void )
{
	if( !in_mouse_savedpos )
		return;

	Platform_SetMousePos( in_lastvalidpos.x, in_lastvalidpos.y );

	in_mouse_savedpos = false;
}

/*
===========
IN_ToggleClientMouse

Called when key_dest is changed
===========
*/
void IN_ToggleClientMouse( int newstate, int oldstate )
{
	if( newstate == oldstate )
		return;

	// since SetCursorType controls cursor visibility
	// execute it first, and then check mouse grab state
	if( newstate == key_menu || newstate == key_console )
	{
		Platform_SetCursorType( dc_arrow );

#if XASH_USE_EVDEV
		Evdev_SetGrab( false );
#endif
	}
	else
	{
		Platform_SetCursorType( dc_none );

#if XASH_USE_EVDEV
		Evdev_SetGrab( true );
#endif
	}

	// don't leave the user without cursor if they enabled m_ignore
	if( m_ignore.value )
		return;

	if( oldstate == key_game )
	{
		IN_DeactivateMouse();
	}
	else if( newstate == key_game )
	{
		IN_ActivateMouse();
	}
}

void IN_SetRelativeMouseMode( qboolean set )
{
	static qboolean s_bRawInput;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_TRUE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, true );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2
		s_bRawInput = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_FALSE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, false );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2

		s_bRawInput = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

void IN_SetMouseGrab( qboolean set )
{
	static qboolean s_bMouseGrab;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bMouseGrab )
	{
		Platform_SetMouseGrab( true );

		s_bMouseGrab = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bMouseGrab )
	{
		Platform_SetMouseGrab( false );

		s_bMouseGrab = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

static void IN_CheckMouseState( qboolean active )
{
	qboolean use_raw_input;

#if XASH_WIN32
	use_raw_input = ( m_rawinput.value && clgame.client_dll_uses_sdl ) || clgame.dllFuncs.pfnLookEvent != NULL;
#else
	use_raw_input = true; // always use SDL code
#endif

	if( m_ignore.value )
		active = false;

	if( active && use_raw_input && !host.mouse_visible && cls.state == ca_active )
		IN_SetRelativeMouseMode( true );
	else
		IN_SetRelativeMouseMode( false );

	if( active && !host.mouse_visible && cls.state == ca_active )
		IN_SetMouseGrab( true );
	else
		IN_SetMouseGrab( false );
}

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( true );
	if( clgame.dllFuncs.IN_ActivateMouse )
		clgame.dllFuncs.IN_ActivateMouse();
	in_mouseactive = true;
}

/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( false );
	if( clgame.dllFuncs.IN_DeactivateMouse )
		clgame.dllFuncs.IN_DeactivateMouse();
	in_mouseactive = false;
}



/*
================
IN_MouseMove
================
*/
static void IN_MouseMove( void )
{
	int x, y;

	if( !in_mouseinitialized )
		return;

	if( Touch_WantVisibleCursor( ))
	{
		// touch emulation overrides all input
		Touch_KeyEvent( 0, 0 );
		return;
	}

	// find mouse movement
	Platform_GetMousePos( &x, &y );

	VGui_MouseMove( x, y );

	// if the menu is visible, move the menu cursor
	UI_MouseMove( x, y );
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int key, int down )
{
	if( !in_mouseinitialized )
		return;

	if( down )
		SetBits( in_mstate, BIT( key ));
	else ClearBits( in_mstate, BIT( key ));

	// touch emulation overrides all input
	if( Touch_WantVisibleCursor( ))
	{
		Touch_KeyEvent( K_MOUSE1 + key, down );
	}
	else if( cls.key_dest == key_game )
	{
		// perform button actions
		VGui_MouseEvent( K_MOUSE1 + key, down );

		// don't do Key_Event here
		// client may override IN_MouseEvent
		// but by default it calls back to Key_Event anyway
		if( in_mouseactive )
			clgame.dllFuncs.IN_MouseEvent( in_mstate );
	}
	else
	{
		// perform button actions
		Key_Event( K_MOUSE1 + key, down );
	}
}

/*
==============
IN_MWheelEvent

direction is negative for wheel down, otherwise wheel up
==============
*/
void IN_MWheelEvent( int y )
{
	int b = y > 0 ? K_MWHEELUP : K_MWHEELDOWN;

	VGui_MWheelEvent( y );

	Key_Event( b, true );
	Key_Event( b, false );
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void )
{
	IN_DeactivateMouse( );

#if XASH_USE_EVDEV
	Evdev_Shutdown();
#endif

	Touch_Shutdown();
}


/*
===========
IN_Init
===========
*/
void IN_Init( void )
{
	Cvar_RegisterVariable( &cl_forwardspeed );
	Cvar_RegisterVariable( &cl_backspeed );
	Cvar_RegisterVariable( &cl_sidespeed );

	if( !Host_IsDedicated() )
	{
		IN_StartupMouse( );

		Joy_Init(); // common joystick support init

		Touch_Init();

#if XASH_USE_EVDEV
		Evdev_Init();
#endif
	}
}

/*
================
IN_JoyMove

Common function for engine joystick movement

	-1 < forwardmove < 1,	-1 < sidemove < 1

================
*/

#define F (1U << 0)	// Forward
#define B (1U << 1)	// Back
#define L (1U << 2)	// Left
#define R (1U << 3)	// Right
#define T (1U << 4)	// Forward stop
#define S (1U << 5)	// Side stop
static void IN_JoyAppendMove( usercmd_t *cmd, float forwardmove, float sidemove )
{
	static uint moveflags = T | S;

	if( forwardmove ) cmd->forwardmove  = forwardmove * cl_forwardspeed.value;
	if( sidemove ) cmd->sidemove  = sidemove * cl_sidespeed.value;

	if( forwardmove )
	{
		moveflags &= ~T;
	}
	else if( !( moveflags & T ) )
	{
		Cmd_ExecuteString( "-back" );
		Cmd_ExecuteString( "-forward" );
		moveflags |= T;
	}

	if( sidemove )
	{
		moveflags &= ~S;
	}
	else if( !( moveflags & S ) )
	{
		Cmd_ExecuteString( "-moveleft" );
		Cmd_ExecuteString( "-moveright" );
		moveflags |= S;
	}

	if ( forwardmove > 0.7f && !( moveflags & F ))
	{
		moveflags |= F;
		Cmd_ExecuteString( "+forward" );
	}
	else if ( forwardmove < 0.7f && ( moveflags & F ))
	{
		moveflags &= ~F;
		Cmd_ExecuteString( "-forward" );
	}

	if ( forwardmove < -0.7f && !( moveflags & B ))
	{
		moveflags |= B;
		Cmd_ExecuteString( "+back" );
	}
	else if ( forwardmove > -0.7f && ( moveflags & B ))
	{
		moveflags &= ~B;
		Cmd_ExecuteString( "-back" );
	}

	if ( sidemove > 0.9f && !( moveflags & R ))
	{
		moveflags |= R;
		Cmd_ExecuteString( "+moveright" );
	}
	else if ( sidemove < 0.9f && ( moveflags & R ))
	{
		moveflags &= ~R;
		Cmd_ExecuteString( "-moveright" );
	}

	if ( sidemove < -0.9f && !( moveflags & L ))
	{
		moveflags |= L;
		Cmd_ExecuteString( "+moveleft" );
	}
	else if ( sidemove > -0.9f && ( moveflags & L ))
	{
		moveflags &= ~L;
		Cmd_ExecuteString( "-moveleft" );
	}
}

static void IN_CollectInput( float *forward, float *side, float *pitch, float *yaw, qboolean includeMouse )
{
	if( includeMouse )
	{
		float x, y;
		Platform_MouseMove( &x, &y );
		*pitch += y * m_pitch.value;
		*yaw   -= x * m_yaw.value;

#if XASH_USE_EVDEV
		IN_EvdevMove( yaw, pitch );
#endif
	}

	Joy_FinalizeMove( forward, side, yaw, pitch );
	Touch_GetMove( forward, side, yaw, pitch );

	if( look_filter.value )
	{
		*pitch = ( inputstate.lastpitch + *pitch ) / 2;
		*yaw   = ( inputstate.lastyaw   + *yaw ) / 2;
		inputstate.lastpitch = *pitch;
		inputstate.lastyaw   = *yaw;
	}

}

/*
================
IN_EngineAppendMove

Called from cl_main.c after generating command in client
================
*/
void IN_EngineAppendMove( float frametime, usercmd_t *cmd, qboolean active )
{
	float forward, side, pitch, yaw;

	if( clgame.dllFuncs.pfnLookEvent )
		return;

	if( cls.key_dest != key_game || cl.paused || cl.intermission )
		return;

	forward = side = pitch = yaw = 0;

	if( active )
	{
		float sensitivity = 1;//( (float)cl.local.scr_fov / (float)90.0f );

		IN_CollectInput( &forward, &side, &pitch, &yaw, false );

		IN_JoyAppendMove( cmd, forward, side );

		if( pitch || yaw )
		{
			cmd->viewangles[YAW]   += yaw * sensitivity;
			cmd->viewangles[PITCH] += pitch * sensitivity;
			cmd->viewangles[PITCH] = bound( -90, cmd->viewangles[PITCH], 90 );
			VectorCopy( cmd->viewangles, cl.viewangles );
		}
	}
}

static void IN_Commands( void )
{
#if XASH_USE_EVDEV
	IN_EvdevFrame();
#endif

	if( clgame.dllFuncs.pfnLookEvent )
	{
		float forward = 0, side = 0, pitch = 0, yaw = 0;

		IN_CollectInput( &forward, &side, &pitch, &yaw, in_mouseinitialized && !m_ignore.value );

		if( cls.key_dest == key_game )
		{
			clgame.dllFuncs.pfnLookEvent( yaw, pitch );
			clgame.dllFuncs.pfnMoveEvent( forward, side );
		}
	}

	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( in_mouseactive );
}

/*
==================
Host_InputFrame

Called every frame, even if not generating commands
==================
*/
void Host_InputFrame( void )
{
	IN_Commands();

	IN_MouseMove();
}
