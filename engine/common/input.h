/*
input.h - win32 input devices
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

#ifndef INPUT_H
#define INPUT_H

/*
==============================================================

INPUT

==============================================================
*/

#include "keydefs.h"

//
// input.c
//
void IN_Init( void );
void Host_InputFrame( void );
void IN_Shutdown( void );
void IN_MouseEvent( void );
void IN_ActivateMouse( qboolean force );
void IN_DeactivateMouse( void );
void IN_MouseSavePos( void );
void IN_MouseRestorePos( void );
void IN_ToggleClientMouse( int newstate, int oldstate );
void IN_SetCursor( void *hCursor );

//
// in_touch.c
//
typedef enum
{
	event_down = 0,
	event_up,
	event_motion
} touchEventType;

extern convar_t *touch_enable;

void IN_TouchDraw( void );
void IN_TouchEditClear( void );
void IN_TouchSetClientOnly( qboolean state );
void IN_TouchRemoveButton( const char *name );
void IN_TouchHideButtons( const char *name, qboolean hide );
//void IN_TouchSetCommand( const char *name, const char *command );
//void IN_TouchSetTexture( const char *name, const char *texture );
//void IN_TouchSetColor( const char *name, byte *color );
void IN_TouchAddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags );
void IN_TouchAddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags );
void IN_TouchInitConfig( void );
void IN_TouchWriteConfig( void );
void IN_TouchInit( void );
void IN_TouchShutdown( void );
void IN_TouchMove( float * forward, float *side, float *yaw, float *pitch );
void IN_TouchResetDefaultButtons( void );
int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy );
void IN_TouchKeyEvent( int key, int down );

//
// in_joy.c
//
enum
{
	JOY_HAT_CENTERED = 0,
	JOY_HAT_UP    = BIT(0),
	JOY_HAT_RIGHT = BIT(1),
	JOY_HAT_DOWN  = BIT(2),
	JOY_HAT_LEFT  = BIT(3),
	JOY_HAT_RIGHTUP   = JOY_HAT_RIGHT | JOY_HAT_UP,
	JOY_HAT_RIGHTDOWN = JOY_HAT_RIGHT | JOY_HAT_DOWN,
	JOY_HAT_LEFTUP    = JOY_HAT_LEFT  | JOY_HAT_UP,
	JOY_HAT_LEFTDOWN  = JOY_HAT_LEFT  | JOY_HAT_DOWN
};
extern convar_t *joy_found;

qboolean Joy_IsActive( void );
void Joy_HatMotionEvent( int id, byte hat, byte value );
void Joy_AxisMotionEvent( int id, byte axis, short value );
void Joy_BallMotionEvent( int id, byte ball, short xrel, short yrel );
void Joy_ButtonEvent( int id, byte button, byte down );
void Joy_AddEvent( int id );
void Joy_RemoveEvent( int id );
void Joy_FinalizeMove( float *fw, float *side, float *dpitch, float *dyaw );
void Joy_Init( void );
void Joy_Shutdown( void );
void Joy_EnableTextInput(qboolean enable, qboolean force);

//
// in_evdev.c
//
#ifdef XASH_USE_EVDEV
void Evdev_SetGrab( qboolean grab );
void Evdev_Shutdown( void );
void Evdev_Init( void );
#endif // XASH_USE_EVDEV

#endif//INPUT_H
