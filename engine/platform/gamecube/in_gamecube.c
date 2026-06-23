/*
in_gamecube.c - GameCube controller input
Copyright (C) 2026 xash3d-gc contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "common.h"
#include "keydefs.h"
#include "input.h"
#include "client.h"
#include "platform/platform.h"

#if XASH_GAMECUBE && XASH_INPUT == INPUT_GAMECUBE
#include <ogc/pad.h>

#define GC_PAD_PORT    0
#define GC_STICK_RANGE 72.0f

typedef struct gc_button_map_s
{
	u16 pad_mask;
	int key;
} gc_button_map_t;

static const gc_button_map_t gc_buttons[] =
{
	{ PAD_BUTTON_A,     K_B_BUTTON },
	{ PAD_BUTTON_B,     K_A_BUTTON },
	{ PAD_BUTTON_X,     K_Y_BUTTON },
	{ PAD_BUTTON_Y,     K_X_BUTTON },
	{ PAD_BUTTON_START, K_START_BUTTON },
	{ PAD_TRIGGER_Z,    K_Z_BUTTON },
	{ PAD_BUTTON_UP,    K_DPAD_UP },
	{ PAD_BUTTON_DOWN,  K_DPAD_DOWN },
	{ PAD_BUTTON_LEFT,  K_DPAD_LEFT },
	{ PAD_BUTTON_RIGHT, K_DPAD_RIGHT },
};

static u16 prev_buttons;
static short prev_side, prev_fwd, prev_pitch, prev_yaw, prev_lt, prev_rt;
static PADStatus gc_pad[PAD_CHANMAX];

static short GC_StickToShort( s8 stick )
{
	return (short)bound( -32768, (int)(stick * (SHRT_MAX / GC_STICK_RANGE)), 32767 );
}

static short GC_TriggerToShort( u8 trigger )
{
	return (short)(((int)trigger * SHRT_MAX) / 255);
}

static void GC_UpdateButtons( u16 held )
{
	size_t i;

	for( i = 0; i < ARRAYSIZE( gc_buttons ); i++ )
	{
		const gc_button_map_t *btn = &gc_buttons[i];

		if(( held & btn->pad_mask ) && !( prev_buttons & btn->pad_mask ))
			Key_Event( btn->key, true );

		if(!( held & btn->pad_mask ) && ( prev_buttons & btn->pad_mask ))
			Key_Event( btn->key, false );
	}

	prev_buttons = held;
}

static void GC_UpdateAxis( engineAxis_t axis, short value, short *prev )
{
	if( value == *prev )
		return;

	Joy_AxisMotionEvent( axis, value );
	*prev = value;
}

static qboolean gc_connected;
static qboolean gc_input_logged;

void Platform_RunEvents( void )
{
	u16 held;
	qboolean connected;

	PAD_ScanPads();
	PAD_Read( gc_pad );

#if XASH_GAMECUBE
	if( !gc_input_logged && Sys_CheckParm( "-gcmap" ))
	{
		Con_Reportf( "Xash3D GameCube: input polling active\n" );
		gc_input_logged = true;
	}
#endif

	connected = ( gc_pad[GC_PAD_PORT].err == PAD_ERR_NONE );

	if( connected != gc_connected )
	{
		gc_connected = connected;
		if( connected )
		{
			prev_buttons = 0;
			prev_side = prev_fwd = prev_pitch = prev_yaw = prev_lt = prev_rt = 0;
			Con_Reportf( "Joystick: GameCube controller connected (port %d)\n", GC_PAD_PORT );
		}
		else
		{
			Con_Reportf( "Joystick: GameCube controller disconnected\n" );
		}
	}

	if( !connected )
		return;

	held = PAD_ButtonsHeld( GC_PAD_PORT );
	GC_UpdateButtons( held );

	/* Emit input polling evidence for G19 interactive smoke test once per session. */
	if( !gc_input_logged )
	{
		Con_Reportf( "Xash3D GameCube: input polling active\n" );
		gc_input_logged = true;
	}

	GC_UpdateAxis( JOY_AXIS_SIDE, GC_StickToShort( PAD_StickX( GC_PAD_PORT )), &prev_side );
	GC_UpdateAxis( JOY_AXIS_FWD, GC_StickToShort( -PAD_StickY( GC_PAD_PORT )), &prev_fwd );
	GC_UpdateAxis( JOY_AXIS_YAW, GC_StickToShort( PAD_SubStickX( GC_PAD_PORT )), &prev_yaw );
	GC_UpdateAxis( JOY_AXIS_PITCH, GC_StickToShort( -PAD_SubStickY( GC_PAD_PORT )), &prev_pitch );
	GC_UpdateAxis( JOY_AXIS_LT, GC_TriggerToShort( PAD_TriggerL( GC_PAD_PORT )), &prev_lt );
	GC_UpdateAxis( JOY_AXIS_RT, GC_TriggerToShort( PAD_TriggerR( GC_PAD_PORT )), &prev_rt );
}

int Platform_JoyInit( void )
{
	PAD_Init();
	Con_Reportf( "Joystick: GameCube controller (port %d)\n", GC_PAD_PORT );
	return 1;
}

void Platform_JoyShutdown( void )
{
}

#endif /* XASH_GAMECUBE && XASH_INPUT == INPUT_GAMECUBE */
