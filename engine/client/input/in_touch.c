/*
touch.c - touchscreen support prototype
Copyright (C) 2015-2018 mittorn

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
#include "math.h"
#include "vgui_draw.h"
#include "mobility_int.h"

#if !XASH_NO_TOUCH

typedef enum
{
	touch_command, // just tap a button
	touch_move,    // like a joystick stick
	touch_joy,     // like a joystick stick, centered
	touch_movejoy, // circular analog movement stick
	touch_lookjoy, // circular analog look stick (rate control)
	touch_crouch,  // configurable hold/toggle crouch
	touch_dpad,    // only two directions
	touch_look,    // like a touchpad
	touch_wheel    // scroll-like
} touchButtonType;

typedef enum
{
	state_none = 0,
	state_edit,
	state_edit_move
} touchState;

typedef enum
{
	round_none = 0,
	round_grid,
	round_aspect
} touchRound;

typedef struct touch_button_s
{
	touchButtonType type;

	// button coordinates
	float x1, y1, x2, y2;

	int gl_texturenum;
	rgba_t color;
	char texture[256];
	char command[256];
	char name[32];
	int finger;
	int flags;
	float fade;
	float fadespeed;
	float fadeend;
	float aspect;
	float stick_x, stick_y; // transient thumb displacement inside the ring
	float stick_start_x, stick_start_y; // neutral pickup point for look
	qboolean crouched;

	// Double-linked list
	struct touch_button_s *next;
	struct touch_button_s *prev;
} touch_button_t;

typedef struct touchdefaultbutton_s
{
	char name[32];
	char texture[256];
	char command[256];
	float x1, y1, x2, y2;
	rgba_t color;
	touchRound round;
	float aspect;
	int flags;
} touchdefaultbutton_t;

typedef struct touchbuttonlist_s
{
	touch_button_t *first;
	touch_button_t *last;
} touchbuttonlist_t;

static struct touch_s
{
	qboolean initialized;
	qboolean config_loaded;
	touchbuttonlist_t list_user, list_edit;
	poolhandle_t mempool;
	touchState state;

	int look_finger;
	qboolean look_stick;
	float look_side, look_forward;
	int move_finger;
	qboolean move_stick;
	int wheel_finger;

	touch_button_t *move_button;
	float move_start_x;
	float move_start_y;

	float wheel_amount;
	string wheel_up;
	string wheel_down;
	string wheel_end;
	int wheel_count;
	qboolean wheel_horizontal;

	float forward;
	float side;
	float yaw;
	float pitch;

	// editing
	touch_button_t *edit;
	touch_button_t *selection;
	touch_button_t *hidebutton;
	int resize_finger;
	qboolean showeditbuttons;
	touch_button_t *crouchmodebutton;
	qboolean crouch_toggle_mode;

	// other features
	qboolean clientonly;
	rgba_t scolor;
	int swidth;
	qboolean precision;

	// textures
	int whitetexture;
	int joytexture; // touch indicator
	int sticktexture; // solid analog-stick thumb
	qboolean configchanged;
	float actual_aspect_ratio; // maximum aspect ratio from launch, or aspect ratio when entering editor
	float config_aspect_ratio; // aspect ratio set by command from config or after entering editor
} touch;

// private to the engine flags
#define TOUCH_FL_UNPRIVILEGED BIT( 10 )

static touchdefaultbutton_t *g_DefaultButtons;
static size_t g_DefaultButtonsLength;

static CVAR_DEFINE_AUTO( touch_in_menu, "0", FCVAR_PRIVILEGED, "draw touch in menu (for internal use only)" );
static CVAR_DEFINE_AUTO( touch_forwardzone, "0.06", FCVAR_FILTERABLE, "forward touch zone" );
static CVAR_DEFINE_AUTO( touch_sidezone, "0.06", FCVAR_FILTERABLE, "side touch zone" );
static CVAR_DEFINE_AUTO( touch_pitch, "90", FCVAR_FILTERABLE, "touch pitch sensitivity" );
static CVAR_DEFINE_AUTO( touch_yaw, "120", FCVAR_FILTERABLE, "touch yaw sensitivity" );
static CVAR_DEFINE_AUTO( touch_nonlinear_look, "0", FCVAR_FILTERABLE, "enable nonlinear touch look" );
static CVAR_DEFINE_AUTO( touch_pow_factor, "1.3", FCVAR_FILTERABLE, "set > 1 to enable" );
static CVAR_DEFINE_AUTO( touch_pow_mult, "400.0", FCVAR_FILTERABLE, "power multiplier, usually 200-1000" );
static CVAR_DEFINE_AUTO( touch_exp_mult, "0", FCVAR_FILTERABLE, "exponent multiplier, usually 20-200, 0 to disable" );
static CVAR_DEFINE_AUTO( touch_grid_count, "50", FCVAR_FILTERABLE, "touch grid count" );
static CVAR_DEFINE_AUTO( touch_grid_enable, "1", FCVAR_FILTERABLE, "enable touch grid" );
static CVAR_DEFINE_AUTO( touch_config_file, "touch.cfg", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "current touch profile file" );
static CVAR_DEFINE_AUTO( touch_precise_amount, "0.5", FCVAR_FILTERABLE, "sensitivity multiplier for precise-look" );
static CVAR_DEFINE_AUTO( touch_highlight_r, "1.0", 0, "highlight r color" );
static CVAR_DEFINE_AUTO( touch_highlight_g, "1.0", 0, "highlight g color" );
static CVAR_DEFINE_AUTO( touch_highlight_b, "1.0", 0, "highlight b color" );
static CVAR_DEFINE_AUTO( touch_highlight_a, "1.0", 0, "highlight alpha" );
static CVAR_DEFINE_AUTO( touch_dpad_radius, "1.0", FCVAR_FILTERABLE, "dpad radius multiplier" );
static CVAR_DEFINE_AUTO( touch_joy_radius, "1.0", FCVAR_FILTERABLE, "joy radius multiplier" );
static CVAR_DEFINE_AUTO( touch_crouch_toggle, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "crouch button mode: 0 hold, 1 toggle" );
static CVAR_DEFINE_AUTO( touch_stick_deadzone, "0.12", FCVAR_FILTERABLE, "radial analog stick dead zone (0-0.9)" );
static CVAR_DEFINE_AUTO( touch_lookjoy_speed, "60", FCVAR_FILTERABLE, "analog look rate before game sensitivity, scaled by touch_yaw and touch_pitch" );
static CVAR_DEFINE_AUTO( touch_lookjoy_curve, "2", FCVAR_FILTERABLE, "analog look radial response exponent (1 linear, 2 fine aiming, maximum 3)" );
static CVAR_DEFINE_AUTO( touch_move_indicator, "0.0", FCVAR_FILTERABLE, "indicate move events (0 to disable)" );
static CVAR_DEFINE_AUTO( touch_joy_texture, "touch_default/joy", FCVAR_FILTERABLE, "texture for move indicator");
static CVAR_DEFINE( touch_emulate, "_touch_emulate", "0", FCVAR_PRIVILEGED, "emulate touch with mouse" );

// Keep positions and lengths separate: safe-area offsets only apply to positions.
static float touch_view_x, touch_view_y;
static float touch_view_width, touch_view_height;
#if XASH_IOS
static float touch_view_points;
#endif

static void Touch_UpdateViewport( void )
{
	float left = 0, top = 0, right = 0, bottom = 0;
#if XASH_IOS
	touch_view_points = IOS_GetTouchInsets( host.hWnd, &left, &top, &right, &bottom );
	touch_view_points *= 1 - left - right;
#endif
	touch_view_x = refState.width * left;
	touch_view_y = refState.height * top;
	touch_view_width = Q_max( 1, refState.width * ( 1 - left - right ));
	touch_view_height = Q_max( 1, refState.height * ( 1 - top - bottom ));
}

#define SCRN_WIDTH(x) (touch_view_width * (x))
#define SCRN_HEIGHT(x) (touch_view_width * (x) * Touch_AspectRatio())
#define TO_SCRN_X(x) (touch_view_x + SCRN_WIDTH(x))
#define TO_SCRN_Y(x) (touch_view_y + SCRN_HEIGHT(x))

static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  );
static void IN_TouchEditClear( void );
static void Touch_InitConfig( void );
static void Touch_ResetCrouch( void );

void Touch_NotifyResize( void )
{
	Touch_UpdateViewport();
	if( refState.width && refState.height && ( !touch.configchanged || !touch.actual_aspect_ratio ))
	{
		float aspect_ratio = (float)refState.height / refState.width;
		if( aspect_ratio < 0.99 && aspect_ratio > touch.actual_aspect_ratio )
			touch.actual_aspect_ratio = aspect_ratio;
	}
}

static inline float Touch_AspectRatio( void )
{
#if XASH_IOS
	// UIKit's current safe rectangle is authoritative after rotation/resizing.
	if( refState.width && refState.height && touch_view_width > 0 )
		return touch_view_height / touch_view_width;
#endif
	if( touch.config_aspect_ratio >= 0.25f )
		return touch.config_aspect_ratio;

	if( touch.actual_aspect_ratio >= 0.25f )
		return touch.actual_aspect_ratio;

	if( refState.width && refState.height )
		return (float)refState.height / refState.width;

	return 9.0f / 16.0f;
}

static void Touch_ConfigAspectRatio_f( void )
{
	touch.config_aspect_ratio = Q_atof( Cmd_Argv( 1 ));
}


/*
==========================
Touch_ExportButtonToConfig

writes button data to config
==========================
*/
static void Touch_ExportButtonToConfig( file_t *f, const touch_button_t *button, qboolean keepAspect )
{
	string newCommand;
	int flags = button->flags;

	if( FBitSet( flags, TOUCH_FL_CLIENT ))
		return; // skip temporary buttons

	if( FBitSet( flags, TOUCH_FL_DEF_SHOW ))
		ClearBits( flags, TOUCH_FL_HIDE );

	if( FBitSet( flags, TOUCH_FL_DEF_HIDE ))
		SetBits( flags, TOUCH_FL_HIDE );

	Cmd_Escape( newCommand, button->command, sizeof( newCommand ));

	FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %g %g %g %g %d %d %d %d %d",
		button->name, button->texture, newCommand,
		button->x1, button->y1, button->x2, button->y2,
		button->color[0], button->color[1], button->color[2], button->color[3],
		flags );

	if( keepAspect )
	{
		float aspect = ( button->y2 - button->y1 ) / (( button->x2 - button->x1 ) / Touch_AspectRatio( ));
		FS_Printf( f, " %g\n", aspect );
	}
	else FS_Printf( f, "\n" );
}

/*
=================
Touch_DumpConfig

Dump config to file
=================
*/
static qboolean Touch_DumpConfig( const char *name, const char *profilename )
{
	file_t *f = FS_Open( name, "w", true );

	if( !f )
	{
		Con_Printf( S_ERROR "Couldn't write %s.\n", name );
		return false;
	}

	FS_Printf( f, "//=======================================================================\n");
	FS_Printf( f, "//\tGenerated by "XASH_ENGINE_NAME" (%i, %s, %s, %s-%s)\n", Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch());
	FS_Printf( f, "//\t\t\ttouchscreen config\n" );
	FS_Printf( f, "//=======================================================================\n" );
	FS_Printf( f, "\ntouch_config_file \"%s\"\n", profilename );
	FS_Printf( f, "\n// touch cvars\n" );
	FS_Printf( f, "\n// sensitivity settings\n" );
	FS_Printf( f, "touch_pitch \"%g\"\n", touch_pitch.value );
	FS_Printf( f, "touch_yaw \"%g\"\n", touch_yaw.value );
	FS_Printf( f, "touch_forwardzone \"%g\"\n", touch_forwardzone.value );
	FS_Printf( f, "touch_sidezone \"%g\"\n", touch_sidezone.value );
	FS_Printf( f, "touch_nonlinear_look \"%d\"\n", touch_nonlinear_look.value ? 1 : 0 );
	FS_Printf( f, "touch_pow_factor \"%g\"\n", touch_pow_factor.value );
	FS_Printf( f, "touch_pow_mult \"%g\"\n", touch_pow_mult.value );
	FS_Printf( f, "touch_exp_mult \"%g\"\n", touch_exp_mult.value );
	FS_Printf( f, "\n// grid settings\n" );
	FS_Printf( f, "touch_grid_count \"%d\"\n", (int)touch_grid_count.value );
	FS_Printf( f, "touch_grid_enable \"%d\"\n", touch_grid_enable.value ? 1 : 0 );
	FS_Printf( f, "\n// global overstroke (width, r, g, b, a)\n" );
	FS_Printf( f, "touch_set_stroke %d %d %d %d %d\n", touch.swidth, touch.scolor[0], touch.scolor[1], touch.scolor[2], touch.scolor[3] );
	FS_Printf( f, "\n// highlight when pressed\n" );
	FS_Printf( f, "touch_highlight_r \"%g\"\n", touch_highlight_r.value );
	FS_Printf( f, "touch_highlight_g \"%g\"\n", touch_highlight_g.value );
	FS_Printf( f, "touch_highlight_b \"%g\"\n", touch_highlight_b.value );
	FS_Printf( f, "touch_highlight_a \"%g\"\n", touch_highlight_a.value );
	FS_Printf( f, "\n// _joy and _dpad options\n" );
	FS_Printf( f, "touch_dpad_radius \"%g\"\n", touch_dpad_radius.value );
	FS_Printf( f, "touch_joy_radius \"%g\"\n", touch_joy_radius.value );
	FS_Printf( f, "\n// how much slowdown when Precise Look button pressed\n" );
	FS_Printf( f, "touch_precise_amount \"%g\"\n", touch_precise_amount.value );
	FS_Printf( f, "\n// crouch and analog stick options\n" );
	FS_Printf( f, "touch_crouch_toggle \"%g\"\n", touch_crouch_toggle.value );
	FS_Printf( f, "touch_stick_deadzone \"%g\"\n", touch_stick_deadzone.value );
	FS_Printf( f, "touch_lookjoy_speed \"%g\"\n", touch_lookjoy_speed.value );
	FS_Printf( f, "touch_lookjoy_curve \"%g\"\n", touch_lookjoy_curve.value );
	FS_Printf( f, "\n// enable/disable move indicator\n" );
	FS_Printf( f, "touch_move_indicator \"%g\"\n", touch_move_indicator.value );

	FS_Printf( f, "\n// reset menu state when execing config\n" );
	FS_Printf( f, "touch_setclientonly 0\n" );
	FS_Printf( f, "\n// touch buttons\n" );
	FS_Printf( f, "touch_removeall\n" );
	FS_Printf( f, "touch_aspectratio %g\n", Touch_AspectRatio());

	for( const touch_button_t *button = touch.list_user.first; button; button = button->next )
		Touch_ExportButtonToConfig( f, button, false );

	FS_Close( f );
	return true;
}

/*
=================
Touch_WriteConfig

save current touch configuration
=================
*/
void Touch_WriteConfig( void )
{
	string newconfigfile, oldconfigfile;

	if( !touch.list_user.first )
		return;

	if( Sys_CheckParm( "-nowriteconfig" ) || !touch.configchanged || !touch.config_loaded )
		return;

	Con_DPrintf( "%s: %s\n", __func__, touch_config_file.string );

	Q_snprintf( newconfigfile, sizeof( newconfigfile ), "%s.new", touch_config_file.string );
	Q_snprintf( oldconfigfile, sizeof( oldconfigfile ), "%s.bak", touch_config_file.string );

	if( Touch_DumpConfig( newconfigfile, touch_config_file.string ))
	{
		FS_Delete( oldconfigfile );
		FS_Rename( touch_config_file.string, oldconfigfile );

		FS_Delete( touch_config_file.string );
		FS_Rename( newconfigfile, touch_config_file.string );
	}
}

/*
=================
Touch_ExportConfig_f

export current touch configuration into profile
=================
*/
static void Touch_ExportConfig_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_exportconfig <name>\n" );
		return;
	}

	if( !touch.list_user.first )
	{
		Con_Printf( "%s: nothing to export\n", __func__ );
		return;
	}

	const char *name = Cmd_Argv( 1 );
	string profilename;

	if( Q_strstr( name, "touch_presets/" ))
	{
		string profilebase;

		COM_FileBase( name, profilebase, sizeof( profilebase ));
		Q_snprintf( profilename, sizeof( profilename ), "touch_profiles/%s (copy).cfg", profilebase );
	}
	else Q_strncpy( profilename, name, sizeof( profilename ));

	Con_Reportf( "Exporting config to \"%s\", profile name \"%s\"\n", name, profilename );
	Touch_DumpConfig( name, profilename );
}

/*
=================
Touch_GenerateCode_f

export current touch configuration into C code
=================
*/
static void Touch_GenerateCode_f( void )
{
	rgba_t c = { 0 };

	if( !touch.list_user.first )
	{
		Con_Printf( "%s: nothing to export\n", __func__ );
		return;
	}

	for( const touch_button_t *button = touch.list_user.first; button; button = button->next )
	{
		int flags = button->flags;

		if( FBitSet( flags, TOUCH_FL_CLIENT ))
			continue; // skip temporary buttons

		if( FBitSet( flags, TOUCH_FL_DEF_SHOW ))
			ClearBits( flags, TOUCH_FL_HIDE );

		if( FBitSet( flags, TOUCH_FL_DEF_HIDE ))
			SetBits( flags, TOUCH_FL_HIDE );

		float aspect = ( button->y2 - button->y1 ) / (( button->x2 - button->x1 ) / Touch_AspectRatio( ));

		if( memcmp( c, button->color, sizeof( c )))
		{
			Con_Printf( "unsigned char color[] = { %d, %d, %d, %d };\n",
				button->color[0], button->color[1], button->color[2], button->color[3] );
			memcpy( c, button->color, sizeof( c ));
		}

		int round;
		if( button->type == touch_command )
		{
			if( fabs( aspect - 1.0f ) < 0.001 )
				round = round_aspect;
			else
				round = round_grid;
		}
		else
			round = round_none;

		Con_Printf( "TOUCH_ADDDEFAULT( \"%s\", \"%s\", \"%s\", %gf, %gf, %gf, %gf, color, %d, %g, %d );\n",
			button->name, button->texture, button->command,
			button->x1, button->y1, button->x2, button->y2,
			round, aspect, flags );
	}
}

static void Touch_RoundAll_f( void )
{
	if( !touch_grid_enable.value )
		return;

	for( touch_button_t *button = touch.list_user.first; button; button = button->next )
		IN_TouchCheckCoords( &button->x1, &button->y1, &button->x2, &button->y2 );
}

static void Touch_ListButtons_f( void )
{
	Touch_InitConfig();

	for( touch_button_t *button = touch.list_user.first; button; button = button->next )
	{
		Con_Printf( "%s %s %s %g %g %g %g %d %d %d %d %d\n",
			button->name, button->texture, button->command,
			button->x1, button->y1, button->x2, button->y2,
			button->color[0], button->color[1], button->color[2], button->color[3],
			button->flags );

		if( FBitSet( button->flags, TOUCH_FL_CLIENT ))
			continue;

		UI_AddTouchButtonToList( button->name, button->texture, button->command, button->color, button->flags );
	}
	touch.configchanged = true;
}

static void Touch_Stroke_f( void )
{
	if( Cmd_Argc() != 6 )
	{
		Con_Printf( S_USAGE "touch_set_stroke <width> <r> <g> <b> <a>\n");
		return;
	}

	touch.swidth = Q_atoi( Cmd_Argv( 1 ) );
	MakeRGBA( touch.scolor, Q_atoi( Cmd_Argv( 2 ) ), Q_atoi( Cmd_Argv( 3 ) ), Q_atoi( Cmd_Argv( 4 ) ), Q_atoi( Cmd_Argv( 5 ) ) );
}

static touch_button_t *Touch_FindNextNoPattern( touch_button_t *buttons, const char *name, qboolean privileged )
{
	for( touch_button_t *b = buttons; b; b = b->next )
	{
		if( !privileged && !FBitSet( b->flags, TOUCH_FL_UNPRIVILEGED ))
			continue;

		if( !Q_strncmp( b->name, name, sizeof( b->name )))
			return b;
	}

	return NULL;
}

static touch_button_t *Touch_FindButtonNoPattern( touchbuttonlist_t *list, const char *name, qboolean privileged )
{
	return Touch_FindNextNoPattern( list->first, name, privileged );
}

static touch_button_t *Touch_FindNext( touch_button_t *buttons, const char *name, qboolean privileged )
{
	qboolean has_pattern = Q_strchr( name, '*' ) != NULL;

	if( !has_pattern )
		return Touch_FindNextNoPattern( buttons, name, privileged );

	for( touch_button_t *b = buttons; b; b = b->next )
	{
		if( !privileged && !FBitSet( b->flags, TOUCH_FL_UNPRIVILEGED ))
			continue;

		if( Q_stricmpext( name, b->name ))
			return b;
	}

	return NULL;
}


static touch_button_t *Touch_FindFirst( touchbuttonlist_t *list, const char *name, qboolean privileged )
{
	return Touch_FindNext( list->first, name, privileged );
}

static void Touch_DisableEdit_f( void )
{
	Touch_ResetCrouch();
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;
	if( touch.selection )
		touch.selection->finger = -1;
	touch.edit = touch.selection = NULL;
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;

	if( touch_in_menu.value )
		Cvar_DirectSet( &touch_in_menu, "0" );
	else if( cls.key_dest == key_game )
		Touch_WriteConfig();
}

void Touch_SetClientOnly( byte state )
{
	// TODO: fix clash with vgui cursors
	if( touch.clientonly == state )
		return;

	// a1ba: the way client only touch buttons are used, they might come from
	// client.dll, locking user in edit state, so disable it first
	Touch_DisableEdit_f();

	touch.clientonly = state;

	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;
	touch.forward = touch.side = 0;

	if( state )
	{
		Platform_SetCursorType( dc_arrow );
		IN_DeactivateMouse();
	}
	else
	{
		Platform_SetCursorType( dc_none );
		IN_ActivateMouse();
	}
}

static void Touch_SetClientOnly_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_setclientonly <state>\n");
		return;
	}

	Touch_SetClientOnly( Q_atoi( Cmd_Argv( 1 )));
}

static void Touch_SetCrouch( touch_button_t *button, qboolean crouched )
{
	if( button->crouched == crouched )
		return;
	button->crouched = crouched;
	if( FBitSet( button->flags, TOUCH_FL_UNPRIVILEGED ))
		Cbuf_AddFilteredText( crouched ? "+duck\n" : "-duck\n" );
	else
		Cbuf_AddText( crouched ? "+duck\n" : "-duck\n" );
}

static void Touch_CrouchEvent( touch_button_t *button, touchEventType type )
{
	if( type == event_down )
		Touch_SetCrouch( button, touch_crouch_toggle.value ? !button->crouched : true );
	else if( type == event_up && !touch_crouch_toggle.value )
		Touch_SetCrouch( button, false );
}

static void Touch_ResetCrouch( void )
{
	// A toggled crouch outlives finger-up. Explicitly release the command when
	// changing modes or leaving the controls to avoid carrying a stale latch.
	for( touch_button_t *b = touch.list_user.first; b; b = b->next )
	{
		if( b->type == touch_crouch )
		{
			Touch_SetCrouch( b, false );
			b->finger = -1;
		}
	}
}

static void Touch_UpdateCrouchMode( void )
{
	qboolean toggle = touch_crouch_toggle.value != 0;
	if( toggle != touch.crouch_toggle_mode )
	{
		Touch_ResetCrouch();
		touch.crouch_toggle_mode = toggle;
		touch.configchanged = true;
	}
	if( touch.crouchmodebutton )
		Q_strncpy( touch.crouchmodebutton->texture, toggle ? "#Crouch: Toggle" : "#Crouch: Hold", sizeof( touch.crouchmodebutton->texture ));
}

static void Touch_ToggleCrouch_f( void )
{
	Cvar_DirectSet( &touch_crouch_toggle, touch_crouch_toggle.value ? "0" : "1" );
	Touch_UpdateCrouchMode();
}

static void Touch_ReleaseStick( touch_button_t *button )
{
	if( button->type == touch_crouch )
	{
		Touch_SetCrouch( button, false );
		button->finger = -1;
		return;
	}
	if( button->type != touch_movejoy && button->type != touch_lookjoy )
		return;
	if( button->type == touch_movejoy && button->finger == touch.move_finger )
	{
		touch.move_finger = -1;
		touch.move_button = NULL;
		touch.forward = touch.side = 0;
	}
	if( button->type == touch_lookjoy && button->finger == touch.look_finger )
	{
		touch.look_finger = -1;
		touch.look_stick = false;
		touch.look_side = touch.look_forward = 0;
	}
	button->finger = -1;
	button->stick_x = button->stick_y = 0;
}

static void Touch_RemoveButtonFromList( touchbuttonlist_t *list, const char *name, qboolean privileged )
{
	IN_TouchEditClear();

	touch_button_t *button;
	while(( button = Touch_FindFirst( list, name, privileged )))
	{
		if( button->prev )
			button->prev->next = button->next;
		else
			list->first = button->next;

		if( button->next )
			button->next->prev = button->prev;
		else
			list->last = button->prev;

		Touch_ReleaseStick( button );
		Mem_Free( button );
	}
}

void Touch_RemoveButton( const char *name, qboolean privileged )
{
	Touch_RemoveButtonFromList( &touch.list_user, name, privileged );
}

static void IN_TouchRemoveButton_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_removebutton <button>\n");
		return;
	}

	Touch_RemoveButton( Cmd_Argv( 1 ), Cmd_CurrentCommandIsPrivileged( ));
}

static void Touch_ClearList( touchbuttonlist_t *list )
{
	if( list == &touch.list_edit )
		touch.crouchmodebutton = NULL;
	while( list->first )
	{
		touch_button_t *remove = list->first;
		list->first = list->first->next;
		Touch_ReleaseStick( remove );
		Mem_Free( remove );
	}
	list->first = list->last = NULL;
}

static void Touch_RemoveAll_f( void )
{
	IN_TouchEditClear();
	Touch_ClearList( &touch.list_user );
	touch.config_aspect_ratio = 0.0f;
}

static void Touch_SetColor( touchbuttonlist_t *list, const char *name, byte *color, qboolean privileged )
{
	for( touch_button_t *b = Touch_FindFirst( list, name, privileged ); b != NULL; b = Touch_FindNext( b->next, name, privileged ))
		Vector4Copy( color, b->color );
}

static void Touch_SetTexture( touchbuttonlist_t *list, const char *name, const char *texture, qboolean privileged )
{
	touch_button_t *button = Touch_FindButtonNoPattern( list, name, privileged );

	if( !button )
		return;

	button->gl_texturenum = -1; // mark for texture load
	Q_strncpy( button->texture, texture, sizeof( button->texture ));
}

static void Touch_SetCommand( touch_button_t *button, const char *command )
{
	Touch_ReleaseStick( button );
	Q_strncpy( button->command, command, sizeof( button->command ));

	if( !Q_strcmp( command, "_look" ))
		button->type = touch_look;
	else if( !Q_strcmp( command, "_move" ))
		button->type = touch_move;
	else if( !Q_strcmp( command, "_crouch" ))
		button->type = touch_crouch;
	else if( !Q_strcmp( command, "_movejoy" ))
		button->type = touch_movejoy;
	else if( !Q_strcmp( command, "_lookjoy" ))
		button->type = touch_lookjoy;
	else if( !Q_strcmp( command, "_joy" ))
		button->type = touch_joy;
	else if( !Q_strcmp( command, "_dpad" ))
		button->type = touch_dpad;
	else if( !Q_strncmp( "_wheel ", command, 7 ) || !Q_strncmp( "_hwheel ", command, 8 ))
		button->type = touch_wheel;
	else
		button->type = touch_command;
}

void Touch_HideButtons( const char *name, byte hide, qboolean privileged )
{
	for( touch_button_t *b = Touch_FindFirst( &touch.list_user, name, privileged ); b != NULL; b = Touch_FindNext( b->next, name, privileged ))
	{
		if( hide )
		{
			Touch_ReleaseStick( b );
			SetBits( b->flags, TOUCH_FL_HIDE );
		}
		else
			ClearBits( b->flags, TOUCH_FL_HIDE );
	}
}

static void Touch_ToggleSelection_f( void )
{
	if( touch.selection )
		touch.selection->flags ^= TOUCH_FL_HIDE;
}

static void Touch_Hide_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_hide <button>\n");
		return;
	}

	Touch_HideButtons( Cmd_Argv( 1 ), true, Cmd_CurrentCommandIsPrivileged( ));
}

static void Touch_Show_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_show <button>\n");
		return;
	}

	Touch_HideButtons( Cmd_Argv( 1 ), false, Cmd_CurrentCommandIsPrivileged( ));
}

static void Touch_FadeButtons( touchbuttonlist_t *list, const char *name, float speed, float end, float start, qboolean privileged )
{
	for( touch_button_t *b = Touch_FindFirst( list, name, privileged ); b != NULL; b = Touch_FindNext( b->next, name, privileged ))
	{
		if( start >= 0 )
			b->fade = start;
		b->fadespeed = speed;
		b->fadeend = end;
	}
}

static void Touch_Fade_f( void )
{
	float start = -1;

	if( Cmd_Argc() == 5 )
	{
		start = Q_atof( Cmd_Argv( 4 ) );
	}
	else if( Cmd_Argc() != 4 )
	{
		Con_Printf( S_USAGE "touch_fade <button> <speed> <end> [start]\n");
		return;
	}

	Touch_FadeButtons( &touch.list_user,Cmd_Argv( 1 ), Q_atof( Cmd_Argv( 2 )), Q_atof( Cmd_Argv( 3 )),
		start, Cmd_CurrentCommandIsPrivileged( ));
}

static void Touch_SetColor_f( void )
{
	if( Cmd_Argc() == 6 )
	{
		rgba_t color = { Q_atoi( Cmd_Argv( 2 )), Q_atoi( Cmd_Argv( 3 )), Q_atoi( Cmd_Argv( 4 )), Q_atoi( Cmd_Argv( 5 )) };
		Touch_SetColor( &touch.list_user, Cmd_Argv( 1 ), color, Cmd_CurrentCommandIsPrivileged( ));
	}
	else Con_Printf( S_USAGE "touch_setcolor <pattern> <r> <g> <b> <a>\n" );
}

static void Touch_SetTexture_f( void )
{
	if( Cmd_Argc() == 3 )
		Touch_SetTexture( &touch.list_user, Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_CurrentCommandIsPrivileged( ));
	else Con_Printf( S_USAGE "touch_settexture <name> <file>\n" );
}

static void Touch_SetFlags_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		qboolean privileged = Cmd_CurrentCommandIsPrivileged();
		touch_button_t *button = Touch_FindButtonNoPattern( &touch.list_user, Cmd_Argv( 1 ), privileged );

		if( button )
		{
			button->flags = ( privileged ? 0 : TOUCH_FL_UNPRIVILEGED | TOUCH_FL_CLIENT ) | Q_atoi( Cmd_Argv( 2 ));
			if( FBitSet( button->flags, TOUCH_FL_HIDE ))
				Touch_ReleaseStick( button );
		}
		else
			Con_Printf( S_ERROR "no such button" );
	}
	else Con_Printf( S_USAGE "touch_setflags <name> <file>\n" );
}

static void Touch_SetCommand_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		touch_button_t *button = Touch_FindButtonNoPattern( &touch.list_user, Cmd_Argv( 1 ), Cmd_CurrentCommandIsPrivileged( ));

		if( button )
			Touch_SetCommand( button, Cmd_Argv( 2 ) );
		else
			Con_Printf( S_ERROR "no such button" );
	}
	else Con_Printf( S_USAGE "touch_setcommand <name> <command>\n" );
}

static void Touch_LoadDefaults_f( void );

static void Touch_ReloadConfig_f( void )
{
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;

	if( touch.selection )
		touch.selection->finger = -1;

	touch.edit = touch.selection = NULL;
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;

	if( touch_in_menu.value )
		Cvar_DirectSet( &touch_in_menu, "0" );

	if( FS_FileExists( touch_config_file.string, true ))
		Cbuf_AddTextf( "exec \"%s\"\n", touch_config_file.string );
	else
	{
		Touch_LoadDefaults_f();
		touch.configchanged = true;
	}
}

static touch_button_t *Touch_AddButton( touchbuttonlist_t *list, const char *name, const char *texture, const char *command,
	float x1, float y1, float x2, float y2, byte *color, qboolean privileged )
{
	touch_button_t *b = Mem_Calloc( touch.mempool, sizeof( *b ));

	Touch_RemoveButtonFromList( list, name, privileged ); // replace if exist

	b->gl_texturenum = -1;
	Q_strncpy( b->texture, texture, sizeof( b->texture ));
	Q_strncpy( b->name, name, sizeof( b->name ));
	b->x1 = x1;
	b->y1 = y1;
	b->x2 = x2;
	b->y2 = y2;
	Vector4Copy( color, b->color );
	b->fade = 1;

	if( !privileged )
		SetBits( b->flags, TOUCH_FL_UNPRIVILEGED | TOUCH_FL_CLIENT );

	Touch_SetCommand( b, command );

	b->finger = -1;
	b->prev = list->last;
	if( b->prev )
		b->prev->next = b;
	list->last = b;

	if( !list->first )
		list->first = b;

	return b;
}

void Touch_AddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
{
	if( !touch.initialized )
		return;

	IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );

	if( round == round_aspect )
		y2 = y1 + ( x2 - x1 ) / (Touch_AspectRatio()) * aspect;

	touch_button_t *button = Touch_AddButton( &touch.list_user, name, texture, command, x1, y1, x2, y2, color, true );
	SetBits( button->flags, TOUCH_FL_CLIENT | TOUCH_FL_NOEDIT );
	button->aspect = aspect;
}

static void Touch_LoadDefaults_f( void )
{
	Touch_UpdateViewport();
	for( int i = 0; i < g_DefaultButtonsLength; i++ )
	{
		touch_button_t *button;
		float x1 = g_DefaultButtons[i].x1,
			  y1 = g_DefaultButtons[i].y1,
			  x2 = g_DefaultButtons[i].x2,
			  y2 = g_DefaultButtons[i].y2;

#if XASH_IOS
		if( !Q_strncmp( g_DefaultButtons[i].texture, "touch_ios/", 10 ))
		{
			float width = x2 - x1;
			qboolean stick = !Q_strcmp( g_DefaultButtons[i].command, "_movejoy" ) || !Q_strcmp( g_DefaultButtons[i].command, "_lookjoy" );
			if( touch_view_points > 0 )
			{
				// Sticks use a comfortable 112-144 point diameter; utility buttons
				// retain a minimum 44 point target on small phones.
				width = stick ? bound( 112, width * touch_view_points, 144 ) / touch_view_points : Q_max( width, 44.0f / touch_view_points );
			}
			if( stick )
			{
				float center_x = ( x1 + x2 ) * 0.5f;
				float center_y = ( y1 + y2 ) * 0.5f;
				x1 = center_x - width * 0.5f;
				y1 = center_y - width / Touch_AspectRatio() * 0.5f;
			}
			x2 = x1 + width;
			if( x2 > 1 )
			{
				x1 -= x2 - 1;
				x2 = 1;
			}
			y2 = y1 + width / Touch_AspectRatio();
			if( y2 > 1 )
			{
				y1 -= y2 - 1;
				y2 = 1;
			}
		}
		else
#endif
		{
			IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );

			if( g_DefaultButtons[i].aspect && g_DefaultButtons[i].round == round_aspect )
			{
				if( g_DefaultButtons[i].texture[0] == '#' )
					y2 = y1 + ( (float)clgame.scrInfo.iCharHeight / (float)clgame.scrInfo.iHeight ) * g_DefaultButtons[i].aspect + touch.swidth * 2.0f / refState.height;
				else
					y2 = y1 + (( x2 - x1 ) / Touch_AspectRatio()) * g_DefaultButtons[i].aspect;
			}

			IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
		}

		button = Touch_AddButton( &touch.list_user, g_DefaultButtons[i].name, g_DefaultButtons[i].texture, g_DefaultButtons[i].command, x1, y1, x2, y2, g_DefaultButtons[i].color, true );
		SetBits( button->flags, g_DefaultButtons[i].flags );
		button->aspect = g_DefaultButtons[i].aspect;
	}
	touch.configchanged = true;
}

// Add default button from client
void Touch_AddDefaultButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
{
	g_DefaultButtons = Mem_Realloc( touch.mempool, g_DefaultButtons, sizeof( *g_DefaultButtons ) * ( g_DefaultButtonsLength + 1 ));

	touchdefaultbutton_t *b = &g_DefaultButtons[g_DefaultButtonsLength];

	Q_strncpy( b->name, name, sizeof( b->name ));
	Q_strncpy( b->texture, texture, sizeof( b->texture ));
	Q_strncpy( b->command, command, sizeof( b->command ));
	b->x1 = x1;
	b->y1 = y1;
	b->x2 = x2;
	b->y2 = y2;
	Vector4Copy( color, b->color );
	b->round = round;
	b->aspect = aspect;
	b->flags = flags;

	g_DefaultButtonsLength++;
}

// Client may remove all default buttons from engine
void Touch_ResetDefaultButtons( void )
{
	g_DefaultButtonsLength = 0;

	if( g_DefaultButtons )
	{
		Mem_Free( g_DefaultButtons );
		g_DefaultButtons = NULL;
	}
}

static void Touch_AddButton_f( void )
{
	if( Cmd_Argc( ) < 4 )
	{
		Con_Printf( S_USAGE "touch_addbutton <name> <texture> <command> [<x1> <y1> <x2> <y2> [ r g b a ] ]\n" );
		return;
	}

	const char *name = Cmd_Argv( 1 );
	string texture;
	Q_strncpy( texture, Cmd_Argv( 2 ), sizeof( texture ));
	const char *command = Cmd_Argv( 3 );
	float x1 = 0.4f, y1 = 0.4f, x2 = 0.6f, y2 = 0.6f;
	rgba_t color = { 255, 255, 255, 255 };

	// HACKHACK: old engine specifically used .tga for touch buttons
	// and because new engine extras.pk3 don't have .tga textures
	// (which instead were converted to .png) strip extension to let
	// to let imagelib choose better format
	//
	// Remove this when old engine migration would be done
	if( !Q_stricmp( COM_FileExtension( texture ), "tga" ))
		COM_StripExtension( texture );

	if( Cmd_Argc( ) >= 8 )
	{
		x1 = Q_atof( Cmd_Argv( 4 ));
		y1 = Q_atof( Cmd_Argv( 5 ));
		x2 = Q_atof( Cmd_Argv( 6 ));
		y2 = Q_atof( Cmd_Argv( 7 ));
	}

	if( Cmd_Argc( ) >= 12 )
	{
		color[0] = Q_atoi( Cmd_Argv( 8 ));
		color[1] = Q_atoi( Cmd_Argv( 9 ));
		color[2] = Q_atoi( Cmd_Argv( 10 ));
		color[3] = Q_atoi( Cmd_Argv( 11 ));
	}

	qboolean privileged = Cmd_CurrentCommandIsPrivileged();
	touch_button_t *button = Touch_AddButton( &touch.list_user, name, texture, command, x1, y1, x2, y2, color, privileged );

	if( Cmd_Argc( ) >= 13 )
		SetBits( button->flags, Q_atoi( Cmd_Argv( 12 )));

	if( Cmd_Argc( ) >= 14 )
	{
		// Recalculate button coordinates aspect ratio
		// This is feature for distributed configs
		float aspect = Q_atof( Cmd_Argv( 13 ));
		if( aspect )
		{
			if( button->texture[0] != '#' )
				button->y2 = button->y1 + (( button->x2 - button->x1 ) / Touch_AspectRatio( )) * aspect;
			button->aspect = aspect;
		}
	}
}

static void Touch_EnableEdit_f( void )
{
	Touch_UpdateViewport();
	Touch_ResetCrouch();
#if XASH_IOS
	float current_ratio = Touch_AspectRatio();
#else
	float current_ratio = (float)refState.height / refState.width;
#endif

	if( touch.state == state_none )
		touch.state = state_edit;

	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;
	touch.move_button = NULL;
	touch.configchanged = true;

#if XASH_IOS
	// iOS profiles use normalized safe-area coordinates; do not remap their Y values.
	touch.actual_aspect_ratio = touch.config_aspect_ratio = current_ratio;
	return;
#endif

	/* try determine the best ratio
	 * User enters editor. Window now have correct size. Need to fix aspect ratio in some cases */
	// Case A: no config was loaded, touch was generated with lower height, but window was resized higher, reset it to actual size
	if( touch.actual_aspect_ratio > current_ratio )
		touch.actual_aspect_ratio = current_ratio;
	if( !touch.config_aspect_ratio )
		touch.config_aspect_ratio = touch.actual_aspect_ratio;
	// Case B: config was loaded, but window may be resized later, so keep y coordinate as is
	touch.actual_aspect_ratio = current_ratio;

	// convert coordinates to actual aspect ratio after it was updated
	if( touch.config_aspect_ratio != touch.actual_aspect_ratio )
	{
		for( touch_button_t *button = touch.list_user.first; button; button = button->next )
		{
			button->y1 /= touch.actual_aspect_ratio / touch.config_aspect_ratio;
			button->y2 /= touch.actual_aspect_ratio / touch.config_aspect_ratio;

			// clamp positions to make buttons visible by user
			if( button->y2 > 1.0f )
			{
				button->y1 -= button->y2 - 1.0f;
				button->y2 -= button->y2 - 1.0f;
			}
		}
		touch.config_aspect_ratio = touch.actual_aspect_ratio;
	}
}

static void Touch_DeleteProfile_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "touch_deleteprofile <name>\n" );
		return;
	}

	// delete profile
	FS_Delete( va( "touch_profiles/%s.cfg", Cmd_Argv( 1 )));
}

static void Touch_InitEditor( void )
{
	float x = 0.1f * (Touch_AspectRatio());
	float y = 0.05f;
	touch_button_t *temp;
	rgba_t color;

	MakeRGBA( color, 255, 255, 255, 255 );

	Touch_ClearList( &touch.list_edit );

	temp = Touch_AddButton( &touch.list_edit, "close", "touch_default/edit_close", "touch_disableedit", 0, y, x, y + 0.1f, color, true );
	SetBits( temp->flags, TOUCH_FL_NOEDIT );

	temp = Touch_AddButton( &touch.list_edit, "close_label", "#Close and save", "", x, y, x + 0.2f, y + 0.1f, color, true );
	SetBits( temp->flags, TOUCH_FL_NOEDIT );

	y += 0.2f;

	temp = Touch_AddButton( &touch.list_edit, "cancel", "touch_default/edit_reset", "touch_reloadconfig", 0, y, x, y + 0.1f, color, true );
	SetBits( temp->flags, TOUCH_FL_NOEDIT );

	temp = Touch_AddButton( &touch.list_edit, "cancel_label", "#Cancel and reset", "", x, y, x + 0.2f, y + 0.1f, color, true );
	SetBits( temp->flags, TOUCH_FL_NOEDIT );

	y += 0.2f;

	touch.hidebutton = Touch_AddButton( &touch.list_edit, "showhide", "touch_default/edit_hide", "touch_toggleselection", 0, y, x, y + 0.1f, color, true );
	SetBits( touch.hidebutton->flags, TOUCH_FL_HIDE | TOUCH_FL_NOEDIT );
#if XASH_IOS
	// Keep the mode preference in the editor, away from gameplay controls.
	touch.crouchmodebutton = Touch_AddButton( &touch.list_edit, "crouch_mode", "#Crouch: Hold", "touch_togglecrouch", 0, 0.65f, 0.3f, 0.79f, color, true );
	SetBits( touch.crouchmodebutton->flags, TOUCH_FL_NOEDIT | TOUCH_FL_STROKE );
	Touch_UpdateCrouchMode();
#endif
}

void Touch_Init( void )
{
	rgba_t color;

	if( touch.initialized )
		return;

	touch.mempool = Mem_AllocPool( "Touch" );
	//touch.first = touch.last = NULL;
	Con_Printf( "%s()\n", __func__ );
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;
	touch.state = state_none;
	touch.showeditbuttons = true;
	touch.clientonly = false;
	touch.precision = false;
	MakeRGBA( touch.scolor, 255, 255, 255, 255 );
	touch.swidth = 1;
	touch.sticktexture = -1;
	g_DefaultButtons = NULL;
	g_DefaultButtonsLength = 0;

	touch.list_edit.first = touch.list_edit.last = NULL;
	touch.list_user.first = touch.list_user.last = NULL;

	// fill default buttons list
	MakeRGBA( color, 255, 255, 255, 255 );
#if !XASH_IOS
	Touch_AddDefaultButton( "look", "", "_look", 0.500000, 0.000000, 1.000000, 1, color, 0, 0, 0 );
	Touch_AddDefaultButton( "move", "", "_move", 0.000000, 0.000000, 0.500000, 1, color, 0, 0, 0 );
#endif
#if XASH_IOS
	// Fixed circular sticks with action buttons outside the right stick.
	MakeRGBA( color, 255, 255, 255, 200 );
	Touch_AddDefaultButton( "move", "touch_ios/stick_ring", "_movejoy", 0.060, 0.600, 0.240, 0.920, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "look", "touch_ios/stick_ring", "_lookjoy", 0.700, 0.600, 0.880, 0.920, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "invprev", "touch_ios/prev", "invprev", 0.020, 0.240, 0.090, 0.364444, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "invnext", "touch_ios/next", "invnext", 0.105, 0.240, 0.175, 0.364444, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "reload", "touch_ios/reload", "+reload", 0.560, 0.340, 0.640, 0.482222, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "use", "touch_ios/use", "+use", 0.680, 0.340, 0.760, 0.482222, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "jump", "touch_ios/jump", "+jump", 0.915, 0.640, 0.990, 0.773333, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "attack", "touch_ios/fire", "+attack", 0.890, 0.360, 0.995, 0.546667, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "attack2", "touch_ios/alt_fire", "+attack2", 0.795, 0.340, 0.875, 0.482222, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "duck", "touch_ios/crouch", "_crouch", 0.915, 0.860, 0.990, 0.993333, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "menu", "touch_ios/menu", "cancelselect", 0.020, 0.020, 0.090, 0.144444, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "edit", "touch_ios/settings", "touch_enableedit", 0.105, 0.020, 0.175, 0.144444, color, round_aspect, 1, 32 );
	Touch_AddDefaultButton( "show_numbers", "touch_ios/weapons", "exec touch_default/numbers.cfg", 0.400, 0.020, 0.470, 0.144444, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "flashlight", "touch_ios/flashlight", "impulse 100", 0.915, 0.020, 0.985, 0.144444, color, round_aspect, 1, 0 );
	Touch_AddDefaultButton( "loadquick", "touch_ios/load", "loadquick", 0.745, 0.020, 0.815, 0.144444, color, round_aspect, 1, 16 );
	Touch_AddDefaultButton( "savequick", "touch_ios/save", "savequick", 0.830, 0.020, 0.900, 0.144444, color, round_aspect, 1, 16 );
	Touch_AddDefaultButton( "scores", "touch_ios/scores", "+showscores", 0.745, 0.020, 0.815, 0.144444, color, round_aspect, 1, 8 );
	Touch_AddDefaultButton( "messagemode", "touch_ios/chat", "messagemode", 0.830, 0.020, 0.900, 0.144444, color, round_aspect, 1, 8 );
	Touch_AddDefaultButton( "spray", "touch_ios/spray", "impulse 201", 0.660, 0.020, 0.730, 0.144444, color, round_aspect, 1, 8 );
	Touch_AddDefaultButton( "voicechat", "touch_ios/mic", "+voicerecord", 0.565, 0.020, 0.635, 0.144444, color, round_aspect, 1, 8 );
#else
	Touch_AddDefaultButton( "invnext", "touch_default/next_weap", "invnext", 0.000000, 0.530200, 0.120000, 0.757428, color, 2, 1, 0 );
	Touch_AddDefaultButton( "invprev", "touch_default/prev_weap", "invprev", 0.000000, 0.075743, 0.120000, 0.302971, color, 2, 1, 0 );
	Touch_AddDefaultButton( "use", "touch_default/use", "+use", 0.880000, 0.454457, 1.000000, 0.681685, color, 2, 1, 0 );
	Touch_AddDefaultButton( "jump", "touch_default/jump", "+jump", 0.880000, 0.227228, 1.000000, 0.454457, color, 2, 1, 0 );
	Touch_AddDefaultButton( "attack", "touch_default/shoot", "+attack", 0.760000, 0.530200, 0.880000, 0.757428, color, 2, 1, 0 );
	Touch_AddDefaultButton( "attack2", "touch_default/shoot_alt", "+attack2", 0.760000, 0.302971, 0.880000, 0.530200, color, 2, 1, 0 );
	Touch_AddDefaultButton( "loadquick", "touch_default/load", "loadquick", 0.760000, 0.000000, 0.840000, 0.142222, color, 2, 1, 16 );
	Touch_AddDefaultButton( "savequick", "touch_default/save", "savequick", 0.840000, 0.000000, 0.920000, 0.142222, color, 2, 1, 16 );
	Touch_AddDefaultButton( "messagemode", "touch_default/keyboard", "messagemode", 0.840000, 0.000000, 0.920000, 0.142222, color, 2, 1, 8 );
	Touch_AddDefaultButton( "reload", "touch_default/reload", "+reload", 0.000000, 0.302971, 0.120000, 0.530200, color, 2, 1, 0 );
	Touch_AddDefaultButton( "flashlight", "touch_default/flash_light_filled", "impulse 100", 0.920000, 0.000000, 1.000000, 0.151486, color, 2, 1, 0 );
	Touch_AddDefaultButton( "scores", "touch_default/map", "+showscores", 0.760000, 0.000000, 0.840000, 0.142222, color, 2, 1, 8 );
	Touch_AddDefaultButton( "show_numbers", "touch_default/show_weapons", "exec touch_default/numbers.cfg", 0.440000, 0.833171, 0.520000, 0.984656, color, 2, 1, 0 );
	Touch_AddDefaultButton( "duck", "touch_default/crouch", "+duck", 0.880000, 0.757428, 1.000000, 0.984656, color, 2, 1, 0 );
	Touch_AddDefaultButton( "tduck", "touch_default/tduck", ";+duck", 0.560000, 0.833171, 0.620000, 0.946785, color, 2, 1, 0 );
	Touch_AddDefaultButton( "edit", "touch_default/settings", "touch_enableedit", 0.420000, 0.000000, 0.500000, 0.151486, color, 2, 1, 32 );
	Touch_AddDefaultButton( "menu", "touch_default/menu", "cancelselect", 0.000000, 0.833171, 0.080000, 0.984656, color, 2, 1, 0 );
	Touch_AddDefaultButton( "spray", "touch_default/spray", "impulse 201", 0.680000, 0.000000, 0.760000, 0.142222, color, 2, 1, 8 );
	Touch_AddDefaultButton( "voicechat", "touch_default/microphone", "+voicerecord", 0.780000, 0.817778, 0.860000, 0.960000, color, 2, 1, 8 );
#endif

	Cmd_AddCommand( "touch_addbutton", Touch_AddButton_f, "add native touch button" );
	Cmd_AddCommand( "touch_removebutton", IN_TouchRemoveButton_f, "remove native touch button" );
	Cmd_AddRestrictedCommand( "touch_togglecrouch", Touch_ToggleCrouch_f, "switch crouch between hold and toggle" );
	Cmd_AddRestrictedCommand( "touch_enableedit", Touch_EnableEdit_f, "enable button editing mode" );
	Cmd_AddRestrictedCommand( "touch_disableedit", Touch_DisableEdit_f, "disable button editing mode" );
	Cmd_AddCommand( "touch_settexture", Touch_SetTexture_f, "change button texture" );
	Cmd_AddCommand( "touch_setcolor", Touch_SetColor_f, "change button color" );
	Cmd_AddCommand( "touch_setcommand", Touch_SetCommand_f, "change button command" );
	Cmd_AddCommand( "touch_setflags", Touch_SetFlags_f, "change button flags (be careful)" );
	Cmd_AddCommand( "touch_show", Touch_Show_f, "show button" );
	Cmd_AddCommand( "touch_hide", Touch_Hide_f, "hide button" );
	Cmd_AddRestrictedCommand( "touch_list", Touch_ListButtons_f, "list buttons" );
	Cmd_AddRestrictedCommand( "touch_removeall", Touch_RemoveAll_f, "remove all buttons" );
	Cmd_AddRestrictedCommand( "touch_loaddefaults", Touch_LoadDefaults_f, "generate config from defaults" );
	Cmd_AddRestrictedCommand( "touch_roundall", Touch_RoundAll_f, "round all buttons coordinates to grid" );
	Cmd_AddRestrictedCommand( "touch_exportconfig", Touch_ExportConfig_f, "export config keeping aspect ratio" );
	Cmd_AddRestrictedCommand( "touch_set_stroke", Touch_Stroke_f, "set global stroke width and color" );
	Cmd_AddRestrictedCommand( "touch_setclientonly", Touch_SetClientOnly_f, "when 1, only client buttons are shown" );
	Cmd_AddRestrictedCommand( "touch_reloadconfig", Touch_ReloadConfig_f, "load config, not saving changes" );
	Cmd_AddRestrictedCommand( "touch_writeconfig", Touch_WriteConfig, "save current config" );
	Cmd_AddRestrictedCommand( "touch_deleteprofile", Touch_DeleteProfile_f, "delete profile by name" );
	Cmd_AddRestrictedCommand( "touch_generate_code", Touch_GenerateCode_f, "create code sample for mobility API" );
	Cmd_AddCommand( "touch_fade", Touch_Fade_f, "start fade animation for selected buttons" );
	Cmd_AddRestrictedCommand( "touch_toggleselection", Touch_ToggleSelection_f, "toggle vidibility on selected button in editor" );
	Cmd_AddRestrictedCommand( "touch_aspectratio", Touch_ConfigAspectRatio_f, "set current aspect ratio" );

	// not saved, just runtime state for scripting
	Cvar_RegisterVariable( &touch_in_menu );

	// sensitivity configuration
	Cvar_RegisterVariable( &touch_forwardzone );
	Cvar_RegisterVariable( &touch_sidezone );
	Cvar_RegisterVariable( &touch_pitch );
	Cvar_RegisterVariable( &touch_yaw );
	Cvar_RegisterVariable( &touch_nonlinear_look );
	Cvar_RegisterVariable( &touch_pow_factor );
	Cvar_RegisterVariable( &touch_pow_mult );
	Cvar_RegisterVariable( &touch_exp_mult );

	// touch.cfg
	Cvar_RegisterVariable( &touch_grid_count );
	Cvar_RegisterVariable( &touch_grid_enable );
	Cvar_RegisterVariable( &touch_config_file );
	Cvar_RegisterVariable( &touch_precise_amount );
	Cvar_RegisterVariable( &touch_highlight_r );
	Cvar_RegisterVariable( &touch_highlight_g );
	Cvar_RegisterVariable( &touch_highlight_b );
	Cvar_RegisterVariable( &touch_highlight_a );
	Cvar_RegisterVariable( &touch_dpad_radius );
	Cvar_RegisterVariable( &touch_joy_radius );
	Cvar_RegisterVariable( &touch_move_indicator );
	Cvar_RegisterVariable( &touch_stick_deadzone );
	Cvar_RegisterVariable( &touch_crouch_toggle );
	Cvar_RegisterVariable( &touch_lookjoy_speed );
	Cvar_RegisterVariable( &touch_lookjoy_curve );
	Cvar_RegisterVariable( &touch_joy_texture );

	// input devices cvar
	Cvar_RegisterVariable( &touch_emulate );

	touch.initialized = true;
}

//int pfnGetScreenInfo( SCREENINFO *pscrinfo );
static void Touch_InitConfig( void )
{
	if( !touch.initialized || !host.config_executed || touch.config_loaded )
		return;

	/// TODO: hud font
	//pfnGetScreenInfo( NULL ); //HACK: update hud screen parameters like iHeight
	if( FS_FileExists( touch_config_file.string, true ) )
	{
		Cbuf_AddTextf( "exec \"%s\"\n", touch_config_file.string );
		Cbuf_Execute();
	}
	else Touch_LoadDefaults_f();

	Touch_InitEditor();
	touch.joytexture = ref.dllFuncs.GL_LoadTexture( touch_joy_texture.string, NULL, 0, TF_NOMIPMAP );
	touch.whitetexture = R_GetBuiltinTexture( REF_WHITE_TEXTURE );
	touch.configchanged = false;
	touch.config_loaded = true;
}

/*
============================================================================

                     TOUCH CONTROLS RENDERING

============================================================================
*/

static qboolean Touch_IsVisible( touch_button_t *button )
{
	if( !FBitSet( button->flags, TOUCH_FL_CLIENT ) && touch.clientonly )
		return false; // skip nonclient buttons in clientonly mode

	if( touch.state >= state_edit )
		return true; // draw when editor is open

	if( FBitSet( button->flags, TOUCH_FL_HIDE ))
		return false; // skip hidden

	if( cl.maxclients == 1 )
	{
		if( FBitSet( button->flags, TOUCH_FL_MP ))
			return false; // skip multiplayer buttons in singleplayer
	}
	else
	{
		if( FBitSet( button->flags, TOUCH_FL_SP ))
			return false; // skip singleplayer(load, save) buttons in multiplayer
	}

	return true;
}

static void Touch_DrawTexture( float x1, float y1, float x2, float y2, int texture, byte *color )
{
	if( x1 >= x2 || y1 >= y2 )
		return;

	ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
	ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X( x1 ), TO_SCRN_Y( y1 ),
		SCRN_WIDTH( x2 - x1 ), SCRN_HEIGHT( y2 - y1 ),
		0, 0, 1, 1, texture );
}

static inline int Touch_GridCountX( void )
{
	return Q_max((int)touch_grid_count.value, 1 );
}

static inline int Touch_GridCountY( void )
{
	float grid_count_y = touch_grid_count.value * Touch_AspectRatio();
	return Q_max((int)grid_count_y, 1 );
}

#define GRID_X ( 1.0f / Touch_GridCountX( ))
#define GRID_Y ( 1.0f / Touch_GridCountY( ))
#define GRID_ROUND_X( x ) ((float)round(( x ) * Touch_GridCountX() ) / Touch_GridCountX())
#define GRID_ROUND_Y( x ) ((float)round(( x ) * Touch_GridCountY() ) / Touch_GridCountY())

static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  )
{
	/// TODO: grid check here
	if( *x2 - *x1 < GRID_X * 2 )
		*x2 = *x1 + GRID_X * 2;

	if( *y2 - *y1 < GRID_Y * 2)
		*y2 = *y1 + GRID_Y * 2;

	if( *x1 < 0 )
	{
		*x2 -= *x1;
		*x1 = 0;
	}

	if( *y1 < 0 )
	{
		*y2 -= *y1;
		*y1 = 0;
	}

	if( *y2 > 1 )
	{
		*y1 -= *y2 - 1;
		*y2 = 1;
	}

	if( *x2 > 1 )
	{
		*x1 -= *x2 - 1;
		*x2 = 1;
	}

	if( touch_grid_enable.value )
	{
		*x1 = GRID_ROUND_X( *x1 );
		*x2 = GRID_ROUND_X( *x2 );
		*y1 = GRID_ROUND_Y( *y1 );
		*y2 = GRID_ROUND_Y( *y2 );
	}
}

static float Touch_DrawCharacter( float x, float y, int number, float size )
{
	if( !cls.creditsFont.valid )
		return 0;

	number &= 255;
	number = Con_UtfProcessChar( number );

	if( !number )
		return 0;

	int w, h;
	R_GetTextureParms( &w, &h, cls.creditsFont.hFontTexture );
	wrect_t *prc = &cls.creditsFont.fontRc[number];

	float s1 = prc->left / (float)w;
	float t1 = prc->top / (float)h;
	float s2 = prc->right / (float)w;
	float t2 = prc->bottom / (float)h;

	float width = ( prc->right - prc->left ) / 1024.0f * size;
	float height = ( prc->bottom - prc->top ) / 1024.0f * size;

	ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X( x ), TO_SCRN_Y( y ), SCRN_WIDTH( width ), SCRN_WIDTH( height ),
		s1, t1, s2, t2, cls.creditsFont.hFontTexture );

	return width;
}

static float Touch_DrawText( float x1, float y1, float x2, float y2, const char *s, byte *color, float size )
{
	float x = x1;
	float maxy = y2;
	float maxx;
	float alpha = color[3] / 255.0f;

	if( x2 )
		maxx = x2 - cls.creditsFont.charWidths['M'] / 1024.0f * size;
	else
		maxx = 1;

	if( !cls.creditsFont.valid )
		return GRID_X * 2;

	Con_UtfProcessChar( 0 );
	ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );

	// text is additive and alpha does not work
	ref.dllFuncs.Color4ub( color[0] * alpha, color[1] * alpha, color[2] * alpha, 255 );

	while( *s )
	{
		while( *s && ( *s != '\n' ) && ( *s != ';' ) && ( x1 < maxx ))
			x1 += Touch_DrawCharacter( x1, y1, *s++, size );
		y1 += cls.creditsFont.charHeight / 1024.f * size / Touch_AspectRatio();

		if( y1 >= maxy )
			break;

		if( *s == '\n' || *s == ';' )
			s++;
		x1 = x;
	}
	return x1;
}

static void Touch_StickVector( touch_button_t *button, float x, float y, float *side, float *forward )
{
	float half_width = Q_max( ( button->x2 - button->x1 ) * 0.5f, 0.0001f );
	float half_height = Q_max( ( button->y2 - button->y1 ) * 0.5f, 0.0001f );
	float center_x = button->type == touch_lookjoy ? button->stick_start_x : ( button->x1 + button->x2 ) * 0.5f;
	float center_y = button->type == touch_lookjoy ? button->stick_start_y : ( button->y1 + button->y2 ) * 0.5f;
	float sx = ( x - center_x ) / half_width;
	float sy = ( y - center_y ) / half_height;
	float length = sqrtf( sx * sx + sy * sy );
	float deadzone = bound( 0, touch_stick_deadzone.value, 0.9f );

	button->stick_x = length > 1 ? sx / length : sx;
	button->stick_y = length > 1 ? sy / length : sy;
	if( length <= deadzone || length < 0.0001f )
	{
		*side = *forward = 0;
		return;
	}

	// Rescale the usable radial range to 0..1 without changing direction. This
	// avoids a step at the dead-zone edge and faster movement on diagonals.
	float amount = ( Q_min( length, 1 ) - deadzone ) / ( 1 - deadzone );
	*side = sx / length * amount;
	*forward = -sy / length * amount;
}

static void Touch_StartLookStick( touch_button_t *button, float x, float y )
{
	// Picking up the stick is neutral, even when the thumb misses the centre.
	button->stick_start_x = x;
	button->stick_start_y = y;
	Touch_StickVector( button, x, y, &touch.look_side, &touch.look_forward );
}

static void Touch_DrawStickThumb( const touch_button_t *button, byte *color )
{
	float width = button->x2 - button->x1, height = button->y2 - button->y1;
	float sx = 0, sy = 0;
	qboolean active = button->type == touch_movejoy ? button->finger == touch.move_finger : button->finger == touch.look_finger;
	if( active && button->finger != -1 && touch.state == state_none )
	{
		sx = button->stick_x;
		sy = button->stick_y;
	}
	// The thumb is 36% of the ring diameter; keep its edge inside the ring.
	float cx = ( button->x1 + button->x2 ) * 0.5f + sx * width * 0.27f;
	float cy = ( button->y1 + button->y2 ) * 0.5f + sy * height * 0.27f;
	if( touch.sticktexture == -1 )
		touch.sticktexture = ref.dllFuncs.GL_LoadTexture( "touch_ios/stick_thumb", NULL, 0, TF_IMAGE );
	Touch_DrawTexture( cx - width * 0.18f, cy - height * 0.18f,
		cx + width * 0.18f, cy + height * 0.18f, touch.sticktexture, color );
}

static void Touch_DrawButtons( touchbuttonlist_t *list )
{
	for( touch_button_t *b = list->first; b; b = b->next )
	{
		if( Touch_IsVisible( b ))
		{
			rgba_t color;

			Vector4Copy( b->color, color );

			if( b->fadespeed )
			{
				b->fade += b->fadespeed * host.frametime;
				b->fade = bound( 0, b->fade, 1 );
				if( b->fade == 0 || b->fade == 1 )
					b->fadespeed = 0;

				if(( b->fade >= b->fadeend && b->fadespeed > 0 ) || ( b->fade <= b->fadeend && b->fadespeed < 0 ))
				{
					b->fadespeed = 0;
					b->fade = b->fadeend;
				}
			}

			if( b->finger != -1 && !FBitSet( b->flags, TOUCH_FL_CLIENT ) )
			{
				color[0] = bound( 0, color[0] * touch_highlight_r.value, 255 );
				color[1] = bound( 0, color[1] * touch_highlight_g.value, 255 );
				color[2] = bound( 0, color[2] * touch_highlight_b.value, 255 );
				color[3] = bound( 0, color[3] * touch_highlight_a.value, 255 );
			}

			if( b->type == touch_crouch && b->crouched )
				color[3] = 255;
			color[3] *= b->fade;

			if( b->texture[0] == '#' )
			{
				Touch_DrawText(
					touch.swidth / (float)refState.width + b->x1,
					touch.swidth / (float)refState.height + b->y1,
					b->x2, b->y2, b->texture + 1, color, b->aspect ? b->aspect : 1 );
			}
			else if( b->texture[0] )
			{
				if( b->gl_texturenum == -1 )
					b->gl_texturenum = ref.dllFuncs.GL_LoadTexture( b->texture, NULL, 0, TF_IMAGE );

				if( FBitSet( b->flags, TOUCH_FL_DRAW_ADDITIVE ))
					ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );
				else
					ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );

				Touch_DrawTexture( b->x1, b->y1, b->x2, b->y2, b->gl_texturenum, color );
				if( b->type == touch_movejoy || b->type == touch_lookjoy )
					Touch_DrawStickThumb( b, color );
			}

			if( FBitSet( b->flags, TOUCH_FL_STROKE ))
			{
				rgba_t scolor;
				const float x1_ = TO_SCRN_X( b->x1 );
				const float y1_ = TO_SCRN_Y( b->y1 );
				const float x2_ = TO_SCRN_X( b->x2 );
				const float y2_ = TO_SCRN_Y( b->y2 );
				const float swidth = touch.swidth;

				Vector4Copy( touch.scolor, scolor );
				scolor[3] *= b->fade;

				ref.dllFuncs.FillRGBA( kRenderTransTexture,
					x1_, y1_,
					swidth, y2_ - y1_ - swidth,
					scolor[0], scolor[1], scolor[2], scolor[3] );

				ref.dllFuncs.FillRGBA( kRenderTransTexture,
					x2_ - swidth, y1_ + swidth,
					swidth, y2_ - y1_ - swidth,
					scolor[0], scolor[1], scolor[2], scolor[3] );

				ref.dllFuncs.FillRGBA( kRenderTransTexture,
					x1_ + swidth, y1_,
					x2_ - x1_ - swidth, swidth,
					scolor[0], scolor[1], scolor[2], scolor[3] );

				ref.dllFuncs.FillRGBA( kRenderTransTexture,
					x1_, y2_ - swidth,
					x2_ - x1_ - swidth, swidth,
					scolor[0], scolor[1], scolor[2], scolor[3] );
			}
		}

		if( touch.state >= state_edit && !FBitSet( b->flags, TOUCH_FL_NOEDIT ))
		{
			rgba_t color;

			if( !FBitSet( b->flags, TOUCH_FL_HIDE ))
				MakeRGBA( color, 255, 255, 0, 32 );
			else
				MakeRGBA( color, 128, 128, 128, 128 );

			ref.dllFuncs.FillRGBA( kRenderTransTexture,
				TO_SCRN_X( b->x1 ), TO_SCRN_Y( b->y1 ),
				SCRN_WIDTH( b->x2 - b->x1 ), SCRN_HEIGHT( b->y2 - b->y1 ), color[0], color[1], color[2], color[3] );

			MakeRGBA( color, 255, 255, 127, 255 );
			Con_DrawString( TO_SCRN_X( b->x1 ), TO_SCRN_Y( b->y1 ), b->name, color );
		}
	}

}

void Touch_Draw( void )
{
	if( !touch.initialized || ( !touch_enable.value && !touch.clientonly ))
		return;

	if( cls.key_dest != key_game && !touch_in_menu.value )
		return;

	if( cls.state == ca_cinematic )
		return;

	Touch_UpdateViewport();
	Touch_InitConfig();
	Touch_UpdateCrouchMode();

	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );

	if( touch.state >= state_edit && touch_grid_enable.value )
	{
		if( touch_in_menu.value )
			ref.dllFuncs.FillRGBA( kRenderTransTexture, 0, 0, 1, 1, 32, 32, 32, 255 );
		else
			ref.dllFuncs.FillRGBA( kRenderTransTexture, 0, 0, 1, 1, 0, 0, 0, 112 );

		for( float x = 0.0f; x < 1.0f; x += GRID_X )
			ref.dllFuncs.FillRGBA( kRenderTransTexture, TO_SCRN_X( x ), TO_SCRN_Y( 0 ), 1, SCRN_HEIGHT( 1 ), 0, 224, 224, 112 );

		for( float x = 0.0f; x < 1.0f; x += GRID_Y )
			ref.dllFuncs.FillRGBA( kRenderTransTexture, TO_SCRN_X( 0 ), TO_SCRN_Y( x ), SCRN_WIDTH( 1 ), 1, 0, 224, 224, 112 );
	}

	Touch_DrawButtons( &touch.list_user );

	if( touch.state >= state_edit )
	{
		if( touch.edit )
		{
			float x1 = touch.edit->x1, y1 = touch.edit->y1, x2 = touch.edit->x2, y2 = touch.edit->y2;
			IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
			ref.dllFuncs.FillRGBA( kRenderTransTexture, TO_SCRN_X( x1 ), TO_SCRN_Y( y1 ),
				SCRN_WIDTH( x2 - x1 ), SCRN_HEIGHT( y2 - y1 ), 0, 255, 0, 32 );
		}

		ref.dllFuncs.FillRGBA( kRenderTransTexture, TO_SCRN_X( 0 ), TO_SCRN_Y( 0 ), SCRN_WIDTH( GRID_X ), SCRN_HEIGHT( GRID_Y ), 255, 255, 255, 64 );

		if( touch.showeditbuttons )
			Touch_DrawButtons( &touch.list_edit );

		/// TODO: move to mainui
		if( touch.selection )
		{
			char text[MAX_VA_STRING];
			rgba_t color = { 255, 255, 255, 255 };
			const touch_button_t *b = touch.selection;

			ref.dllFuncs.FillRGBA( kRenderTransTexture, TO_SCRN_X( b->x1 ), TO_SCRN_Y( b->y1 ),
				SCRN_WIDTH( b->x2 - b->x1 ), SCRN_HEIGHT( b->y2 - b->y1 ), 255, 0, 0, 64 );

			Q_snprintf( text, sizeof( text ), "Selection:\nName: %s\nTexture: %s\nCommand: %s", b->name, b->texture, b->command );

			Con_DrawString( TO_SCRN_X( 0 ), TO_SCRN_Y( GRID_Y * 11 ), text, color );
		}
	}

	if( touch.move_finger != -1 && touch.move_button && touch.move_button->type != touch_movejoy && touch_move_indicator.value > 0.0f )
	{
		float width, height;
		float size = touch_move_indicator.value;

		if( FBitSet( touch_joy_texture.flags, FCVAR_CHANGED ) )
		{
			ClearBits( touch_joy_texture.flags, FCVAR_CHANGED );
			touch.joytexture = ref.dllFuncs.GL_LoadTexture( touch_joy_texture.string, NULL, 0, TF_IMAGE );
		}

		if( touch.move_button->type == touch_move )
		{
			width =  touch_sidezone.value;
			height = touch_forwardzone.value;
		}
		else
		{
			width = (touch.move_button->x2 - touch.move_button->x1)/2;
			height = (touch.move_button->y2 - touch.move_button->y1)/2;
		}

		ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
		ref.dllFuncs.Color4ub( 255, 255, 255, 128 );
		ref.dllFuncs.R_DrawStretchPic(
			TO_SCRN_X( touch.move_start_x - GRID_X * size ),
			TO_SCRN_Y( touch.move_start_y - GRID_Y * size ),
			SCRN_WIDTH( GRID_X * 2 * size ),
			SCRN_HEIGHT( GRID_Y * 2 * size ),
			0, 0, 1, 1, touch.joytexture );
		ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
		ref.dllFuncs.R_DrawStretchPic(
			TO_SCRN_X( touch.move_start_x + touch.side * width - GRID_X * size ),
			TO_SCRN_Y( touch.move_start_y - touch.forward * height - GRID_Y * size ),
			SCRN_WIDTH( GRID_X * 2 * size ),
			SCRN_HEIGHT( GRID_Y * 2 * size ),
			0, 0, 1, 1, touch.joytexture );
	}
}

// clear move and selection state
static void IN_TouchEditClear( void )
{
	if( touch.state < state_edit )
		return;

	touch.state = state_edit;

	if( touch.edit )
		touch.edit->finger = -1;

	touch.resize_finger = -1;
	touch.edit = NULL;
	touch.selection = NULL;
}

static void Touch_EditMove( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	if( touch.edit->finger == fingerID )
	{
		if( type == event_up ) // shutdown button move
		{
			touch_button_t *b = touch.edit;

			IN_TouchCheckCoords( &b->x1, &b->y1, &b->x2, &b->y2 );
			IN_TouchEditClear();

			touch.selection = b;

			// update "hide" editor button
			touch.hidebutton->gl_texturenum = -1;
			ClearBits( touch.hidebutton->flags, TOUCH_FL_HIDE );

			if( FBitSet( b->flags, TOUCH_FL_HIDE ))
				Q_strncpy( touch.hidebutton->texture, "touch_default/edit_show", sizeof( touch.hidebutton->texture ));
			else
				Q_strncpy( touch.hidebutton->texture, "touch_default/edit_hide", sizeof( touch.hidebutton->texture ));
		}
		else if( type == event_motion ) // shutdown button move
		{
			touch.edit->y1 += dy;
			touch.edit->y2 += dy;
			touch.edit->x1 += dx;
			touch.edit->x2 += dx;
		}
	}
	else
	{
		if( type == event_down ) // enable resizing
		{
			if( touch.resize_finger == -1 )
				touch.resize_finger = fingerID;
		}
		else if( type == event_up ) // disable resizing
		{
			if( touch.resize_finger == fingerID )
				touch.resize_finger = -1;
		}
		else if( type == event_motion ) // perform resizing
		{
			if( touch.resize_finger == fingerID )
			{
				touch.edit->y2 += dy;
				touch.edit->x2 += dx;
			}
		}
	}
}

static void Touch_Motion( int fingerID, float x, float y, float dx, float dy )
{
	// process wheel
	if( fingerID == touch.wheel_finger )
	{
		touch.wheel_amount += touch.wheel_horizontal ? dx : dy;

		if( touch.wheel_amount > 0.1f )
		{
			Cbuf_AddText( touch.wheel_down );
			touch.wheel_count++;
			touch.wheel_amount = 0;
		}

		if( touch.wheel_amount < -0.1f )
		{
			Cbuf_AddText( touch.wheel_up );
			touch.wheel_count++;
			touch.wheel_amount = 0;
		}

		return;
	}

	// Circular sticks use displacement, including when held still between events.
	for( touch_button_t *b = touch.list_user.first; b; b = b->next )
	{
		if( b->finger != fingerID )
			continue;
		if( b->type == touch_movejoy && fingerID == touch.move_finger )
		{
			Touch_StickVector( b, x, y, &touch.side, &touch.forward );
			return;
		}
		if( b->type == touch_lookjoy && fingerID == touch.look_finger )
		{
			Touch_StickVector( b, x, y, &touch.look_side, &touch.look_forward );
			return;
		}
	}

	// walk
	if( fingerID == touch.move_finger )
	{
		const touch_button_t *b = touch.move_button;

		if( !b || b->type == touch_move )
		{
			// check bounds
			if( touch_forwardzone.value <= 0 )
				Cvar_DirectSet( &touch_forwardzone, "0.5" );

			if( touch_sidezone.value <= 0 )
				Cvar_DirectSet( &touch_sidezone, "0.3" );

			// move relative to touch start
			touch.forward = ( touch.move_start_y - y ) / touch_forwardzone.value;
			touch.side = ( x - touch.move_start_x ) / touch_sidezone.value;
		}
		else
		{
			// move relative to joy center
			touch.forward = (( b->y2 + b->y1 ) - y * 2 ) / ( b->y2 - b->y1 );
			touch.side = ( x * 2 - ( b->x2 + b->x1 )) / ( b->x2 - b->x1 );

			if( b->type == touch_joy )
			{
				touch.forward *= touch_joy_radius.value;
				touch.side *= touch_joy_radius.value;
			}
			else if( b->type == touch_dpad )
			{
				// like joy, but without acceleration. useful for bhop
				touch.forward = round( touch.forward * touch_dpad_radius.value );
				touch.side = round( touch.side * touch_dpad_radius.value );
			}
		}

		touch.forward = bound( -1, touch.forward, 1 );
		touch.side = bound( -1, touch.side, 1 );
	}

	// process look
	if( fingerID == touch.look_finger )
	{
		if( touch.precision )
		{
			dx *= touch_precise_amount.value;
			dy *= touch_precise_amount.value;
		}

		if( touch_nonlinear_look.value )
		{
			// save angle, modify only velocity
			float dabs = sqrt( dx * dx + dy * dy );

			if( dabs < 0.000001f )
				return; // no motion, avoid division by zero

			float dcos = dx / dabs;
			float dsin = dy / dabs;

			if( touch_exp_mult.value > 1 )
				dabs = ( exp( dabs * touch_exp_mult.value ) - 1 ) / touch_exp_mult.value;

			if( touch_pow_mult.value > 1 && touch_pow_factor.value > 1 )
				dabs = pow( dabs * touch_pow_mult.value, touch_pow_factor.value ) / touch_pow_mult.value;

			dx = dabs * dcos;
			dy = dabs * dsin;
		}

		// prevent breaking engine/client with bad values
		if( IS_NAN( dx ) || IS_NAN( dy ))
			return;

		// accumulate
		touch.yaw -= dx * touch_yaw.value;
		touch.pitch += dy * touch_pitch.value;
	}
}

static qboolean Touch_ButtonPress( touchbuttonlist_t *list, touchEventType type, int fingerID, float x, float y )
{
	qboolean result = false;

	if( type != event_down && type != event_up )
		return false;

	// run from end(front) to start(back)
	for( touch_button_t *button = list->last; button; button = button->prev )
	{
		// skip invisible buttons
		if( !Touch_IsVisible( button ))
			continue;

		if( type == event_down )
		{
			// button bounds check
			if( x < button->x1 || x > button->x2 || y < button->y1 || y > button->y2 )
				continue;

			if( button->type == touch_movejoy || button->type == touch_lookjoy )
			{
				float rx = ( x - ( button->x1 + button->x2 ) * 0.5f ) / Q_max( ( button->x2 - button->x1 ) * 0.5f, 0.0001f );
				float ry = ( y - ( button->y1 + button->y2 ) * 0.5f ) / Q_max( ( button->y2 - button->y1 ) * 0.5f, 0.0001f );
				if( rx * rx + ry * ry > 1 )
					continue;
			}
			if( button->type == touch_crouch && button->finger != -1 )
				continue;
			button->finger = fingerID;

			if( button->type == touch_crouch )
			{
				Touch_CrouchEvent( button, event_down );
				result = true;
			}
			else if( button->type == touch_command )
			{
				char command[256];

				// command down: just execute command
				Q_snprintf( command, sizeof( command ), "%s\n", button->command );
				if( FBitSet( button->flags, TOUCH_FL_UNPRIVILEGED ))
					Cbuf_AddFilteredText( command );
				else Cbuf_AddText( command );

				// increase precision
				if( FBitSet( button->flags, TOUCH_FL_PRECISION ))
					touch.precision = true;

				result = true;
			}
			else if( button->type == touch_wheel )
			{
				string command;

				touch.wheel_finger = fingerID;
				touch.wheel_amount = touch.wheel_count = 0;

				Cmd_TokenizeString( button->command );

				touch.wheel_horizontal = !Q_strcmp( Cmd_Argv( 0 ), "_hwheel" );
				Q_snprintf( touch.wheel_up, sizeof( touch.wheel_up ), "%s\n", Cmd_Argv( 1 ));
				Q_snprintf( touch.wheel_down, sizeof( touch.wheel_down ), "%s\n", Cmd_Argv( 2 ));
				Q_snprintf( touch.wheel_end, sizeof( touch.wheel_end ), "%s\n", Cmd_Argv( 3 ));
				if( Q_snprintf( command, sizeof( command ), "%s\n", Cmd_Argv( 4 )) > 1 )
				{
					if( FBitSet( button->flags, TOUCH_FL_UNPRIVILEGED ))
						Cbuf_AddFilteredText( command );
					else Cbuf_AddText( command );
					touch.wheel_count++;
				}

				// increase precision
				if( FBitSet( button->flags, TOUCH_FL_PRECISION ))
					touch.precision = true;

				result = true;
			}
			// initialize motion when player touched motion zone
			else if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad || button->type == touch_movejoy )
			{
				if( touch.move_finger !=-1 )
				{
					// prevent initializing move while already moving
					// revert finger switch, leave first finger
					button->finger = touch.move_finger;
					continue;
				}

				result = true;

				if( touch.look_finger == fingerID )
				{
					// this is an error, try recover
					touch.move_finger = touch.look_finger = -1;

					// player touched touch_move with enabled look mode
					// and same finger id. release all move triggers
					for( touch_button_t *newbutton = list->first; newbutton; newbutton = newbutton->next )
					{
						if( newbutton->type == touch_move || newbutton->type == touch_look || newbutton->type == touch_movejoy || newbutton->type == touch_lookjoy )
							newbutton->finger = -1;
					}

					Con_DPrintf( S_ERROR "Touch: touch_move on look finger %d!\n", fingerID );
					continue;
				}

				// initialize move mode
				touch.move_finger = fingerID;
				touch.move_stick = button->type == touch_movejoy;
				touch.move_button = button;

				if( button->type == touch_movejoy )
					Touch_StickVector( button, x, y, &touch.side, &touch.forward );
				else if( button->type == touch_move )
				{
					// initial position is first touch
					touch.move_start_x = x;
					touch.move_start_y = y;
				}
				else
				{
					// initial position is button center
					touch.move_start_y = ( button->y2 + button->y1 ) / 2;
					touch.move_start_x = ( button->x2 + button->x1 ) / 2;

					// start move instanly
					touch.forward = (( button->y2 + button->y1 ) - y * 2 ) / ( button->y2 - button->y1 );
					touch.side = (x * 2 - ( button->x2 + button->x1 )) / ( button->x2 - button->x1 );

					// same as joy, but round
					if( button->type == touch_dpad )
					{
						touch.forward = round( touch.forward );
						touch.side = round( touch.side );
					}
				}
			}
			// initialize look
			else if( button->type == touch_look || button->type == touch_lookjoy )
			{
				if( touch.look_finger !=-1 )
				{
					// prevent initializing look while already looking
					// revert finger switch, leave first finger
					button->finger = touch.look_finger;
					continue;
				}

				result = true;

				if( touch.move_finger == fingerID )
				{
					// this is an error, try recover
					touch.move_finger = touch.look_finger = -1;

					// player touched touch_move with enabled look mode
					// and same finger id. release all move triggers
					for( touch_button_t *newbutton = list->first; newbutton; newbutton = newbutton->next )
					{
						if( newbutton->type == touch_move || newbutton->type == touch_look || newbutton->type == touch_movejoy || newbutton->type == touch_lookjoy )
							newbutton->finger = -1;
					}

					Con_Printf( S_ERROR "touch: touch_look on move finger %d!\n", fingerID );
					continue;
				}

				touch.look_finger = fingerID;
				touch.look_stick = button->type == touch_lookjoy;
				touch.look_side = touch.look_forward = 0;
				if( touch.look_stick )
					Touch_StartLookStick( button, x, y );
			}
		}
		else if( type == event_up )
		{
			// no bounds check here.
			// button released when finger released
			if( fingerID != button->finger )
				continue;

			button->finger = -1;

			if( button->type == touch_crouch )
			{
				Touch_CrouchEvent( button, event_up );
				result = true;
			}
			// handle +command, replace by -command
			else if( button->type == touch_command )
			{
				if( button->command[0] == '+' )
				{
					char command[256];

					Q_snprintf( command, sizeof( command ), "-%s\n", &button->command[1] );
					if( FBitSet( button->flags, TOUCH_FL_UNPRIVILEGED ))
						Cbuf_AddFilteredText( command );
					else Cbuf_AddText( command );
				}

				// disable precision mode
				if( FBitSet( button->flags, TOUCH_FL_PRECISION ))
					touch.precision = false;

				result = true;
			}
			// handle wheel end
			else if( button->type == touch_wheel )
			{
				if( touch.wheel_count )
				{
					if( FBitSet( button->flags, TOUCH_FL_UNPRIVILEGED ))
						Cbuf_AddFilteredText( touch.wheel_end );
					else Cbuf_AddText( touch.wheel_end );
				}

				// disable precision mode
				if( FBitSet( button->flags, TOUCH_FL_PRECISION ))
					touch.precision = false;

				touch.wheel_finger = -1;

				result = true;
			}
			// release motion buttons
			else if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad || button->type == touch_movejoy )
			{
				touch.move_finger = -1;
				touch.forward = touch.side = 0;
				touch.move_button = NULL;
			}
			// release look buttons
			else if( button->type == touch_look || button->type == touch_lookjoy )
			{
				touch.look_finger = -1;
				touch.look_stick = false;
				touch.look_side = touch.look_forward = 0;
			}
		}
	}

	return result;
}

static qboolean Touch_ButtonEdit( touchEventType type, int fingerID, float x, float y )
{
	// edit buttons are on y1
	if( type == event_down )
	{
		if( x < GRID_X && y < GRID_Y )
		{
			touch.showeditbuttons = !touch.showeditbuttons;
			return true;
		}

		if( touch.showeditbuttons && Touch_ButtonPress( &touch.list_edit, type, fingerID, x, y ))
			return true;
	}

	// run from end(front) to start(back)
	for( touch_button_t *button = touch.list_user.last; button; button = button->prev )
	{
		if( type == event_down )
		{
			if( x > button->x1 && x < button->x2 && y > button->y1 && y < button->y2 )
			{
				button->finger = fingerID;

				// do not edit NOEDIT buttons
				if( FBitSet( button->flags, TOUCH_FL_NOEDIT ))
					continue;

				touch.edit = button;
				touch.selection = NULL;

				// make button last to bring it up
				if( button->next && button->type == touch_command )
				{
					if( button->prev )
						button->prev->next = button->next;
					else
						touch.list_user.first = button->next;

					button->next->prev = button->prev;
					touch.list_user.last->next = button;
					button->prev = touch.list_user.last;
					button->next = NULL;
					touch.list_user.last = button;
				}
				touch.state = state_edit_move;
				return true;
			}
		}

		if( type == event_up )
		{
			if( fingerID == button->finger )
				button->finger = -1;
		}
	}

	if( type == event_down )
	{
		touch.selection = NULL;
		touch.hidebutton->flags |= TOUCH_FL_HIDE;
	}

	return false;
}

static int Touch_ControlsEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	if( touch.state == state_edit_move )
	{
		Touch_EditMove( type, fingerID, x, y, dx, dy );
		return true;
	}

	if( touch.state == state_edit && Touch_ButtonEdit( type, fingerID, x, y ))
		return true;
	if( Touch_ButtonPress( &touch.list_user, type, fingerID, x, y ))
		return true;
	if( type == event_motion )
		Touch_Motion( fingerID, x, y, dx, dy );
	return true;
}

int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	if( ref.rotation & 1 )
	{
		// swap x and y and invert y
		float temp = x;
		x = y;
		if( ref.rotation == REF_ROTATE_CW )
			y = 1.0f - temp;
		else
			y = temp;

		temp = dx;
		dx = dy;
		if( ref.rotation == REF_ROTATE_CW )
			dy = -temp;
		else
			dy = temp;
	}


	if( cls.key_dest == key_game && cls.state == ca_cinematic )
	{
		if( type == event_up )
			CL_Escape_f();

		return true;
	}

//	Con_Printf("%f %f\n", TO_SCRN_X(x), TO_SCRN_Y(y));
	// simulate menu mouse click
	if( cls.key_dest != key_game && !touch_in_menu.value )
	{
		Touch_ResetCrouch();
		touch.move_finger = touch.resize_finger = touch.look_finger = touch.wheel_finger = -1;
		// Hack for keyboard, hope it help
		// a1ba: this is absolutely horrible
		if( cls.key_dest == key_console || cls.key_dest == key_message )
		{
			static float x1 = 0.0f;
			x1 += dx;

			if( type == event_up ) // don't show keyboard on every tap
			{
				Key_EnableTextInput( true, true );
				x1 = 0.0f;
			}

			if( cls.key_dest == key_console )
			{
				static float y1 = 0;
				y1 += dy;
				if( dy > 0.4f )
					Con_Bottom();

				if( y1 > 0.01f )
				{
					Con_PageUp( 1 );
					y1 = 0;
				}
				if( y1 < -0.01f )
				{
					Con_PageDown( 1 );
					y1 = 0;
				}
			}

			// exit of console area
			if( type == event_down && x < 0.1f && y > 0.9f )
			{
				if( cls.key_dest == key_console )
					Key_Console( K_ESCAPE );
				else
					Key_Message( K_ESCAPE );
				return 0;
			}

			// swipe from edge to exit console/chat
			if(( x > 0.7f && x1 < -0.1f ) || ( x < 0.3f && x1 > 0.1f ))
			{
				if( cls.key_dest == key_console )
					Key_Console( K_ESCAPE );
				else
					Key_Message( K_ESCAPE );
				x1 = 0.0f;
				return 0;
			}
		}
		UI_MouseMove( x * refState.width, y * refState.height );

		//MsgDev( D_NOTE, "touch %d %d\n", TO_SCRN_X(x), TO_SCRN_Y(y) );
		if( type == event_down )
			Key_Event( K_MOUSE1, true );

		if( type == event_up )
			Key_Event( K_MOUSE1, false );

		return 0;
	}


	if( VGui_IsActive() )
	{
		VGui_MouseMove( x * refState.width, y * refState.height );

		switch( type )
		{
		case event_down:
			VGui_MouseEvent( K_MOUSE1, 1 );
			break;
		case event_up:
			VGui_MouseEvent( K_MOUSE1, 0 );
			break;
		default:
			break;
		}
	}

	if( !touch.initialized || ( !touch_enable.value && !touch.clientonly ))
		return false;

	Touch_UpdateViewport();
	Touch_UpdateCrouchMode();
#if XASH_IOS
	float screen_y = y;
#else
	float screen_y = y * (float)refState.height / refState.width / Touch_AspectRatio();
#endif

	if( clgame.dllFuncs.pfnTouchEvent && clgame.dllFuncs.pfnTouchEvent( type, fingerID, x, screen_y, dx, dy ) )
		return true;

	// Do not clamp outside touches onto an edge button. Keep up/motion events
	// flowing so a finger released outside the safe rectangle cannot stick.
	x = ( x * refState.width - touch_view_x ) / touch_view_width;
	y = ( y * refState.height - touch_view_y ) / SCRN_HEIGHT( 1 );
	dx *= (float)refState.width / touch_view_width;
#if XASH_IOS
	dy *= (float)refState.height / touch_view_height;
#endif
	return Touch_ControlsEvent( type, fingerID, x, y, dx, dy );
}

void Touch_GetMove( float *forward, float *side, float *pitch, float *yaw )
{
	if( !touch.move_stick || touch.move_finger != -1 )
	{
		*forward += touch.forward;
		*side += touch.side;
	}
	*pitch += touch.pitch;
	*yaw += touch.yaw;
	touch.yaw = touch.pitch = 0;
}

void Touch_GetLookStickMove( float *pitch, float *yaw )
{
	if( touch.look_finger != -1 && touch.look_stick && touch.state == state_none && cls.key_dest == key_game && ( touch_enable.value || touch.clientonly ))
	{
		float speed = Q_max( 0, touch_lookjoy_speed.value );
		// Use wall time rather than host_framerate; cap a stall's first turn.
		float time = bound( 0, host.realframetime, 0.1f );
		float magnitude = sqrtf( touch.look_side * touch.look_side + touch.look_forward * touch.look_forward );
		float curve = bound( 1, touch_lookjoy_curve.value, 3 );
		float response = powf( bound( 0, magnitude, 1 ), curve - 1 );
		// The menu uses m_pitch's sign for inversion. Its mouse-specific scale
		// does not apply here; touch_pitch/touch_yaw supply touch sensitivity.
		float invert = m_pitch.value < 0 ? -1 : 1;
		if( touch.precision )
			speed *= touch_precise_amount.value;
		// Use the existing input accumulator exactly once. Half-Life's client
		// applies aim/zoom sensitivity after receiving these angular deltas.
		// 120 is the default touch_yaw reference; the default pitch of 90 gives
		// a slower vertical rate while retaining existing profile adjustments.
		*yaw -= touch.look_side * response * speed * time * touch_yaw.value / 120.0f;
		*pitch -= touch.look_forward * response * speed * time * touch_pitch.value / 120.0f * invert;
	}
}

void Touch_KeyEvent( int key, int down )
{
	static float lx, ly;
	static int kidNamedFinger = -1;
	touchEventType event;
	float x, y;
	int finger, xi, yi;

	if( !Touch_WantVisibleCursor( ))
		return;

	if( !key )
	{
		if( kidNamedFinger < 0 )
			return;

		finger = kidNamedFinger;
		event  = event_motion;
	}
	else
	{
		finger = key == K_MOUSE1 ? 0 : 1;
		if( down )
		{
			event = event_down;
			kidNamedFinger = finger;
		}
		else
		{
			event = event_up;
			kidNamedFinger = -1;
		}
	}

	Platform_GetMousePos( &xi, &yi );

	x = xi / (float)refState.width;
	y = yi / (float)refState.height;

	// Con_DPrintf( "event %d %.2f %.2f %.2f %.2f\n", event, x, y, x - lx, y - ly );

	IN_TouchEvent( event, finger, x, y, x - lx, y - ly );

	lx = x;
	ly = y;
}

qboolean Touch_WantVisibleCursor( void )
{
	return ( touch_enable.value && touch_emulate.value ) || touch.clientonly || touch_in_menu.value;
}

void Touch_Shutdown( void )
{
	if( !touch.initialized )
		return;
	Touch_RemoveAll_f();
	Cmd_RemoveCommand( "touch_addbutton" );
	Cmd_RemoveCommand( "touch_removebutton" );
	Cmd_RemoveCommand( "touch_enableedit" );
	Cmd_RemoveCommand( "touch_togglecrouch" );
	Cmd_RemoveCommand( "touch_disableedit" );
	Cmd_RemoveCommand( "touch_settexture" );
	Cmd_RemoveCommand( "touch_setcolor" );
	Cmd_RemoveCommand( "touch_setcommand" );
	Cmd_RemoveCommand( "touch_setflags" );
	Cmd_RemoveCommand( "touch_show" );
	Cmd_RemoveCommand( "touch_hide" );
	Cmd_RemoveCommand( "touch_list" );
	Cmd_RemoveCommand( "touch_removeall" );
	Cmd_RemoveCommand( "touch_loaddefaults" );
	Cmd_RemoveCommand( "touch_roundall" );
	Cmd_RemoveCommand( "touch_exportconfig" );
	Cmd_RemoveCommand( "touch_set_stroke" );
	Cmd_RemoveCommand( "touch_setclientonly" );
	Cmd_RemoveCommand( "touch_reloadconfig" );
	Cmd_RemoveCommand( "touch_writeconfig" );
	Cmd_RemoveCommand( "touch_generate_code" );

	touch.initialized = false;
	Mem_FreePool( &touch.mempool );
}

#endif // !XASH_NO_TOUCH
