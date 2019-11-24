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

#include "common.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"

#ifdef XASH_SDL
#include <SDL.h>
#endif

#include "platform/platform.h"

void*		in_mousecursor;
qboolean	in_mouseactive;				// false when not focus app
qboolean	in_mouseinitialized;
qboolean	in_mouse_suspended;
POINT		in_lastvalidpos;
qboolean	in_mouse_savedpos;
static struct inputstate_s
{
	float lastpitch, lastyaw;
} inputstate;

extern convar_t *vid_fullscreen;
convar_t *m_enginemouse;
convar_t *m_pitch;
convar_t *m_yaw;

convar_t *m_enginesens;
convar_t *m_ignore;
convar_t *cl_forwardspeed;
convar_t *cl_sidespeed;
convar_t *cl_backspeed;
convar_t *look_filter;

/*
================
IN_CollectInputDevices

Returns a bit mask representing connected devices or, at least, enabled
================
*/
uint IN_CollectInputDevices( void )
{
	uint ret = 0;

	if( !m_ignore->value ) // no way to check is mouse connected, so use cvar only
		ret |= INPUT_DEVICE_MOUSE;

	if( CVAR_TO_BOOL(touch_enable) )
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
	extern convar_t *joy_enable; // private to input system

	if( lock )
	{
		SetBits( m_ignore->flags, FCVAR_READ_ONLY );
		SetBits( joy_enable->flags, FCVAR_READ_ONLY );
		SetBits( touch_enable->flags, FCVAR_READ_ONLY );
	}
	else
	{
		ClearBits( m_ignore->flags, FCVAR_READ_ONLY );
		ClearBits( joy_enable->flags, FCVAR_READ_ONLY );
		ClearBits( touch_enable->flags, FCVAR_READ_ONLY );
	}
}


/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse( void )
{
	m_ignore = Cvar_Get( "m_ignore", DEFAULT_M_IGNORE, FCVAR_ARCHIVE , "ignore mouse events" );

	m_enginemouse = Cvar_Get( "m_enginemouse", "0", FCVAR_ARCHIVE, "read mouse events in engine instead of client" );
	m_enginesens = Cvar_Get( "m_enginesens", "0.3", FCVAR_ARCHIVE, "mouse sensitivity, when m_enginemouse enabled" );
	m_pitch = Cvar_Get( "m_pitch", "0.022", FCVAR_ARCHIVE, "mouse pitch value" );
	m_yaw = Cvar_Get( "m_yaw", "0.022", FCVAR_ARCHIVE, "mouse yaw value" );
	look_filter = Cvar_Get( "look_filter", "0", FCVAR_ARCHIVE, "filter look events making it smoother" );

	// You can use -nomouse argument to prevent using mouse from client
	// -noenginemouse will disable all mouse input
	if( Sys_CheckParm(  "-noenginemouse" )) return;

	in_mouseinitialized = true;
}

static void IN_ActivateCursor( void )
{
	if( cls.key_dest == key_menu )
	{
#ifdef XASH_SDL
		SDL_SetCursor( in_mousecursor );
#endif
	}
}

void IN_SetCursor( void *hCursor )
{
	in_mousecursor = hCursor;

	IN_ActivateCursor();
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
	if( newstate == oldstate ) return;

	if( oldstate == key_game )
	{
		if( cls.initialized )
			clgame.dllFuncs.IN_DeactivateMouse();
	}
	else if( newstate == key_game )
	{
		// reset mouse pos, so cancel effect in game
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		if( CVAR_TO_BOOL( touch_enable ) )
		{
			SDL_SetRelativeMouseMode( SDL_FALSE );
			SDL_SetWindowGrab( host.hWnd, SDL_FALSE );
		}
		else
#endif
		{
			Platform_SetMousePos( host.window_center_x, host.window_center_y );
#if XASH_SDL
			SDL_SetWindowGrab( host.hWnd, SDL_TRUE );
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
			if( clgame.dllFuncs.pfnLookEvent )
				SDL_SetRelativeMouseMode( SDL_TRUE );
#endif
#endif // XASH_SDL
		}
		if( cls.initialized )
			clgame.dllFuncs.IN_ActivateMouse();
	}

	if( ( newstate == key_menu  || newstate == key_console || newstate == key_message ) && ( !CL_IsBackgroundMap() || CL_IsBackgroundDemo( )))
	{
#ifdef XASH_SDL
		SDL_SetWindowGrab(host.hWnd, SDL_FALSE);
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		if( clgame.dllFuncs.pfnLookEvent )
			SDL_SetRelativeMouseMode( SDL_FALSE );
#endif
#endif // XASH_SDL
#if XASH_ANDROID
		Android_ShowMouse( true );
#endif
#ifdef XASH_USE_EVDEV
		Evdev_SetGrab( false );
#endif
	}
	else
	{
#if XASH_ANDROID
		Android_ShowMouse( false );
#endif
#ifdef XASH_USE_EVDEV
		Evdev_SetGrab( true );
#endif
	}
}

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( qboolean force )
{
	int		width, height;
	static int	oldstate;
			
	if( !in_mouseinitialized )
		return;

	if( CL_Active() && host.mouse_visible && !force )
		return;	// VGUI controls  

	if( cls.key_dest == key_menu && !Cvar_VariableInteger( "fullscreen" ))
	{
		// check for mouse leave-entering
		if( !in_mouse_suspended && !UI_MouseInRect( ))
			in_mouse_suspended = true;

		if( oldstate != in_mouse_suspended )
		{
			if( in_mouse_suspended )
			{
#ifdef XASH_SDL
				/// TODO: Platform_ShowCursor
				if( !touch_emulate )
					SDL_ShowCursor( SDL_FALSE );
#endif
				UI_ShowCursor( false );
			}
		}

		oldstate = in_mouse_suspended;

		if( in_mouse_suspended )
		{
			in_mouse_suspended = false;
			in_mouseactive = false; // re-initialize mouse
			UI_ShowCursor( true );
		}
	}

	if( in_mouseactive ) return;
	in_mouseactive = true;

	if( UI_IsVisible( )) return;

	if( cls.key_dest == key_game )
	{
		clgame.dllFuncs.IN_ActivateMouse();
#ifdef XASH_SDL
		SDL_GetRelativeMouseState( 0, 0 ); // Reset mouse position
#endif
	}
}

/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized || !in_mouseactive )
		return;

	if( cls.key_dest == key_game )
	{
		clgame.dllFuncs.IN_DeactivateMouse();
	}

	in_mouseactive = false;
#ifdef XASH_SDL
	SDL_SetWindowGrab( host.hWnd, SDL_FALSE );
#endif // XASH_SDL
}

/*
================
IN_MouseMove
================
*/
void IN_MouseMove( void )
{
	POINT	current_pos;
	
	if( !in_mouseinitialized || !in_mouseactive || !UI_IsVisible( ))
		return;

	// find mouse movement
	Platform_GetMousePos( &current_pos.x, &current_pos.y );

	VGui_MouseMove( current_pos.x, current_pos.y );

	// HACKHACK: show cursor in UI, as mainui doesn't call
	// platform-dependent SetCursor anymore
#ifdef XASH_SDL
	if( UI_IsVisible() )
		SDL_ShowCursor( SDL_TRUE );
#endif

	// if the menu is visible, move the menu cursor
	UI_MouseMove( current_pos.x, current_pos.y );

	IN_ActivateCursor();
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( void )
{
	int	i;

	// touch emu: handle motion
	if( CVAR_TO_BOOL( touch_emulate ))
	{
		if( Key_IsDown( K_SHIFT ) )
			Touch_KeyEvent( K_MOUSE2, 2 );
		else
			Touch_KeyEvent( K_MOUSE1, 2 );
	}

	if( !in_mouseinitialized || !in_mouseactive )
		return;


	if( m_ignore->value )
		return;

	if( cls.key_dest == key_game )
	{
#if defined( XASH_SDL )
		static qboolean ignore; // igonre mouse warp event
		int x, y;
		Platform_GetMousePos(&x, &y);
		if( host.mouse_visible )
			SDL_ShowCursor( SDL_TRUE );
		else if( !CVAR_TO_BOOL( touch_emulate ) )
			SDL_ShowCursor( SDL_FALSE );

		if( x < host.window_center_x / 2 ||
			y < host.window_center_y / 2 ||
			x > host.window_center_x + host.window_center_x / 2 ||
			y > host.window_center_y + host.window_center_y / 2 )
		{
			Platform_SetMousePos( host.window_center_x, host.window_center_y );
			ignore = 1; // next mouse event will be mouse warp
			return;
		}

		if ( !ignore )
		{
			if( !m_enginemouse->value )
			{
				// a1ba: mouse keys are now separated
				// so pass 0 here
				clgame.dllFuncs.IN_MouseEvent( 0 );
			}
		}
		else
		{
			SDL_GetRelativeMouseState( 0, 0 ); // reset relative state
			ignore = 0;
		}
#endif
		return;
	}
	else
	{
#if XASH_SDL && !XASH_WIN32
#if SDL_VERSION_ATLEAST( 2, 0, 0 )
		SDL_SetRelativeMouseMode( SDL_FALSE );
#endif // SDL_VERSION_ATLEAST( 2, 0, 0 )
		SDL_ShowCursor( SDL_TRUE );
#endif // XASH_SDL && !XASH_WIN32
		IN_MouseMove();
	}
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void )
{
	IN_DeactivateMouse( );

#ifdef XASH_USE_EVDEV
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
	cl_forwardspeed	= Cvar_Get( "cl_forwardspeed", "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Default forward move speed" );
	cl_backspeed	= Cvar_Get( "cl_backspeed", "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Default back move speed"  );
	cl_sidespeed	= Cvar_Get( "cl_sidespeed", "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Default side move speed"  );

	if( !Host_IsDedicated() )
	{
		IN_StartupMouse( );

		Joy_Init(); // common joystick support init

		Touch_Init();

#ifdef XASH_USE_EVDEV
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

	if( forwardmove ) cmd->forwardmove  = forwardmove * cl_forwardspeed->value;
	if( sidemove ) cmd->sidemove  = sidemove * cl_sidespeed->value;

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

void IN_CollectInput( float *forward, float *side, float *pitch, float *yaw, qboolean includeMouse, qboolean includeSdlMouse )
{
	if( includeMouse )
	{
#if XASH_INPUT == INPUT_SDL
		if( includeSdlMouse )
		{
			int x, y;
			SDL_GetRelativeMouseState( &x, &y );
			*pitch += y * m_pitch->value;
			*yaw   -= x * m_yaw->value;
		}
#endif // INPUT_SDL

#if XASH_INPUT == INPUT_ANDROID
		{
			float x, y;
			Android_MouseMove( &x, &y );
			*pitch += y * m_pitch->value;
			*yaw   -= x * m_yaw->value;
		}
#endif // ANDROID

#ifdef XASH_USE_EVDEV
		IN_EvdevMove( yaw, pitch );
#endif
	}
	
	Joy_FinalizeMove( forward, side, yaw, pitch );
	Touch_GetMove( forward, side, yaw, pitch );
	
	if( look_filter->value )
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
void IN_EngineAppendMove( float frametime, void *cmd1, qboolean active )
{
	float forward, side, pitch, yaw;
	usercmd_t *cmd = cmd1;

	if( clgame.dllFuncs.pfnLookEvent )
		return;

	if( cls.key_dest != key_game || cl.paused || cl.intermission )
		return;

	forward = side = pitch = yaw = 0;

	if( active )
	{
		float sensitivity = 1;//( (float)cl.local.scr_fov / (float)90.0f );

		IN_CollectInput( &forward, &side, &pitch, &yaw, in_mouseinitialized && !CVAR_TO_BOOL( m_ignore ), m_enginemouse->value );
		
		IN_JoyAppendMove( cmd, forward, side );

		if( pitch || yaw )
		{
			cmd->viewangles[YAW]   += yaw * sensitivity;
			cmd->viewangles[PITCH] += pitch * sensitivity;
			cmd->viewangles[PITCH] = bound( -90, cmd->viewangles[PITCH], 90 );
			VectorCopy(cmd->viewangles, cl.viewangles);
		}
	}
}

/*
==================
Host_InputFrame

Called every frame, even if not generating commands
==================
*/
void Host_InputFrame( void )
{
	qboolean	shutdownMouse = false;
	float forward = 0, side = 0, pitch = 0, yaw = 0;

	Sys_SendKeyEvents ();

#ifdef XASH_USE_EVDEV
	IN_EvdevFrame();
#endif

	if( clgame.dllFuncs.pfnLookEvent )
	{
		IN_CollectInput( &forward, &side, &pitch, &yaw, in_mouseinitialized && !CVAR_TO_BOOL( m_ignore ), true );

		if( cls.key_dest == key_game )
		{
			clgame.dllFuncs.pfnLookEvent( yaw, pitch );
			clgame.dllFuncs.pfnMoveEvent( forward, side );
		}
	}

	Cbuf_Execute ();

	if( !in_mouseinitialized )
		return;

	if( host.status != HOST_FRAME )
	{
		IN_DeactivateMouse();
		return;
	}

	// release mouse during pause or console typeing
	if( cl.paused && cls.key_dest == key_game )
		shutdownMouse = true;
	
	if( shutdownMouse && !Cvar_VariableInteger( "fullscreen" ))
	{
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse( false );
	IN_MouseMove();
}
