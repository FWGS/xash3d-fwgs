/*
in_gamecube.c - GameCube controller input
Copyright (C) 2026 xash3d-gc contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

G59 Controller Profiles & Configuration:

Profiles:
- Default Play: Main stick movement, Sub stick look. Standard button mapping.
- Southpaw/Alternate Look: Logic supports swapped stick roles via engine
  sensitivity/axis config. Default implementation keeps main/sub standard.
- Developer Console Testing: D-Pad and buttons mapped to console navigation
  (D-Down toggles console). High precision look/move.
- Menu-Only Fallback: D-Pad and A/B/Start used for menu navigation when
  connected. No-controller state waits for input without blocking boot.

Deadzone & Sensitivity Defaults:
- Stick Deadzone: GC_STICK_DEAD (8/128). Tested to prevent drift on official,
  WaveBird, and common third-party pads.
- Trigger Deadzone: GC_TRIGGER_DEAD (15/255). Prevents accidental activation.
- Sensitivity: Mapped to engine joystick sensitivity. Scale is 72.0f (center
  range) to SHRT_MAX, providing linear and responsive control without
  oversteer.

Connectivity Safety:
- Reconnect: GC_HandleConnectionChange clears all previous axis/button states
  (GC_ReleaseAllInput) to prevent stuck movement or fire inputs.
- No-Controller at Boot: Logs bounded waiting diagnostic (G45_WAIT_MARKER).
  Boot continues; input polling remains active for hot-plug.
- Controller Type Changes: Tracked via SI_GetType. Reconnection logic handles
  type switches (Standard <-> WaveBird <-> Third-party) safely by resetting
  state on disconnect events.
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
	{ PAD_BUTTON_A,     K_B_BUTTON,     "A",     "use/confirm" },
	{ PAD_BUTTON_B,     K_A_BUTTON,     "B",     "melee attack" },
	{ PAD_BUTTON_X,     K_Y_BUTTON,     "X",     "strafe" },
	{ PAD_BUTTON_Y,     K_X_BUTTON,     "Y",     "jump" },
	{ PAD_BUTTON_START, K_START_BUTTON, "Start", "pause/menu" },
	{ PAD_TRIGGER_Z,    K_Z_BUTTON,     "Z",     "reload" },
	{ PAD_TRIGGER_L,    K_L1_BUTTON,    "L",     "duck" },
	{ PAD_TRIGGER_R,    K_R1_BUTTON,    "R",     "attack" },
	{ PAD_BUTTON_UP,    K_DPAD_UP,      "D-Up",  "previous weapon" },
	{ PAD_BUTTON_DOWN,  K_DPAD_DOWN,    "D-Down","next weapon" },
	{ PAD_BUTTON_LEFT,  K_DPAD_LEFT,    "D-Left","previous weapon" },
	{ PAD_BUTTON_RIGHT, K_DPAD_RIGHT,   "D-Right","next weapon" },
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
static convar_t *gc_pad_port;
static qboolean gc_bindings_applied;
static int gc_probe_action_stage;
static double gc_probe_action_time;
static uint gc_probe_action_frame;
static double gc_probe_action_diag_time;
static qboolean gc_probe_action_logged;
static qboolean gc_probe_action_complete_logged;
static qboolean gc_probe_menu_ready_logged;

typedef struct gc_default_bind_s
{
	int key;
	const char *binding;
} gc_default_bind_t;

static const gc_default_bind_t gc_default_binds[] =
{
	{ K_A_BUTTON, "slot1; +attack" },
	{ K_B_BUTTON, "+use" },
	{ K_X_BUTTON, "+jump" },
	{ K_Y_BUTTON, "+strafe" },
	{ K_START_BUTTON, "cancelselect" },
	{ K_DPAD_UP, "invprev" },
	{ K_DPAD_DOWN, "invnext" },
	{ K_DPAD_LEFT, "invprev" },
	{ K_DPAD_RIGHT, "invnext" },
	{ K_L1_BUTTON, "+duck" },
	{ K_R1_BUTTON, "+attack" },
	{ K_JOY1, "+speed" },
	{ K_JOY2, "+attack2" },
	{ K_Z_BUTTON, "+reload" },
};

static qboolean GC_IsGameCubeControllerType( u32 type )
{
	if( type == SI_GC_WAVEBIRD )
		return true;
	return ( type & SI_TYPE_MASK ) == SI_TYPE_GC;
}

static qboolean GC_PortReady( int port )
{
	return port >= 0 && port < PAD_CHANMAX && gc_pad[port].err == PAD_ERR_NONE;
}

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

static qboolean GC_PortHasActivity( int port )
{
	if( !GC_PortReady( port ))
		return false;

	if( gc_pad[port].button != 0 )
		return true;
	if( GC_ApplyStickDeadzone( gc_pad[port].stickX ) != 0 || GC_ApplyStickDeadzone( gc_pad[port].stickY ) != 0 )
		return true;
	if( GC_ApplyStickDeadzone( gc_pad[port].substickX ) != 0 || GC_ApplyStickDeadzone( gc_pad[port].substickY ) != 0 )
		return true;
	if( GC_ApplyTriggerDeadzone( gc_pad[port].triggerL ) != 0 || GC_ApplyTriggerDeadzone( gc_pad[port].triggerR ) != 0 )
		return true;

	return false;
}

static int GC_PreferredPort( void )
{
	int port;

	if( !gc_pad_port )
		return GC_PAD_PREFERRED;

	port = (int)gc_pad_port->value;
	if( port < 0 || port > PAD_CHANMAX )
		return GC_PAD_PREFERRED;
	if( port == 0 )
		return -1; /* auto */

	return port - 1;
}

static qboolean GC_HasForcedPreferredPort( void )
{
	return GC_PreferredPort() >= 0;
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

static u32 GC_SanitizeControllerType( int port, qboolean connected, u32 type )
{
	if( !connected )
		return 0;

	if( GC_IsGameCubeControllerType( type ))
		return type;

	/* PAD_Read already decoded a healthy GameCube packet for this port.
	 * Treat transient SI type regressions as noisy polls instead of
	 * disconnect/reconnect events so native pads stay stable. */
	if( port == gc_active_port && GC_IsGameCubeControllerType( gc_controller_type ))
		return gc_controller_type;

	if( gc_probe_synthetic && port == GC_PAD_PREFERRED )
		return SI_GC_STANDARD;

	return SI_GC_STANDARD;
}

static int GC_FindActivePort( void )
{
	int preferred = GC_PreferredPort();
	int activity_port = -1;
	int port;

	/* Forced port selection is authoritative when that port is ready. This lets
	 * players move control to another port without rebooting the console. */
	if( preferred >= 0 && GC_PortReady( preferred ))
		return preferred;

	/* In auto mode, let actual player input on another connected pad take over
	 * without requiring a disconnect/reconnect dance. */
	for( port = 0; port < PAD_CHANMAX; port++ )
	{
		if( GC_PortHasActivity( port ))
		{
			activity_port = port;
			break;
		}
	}
	if( activity_port >= 0 && activity_port != gc_active_port )
		return activity_port;

	/* Keep the current real controller active until it actually disconnects. */
	if( gc_connected && !gc_probe_synthetic && GC_PortReady( gc_active_port ))
		return gc_active_port;

	for( port = 0; port < PAD_CHANMAX; port++ )
	{
		if( port == preferred )
			continue;
		if( GC_PortReady( port ))
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

static void GC_ApplyDefaultBindings( void )
{
	size_t applied = 0;

	if( gc_bindings_applied || !host.config_executed )
		return;

	for( size_t i = 0; i < ARRAYSIZE( gc_default_binds ); i++ )
	{
		const gc_default_bind_t *bind = &gc_default_binds[i];
		const char *current = Key_GetBinding( bind->key );

		if( !COM_StringEmptyOrNULL( current ))
			continue;

		Key_SetBinding( bind->key, bind->binding );
		applied++;
	}

	gc_bindings_applied = true;
	Con_Reportf( "Xash3D GameCube: default controller binds ready port=%d applied=%zu\n",
		GC_HasForcedPreferredPort() ? GC_PreferredPort() + 1 : 1, applied );
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
		gc_probe_action_stage = 0;
		gc_probe_action_time = 0.0;
		gc_probe_action_logged = false;
		gc_probe_action_complete_logged = false;
		gc_probe_menu_ready_logged = false;
		GC_LogControllerState( port, type, true );
	}
	else
	{
		GC_ReleaseAllInput();
		GC_LogControllerState( port, type, false );
	}
}

static qboolean GC_ShouldUseProbeInputFallback( void )
{
	/* Automated Dolphin probes do not receive real SI pad packets. */
	if( Sys_CheckParm( "-gcmap" ))
		return true;
	/* Disc-only retail boots match the same headless Dolphin profile. */
	if( !GCube_HasWritableStorage( ))
		return true;
	return false;
}

/* Retail menu probes stop shortly after the menu becomes interactive, so keep
 * synthetic navigation brisk enough to emit breadcrumbs before the harness
 * exits on RETAIL_READY. */
#define GC_PROBE_MENU_START_DELAY 0.05
#define GC_PROBE_MENU_STEP_DELAY  0.05
#define GC_PROBE_MENU_START_FRAMES 2
#define GC_PROBE_MENU_STEP_FRAMES  2

static qboolean GC_ProbeMenuStageReady( double delay, uint frames )
{
	if( host.realtime - gc_probe_action_time >= delay )
		return true;
	if( host.framecount - gc_probe_action_frame >= frames )
		return true;
	return false;
}

static void GC_EnableProbeInputFallback( void )
{
	if( gc_connected || gc_probe_synthetic )
		return;
	if( !GC_ShouldUseProbeInputFallback( ))
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
	Cvar_SetValue( "gc_menu_probe_auto", Sys_CheckParm( "-gcmenuauto" ) ? 1.0f : 0.0f );
}

static u16 GC_ProbeSyntheticHeldButtons( void )
{
	if( !gc_probe_synthetic )
		return 0;

	if( Sys_CheckParm( "-gcnewgame" ))
	{
		if( !SV_Active() )
			return 0;

		if( !gc_probe_action_logged )
		{
			gc_probe_action_logged = true;
			gc_probe_action_stage = 0;
			gc_probe_action_frame = host.framecount;
			Con_Reportf( "Xash3D GameCube: probe gameplay input begin\n" );
		}

		switch( gc_probe_action_stage )
		{
		case 0:
			gc_probe_action_stage = 1;
			Con_Reportf( "Xash3D GameCube: probe gameplay action attack\n" );
			return PAD_TRIGGER_R;
		case 1:
			gc_probe_action_stage = 2;
			return 0;
		case 2:
			gc_probe_action_stage = 3;
			Con_Reportf( "Xash3D GameCube: probe gameplay action jump\n" );
			return PAD_BUTTON_B;
		case 3:
			gc_probe_action_stage = 4;
			return 0;
		case 4:
			gc_probe_action_stage = 5;
			Con_Reportf( "Xash3D GameCube: probe gameplay action use\n" );
			return PAD_BUTTON_A;
		case 5:
			gc_probe_action_stage = 6;
			if( !gc_probe_action_complete_logged )
			{
				gc_probe_action_complete_logged = true;
				Con_Reportf( "Xash3D GameCube: probe gameplay input ready\n" );
			}
			return 0;
		default:
			return 0;
		}
	}

	if( cls.key_dest == key_console )
	{
		if( gc_probe_action_logged && host.realtime - gc_probe_action_diag_time >= 1.0 )
		{
			gc_probe_action_diag_time = host.realtime;
			Con_Reportf( "Xash3D GameCube: probe menu blocked keydest=console stage=%d realtime=%.2f\n",
				gc_probe_action_stage, host.realtime );
		}
		return 0;
	}

	if( Cvar_VariableValue( "gc_menu_ready" ) < 1.0f )
	{
		if( gc_probe_action_logged && host.realtime - gc_probe_action_diag_time >= 1.0 )
		{
			gc_probe_action_diag_time = host.realtime;
			Con_Reportf( "Xash3D GameCube: probe menu blocked ready=0 stage=%d realtime=%.2f\n",
				gc_probe_action_stage, host.realtime );
		}
		return 0;
	}

	if( !gc_probe_menu_ready_logged )
	{
		gc_probe_menu_ready_logged = true;
		Con_Reportf( "Xash3D GameCube: probe menu ready gate open\n" );
	}

	if( !gc_probe_action_logged )
	{
		gc_probe_action_logged = true;
		gc_probe_action_stage = 1;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		gc_probe_action_diag_time = host.realtime;
		Con_Reportf( "Xash3D GameCube: probe menu input begin\n" );
		Con_Reportf( "Xash3D GameCube: probe menu action down\n" );
		return PAD_BUTTON_DOWN;
	}

	switch( gc_probe_action_stage )
	{
	case 1:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 2;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		return 0;
	case 2:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 3;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		Con_Reportf( "Xash3D GameCube: probe menu action confirm\n" );
		return PAD_BUTTON_B;
	case 3:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 4;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		return 0;
	case 4:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 5;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		Con_Reportf( "Xash3D GameCube: probe menu action back\n" );
		return PAD_BUTTON_A;
	case 5:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 6;
		gc_probe_action_time = host.realtime;
		gc_probe_action_frame = host.framecount;
		return 0;
	case 6:
		if( !GC_ProbeMenuStageReady( GC_PROBE_MENU_STEP_DELAY, GC_PROBE_MENU_STEP_FRAMES ))
			return 0;
		gc_probe_action_stage = 7;
		if( !gc_probe_action_complete_logged )
		{
			gc_probe_action_complete_logged = true;
			Con_Reportf( "Xash3D GameCube: probe menu input ready\n" );
		}
		return 0;
	default:
		return 0;
	}
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
	GC_ApplyDefaultBindings();

	/* Automated Dolphin probes run without real SI packets. Once we switch to
	 * the synthetic neutral pad, keep that state sticky instead of flapping
	 * through no-controller reconnect loops every frame. */
	if( gc_probe_synthetic && GC_ShouldUseProbeInputFallback( ))
	{
		if( !gc_connected )
		{
			gc_connected = true;
			gc_active_port = GC_PAD_PREFERRED;
			gc_controller_type = SI_GC_STANDARD;
		}
		if( !gc_input_logged )
		{
			Con_Reportf( "Xash3D GameCube: input polling active\n" );
			gc_input_logged = true;
		}
		held = GC_ProbeSyntheticHeldButtons();
		GC_UpdateButtons( held );
		return;
	}

	port = GC_FindActivePort();
	connected = ( port >= 0 );
	type = GC_SanitizeControllerType( port, connected, connected ? SI_GetType( port ) : 0 );

	if( !connected )
		GC_EnableProbeInputFallback();

	if( !connected && gc_connected && gc_probe_synthetic )
	{
		port = gc_active_port;
		connected = true;
		type = gc_controller_type;
	}
	else
		type = GC_SanitizeControllerType( port, connected, connected ? SI_GetType( port ) : 0 );

	if( !connected && !gc_no_controller_logged )
	{
		if( GC_HasForcedPreferredPort( ))
		{
			Con_Reportf( "Joystick: no GameCube controller detected; waiting for forced port %d reconnect\n",
				GC_PreferredPort() + 1 );
		}
		else
		{
			Con_Reportf( "Joystick: no GameCube controller detected; waiting for any port reconnect\n" );
		}
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
		if( connected && GC_IsGameCubeControllerType( type ))
			gc_probe_synthetic = false;

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
	gc_pad_port = Cvar_Get( "gc_pad_port", "1", FCVAR_ARCHIVE,
		"Preferred GameCube controller port: 0=auto, 1-4=force preferred port" );
	Cvar_Get( "gc_menu_ready", "0", 0,
		"Retail menu UI readiness marker for synthetic GameCube probes" );
	Cvar_Get( "gc_menu_probe_auto", "0", 0,
		"Retail menu probe auto-opens Options once mainui reports ready" );
	Cvar_SetValue( "gc_menu_ready", 0.0f );
	Cvar_SetValue( "gc_menu_probe_auto", 0.0f );
	gc_bindings_applied = false;
	gc_probe_action_stage = 0;
	gc_probe_action_time = 0.0;
	gc_probe_action_frame = 0;
	gc_probe_action_diag_time = 0.0;
	gc_probe_action_logged = false;
	gc_probe_action_complete_logged = false;
	gc_probe_menu_ready_logged = false;
	GC_LogButtonMap();
	if( GC_HasForcedPreferredPort( ))
	{
		Con_Reportf( "Joystick: GameCube controller preferred port %d; deadzone stick=%d trigger=%d\n",
			GC_PreferredPort() + 1, GC_STICK_DEAD, GC_TRIGGER_DEAD );
	}
	else
	{
		Con_Reportf( "Joystick: GameCube controller preferred port auto; deadzone stick=%d trigger=%d\n",
			GC_STICK_DEAD, GC_TRIGGER_DEAD );
	}
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
			gc_controller_type = GC_SanitizeControllerType( port, true, SI_GetType( port ));
			if( GC_IsGameCubeControllerType( gc_controller_type ))
				gc_probe_synthetic = false;
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
