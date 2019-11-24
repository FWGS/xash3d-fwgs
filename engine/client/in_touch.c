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

typedef enum
{
	touch_command, // just tap a button
	touch_move,    // like a joystick stick
	touch_joy,     // like a joystick stick, centered
	touch_dpad,    // only two directions
	touch_look,     // like a touchpad
	touch_wheel		// scroll-like
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
	// Touch button type: tap, stick or slider
	touchButtonType type;

	// Field of button
	float x1, y1, x2, y2;

	// Button texture
	int texture;
	rgba_t color;
	char texturefile[256];
	char command[256];
	char name[32];
	int finger;
	int flags;
	float fade;
	float fadespeed;
	float fadeend;
	float aspect;

	// Double-linked list
	struct touch_button_s *next;
	struct touch_button_s *prev;

} touch_button_t;

typedef struct touchdefaultbutton_s
{
	char name[32];
	char texturefile[256];
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

struct touch_s
{
	qboolean initialized;
	qboolean config_loaded;
	touchbuttonlist_t list_user, list_edit;
	byte *mempool;
	touchState state;

	int look_finger;
	int move_finger;
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

	// other features
	qboolean clientonly;
	rgba_t scolor;
	int swidth;
	qboolean precision;

	// textures
	int whitetexture;
	int joytexture; // touch indicator
	qboolean configchanged;
} touch;

touchdefaultbutton_t g_DefaultButtons[256];
int g_LastDefaultButton;

convar_t *touch_pitch;
convar_t *touch_yaw;
convar_t *touch_forwardzone;
convar_t *touch_sidezone;
convar_t *touch_nonlinear_look;
convar_t *touch_pow_mult;
convar_t *touch_pow_factor;
convar_t *touch_exp_mult;
convar_t *touch_grid_enable;
convar_t *touch_grid_count;
convar_t *touch_config_file;
convar_t *touch_enable;
convar_t *touch_in_menu;
convar_t *touch_joy_radius;
convar_t *touch_dpad_radius;
convar_t *touch_move_indicator;
convar_t *touch_highlight_r;
convar_t *touch_highlight_g;
convar_t *touch_highlight_b;
convar_t *touch_highlight_a;
convar_t *touch_precise_amount;
convar_t *touch_joy_texture;
convar_t *touch_emulate;

// code looks smaller with it
#define B(x) (button->x)
#define SCR_W ((float)refState.width)
#define SCR_H ((float)refState.height)
#define TO_SCRN_Y(x) (refState.height * (x))
#define TO_SCRN_X(x) (refState.width * (x))

int pfnDrawCharacter( int x, int y, int number, int r, int g, int b );
static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  );
static void IN_TouchEditClear( void );
static void Touch_InitConfig( void );

void Touch_WriteConfig( void )
{
	file_t	*f;
	char newconfigfile[64];
	char oldconfigfile[64];

	if( !touch.list_user.first ) return;

	if( Sys_CheckParm( "-nowriteconfig" ) || !touch.configchanged || !touch.config_loaded )
		return;

	Con_DPrintf( "Touch_WriteConfig(): %s\n", touch_config_file->string );

	Q_snprintf( newconfigfile, 64, "%s.new", touch_config_file->string );
	Q_snprintf( oldconfigfile, 64, "%s.bak", touch_config_file->string );

	f = FS_Open( newconfigfile, "w", true );
	if( f )
	{
		touch_button_t *button;
		FS_Printf( f, "//=======================================================================\n");
		FS_Printf( f, "//\tCopyright SDLash3D team & XashXT group %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
		FS_Printf( f, "//\t\t\ttouchscreen config\n" );
		FS_Printf( f, "//=======================================================================\n" );
		FS_Printf( f, "\ntouch_config_file \"%s\"\n", touch_config_file->string );
		FS_Printf( f, "\n// touch cvars\n" );
		FS_Printf( f, "\n// sensitivity settings\n" );
		FS_Printf( f, "touch_pitch \"%f\"\n", touch_pitch->value );
		FS_Printf( f, "touch_yaw \"%f\"\n", touch_yaw->value );
		FS_Printf( f, "touch_forwardzone \"%f\"\n", touch_forwardzone->value );
		FS_Printf( f, "touch_sidezone \"%f\"\n", touch_sidezone->value );
		FS_Printf( f, "touch_nonlinear_look \"%d\"\n", CVAR_TO_BOOL(touch_nonlinear_look));
		FS_Printf( f, "touch_pow_factor \"%f\"\n", touch_pow_factor->value );
		FS_Printf( f, "touch_pow_mult \"%f\"\n", touch_pow_mult->value );
		FS_Printf( f, "touch_exp_mult \"%f\"\n", touch_exp_mult->value );		FS_Printf( f, "\n// grid settings\n" );
		FS_Printf( f, "touch_grid_count \"%d\"\n", (int)touch_grid_count->value );
		FS_Printf( f, "touch_grid_enable \"%d\"\n", CVAR_TO_BOOL(touch_grid_enable));
		FS_Printf( f, "\n// global overstroke (width, r, g, b, a)\n" );
		FS_Printf( f, "touch_set_stroke %d %d %d %d %d\n", touch.swidth, touch.scolor[0], touch.scolor[1], touch.scolor[2], touch.scolor[3] );
		FS_Printf( f, "\n// highlight when pressed\n" );
		FS_Printf( f, "touch_highlight_r \"%f\"\n", touch_highlight_r->value );
		FS_Printf( f, "touch_highlight_g \"%f\"\n", touch_highlight_g->value );
		FS_Printf( f, "touch_highlight_b \"%f\"\n", touch_highlight_b->value );
		FS_Printf( f, "touch_highlight_a \"%f\"\n", touch_highlight_a->value );
		FS_Printf( f, "\n// _joy and _dpad options\n" );
		FS_Printf( f, "touch_dpad_radius \"%f\"\n", touch_dpad_radius->value );
		FS_Printf( f, "touch_joy_radius \"%f\"\n", touch_joy_radius->value );
		FS_Printf( f, "\n// how much slowdown when Precise Look button pressed\n" );
		FS_Printf( f, "touch_precise_amount \"%f\"\n", touch_precise_amount->value );
		FS_Printf( f, "\n// enable/disable move indicator\n" );
		FS_Printf( f, "touch_move_indicator \"%f\"\n", touch_move_indicator->value );

		FS_Printf( f, "\n// reset menu state when execing config\n" );
		FS_Printf( f, "touch_setclientonly 0\n" );
		FS_Printf( f, "\n// touch buttons\n" );
		FS_Printf( f, "touch_removeall\n" );

		for( button = touch.list_user.first; button; button = button->next )
		{
			string newCommand;
			int flags = button->flags;

			if( flags & TOUCH_FL_CLIENT )
				continue; //skip temporary buttons

			if( flags & TOUCH_FL_DEF_SHOW )
				flags &= ~TOUCH_FL_HIDE;

			if( flags & TOUCH_FL_DEF_HIDE )
				flags |= TOUCH_FL_HIDE;

			/// TODO: EscapeCommand
			//Com_EscapeCommand( newCommand, B(command), MAX_STRING );
			Q_strncpy( newCommand, B(command), MAX_STRING );


			FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %f %f %f %f %d %d %d %d %d\n", 
				B(name), B(texturefile), newCommand,
				B(x1), B(y1), B(x2), B(y2),
				B(color[0]), B(color[1]), B(color[2]), B(color[3]), flags );
		}

		FS_Close( f );

		FS_Delete( oldconfigfile );
		FS_Rename( touch_config_file->string, oldconfigfile );
		FS_Delete( touch_config_file->string );
		FS_Rename( newconfigfile, touch_config_file->string );
	}
	else Con_Printf( S_ERROR "Couldn't write %s.\n", touch_config_file->string );
}

void Touch_ExportConfig_f( void )
{
	file_t	*f;
	const char *name;

	if( Cmd_Argc() != 2 )
	{
		Con_Printf( "Usage: touch_exportconfig <name>\n" );
		return;
	}

	if( !touch.list_user.first ) return;

	name = Cmd_Argv( 1 );

	Con_Reportf( "Exporting config to %s\n", name );
	f = FS_Open( name, "w", true );
	if( f )
	{
		char profilename[256];
		char profilebase[256];
		touch_button_t *button;
		if( Q_strstr( name, "touch_presets/" ) )
		{
			COM_FileBase( name, profilebase );
			Q_snprintf( profilename, 256, "touch_profiles/%s (copy).cfg", profilebase );
		}
		else Q_strncpy( profilename, name, 256 );
		FS_Printf( f, "//=======================================================================\n");
		FS_Printf( f, "//\tCopyright SDLash3D team & XashXT group %s ©\n", Q_timestamp( TIME_YEAR_ONLY ));
		FS_Printf( f, "//\t\t\ttouchscreen preset\n" );
		FS_Printf( f, "//=======================================================================\n" );
		FS_Printf( f, "\ntouch_config_file \"%s\"\n", profilename );
		FS_Printf( f, "\n// touch cvars\n" );
		FS_Printf( f, "\n// sensitivity settings\n" );
		FS_Printf( f, "touch_pitch \"%f\"\n", touch_pitch->value );
		FS_Printf( f, "touch_yaw \"%f\"\n", touch_yaw->value );
		FS_Printf( f, "touch_forwardzone \"%f\"\n", touch_forwardzone->value );
		FS_Printf( f, "touch_sidezone \"%f\"\n", touch_sidezone->value );
		FS_Printf( f, "touch_nonlinear_look \"%d\"\n", CVAR_TO_BOOL(touch_nonlinear_look) );
		FS_Printf( f, "touch_pow_factor \"%f\"\n", touch_pow_factor->value );
		FS_Printf( f, "touch_pow_mult \"%f\"\n", touch_pow_mult->value );
		FS_Printf( f, "touch_exp_mult \"%f\"\n", touch_exp_mult->value );
		FS_Printf( f, "\n// grid settings\n" );
		FS_Printf( f, "touch_grid_count \"%d\"\n", (int)touch_grid_count );
		FS_Printf( f, "touch_grid_enable \"%d\"\n", CVAR_TO_BOOL(touch_grid_enable) );
		FS_Printf( f, "\n// global overstroke (width, r, g, b, a)\n" );
		FS_Printf( f, "touch_set_stroke %d %d %d %d %d\n", touch.swidth, touch.scolor[0], touch.scolor[1], touch.scolor[2], touch.scolor[3] );
		FS_Printf( f, "\n// highlight when pressed\n" );
		FS_Printf( f, "touch_highlight_r \"%f\"\n", touch_highlight_r->value );
		FS_Printf( f, "touch_highlight_g \"%f\"\n", touch_highlight_g->value );
		FS_Printf( f, "touch_highlight_b \"%f\"\n", touch_highlight_b->value );
		FS_Printf( f, "touch_highlight_a \"%f\"\n", touch_highlight_a->value );
		FS_Printf( f, "\n// _joy and _dpad options\n" );
		FS_Printf( f, "touch_dpad_radius \"%f\"\n", touch_dpad_radius->value );
		FS_Printf( f, "touch_joy_radius \"%f\"\n", touch_joy_radius->value );
		FS_Printf( f, "\n// how much slowdown when Precise Look button pressed\n" );
		FS_Printf( f, "touch_precise_amount \"%f\"\n", touch_precise_amount->value );
		FS_Printf( f, "\n// enable/disable move indicator\n" );
		FS_Printf( f, "touch_move_indicator \"%f\"\n", touch_move_indicator->value );

		FS_Printf( f, "\n// reset menu state when execing config\n" );
		FS_Printf( f, "touch_setclientonly 0\n" );
		FS_Printf( f, "\n// touch buttons\n" );
		FS_Printf( f, "touch_removeall\n" );
		for( button = touch.list_user.first; button; button = button->next )
		{
			string newCommand;
			float aspect;
			int flags = button->flags;
			if( flags & TOUCH_FL_CLIENT )
				continue; //skip temporary buttons
			if( flags & TOUCH_FL_DEF_SHOW )
				flags &= ~TOUCH_FL_HIDE;
			if( flags & TOUCH_FL_DEF_HIDE )
				flags |= TOUCH_FL_HIDE;

			aspect = ( B(y2) - B(y1) ) / ( ( B(x2) - B(x1) ) /(SCR_H/SCR_W) );

			/// TODO: EscapeCommand
			//Com_EscapeCommand( newCommand, B(command), MAX_STRING );
			Q_strncpy( newCommand, B(command), MAX_STRING );

			FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %f %f %f %f %d %d %d %d %d %f\n", 
				B(name), B(texturefile), newCommand,
				B(x1), B(y1), B(x2), B(y2),
				B(color[0]), B(color[1]), B(color[2]), B(color[3]), flags, aspect );
		}
		FS_Printf( f, "\n// round button coordinates to grid\n" );
		FS_Printf( f, "touch_roundall\n" );
		FS_Close( f );
	}
	else Con_Printf( S_ERROR "Couldn't write %s.\n", name );
}

void Touch_GenetateCode_f( void )
{
	touch_button_t *button;
	rgba_t c = {0,0,0,0};

	if( Cmd_Argc() != 1 )
	{
		Con_Printf( "Usage: touch_generate_code\n" );
		return;
	}

	if( !touch.list_user.first ) return;

	for( button = touch.list_user.first; button; button = button->next )
	{
		float aspect;
		int flags = button->flags;
		if( flags & TOUCH_FL_CLIENT )
			continue; //skip temporary buttons
		if( flags & TOUCH_FL_DEF_SHOW )
			flags &= ~TOUCH_FL_HIDE;
		if( flags & TOUCH_FL_DEF_HIDE )
			flags |= TOUCH_FL_HIDE;

		aspect = ( B(y2) - B(y1) ) / ( ( B(x2) - B(x1) ) /(SCR_H/SCR_W) );
		if( memcmp( &c, &B(color), sizeof( rgba_t ) ) )
		{
			Con_Printf( "unsigned char color[] = { %d, %d, %d, %d };\n", B(color[0]), B(color[1]), B(color[2]), B(color[3]) );
			memcpy( &c, &B(color), sizeof( rgba_t ) );
		}
		Con_Printf( "TOUCH_ADDDEFAULT( \"%s\", \"%s\", \"%s\", %f, %f, %f, %f, color, %d, %f, %d );\n",
			B(name), B(texturefile), B(command),
			B(x1), B(y1), B(x2), B(y2), (B(type) == touch_command)?(fabs( aspect  - 1.0f) < 0.0001)?2:1:0, aspect, flags );
	}
}

void Touch_RoundAll_f( void )
{
	touch_button_t *button;
	if( !touch_grid_enable->value )
		return;
	for( button = touch.list_user.first; button; button = button->next )
		IN_TouchCheckCoords( &B(x1), &B(y1), &B(x2), &B(y2) );
}

void Touch_ListButtons_f( void )
{
	touch_button_t *button;
	Touch_InitConfig();

	for( button = touch.list_user.first; button; button = button->next )
	{
		Con_Printf( "%s %s %s %f %f %f %f %d %d %d %d %d\n",
			B(name), B(texturefile), B(command),
			B(x1), B(y1), B(x2), B(y2),
			B(color[0]), B(color[1]), B(color[2]), B(color[3]), B(flags) );
		if( B(flags) & TOUCH_FL_CLIENT)
			continue;
		UI_AddTouchButtonToList( B(name), B(texturefile), B(command),B(color), B(flags) );
	}
	touch.configchanged = true;
}

void Touch_Stroke_f( void )
{
	touch.swidth = Q_atoi( Cmd_Argv( 1 ) );
	MakeRGBA( touch.scolor, Q_atoi( Cmd_Argv( 2 ) ), Q_atoi( Cmd_Argv( 3 ) ), Q_atoi( Cmd_Argv( 4 ) ), Q_atoi( Cmd_Argv( 5 ) ) );
}

touch_button_t *Touch_FindButton( touchbuttonlist_t *list, const char *name )
{
	touch_button_t *button;

	for ( button = list->first; button; button = button->next )
		if( !Q_strncmp( button->name, name, 32 ) )
			return button;
	return NULL;
}

touch_button_t *Touch_FindFirst( touchbuttonlist_t *list, const char *name )
{
	touch_button_t *button;

	for ( button = list->first; button; button = button->next )
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
			return button;
	return NULL;
}

void Touch_SetClientOnly( qboolean state )
{
	touch.clientonly = state;
	host.mouse_visible = state;

	touch.move_finger = touch.look_finger = -1;
	touch.forward = touch.side = 0;

	/// TODO: touch sdl platform
#if 0
	if( state )
	{
		SDL_SetRelativeMouseMode( SDL_FALSE );
		SDL_ShowCursor( true );
		IN_DeactivateMouse();
	}
	else
	{
		SDL_ShowCursor( false );
		SDL_GetRelativeMouseState( 0, 0 );
	}
#endif
}

void Touch_SetClientOnly_f( void )
{
	Touch_SetClientOnly( Q_atoi( Cmd_Argv( 1 ) ) );
}

void Touch_RemoveButtonFromList( touchbuttonlist_t *list, const char *name )
{
	touch_button_t *button;

	IN_TouchEditClear();

	while( ( button = Touch_FindFirst( &touch.list_user, name ) ) )
	{
		if( button->prev )
			button->prev->next = button->next;
		else
			list->first = button->next;
		if( button->next )
			button->next->prev = button->prev;
		else
			list->last = button->prev;
		Mem_Free( button );
	}

}

void Touch_RemoveButton( const char *name )
{
	Touch_RemoveButtonFromList( &touch.list_user, name );
}

void IN_TouchRemoveButton_f( void )
{
	Touch_RemoveButton( Cmd_Argv( 1 ) );
}

void Touch_ClearList( touchbuttonlist_t *list )
{
	while( list->first )
	{
		touch_button_t *remove = list->first;
		list->first = list->first->next;
		Mem_Free ( remove );
	}
	list->first = list->last = NULL;
}

void Touch_RemoveAll_f( void )
{
	IN_TouchEditClear();
	Touch_ClearList( &touch.list_user );
}

void Touch_SetColor( touchbuttonlist_t *list, const char *name, byte *color )
{
	touch_button_t *button;
	for( button = list->first; button; button = button->next )
	{
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
			MakeRGBA( button->color, color[0], color[1], color[2], color[3] );
	}
}

void Touch_SetTexture( touchbuttonlist_t *list, const char *name, const char *texture )
{
	touch_button_t *button = Touch_FindButton( list, name );
	if( !button )
		return;
	button->texture = -1; // mark for texture load
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
}

void Touch_SetCommand( touch_button_t *button, const char *command )
{
	Q_strncpy( button->command, command, sizeof( button->command ) );

	if( !Q_strcmp( command, "_look" ) )
		button->type = touch_look;
	if( !Q_strcmp( command, "_move" ) )
		button->type = touch_move;
	if( !Q_strcmp( command, "_joy" ) )
		button->type = touch_joy;
	if( !Q_strcmp( command, "_dpad" ) )
		button->type = touch_dpad;
	if( Q_stricmpext( "_wheel *", command ) || Q_stricmpext( "_hwheel *", command ) )
		button->type = touch_wheel;
}

void Touch_HideButtons( const char *name, byte hide )
{
	touch_button_t *button;

	for( button = touch.list_user.first; button; button = button->next)
	{
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
		{
			if( hide )
				button->flags |= TOUCH_FL_HIDE;
			else
				button->flags &= ~TOUCH_FL_HIDE;
		}
	}
	
}

void Touch_ToggleSelection_f( void )
{
	if( touch.selection )
		touch.selection->flags ^= TOUCH_FL_HIDE;
}

void Touch_Hide_f( void )
{
	Touch_HideButtons( Cmd_Argv( 1 ), true );
}

void Touch_Show_f( void )
{
	Touch_HideButtons( Cmd_Argv( 1 ), false );
}

void Touch_FadeButtons( touchbuttonlist_t *list, const char *name, float speed, float end, float start  )
{
	touch_button_t *button;
	for( button = list->first; button; button = button->next)
	{
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
		{
			if( start >= 0 )
				button->fade = start;
			button->fadespeed = speed;
			button->fadeend = end;
		}
	}
}
void Touch_Fade_f( void )
{
	float start = -1;
	if( Cmd_Argc() < 4 )
		return;
	if( Cmd_Argc() > 4 )
		start = Q_atof( Cmd_Argv( 4 ) );
	Touch_FadeButtons( &touch.list_user, Cmd_Argv( 1 ), Q_atof( Cmd_Argv( 2 )), Q_atof( Cmd_Argv( 3 )), start );
}

void Touch_SetColor_f( void )
{
	rgba_t color;
	if( Cmd_Argc() == 6 )
	{
		MakeRGBA( color,  Q_atoi( Cmd_Argv(2) ), Q_atoi( Cmd_Argv(3) ), Q_atoi( Cmd_Argv(4) ), Q_atoi( Cmd_Argv(5) ) );
		Touch_SetColor( &touch.list_user, Cmd_Argv(1), color );
		return;
	}
	Con_Printf( "Usage: touch_setcolor <pattern> <r> <g> <b> <a>\n" );
}

void Touch_SetTexture_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		Touch_SetTexture( &touch.list_user, Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}
	Con_Printf( "Usage: touch_settexture <name> <file>\n" );
}

void Touch_SetFlags_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		touch_button_t *button = Touch_FindButton( &touch.list_user, Cmd_Argv( 1 ) );
		if( button )
			button->flags = Q_atoi( Cmd_Argv( 2 ) );
		return;
	}
	Con_Printf( "Usage: touch_setflags <name> <file>\n" );
}

void Touch_SetCommand_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		touch_button_t *button = Touch_FindButton( &touch.list_user, Cmd_Argv(1) );

		if( !button )
			Con_Printf( S_ERROR "no such button" );
		else
			Touch_SetCommand( button, Cmd_Argv( 2 ) );

		return;
	}
	Con_Printf( "Usage: touch_setcommand <name> <command>\n" );
}
void Touch_ReloadConfig_f( void )
{
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;
	if( touch.selection )
		touch.selection->finger = -1;
	touch.edit = touch.selection = NULL;
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;

	Cbuf_AddText( va("exec %s\n", touch_config_file->string ) );
}

touch_button_t *Touch_AddButton( touchbuttonlist_t *list, const char *name,  const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color )
{
	touch_button_t *button = Mem_Calloc( touch.mempool, sizeof( touch_button_t ) );
	button->texture = -1;
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
	Q_strncpy( button->name, name, 32 );
	Touch_RemoveButtonFromList( list, name ); //replace if exist
	button->x1 = x1;
	button->y1 = y1;
	button->x2 = x2;
	button->y2 = y2;
	MakeRGBA( button->color, color[0], color[1], color[2], color[3] );
	button->command[0] = 0;
	button->flags = 0;
	button->fade = 1;

	Touch_SetCommand( button, command );

	button->finger = -1;
	button->next = NULL;
	button->prev = list->last;
	if( list->last )
		list->last->next = button;
	list->last = button;

	if( !list->first )
		list->first = button;

	return button;
}

void Touch_AddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
{
	touch_button_t *button;

	if( !touch.initialized )
		return;
	if( round )
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
	if( round == round_aspect )
	{
		y2 = y1 + ( x2 - x1 ) * (SCR_W/SCR_H) * aspect;
	}
	button = Touch_AddButton( &touch.list_user, name, texture, command, x1, y1, x2, y2, color );
	button->flags |= flags | TOUCH_FL_CLIENT | TOUCH_FL_NOEDIT;
	button->aspect = aspect;
}

void Touch_LoadDefaults_f( void )
{
	int i;
	for( i = 0; i < g_LastDefaultButton; i++ )
	{
		touch_button_t *button;
		float x1 = g_DefaultButtons[i].x1, 
			  y1 = g_DefaultButtons[i].y1,
			  x2 = g_DefaultButtons[i].x2,
			  y2 = g_DefaultButtons[i].y2; 
		
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
		
		if( g_DefaultButtons[i].aspect && g_DefaultButtons[i].round == round_aspect )
		{
			if( g_DefaultButtons[i].texturefile[0] == '#' )
				y2 = y1 + ( (float)clgame.scrInfo.iCharHeight / (float)clgame.scrInfo.iHeight ) * g_DefaultButtons[i].aspect + touch.swidth*2/SCR_H;
			else
				y2 = y1 + ( x2 - x1 ) * (SCR_W/SCR_H) * g_DefaultButtons[i].aspect;
		}
		
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
		button = Touch_AddButton( &touch.list_user, g_DefaultButtons[i].name, g_DefaultButtons[i].texturefile, g_DefaultButtons[i].command, x1, y1, x2, y2, g_DefaultButtons[i].color );
		button->flags |= g_DefaultButtons[i].flags;
		button->aspect = g_DefaultButtons[i].aspect;
	}
}


// Add default button from client
void Touch_AddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
{
	if( g_LastDefaultButton >= 255 )
		return;
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].name, name, 32 );
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].texturefile, texturefile, 256 );
	Q_strncpy( g_DefaultButtons[g_LastDefaultButton].command, command, 256 );
	g_DefaultButtons[g_LastDefaultButton].x1 = x1;
	g_DefaultButtons[g_LastDefaultButton].y1 = y1;
	g_DefaultButtons[g_LastDefaultButton].x2 = x2;
	g_DefaultButtons[g_LastDefaultButton].y2 = y2;
	MakeRGBA( g_DefaultButtons[g_LastDefaultButton].color, color[0], color[1], color[2], color[3] );
	g_DefaultButtons[g_LastDefaultButton].round = round;
	g_DefaultButtons[g_LastDefaultButton].aspect = aspect;
	g_DefaultButtons[g_LastDefaultButton].flags = flags;
	g_LastDefaultButton++;
}

// Client may remove all default buttons from engine
void Touch_ResetDefaultButtons( void )
{
	g_LastDefaultButton = 0;
}
void Touch_AddButton_f( void )
{
	rgba_t color;
	int argc = Cmd_Argc( );

	if( argc >= 12 )
	{
		touch_button_t *button;
		MakeRGBA( color, Q_atoi( Cmd_Argv(8) ), Q_atoi( Cmd_Argv(9) ), 
			Q_atoi( Cmd_Argv(10) ), Q_atoi( Cmd_Argv(11) ) );
		button = Touch_AddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
			Q_atof( Cmd_Argv(4) ), Q_atof( Cmd_Argv(5) ), 
			Q_atof( Cmd_Argv(6) ), Q_atof( Cmd_Argv(7) ) ,
			color );
		if( argc >= 13 )
			button->flags = Q_atoi( Cmd_Argv(12) );
		if( argc >= 14 )
		{
			// Recalculate button coordinates aspect ratio
			// This is feature for distributed configs
			float aspect = Q_atof( Cmd_Argv(13) );
			if( aspect )
			{
				if( B(texturefile)[0] != '#' )
					B(y2) = B(y1) + ( B(x2) - B(x1) ) * (SCR_W/SCR_H) * aspect;
				B(aspect) = aspect;
			}
		}
				
		return;
	}	
	if( argc == 8 )
	{
		MakeRGBA( color, 255, 255, 255, 255 );
		Touch_AddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
			Q_atof( Cmd_Argv(4) ), Q_atof( Cmd_Argv(5) ), 
			Q_atof( Cmd_Argv(6) ), Q_atof( Cmd_Argv(7) ),
			color );
		return;
	}
	if( argc == 4 )
	{
		MakeRGBA( color, 255, 255, 255, 255 );
		Touch_AddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3), 0.4, 0.4, 0.6, 0.6, color );
		return;
	}
	Con_Printf( "Usage: touch_addbutton <name> <texture> <command> [<x1> <y1> <x2> <y2> [ r g b a ] ]\n" );
}

void Touch_EnableEdit_f( void )
{
	if( touch.state == state_none )
		touch.state = state_edit;
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;
	touch.move_button = NULL;
	touch.configchanged = true;
}

void Touch_DisableEdit_f( void )
{
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;
	if( touch.selection )
		touch.selection->finger = -1;
	touch.edit = touch.selection = NULL;
	touch.resize_finger = touch.move_finger = touch.look_finger = touch.wheel_finger = -1;

	if( CVAR_TO_BOOL(touch_in_menu) )
	{
		Cvar_Set( "touch_in_menu", "0" );
	}
	else  if( cls.key_dest ==  key_game )
		Touch_WriteConfig();
}

void Touch_DeleteProfile_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( "Usage: touch_deleteprofile <name>\n" );
		return;
	}

	// delete profile
	FS_Delete( va( "touch_profiles/%s.cfg", Cmd_Argv( 1 )));
}

void Touch_InitEditor( void )
{
	float x = 0.1f * (SCR_H/SCR_W);
	float y = 0.05f;

	Touch_ClearList( &touch.list_edit );

	Touch_AddButton( &touch.list_edit, "close", "touch_default/edit_close.tga", "touch_disableedit", 0, y, x, y + 0.1f, (byte*)"\xff\xff\xff\xff" )->flags |= TOUCH_FL_NOEDIT;
	Touch_AddButton( &touch.list_edit, "close", "#Close and save", "", x, y, x + 0.2f, y + 0.1f, (byte*)"\xff\xff\xff\xff" )->flags |= TOUCH_FL_NOEDIT;
	y += 0.2f;
	Touch_AddButton( &touch.list_edit, "cancel", "touch_default/edit_reset.tga", "touch_reloadconfig", 0, y, x, y + 0.1f, (byte*)"\xff\xff\xff\xff" )->flags |= TOUCH_FL_NOEDIT;
	Touch_AddButton( &touch.list_edit, "close", "#Cancel and reset", "", x, y, x + 0.2f, y + 0.1f, (byte*)"\xff\xff\xff\xff" )->flags |= TOUCH_FL_NOEDIT;
	y += 0.2f;
	touch.hidebutton = Touch_AddButton( &touch.list_edit, "showhide", "touch_default/edit_hide.tga", "touch_toggleselection", 0, y, x, y + 0.1f, (byte*)"\xff\xff\xff\xff" );
	touch.hidebutton->flags |= TOUCH_FL_HIDE | TOUCH_FL_NOEDIT;
}

void Touch_Init( void )
{
	rgba_t color;
	if( touch.initialized )
		return;
	touch.mempool = Mem_AllocPool( "Touch" );
	//touch.first = touch.last = NULL;
	Con_Printf( "IN_TouchInit()\n");
	touch.move_finger = touch.resize_finger = touch.look_finger = touch.wheel_finger = -1;
	touch.state = state_none;
	touch.showeditbuttons = true;
	touch.clientonly = false;
	touch.precision = false;
	MakeRGBA( touch.scolor, 255, 255, 255, 255 );
	touch.swidth = 1;
	g_LastDefaultButton = 0;

	touch.list_edit.first = touch.list_edit.last = NULL;
	touch.list_user.first = touch.list_user.last = NULL;

	// fill default buttons list
	MakeRGBA( color, 255, 255, 255, 255 );
	Touch_AddDefaultButton( "look", "", "_look", 0.500000, 0.000000, 1.000000, 1, color, 0, 0, 0 );
	Touch_AddDefaultButton( "move", "", "_move", 0.000000, 0.000000, 0.500000, 1, color, 0, 0, 0 );
	Touch_AddDefaultButton( "invnext", "touch_default/next_weap.tga", "invnext", 0.000000, 0.530200, 0.120000, 0.757428, color, 2, 1, 0 );
	Touch_AddDefaultButton( "invprev", "touch_default/prev_weap.tga", "invprev", 0.000000, 0.075743, 0.120000, 0.302971, color, 2, 1, 0 );
	Touch_AddDefaultButton( "use", "touch_default/use.tga", "+use", 0.880000, 0.454457, 1.000000, 0.681685, color, 2, 1, 0 );
	Touch_AddDefaultButton( "jump", "touch_default/jump.tga", "+jump", 0.880000, 0.227228, 1.000000, 0.454457, color, 2, 1, 0 );
	Touch_AddDefaultButton( "attack", "touch_default/shoot.tga", "+attack", 0.760000, 0.530200, 0.880000, 0.757428, color, 2, 1, 0 );
	Touch_AddDefaultButton( "attack2", "touch_default/shoot_alt.tga", "+attack2", 0.760000, 0.302971, 0.880000, 0.530200, color, 2, 1, 0 );
	Touch_AddDefaultButton( "loadquick", "touch_default/load.tga", "loadquick", 0.760000, 0.000000, 0.840000, 0.151486, color, 2, 1, 16 );
	Touch_AddDefaultButton( "savequick", "touch_default/save.tga", "savequick", 0.840000, 0.000000, 0.920000, 0.151486, color, 2, 1, 16 );
	Touch_AddDefaultButton( "messagemode", "touch_default/keyboard.tga", "messagemode", 0.840000, 0.000000, 0.920000, 0.151486, color, 2, 1, 8 );
	Touch_AddDefaultButton( "reload", "touch_default/reload.tga", "+reload", 0.000000, 0.302971, 0.120000, 0.530200, color, 2, 1, 0 );
	Touch_AddDefaultButton( "flashlight", "touch_default/flash_light_filled.tga", "impulse 100", 0.920000, 0.000000, 1.000000, 0.151486, color, 2, 1, 0 );
	Touch_AddDefaultButton( "scores", "touch_default/map.tga", "+showscores", 0.760000, 0.000000, 0.840000, 0.151486, color, 2, 1, 8 );
	Touch_AddDefaultButton( "show_numbers", "touch_default/show_weapons.tga", "exec touch_default/numbers.cfg", 0.440000, 0.833171, 0.520000, 0.984656, color, 2, 1, 0 );
	Touch_AddDefaultButton( "duck", "touch_default/crouch.tga", "+duck", 0.880000, 0.757428, 1.000000, 0.984656, color, 2, 1, 0 );
	Touch_AddDefaultButton( "tduck", "touch_default/tduck.tga", ";+duck", 0.560000, 0.833171, 0.620000, 0.946785, color, 2, 1, 0 );
	Touch_AddDefaultButton( "edit", "touch_default/settings.tga", "touch_enableedit", 0.420000, 0.000000, 0.500000, 0.151486, color, 2, 1, 32 );
	Touch_AddDefaultButton( "menu", "touch_default/menu.tga", "escape", 0.000000, 0.833171, 0.080000, 0.984656, color, 2, 1, 0 );

	
	Cmd_AddCommand( "touch_addbutton", Touch_AddButton_f, "add native touch button" );
	Cmd_AddCommand( "touch_removebutton", IN_TouchRemoveButton_f, "remove native touch button" );
	Cmd_AddCommand( "touch_enableedit", Touch_EnableEdit_f, "enable button editing mode" );
	Cmd_AddCommand( "touch_disableedit", Touch_DisableEdit_f, "disable button editing mode" );
	Cmd_AddCommand( "touch_settexture", Touch_SetTexture_f, "change button texture" );
	Cmd_AddCommand( "touch_setcolor", Touch_SetColor_f, "change button color" );
	Cmd_AddCommand( "touch_setcommand", Touch_SetCommand_f, "change button command" );
	Cmd_AddCommand( "touch_setflags", Touch_SetFlags_f, "change button flags (be careful)" );
	Cmd_AddCommand( "touch_show", Touch_Show_f, "show button" );
	Cmd_AddCommand( "touch_hide", Touch_Hide_f, "hide button" );
	Cmd_AddCommand( "touch_list", Touch_ListButtons_f, "list buttons" );
	Cmd_AddCommand( "touch_removeall", Touch_RemoveAll_f, "remove all buttons" );
	Cmd_AddCommand( "touch_loaddefaults", Touch_LoadDefaults_f, "generate config from defaults" );
	Cmd_AddCommand( "touch_roundall", Touch_RoundAll_f, "round all buttons coordinates to grid" );
	Cmd_AddCommand( "touch_exportconfig", Touch_ExportConfig_f, "export config keeping aspect ratio" );
	Cmd_AddCommand( "touch_set_stroke", Touch_Stroke_f, "set global stroke width and color" );
	Cmd_AddCommand( "touch_setclientonly", Touch_SetClientOnly_f, "when 1, only client buttons are shown" );
	Cmd_AddCommand( "touch_reloadconfig", Touch_ReloadConfig_f, "load config, not saving changes" );
	Cmd_AddCommand( "touch_writeconfig", Touch_WriteConfig, "save current config" );
	Cmd_AddCommand( "touch_deleteprofile", Touch_DeleteProfile_f, "delete profile by name" );
	Cmd_AddCommand( "touch_generate_code", Touch_GenetateCode_f, "create code sample for mobility API" );
	Cmd_AddCommand( "touch_fade", Touch_Fade_f, "start fade animation for selected buttons" );
	Cmd_AddCommand( "touch_toggleselection", Touch_ToggleSelection_f, "toggle vidibility on selected button in editor" );

	// not saved, just runtime state for scripting
	touch_in_menu = Cvar_Get( "touch_in_menu", "0", 0, "draw touch in menu (for internal use only)" );

	// sensitivity configuration
	touch_forwardzone = Cvar_Get( "touch_forwardzone", "0.06", 0, "forward touch zone" );
	touch_sidezone = Cvar_Get( "touch_sidezone", "0.06", 0, "side touch zone" );
	touch_pitch = Cvar_Get( "touch_pitch", "90", 0, "touch pitch sensitivity" );
	touch_yaw = Cvar_Get( "touch_yaw", "120", 0, "touch yaw sensitivity" );
	touch_nonlinear_look = Cvar_Get( "touch_nonlinear_look", "0", 0, "enable nonlinear touch look" );
	touch_pow_factor = Cvar_Get( "touch_pow_factor", "1.3", 0, "set > 1 to enable" );
	touch_pow_mult = Cvar_Get( "touch_pow_mult", "400.0", 0, "power multiplier, usually 200-1000" );
	touch_exp_mult = Cvar_Get( "touch_exp_mult", "0", 0, "exponent multiplier, usually 20-200, 0 to disable" );

	// touch.cfg
	touch_grid_count = Cvar_Get( "touch_grid_count", "50", 0, "touch grid count" );
	touch_grid_enable = Cvar_Get( "touch_grid_enable", "1", 0, "enable touch grid" );
	touch_config_file = Cvar_Get( "touch_config_file", "touch.cfg", FCVAR_ARCHIVE, "current touch profile file" );
	touch_precise_amount = Cvar_Get( "touch_precise_amount", "0.5", 0, "sensitivity multiplier for precise-look" );
	touch_highlight_r = Cvar_Get( "touch_highlight_r", "1.0", 0, "highlight r color" );
	touch_highlight_g = Cvar_Get( "touch_highlight_g", "1.0", 0, "highlight g color" );
	touch_highlight_b = Cvar_Get( "touch_highlight_b", "1.0", 0, "highlight b color" );
	touch_highlight_a = Cvar_Get( "touch_highlight_a", "1.0", 0, "highlight alpha" );
	touch_dpad_radius = Cvar_Get( "touch_dpad_radius", "1.0", 0, "dpad radius multiplier" );
	touch_joy_radius = Cvar_Get( "touch_joy_radius", "1.0", 0, "joy radius multiplier" );
	touch_move_indicator = Cvar_Get( "touch_move_indicator", "0.0", 0, "indicate move events (0 to disable)" );
	touch_joy_texture = Cvar_Get( "touch_joy_texture", "touch_default/joy.tga", 0, "texture for move indicator");

	// input devices cvar
	touch_enable = Cvar_Get( "touch_enable", DEFAULT_TOUCH_ENABLE, FCVAR_ARCHIVE, "enable touch controls" );
	touch_emulate = Cvar_Get( "touch_emulate", "0", FCVAR_ARCHIVE, "emulate touch with mouse" );

	/// TODO: touch sdl platform
	// SDL_SetHint( SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1" );

	touch.initialized = true;
}
//int pfnGetScreenInfo( SCREENINFO *pscrinfo );
static void Touch_InitConfig( void )
{
	if( !touch.initialized )
		return;

	if( !host.config_executed )
		return;

	if( touch.config_loaded )
		return;

	/// TODO: hud font
	//pfnGetScreenInfo( NULL ); //HACK: update hud screen parameters like iHeight
	if( FS_FileExists( touch_config_file->string, true ) )
		Cbuf_AddText( va( "exec \"%s\"\n", touch_config_file->string ) );
	else Touch_LoadDefaults_f( );

	Touch_InitEditor();
	touch.joytexture = ref.dllFuncs.GL_LoadTexture( touch_joy_texture->string, NULL, 0, TF_NOMIPMAP );
	touch.whitetexture = R_GetBuiltinTexture( REF_WHITE_TEXTURE );
	touch.configchanged = false;
	touch.config_loaded = true;
}
qboolean Touch_IsVisible( touch_button_t *button )
{
	if( !(button->flags & TOUCH_FL_CLIENT) && touch.clientonly )
		return false; // skip nonclient buttons in clientonly mode

	if( touch.state >= state_edit )
		return true; //!!! Draw when editor is open

	if( button->flags & TOUCH_FL_HIDE )
		return false; // skip hidden

	if( button->flags & TOUCH_FL_SP && CL_GetMaxClients() != 1 )
		return false; // skip singleplayer(load, save) buttons in multiplayer

	if( button->flags & TOUCH_FL_MP && CL_GetMaxClients() == 1 )
		return false; // skip multiplayer buttons in singleplayer

	return true;
	/*
	return ( !touch.clientonly || ( button->flags & TOUCH_FL_CLIENT) ) && 
	( 
	( touch.state >= state_edit )
	||( !( button->flags & TOUCH_FL_HIDE ) 
	&& ( !(button->flags & TOUCH_FL_SP) || ( CL_GetMaxClients() == 1 ) ) 
	&& ( !(button->flags & TOUCH_FL_MP) || ( CL_GetMaxClients() !=  1 ) ) ) 
	 );
	 */
}

void Touch_DrawTexture ( float x1, float y1, float x2, float y2, int texture, byte r, byte g, byte b, byte a )
{
	if( x1 >= x2 )
		return;

	if( y1 >= y2 )
		return;

	ref.dllFuncs.Color4ub( r, g, b, a );
	ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(x1),
		TO_SCRN_Y(y1),
		TO_SCRN_X(x2 - x1),
		TO_SCRN_Y(y2 - y1),
		0, 0, 1, 1, texture );
}

#define GRID_COUNT_X ((int)touch_grid_count->value)
#define GRID_COUNT_Y (((int)touch_grid_count->value) * SCR_H / SCR_W)
#define GRID_X (1.0f/GRID_COUNT_X)
#define GRID_Y (SCR_W/SCR_H/GRID_COUNT_X)
#define GRID_ROUND_X(x) ((float)round( x * GRID_COUNT_X ) / GRID_COUNT_X)
#define GRID_ROUND_Y(x) ((float)round( x * GRID_COUNT_Y ) / GRID_COUNT_Y)

static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  )
{
	/// TODO: grid check here
	if( *x2 - *x1 < GRID_X * 2 )
		*x2 = *x1 + GRID_X * 2;
	if( *y2 - *y1 < GRID_Y * 2)
		*y2 = *y1 + GRID_Y * 2;
	if( *x1 < 0 )
		*x2 -= *x1, *x1 = 0;
	if( *y1 < 0 )
		*y2 -= *y1, *y1 = 0;
	if( *y2 > 1 )
		*y1 -= *y2 - 1, *y2 = 1;
	if( *x2 > 1 )
		*x1 -= *x2 - 1, *x2 = 1;
	if ( CVAR_TO_BOOL(touch_grid_enable) )
	{
		*x1 = GRID_ROUND_X( *x1 );
		*x2 = GRID_ROUND_X( *x2 );
		*y1 = GRID_ROUND_Y( *y1 );
		*y2 = GRID_ROUND_Y( *y2 );
	}
}

float Touch_DrawCharacter( float x, float y, int number, float size )
{
	float	s1, s2, t1, t2, width, height;
	int	w, h;
	wrect_t *prc;
	if( !cls.creditsFont.valid )
		return 0;

	number &= 255;
	number = Con_UtfProcessChar( number );


	R_GetTextureParms( &w, &h, cls.creditsFont.hFontTexture );
	prc = &cls.creditsFont.fontRc[number];

	s1 = ((float)prc->left) / (float)w;
	t1 = ((float)prc->top) / (float)h;
	s2 = ((float)prc->right) / (float)w;
	t2 = ((float)prc->bottom) / (float)h;

	width = ((float)( prc->right - prc->left )) / 1024.0f * size;
	height = ((float)( prc->bottom - prc->top )) / 1024.0f * size;

	ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(x), TO_SCRN_Y(y), TO_SCRN_X(width), TO_SCRN_X(height), s1, t1, s2, t2, cls.creditsFont.hFontTexture );
	return width;
}

float Touch_DrawText( float x1, float y1, float x2, float y2, const char *s, byte *color, float size )
{
	float x = x1;
	float maxy = y2;
	float maxx;
	if( x2 )
		maxx = x2 - cls.creditsFont.charWidths['M'] / 1024.0f * size;
	else
		maxx = 1;
	
	if( !cls.creditsFont.valid )
		return GRID_X * 2;
	Con_UtfProcessChar( 0 );

	ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );

	// text is additive and alpha does not work
	ref.dllFuncs.Color4ub( color[0] * ( (float)color[3] /255.0f ), color[1] * ( (float)color[3] /255.0f ),
			color[2] * ( (float)color[3] /255.0f ), 255 );

	while( *s )
	{
		while( *s && ( *s != '\n' ) && ( *s != ';' ) && ( x1 < maxx ) )
			x1 += Touch_DrawCharacter( x1, y1, *s++, size );
		y1 += cls.creditsFont.charHeight / 1024.f * size / SCR_H * SCR_W;

		if( y1 >= maxy )
			break;

		if( *s == '\n' || *s == ';' )
			s++;
		x1 = x;
	}
	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
	return x1;
}

void Touch_DrawButtons( touchbuttonlist_t *list )
{
	touch_button_t *button;
	
	for( button = list->first; button; button = button->next )
	{
		if( Touch_IsVisible( button ) )
		{
			rgba_t color;
			MakeRGBA( color, B( color[0] ), B( color[1] ), B( color[2] ), B( color[3] ) );

			if( B( fadespeed ) )
			{
				button->fade += B( fadespeed ) * host.frametime;
				button->fade = bound( 0, B(fade), 1 );
				if( ( B( fade ) == 0 ) || ( B(fade) == 1 ) )
					B( fadespeed ) = 0;
				if( ( ( B( fade ) >= B( fadeend ) ) && ( B( fadespeed ) > 0 ) ) ||
					( ( B( fade ) <= B( fadeend ) ) && ( B( fadespeed ) < 0 ) ) )
					B( fadespeed ) = 0, B( fade ) = B( fadeend ) ;
			}

			if( ( B( finger ) != -1 ) && !( B( flags ) & TOUCH_FL_CLIENT ) )
			{
				color[0] = bound( 0,(float) color[0] * touch_highlight_r->value, 255 );
				color[1] = bound( 0,(float) color[1] * touch_highlight_g->value, 255 );
				color[2] = bound( 0,(float) color[2] * touch_highlight_b->value, 255 );
				color[3] = bound( 0,(float) color[3] * touch_highlight_a->value, 255 );
			}

			color[3] *= B( fade );
			if( button->texturefile[0] == '#' )
				Touch_DrawText( touch.swidth/SCR_W + B(x1), touch.swidth/SCR_H + B(y1), B(x2), B(y2), button->texturefile + 1, color, B( aspect )?B(aspect):1 );
			else if( button->texturefile[0] )
			{
				if( button->texture == -1 )
				{
					button->texture = ref.dllFuncs.GL_LoadTexture( button->texturefile, NULL, 0, TF_NOMIPMAP );
				}

				if( B(flags) & TOUCH_FL_DRAW_ADDITIVE )
					ref.dllFuncs.GL_SetRenderMode( kRenderTransAdd );

				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), B(texture), color[0], color[1], color[2], color[3] );

				ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
			}
			if( B(flags) & TOUCH_FL_STROKE )
			{
				ref.dllFuncs.Color4ub( touch.scolor[0], touch.scolor[1], touch.scolor[2], touch.scolor[3] * B( fade ) );
				ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(B(x1)),
					TO_SCRN_Y(B(y1)),
					touch.swidth,
					TO_SCRN_Y(B(y2)-B(y1)) - touch.swidth,
					0, 0, 1, 1, touch.whitetexture );
				ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(B(x1)) + touch.swidth,
					TO_SCRN_Y(B(y1)),
					TO_SCRN_X(B(x2)-B(x1)) - touch.swidth,
					touch.swidth,
					0, 0, 1, 1, touch.whitetexture );
				ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(B(x2))-touch.swidth,
					TO_SCRN_Y(B(y1)) + touch.swidth,
					touch.swidth,
					TO_SCRN_Y(B(y2)-B(y1)) - touch.swidth,
					0, 0, 1, 1, touch.whitetexture );
				ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(B(x1)),
					TO_SCRN_Y(B(y2))-touch.swidth,
					TO_SCRN_X(B(x2)-B(x1)) - touch.swidth,
					touch.swidth,
					0, 0, 1, 1, touch.whitetexture );
				ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
			}
		}
		if( touch.state >= state_edit && !( button->flags & TOUCH_FL_NOEDIT )  )
		{
			rgba_t color;
			if( !( button->flags & TOUCH_FL_HIDE ) )
				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), touch.whitetexture, 255, 255, 0, 32 );
			else
				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), touch.whitetexture, 128, 128, 128, 128 );
			MakeRGBA( color, 255, 255,127, 255 );
			Con_DrawString( TO_SCRN_X( B(x1) ), TO_SCRN_Y( B(y1) ), B(name), color );
		}
	}
	
}

void Touch_Draw( void )
{
	touch_button_t *button;

	if( !touch.initialized || (!CVAR_TO_BOOL(touch_enable) && !touch.clientonly) )
		return;

	Touch_InitConfig();

	if( cls.key_dest != key_game && !CVAR_TO_BOOL(touch_in_menu) )
		return;

	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );

	if( touch.state >= state_edit && CVAR_TO_BOOL(touch_grid_enable) )
	{
		float x;
		if( CVAR_TO_BOOL(touch_in_menu) )
			Touch_DrawTexture( 0, 0, 1, 1, touch.whitetexture, 32, 32, 32, 255 );
		else
			Touch_DrawTexture( 0, 0, 1, 1, touch.whitetexture, 0, 0, 0, 112 );
		ref.dllFuncs.Color4ub( 0, 224, 224, 112 );
		for ( x = 0; x < 1 ; x += GRID_X )
			ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X(x),
				0,
				1,
				TO_SCRN_Y(1),
				0, 0, 1, 1, touch.whitetexture );
		for ( x = 0; x < 1 ; x += GRID_Y )
			ref.dllFuncs.R_DrawStretchPic( 0,
				TO_SCRN_Y(x),
				TO_SCRN_X(1),
				1,
				0, 0, 1, 1, touch.whitetexture );
	}

	Touch_DrawButtons( &touch.list_user );

	if( touch.state >= state_edit )
	{
		rgba_t color;

		MakeRGBA( color, 255, 255, 255, 255 );

		if( touch.edit )
		{
			float	x1 = touch.edit->x1,
					y1 = touch.edit->y1,
					x2 = touch.edit->x2,
					y2 = touch.edit->y2;
			IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
			Touch_DrawTexture( x1, y1, x2, y2, touch.whitetexture, 0, 255, 0, 32 );
		}

		Touch_DrawTexture( 0, 0, GRID_X, GRID_Y, touch.whitetexture, 255, 255, 255, 64 );


		if( touch.showeditbuttons )
			Touch_DrawButtons( &touch.list_edit );

		/// TODO: move to mainui
		if( touch.selection )
		{
			button = touch.selection;
			Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), touch.whitetexture, 255, 0, 0, 64 );

			Con_DrawString( 0, TO_SCRN_Y(GRID_Y * 11), "Selection:", color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*12), "Name: ", color ),
											   TO_SCRN_Y(GRID_Y*12), B(name), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*13), "Texture: ", color ),
											   TO_SCRN_Y(GRID_Y*13), B(texturefile), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*14), "Command: ", color ),
											   TO_SCRN_Y(GRID_Y*14), B(command), color );
		}
	}

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );

	if( ( touch.move_finger != -1 ) && touch.move_button && touch_move_indicator->value )
	{
		float width;
		float height;
		if( FBitSet( touch_joy_texture->flags, FCVAR_CHANGED ) )
		{
			ClearBits( touch_joy_texture->flags, FCVAR_CHANGED );
			touch.joytexture = ref.dllFuncs.GL_LoadTexture( touch_joy_texture->string, NULL, 0, TF_NOMIPMAP );
		}
		if( touch.move_button->type == touch_move )
		{
			width =  touch_sidezone->value;
			height = touch_forwardzone->value;
		}
		else
		{
			width = (touch.move_button->x2 - touch.move_button->x1)/2;
			height = (touch.move_button->y2 - touch.move_button->y1)/2;
		}
		ref.dllFuncs.Color4ub( 255, 255, 255, 128 );
		ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X( touch.move_start_x - GRID_X * touch_move_indicator->value ),
						  TO_SCRN_Y( touch.move_start_y - GRID_Y * touch_move_indicator->value ),
						  TO_SCRN_X( GRID_X * 2 * touch_move_indicator->value ), TO_SCRN_Y( GRID_Y * 2 * touch_move_indicator->value ), 0, 0, 1, 1, touch.joytexture );
		ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
		ref.dllFuncs.R_DrawStretchPic( TO_SCRN_X( touch.move_start_x + touch.side * width - GRID_X * touch_move_indicator->value ),
						  TO_SCRN_Y( touch.move_start_y - touch.forward * height - GRID_Y * touch_move_indicator->value ),
						  TO_SCRN_X( GRID_X * 2 * touch_move_indicator->value ), TO_SCRN_Y( GRID_Y * 2 * touch_move_indicator->value ), 0, 0, 1, 1, touch.joytexture );

	}

}

// clear move and selection state
static void IN_TouchEditClear( void )
{
	// allow keep move/look fingers when doing touch_removeall
	//touch.move_finger = touch.look_finger = -1;

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
			touch_button_t *button = touch.edit;
			IN_TouchCheckCoords( &B(x1), &B(y1), &B(x2), &B(y2) );
			IN_TouchEditClear();
			if( button->type == touch_command )
			{
				touch.selection = button;

				// update "hide" editor button
				touch.hidebutton->texture = -1;
				touch.hidebutton->flags &= ~TOUCH_FL_HIDE;

				if( button->flags & TOUCH_FL_HIDE )
					Q_strcpy( touch.hidebutton->texturefile, "touch_default/edit_show.tga" );
				else
					Q_strcpy( touch.hidebutton->texturefile, "touch_default/edit_hide.tga" );
			}
		}
		if( type == event_motion ) // shutdown button move
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
			{
				touch.resize_finger = fingerID;
			}
		}
		if( type == event_up ) // disable resizing
		{
			if( touch.resize_finger == fingerID )
			{
				touch.resize_finger = -1;
			}
		}
		if( type == event_motion ) // perform resizing
		{
			if( touch.resize_finger == fingerID )
			{
				touch.edit->y2 += dy;
				touch.edit->x2 += dx;
			}
		}
	}
}

static void Touch_Motion( touchEventType type, int fingerID, float x, float y, float dx, float dy )
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

	// walk
	if( fingerID == touch.move_finger )
	{
		// check bounds
		if( touch_forwardzone->value <= 0 )
			Cvar_SetValue( "touch_forwardzone", 0.5 );
		if( touch_sidezone->value <= 0 )
			Cvar_SetValue( "touch_sidezone", 0.3 );

		if( !touch.move_button || touch.move_button->type == touch_move )
		{
			// move relative to touch start
			touch.forward = ( touch.move_start_y - y ) / touch_forwardzone->value;
			touch.side = ( x - touch.move_start_x ) / touch_sidezone->value;
		}
		else if( touch.move_button->type == touch_joy )
		{
			// move relative to joy center
			touch.forward = ( ( touch.move_button->y2 + touch.move_button->y1 ) - y * 2 ) / ( touch.move_button->y2 - touch.move_button->y1 ) * touch_joy_radius->value;
			touch.side = ( x * 2 - ( touch.move_button->x2 + touch.move_button->x1 ) ) / ( touch.move_button->x2 - touch.move_button->x1 ) * touch_joy_radius->value;
		}
		else if( touch.move_button->type == touch_dpad )
		{
			// like joy, but without acceleration. useful for bhop
			touch.forward = round( ( (touch.move_button->y2 + touch.move_button->y1) - y * 2 ) / ( touch.move_button->y2 - touch.move_button->y1 ) * touch_dpad_radius->value );
			touch.side = round( ( x * 2 - (touch.move_button->x2 + touch.move_button->x1) ) / ( touch.move_button->x2 - touch.move_button->x1 ) * touch_dpad_radius->value );
		}

		touch.forward = bound( -1, touch.forward, 1 );
		touch.side = bound( -1, touch.side, 1 );
	}

	// process look
	if( fingerID == touch.look_finger )
	{
		if( touch.precision )
			dx *= touch_precise_amount->value, dy *= touch_precise_amount->value;
			
		if( CVAR_TO_BOOL(touch_nonlinear_look) )
		{
			float dabs, dcos, dsin;

			// save angle, modify only velocity
			dabs = sqrt( dx * dx + dy * dy );

			if( dabs < 0.000001f )
				return; // no motion, avoid division by zero

			dcos = dx / dabs;
			dsin = dy / dabs;
		
			if( touch_exp_mult->value > 1 )
				dabs = ( exp( dabs * touch_exp_mult->value ) - 1 ) / touch_exp_mult->value;

			if( touch_pow_mult->value > 1 && touch_pow_factor->value > 1 )
				dabs = pow( dabs * touch_pow_mult->value, touch_pow_factor->value ) / touch_pow_mult->value;

			dx = dabs * dcos;
			dy = dabs * dsin;
		}

		// prevent breaking engine/client with bad values
		if( IS_NAN( dx ) || IS_NAN( dy ) )
			return;

		// accumulate
		touch.yaw -= dx * touch_yaw->value, touch.pitch += dy * touch_pitch->value;
	}
}


static qboolean Touch_ButtonPress( touchbuttonlist_t *list, touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touch_button_t *button;
	qboolean result = false;

	// run from end(front) to start(back)
	for( button = list->last; button; button = button->prev )
	{

		// skip invisible buttons
		if( !Touch_IsVisible( button ) )
			continue;

		if( type == event_down )
		{
			// button bounds check
			if( ( x > button->x1 &&
				 x < button->x2 ) &&
				( y < button->y2 &&
				  y > button->y1 ) )
			{
				button->finger = fingerID;

				if( button->type == touch_command )
				{
					char command[256];

					// command down: just execute command
					Q_snprintf( command, sizeof( command ), "%s\n", button->command );
					Cbuf_AddText( command );

					// increase precision
					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = true;

					result = true;
				}

				if( button->type == touch_wheel )
				{
					string command;
					touch.wheel_finger = fingerID;
					touch.wheel_amount = touch.wheel_count = 0;

					Cmd_TokenizeString( button->command );

					touch.wheel_horizontal = !Q_strcmp( Cmd_Argv( 0 ), "_hwheel" );

					Q_snprintf( touch.wheel_up, sizeof( touch.wheel_up ), "%s\n", Cmd_Argv( 1 ) );
					Q_snprintf( touch.wheel_down, sizeof( touch.wheel_down ), "%s\n", Cmd_Argv( 2 ) );
					Q_snprintf( touch.wheel_end, sizeof( touch.wheel_end ), "%s\n", Cmd_Argv( 3 ) );
					if( Q_snprintf( command, sizeof( command ), "%s\n", Cmd_Argv( 4 ) ) > 1)
					{
						Cbuf_AddText( command );
						touch.wheel_count++;
					}

					// increase precision
					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = true;

					result = true;
				}

				// initialize motion when player touched motion zone
				if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad  )
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
						touch_button_t *newbutton;

						// this is an error, try recover
						touch.move_finger = touch.look_finger = -1;

						// player touched touch_move with enabled look mode
						// and same finger id. release all move triggers
						for( newbutton = list->first; newbutton; newbutton = newbutton->next )
							if( ( newbutton->type == touch_move ) || ( newbutton->type == touch_look ) ) newbutton->finger = -1;

						Con_DPrintf( S_ERROR "Touch: touch_move on look finger %d!\n", fingerID );
						continue;
					}

					// initialize move mode
					touch.move_finger = fingerID;
					touch.move_button = button;

					if( touch.move_button->type == touch_move )
					{
						// initial position is first touch
						touch.move_start_x = x;
						touch.move_start_y = y;
					}
					else if( touch.move_button->type == touch_joy )
					{
						// initial position is button center
						touch.move_start_y = (touch.move_button->y2 + touch.move_button->y1) / 2;
						touch.move_start_x = (touch.move_button->x2 + touch.move_button->x1) / 2;

						// start move instanly
						touch.forward = ((touch.move_button->y2 + touch.move_button->y1) - y * 2) / (touch.move_button->y2 - touch.move_button->y1);
						touch.side = (x * 2 - (touch.move_button->x2 + touch.move_button->x1)) / (touch.move_button->x2 - touch.move_button->x1);
					}
					else if( touch.move_button->type == touch_dpad )
					{
						// dame as joy, but round
						touch.move_start_y = (touch.move_button->y2 + touch.move_button->y1) / 2;
						touch.move_start_x = (touch.move_button->x2 + touch.move_button->x1) / 2;

						// start move instanly
						touch.forward = round(((touch.move_button->y2 + touch.move_button->y1) - y * 2) / (touch.move_button->y2 - touch.move_button->y1));
						touch.side = round((x * 2 - (touch.move_button->x2 + touch.move_button->x1)) / (touch.move_button->x2 - touch.move_button->x1));
					}
				}

				// initialize look
				if( button->type == touch_look )
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
						touch_button_t *newbutton;

						// this is an error, try recover
						touch.move_finger = touch.look_finger = -1;

						// player touched touch_move with enabled look mode
						// and same finger id. release all move triggers
						for( newbutton = list->first; newbutton; newbutton = newbutton->next )
							if( ( newbutton->type == touch_move ) || ( newbutton->type == touch_look ) ) newbutton->finger = -1;

						Con_Printf( S_ERROR "touch: touch_look on move finger %d!\n", fingerID );
						continue;
					}

					touch.look_finger = fingerID;
				}
			}
		}

		if( type == event_up )
		{
			// no bounds check here.
			// button released when finger released
			if( fingerID == button->finger )
			{
				button->finger = -1;

				// handle +command, replace by -command
				if( button->type == touch_command )
				{
					if( button->command[0] == '+' )
					{
						char command[256];

						Q_snprintf( command, sizeof( command ), "%s\n", button->command );
						command[0] = '-';
						Cbuf_AddText( command );
					}

					// disable precision mode
					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = false;

					result = true;
				}

				// handle wheel end
				if( button->type == touch_wheel )
				{
					if( touch.wheel_count )
						Cbuf_AddText( touch.wheel_end );

					// disable precision mode
					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = false;

					touch.wheel_finger = -1;

					result = true;
				}

				// release motion buttons
				if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad )
				{
					touch.move_finger = -1;
					touch.forward = touch.side = 0;
					touch.move_button = NULL;
				}

				// release look buttons
				if( button->type == touch_look )
				{
					touch.look_finger = -1;
				}
			}
		}
	}

	return result;
}

static qboolean Touch_ButtonEdit( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touch_button_t *button;

	// edit buttons are on y1
	if( type == event_down )
	{
		if( (x < GRID_X) && (y < GRID_Y) )
		{
			touch.showeditbuttons ^= true;
			return true;
		}

		if( touch.showeditbuttons && Touch_ButtonPress( &touch.list_edit, type, fingerID, x, y, dx, dy ) )
			return true;
	}

	// run from end(front) to start(back)
	for( button = touch.list_user.last; button; button = button->prev )
	{
		if( type == event_down )
		{
			if( ( x > button->x1 &&
				 x < button->x2 ) &&
				( y < button->y2 &&
				  y > button->y1 ) )
			{
				button->finger = fingerID;
				
				// do not edit NOEDIT buttons
				if( button->flags & TOUCH_FL_NOEDIT )
						continue;

				touch.edit = button;
				touch.selection = NULL;

				// make button last to bring it up
				if( ( button->next ) && ( button->type == touch_command ) )
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
			if( fingerID == button->finger )
				button->finger = -1;
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
		return 1;
	}

	if( touch.state == state_edit && Touch_ButtonEdit( type, fingerID, x, y, dx, dy ) )
		return true;
	if( Touch_ButtonPress( &touch.list_user, type, fingerID, x, y, dx, dy ) )
		return true;
	if( type == event_motion )
		Touch_Motion( type, fingerID, x, y, dx, dy );
	return true;
}

int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{

	// simulate menu mouse click
	if( cls.key_dest != key_game && !CVAR_TO_BOOL(touch_in_menu) )
	{
		touch.move_finger = touch.resize_finger = touch.look_finger = -1;
		// Hack for keyboard, hope it help
		if( cls.key_dest == key_console || cls.key_dest == key_message )
		{
			Key_EnableTextInput( true, true );
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
				Cbuf_AddText( "escape\n" );
		}
		UI_MouseMove( TO_SCRN_X(x), TO_SCRN_Y(y) );
		//MsgDev( D_NOTE, "touch %d %d\n", TO_SCRN_X(x), TO_SCRN_Y(y) );
		if( type == event_down )
			Key_Event( K_MOUSE1, true );
		if( type == event_up )
			Key_Event( K_MOUSE1, false );
		return 0;
	}


	if( VGui_IsActive() )
	{
		VGui_MouseMove( TO_SCRN_X(x), TO_SCRN_Y(y) );

		if( type != event_motion )
			VGui_KeyEvent( K_MOUSE1, type == event_down ? 1 : 0 );

		// allow scoreboard scroll
		if( host.mouse_visible && type == event_motion )
			return 0;
	}

	if( !touch.initialized || (!CVAR_TO_BOOL(touch_enable) && !touch.clientonly) )
	{
#if 0
		if( type == event_down )
			Key_Event( K_MOUSE1, true );
		if( type == event_up )
			Key_Event( K_MOUSE1, false );
		Android_AddMove( dx * (float)refState.width, dy * (float)refState.height );
#endif
		return 0;
	}

	if( clgame.dllFuncs.pfnTouchEvent && clgame.dllFuncs.pfnTouchEvent( type, fingerID, x, y, dx, dy ) )
		return true;

	return Touch_ControlsEvent( type, fingerID, x, y, dx, dy );
}

void Touch_GetMove( float *forward, float *side, float *yaw, float *pitch )
{
	*forward += touch.forward;
	*side += touch.side;
	*yaw += touch.yaw;
	*pitch += touch.pitch;
	touch.yaw = touch.pitch = 0;
}

void Touch_KeyEvent( int key, int down )
{
	int xi, yi;
	float x, y;
	static float lx, ly;

	if( !CVAR_TO_BOOL(touch_emulate) )
	{
		if( CVAR_TO_BOOL(touch_enable) )
			return;

		if( !touch.clientonly )
			return;
	}

	Platform_GetMousePos( &xi, &yi );

	x = xi/SCR_W;
	y = yi/SCR_H;

	if( cls.key_dest == key_menu && down < 2 && key == K_MOUSE1 )
	{
		UI_MouseMove( xi, yi );
		UI_KeyEvent( key, down );
	}

	if( down == 1 )
		Touch_ControlsEvent( event_down, key == K_MOUSE1?0:1, x, y, 0, 0 );
	else
		Touch_ControlsEvent( down? event_motion: event_up, key == K_MOUSE1?0:1, x, y, x-lx, y-ly );
	lx = x, ly = y;
}

void Touch_Shutdown( void )
{
	if( !touch.initialized ) 
		return;
	Touch_RemoveAll_f();
	Cmd_RemoveCommand( "touch_addbutton" );
	Cmd_RemoveCommand( "touch_removebutton" );
	Cmd_RemoveCommand( "touch_enableedit" );
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
