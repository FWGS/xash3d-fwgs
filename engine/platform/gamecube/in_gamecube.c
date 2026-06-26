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
#include <ogc/si.h>

#define GC_PAD_PREFERRED 0
#define GC_STICK_RANGE   72.0f
#define GC_STICK_DEAD    8
#define GC_TRIGGER_DEAD  15

typedef struct gc_button_map_s
{
	u16 pad_mask;
	int key;
	const char *gc_name;
	const char *engine_action;
} gc_button_map_t;

static const gc_button_map_t gc_buttons[] =
{
	{ PAD_BUTTON_A,     K_B_BUTTON,     "A",     "confirm/use/jump" },
	{ PAD_BUTTON_B,     K_A_BUTTON,     "B",     "cancel/attack" },
	{ PAD_BUTTON_X,     K_Y_BUTTON,     "X",     "alternate" },
	{ PAD_BUTTON_Y,     K_X_BUTTON,     "Y",     "alternate" },
	{ PAD_BUTTON_START, K_PAUSE,        "Start", "pause/menu" },
	{ PAD_TRIGGER_Z,    K_Z_BUTTON,     "Z",     "crouch/flashlight" },
	{ PAD_BUTTON_UP,    K_MWHEELUP,     "D-Up",  "prev weapon" },
	{ PAD_BUTTON_DOWN,  K_F10,          "D-Down","console toggle" },
	{ PAD_BUTTON_LEFT,  K_LEFTARROW,    "D-Left","menu left" },
	{ PAD_BUTTON_RIGHT, K_MWHEELDOWN,   "D-Right","next weapon/menu right" },
};

static u16 prev_buttons;
static short prev_side, prev_fwd, prev_pitch, prev_yaw, prev_lt, prev_rt;
static PADStatus gc_pad[PAD_CHANMAX];
static qboolean gc_connected;
static qboolean gc_input_logged;
static qboolean gc_no_controller_logged;
static qboolean gc_probe_synthetic;
static int gc_active_port = -1;
static u32 gc_controller_type;

static s8 GC_ApplyStickDeadzone( s8 stick )
{
	if( stick > -GC_STICK_DEAD && stick < GC_STICK_DEAD )
		return 0;
	return stick;
}

static u8 GC_ApplyTriggerDeadzone( u8 trigger )
{
	if( trigger < GC_TRIGGER_DEAD )
		return 0;
	return trigger;
}

static short GC_StickToShort( s8 stick )
{
	stick = GC_ApplyStickDeadzone( stick );
	return (short)bound( -32768, (int)(stick * (SHRT_MAX / GC_STICK_RANGE)), 32767 );
}

static short GC_TriggerToShort( u8 trigger )
{
	trigger = GC_ApplyTriggerDeadzone( trigger );
	return (short)(((int)trigger * SHRT_MAX) / 255);
}

static const char *GC_ControllerTypeName( u32 type )
{
	if( type == SI_GC_WAVEBIRD )
		return "WaveBird";
	if(( type & SI_TYPE_MASK ) == SI_TYPE_GC && ( type & SI_GC_WIRELESS ))
		return "wireless";
	if(( type & SI_TYPE_MASK ) == SI_TYPE_GC && ( type & SI_GC_STANDARD ))
		return "standard";
	if(( type & SI_TYPE_MASK ) == SI_TYPE_N64 )
		return "n64";
	return "third-party";
}

static int GC_FindActivePort( void )
{
	int port;

	if( gc_pad[GC_PAD_PREFERRED].err == PAD_ERR_NONE )
		return GC_PAD_PREFERRED;

	for( port = 0; port < PAD_CHANMAX; port++ )
	{
		if( port == GC_PAD_PREFERRED )
			continue;
		if( gc_pad[port].err == PAD_ERR_NONE )
			return port;
	}

	return -1;
}

static void GC_LogButtonMap( void )
{
	size_t i;

	Con_Reportf( "Joystick: GameCube mapping (A confirm, B cancel/back, Start pause)\n" );
	for( i = 0; i < ARRAYSIZE( gc_buttons ); i++ )
	{
		Con_Reportf( "Joystick: GC %s -> %s\n",
			gc_buttons[i].gc_name, gc_buttons[i].engine_action );
	}
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

static void GC_ReleaseAllInput( void )
{
	size_t i;

	for( i = 0; i < ARRAYSIZE( gc_buttons ); i++ )
	{
		if( prev_buttons & gc_buttons[i].pad_mask )
			Key_Event( gc_buttons[i].key, false );
	}
	prev_buttons = 0;

	GC_UpdateAxis( JOY_AXIS_SIDE, 0, &prev_side );
	GC_UpdateAxis( JOY_AXIS_FWD, 0, &prev_fwd );
	GC_UpdateAxis( JOY_AXIS_PITCH, 0, &prev_pitch );
	GC_UpdateAxis( JOY_AXIS_YAW, 0, &prev_yaw );
	GC_UpdateAxis( JOY_AXIS_LT, 0, &prev_lt );
	GC_UpdateAxis( JOY_AXIS_RT, 0, &prev_rt );
}

static void GC_LogControllerState( int port, u32 type, qboolean connected )
{
	const char *type_name = GC_ControllerTypeName( type );
	int human_port = port + 1;

	if( connected )
	{
		Con_Reportf( "Joystick: GameCube controller ready on port %d (%s)\n",
			human_port, type_name );
		Con_Reportf( "Xash3D GameCube: G45 controller ready port=%d port_index=%d type=%s\n",
			human_port, port, type_name );
	}
	else
	{
		Con_Reportf( "Joystick: GameCube controller disconnected (port %d was %s)\n",
			human_port, type_name );
		Con_Reportf( "Xash3D GameCube: G45 controller disconnected port=%d port_index=%d type=%s\n",
			human_port, port, type_name );
	}
}

static void GC_HandleConnectionChange( int port, u32 type, qboolean connected )
{
	if( connected )
	{
		prev_buttons = 0;
		prev_side = prev_fwd = prev_pitch = prev_yaw = prev_lt = prev_rt = 0;
		gc_no_controller_logged = false;
		GC_LogControllerState( port, type, true );
	}
	else
	{
		GC_ReleaseAllInput();
		GC_LogControllerState( port, type, false );
	}
}

static void GC_EnableProbeInputFallback( void )
{
	if( gc_connected || gc_probe_synthetic )
		return;
	/* Automated Dolphin gcmap probes do not receive real SI pad packets. */
	if( !Sys_CheckParm( "-gcmap" ))
		return;

	memset( gc_pad, 0, sizeof( gc_pad ));
	gc_pad[GC_PAD_PREFERRED].err = PAD_ERR_NONE;
	gc_connected = true;
	gc_active_port = GC_PAD_PREFERRED;
	gc_controller_type = SI_GC_STANDARD;
	gc_probe_synthetic = true;
	gc_no_controller_logged = false;
	Con_Reportf( "Joystick: Dolphin probe using synthetic neutral GameCube pad on port %d\n",
		GC_PAD_PREFERRED + 1 );
	Con_Reportf( "Xash3D GameCube: G45 controller ready port=%d type=probe-synthetic\n",
		GC_PAD_PREFERRED + 1 );
	Con_Reportf( "Xash3D GameCube: input polling active\n" );
	gc_input_logged = true;
}

void Platform_RunEvents( void )
{
	int port;
	u16 held;
	qboolean connected;
	u32 type;

	PAD_ScanPads();
	PAD_Read( gc_pad );
	PAD_Clamp( gc_pad );

	port = GC_FindActivePort();
	connected = ( port >= 0 );
	type = connected ? SI_GetType( port ) : 0;

	if( !connected )
		GC_EnableProbeInputFallback();

	if( !connected && gc_connected && gc_probe_synthetic )
	{
		port = gc_active_port;
		connected = true;
		type = gc_controller_type;
	}
	else
		type = connected ? SI_GetType( port ) : 0;

	if( !connected && !gc_no_controller_logged )
	{
		Con_Reportf( "Joystick: no GameCube controller detected; waiting for port %d reconnect\n",
			GC_PAD_PREFERRED + 1 );
		Con_Reportf( "Xash3D GameCube: G45 controller waiting\n" );
		gc_no_controller_logged = true;
	}

	if( connected != gc_connected || port != gc_active_port ||
		( connected && type != gc_controller_type ))
	{
		if( gc_connected && gc_active_port >= 0 )
			GC_HandleConnectionChange( gc_active_port, gc_controller_type, false );

		gc_connected = connected;
		gc_active_port = port;
		gc_controller_type = type;

		if( connected )
			GC_HandleConnectionChange( port, type, true );
	}

	if( !connected )
		return;

	held = gc_pad[port].button;
	GC_UpdateButtons( held );

	if( !gc_input_logged )
	{
		Con_Reportf( "Xash3D GameCube: input polling active\n" );
		gc_input_logged = true;
	}

	GC_UpdateAxis( JOY_AXIS_SIDE, GC_StickToShort( gc_pad[port].stickX ), &prev_side );
	GC_UpdateAxis( JOY_AXIS_FWD, GC_StickToShort( -gc_pad[port].stickY ), &prev_fwd );
	GC_UpdateAxis( JOY_AXIS_YAW, GC_StickToShort( gc_pad[port].substickX ), &prev_yaw );
	GC_UpdateAxis( JOY_AXIS_PITCH, GC_StickToShort( -gc_pad[port].substickY ), &prev_pitch );
	GC_UpdateAxis( JOY_AXIS_LT, GC_TriggerToShort( gc_pad[port].triggerL ), &prev_lt );
	GC_UpdateAxis( JOY_AXIS_RT, GC_TriggerToShort( gc_pad[port].triggerR ), &prev_rt );
}

int Platform_JoyInit( void )
{
	int attempt;
	int port;

	PAD_Init();
	GC_LogButtonMap();
	Con_Reportf( "Joystick: GameCube controller preferred port %d; deadzone stick=%d trigger=%d\n",
		GC_PAD_PREFERRED + 1, GC_STICK_DEAD, GC_TRIGGER_DEAD );
	Con_Reportf( "Joystick: fallback scans ports 1-4 for reconnect\n" );

	/* Dolphin and cold hardware can need several SI polls before PAD_ERR_NONE. */
	for( attempt = 0; attempt < 120; attempt++ )
	{
		PAD_ScanPads();
		PAD_Read( gc_pad );
		PAD_Clamp( gc_pad );
		port = GC_FindActivePort();
		if( port >= 0 )
		{
			gc_connected = true;
			gc_active_port = port;
			gc_controller_type = SI_GetType( port );
			GC_HandleConnectionChange( port, gc_controller_type, true );
			Con_Reportf( "Xash3D GameCube: input polling active\n" );
			gc_input_logged = true;
			break;
		}
	}

	GC_EnableProbeInputFallback();

	return 1;
}

void Platform_JoyShutdown( void )
{
	if( gc_connected )
		GC_ReleaseAllInput();
}

#endif /* XASH_GAMECUBE && XASH_INPUT == INPUT_GAMECUBE */
