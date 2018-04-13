/*
touch.c - touchscreen support prototype
Copyright (C) 2015 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef XASH_DEDICATED
#include "common.h"
#include "gl_local.h"
#include "input.h"
#include "client.h"
#include "touch.h"
#include "math.h"
#include "vgui_draw.h"
#ifdef XASH_SDL
#include <SDL_hints.h>
#include <SDL_keyboard.h>
#endif

typedef enum
{
	touch_command, // Just tap a button
	touch_move,    // Like a joystick stick.
	touch_joy,     // Like a joystick stick, centered.
	touch_dpad,    // Only two directions.
	touch_look     // Like a touchpad.
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



typedef struct touchbutton2_s
{
	// Touch button type: tap, stick or slider
	touchButtonType type;
	// Field of button in pixels
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
	struct touchbutton2_s *next;
	struct touchbutton2_s *prev;

} touchbutton2_t;

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
	touchbutton2_t *first;
	touchbutton2_t *last;
} touchbuttonlist_t;

struct touch_s
{
	qboolean initialized;
	touchbuttonlist_t list_user;
	byte *mempool;
	touchState state;
	int look_finger;
	int move_finger;
	touchbutton2_t *move;
	float move_start_x;
	float move_start_y;
	float forward;
	float side;
	float yaw;
	float pitch;
	// editing
	touchbutton2_t *edit;
	touchbutton2_t *selection;
	int resize_finger;
	qboolean showbuttons;
	qboolean clientonly;
	rgba_t scolor;
	int swidth;
	qboolean precision;
	// textures
	int showtexture;
	int hidetexture;
	int resettexture;
	int closetexture;
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

// code looks smaller with it
#define B(x) button->x
#define SCR_W (scr_width->value)
#define SCR_H (scr_height->value)
#define TO_SCRN_Y(x) (scr_height->integer * (x))
#define TO_SCRN_X(x) (scr_width->integer * (x))

int pfnDrawCharacter( int x, int y, int number, int r, int g, int b );
static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  );

void IN_TouchWriteConfig( void )
{
	file_t	*f;
	char newconfigfile[64];
	char oldconfigfile[64];

	if( !touch.list_user.first ) return;

	if( Sys_CheckParm( "-nowriteconfig" ) || !touch.configchanged )
		return;

	MsgDev( D_NOTE, "IN_TouchWriteConfig(): %s\n", touch_config_file->string );

	Q_snprintf( newconfigfile, 64, "%s.new", touch_config_file->string );
	Q_snprintf( oldconfigfile, 64, "%s.bak", touch_config_file->string );

	f = FS_Open( newconfigfile, "w", true );
	if( f )
	{
		touchbutton2_t *button;
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
		FS_Printf( f, "touch_nonlinear_look \"%d\"\n", touch_nonlinear_look->integer );
		FS_Printf( f, "touch_pow_factor \"%f\"\n", touch_pow_factor->value );
		FS_Printf( f, "touch_pow_mult \"%f\"\n", touch_pow_mult->value );
		FS_Printf( f, "touch_exp_mult \"%f\"\n", touch_exp_mult->value );		FS_Printf( f, "\n// grid settings\n" );
		FS_Printf( f, "touch_grid_count \"%d\"\n", touch_grid_count->integer );
		FS_Printf( f, "touch_grid_enable \"%d\"\n", touch_grid_enable->integer );
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
		FS_Printf( f, "touch_move_indicator \"%d\"\n", touch_move_indicator->integer );

		FS_Printf( f, "\n// reset menu state when execing config\n" );
		FS_Printf( f, "touch_setclientonly 0\n" );
		FS_Printf( f, "\n// touch buttons\n" );
		FS_Printf( f, "touch_removeall\n" );
		for( button = touch.list_user.first; button; button = button->next )
		{
			int flags = button->flags;
			if( flags & TOUCH_FL_CLIENT )
				continue; //skip temporary buttons
			if( flags & TOUCH_FL_DEF_SHOW )
				flags &= ~TOUCH_FL_HIDE;
			if( flags & TOUCH_FL_DEF_HIDE )
				flags |= TOUCH_FL_HIDE;

			FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %f %f %f %f %d %d %d %d %d\n", 
				B(name), B(texturefile), B(command),
				B(x1), B(y1), B(x2), B(y2),
				B(color[0]), B(color[1]), B(color[2]), B(color[3]), flags );
		}

		FS_Close( f );
		FS_Delete( oldconfigfile );
		FS_Rename( touch_config_file->string, oldconfigfile );
		FS_Delete( touch_config_file->string );
		FS_Rename( newconfigfile, touch_config_file->string );
	}
	else MsgDev( D_ERROR, "Couldn't write %s.\n", touch_config_file->string );
}

void IN_TouchExportConfig_f( void )
{
	file_t	*f;
	char *name;

	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: touch_exportconfig <name>\n" );
		return;
	}

	if( !touch.list_user.first ) return;

	name = Cmd_Argv( 1 );

	MsgDev( D_NOTE, "Exporting config to %s\n", name );
	f = FS_Open( name, "w", true );
	if( f )
	{
		char profilename[256];
		char profilebase[256];
		touchbutton2_t *button;
		if( Q_strstr( name, "touch_presets/" ) )
		{
			FS_FileBase( name, profilebase );
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
		FS_Printf( f, "touch_nonlinear_look \"%d\"\n", touch_nonlinear_look->integer );
		FS_Printf( f, "touch_pow_factor \"%f\"\n", touch_pow_factor->value );
		FS_Printf( f, "touch_pow_mult \"%f\"\n", touch_pow_mult->value );
		FS_Printf( f, "touch_exp_mult \"%f\"\n", touch_exp_mult->value );
		FS_Printf( f, "\n// grid settings\n" );
		FS_Printf( f, "touch_grid_count \"%d\"\n", touch_grid_count->integer );
		FS_Printf( f, "touch_grid_enable \"%d\"\n", touch_grid_enable->integer );
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
		FS_Printf( f, "touch_move_indicator \"%d\"\n", touch_move_indicator->integer );

		FS_Printf( f, "\n// reset menu state when execing config\n" );
		FS_Printf( f, "touch_setclientonly 0\n" );
		FS_Printf( f, "\n// touch buttons\n" );
		FS_Printf( f, "touch_removeall\n" );
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

			FS_Printf( f, "touch_addbutton \"%s\" \"%s\" \"%s\" %f %f %f %f %d %d %d %d %d %f\n", 
				B(name), B(texturefile), B(command),
				B(x1), B(y1), B(x2), B(y2),
				B(color[0]), B(color[1]), B(color[2]), B(color[3]), flags, aspect );
		}
		FS_Printf( f, "\n// round button coordinates to grid\n" );
		FS_Printf( f, "touch_roundall\n" );
		FS_Close( f );
	}
	else MsgDev( D_ERROR, "Couldn't write %s.\n", name );
}

void IN_TouchGenetateCode_f( void )
{
	touchbutton2_t *button;
	rgba_t c = {0,0,0,0};

	if( Cmd_Argc() != 1 )
	{
		Msg( "Usage: touch_generate_code\n" );
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
		if( Q_memcmp( &c, &B(color), sizeof( rgba_t ) ) )
		{
			Msg( "MakeRGBA( color, %d, %d, %d, %d );\n", B(color[0]), B(color[1]), B(color[2]), B(color[3]) );
			Q_memcpy( &c, &B(color), sizeof( rgba_t ) );
		}
		Msg( "TOUCH_ADDDEFAULT( \"%s\", \"%s\", \"%s\", %f, %f, %f, %f, color, %d, %f, %d );\n",
			B(name), B(texturefile), B(command),
			B(x1), B(y1), B(x2), B(y2), (B(type) == touch_command)?(fabs( aspect  - 1.0f) < 0.0001)?2:1:0, aspect, flags );
	}
}

void IN_TouchRoundAll_f( void )
{
	touchbutton2_t *button;
	if( !touch_grid_enable->value )
		return;
	for( button = touch.list_user.first; button; button = button->next )
		IN_TouchCheckCoords( &B(x1), &B(y1), &B(x2), &B(y2) );
}

void IN_TouchListButtons_f( void )
{
	touchbutton2_t *button;
	for( button = touch.list_user.first; button; button = button->next )
	{
		Msg( "%s %s %s %f %f %f %f %d %d %d %d %d\n", 
			B(name), B(texturefile), B(command),
			B(x1), B(y1), B(x2), B(y2),
			B(color[0]), B(color[1]), B(color[2]), B(color[3]), B(flags) );
		if( B(flags) & TOUCH_FL_CLIENT)
			continue;
		UI_AddTouchButtonToList( B(name), B(texturefile), B(command),B(color), B(flags) );
	}
	touch.configchanged = true;
}

void IN_TouchStroke_f( void )
{
	touch.swidth = Q_atoi( Cmd_Argv( 1 ) );
	MakeRGBA( touch.scolor, Q_atoi( Cmd_Argv( 2 ) ), Q_atoi( Cmd_Argv( 3 ) ), Q_atoi( Cmd_Argv( 4 ) ), Q_atoi( Cmd_Argv( 5 ) ) );
}

touchbutton2_t *IN_TouchFindButton( touchbuttonlist_t *list, const char *name )
{
	touchbutton2_t *button;

	for ( button = list->first; button; button = button->next )
		if( !Q_strncmp( button->name, name, 32 ) )
			return button;
	return NULL;
}

touchbutton2_t *IN_TouchFindFirst( touchbuttonlist_t *list, const char *name )
{
	touchbutton2_t *button;

	for ( button = list->first; button; button = button->next )
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
			return button;
	return NULL;
}

void IN_TouchSetClientOnly( qboolean state )
{
	touch.clientonly = state;
	host.mouse_visible = state;
#ifdef XASH_SDL
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

void IN_TouchSetClientOnly_f( void )
{
	IN_TouchSetClientOnly( Q_atoi( Cmd_Argv( 1 ) ) );
}

void Touch_RemoveButton( touchbuttonlist_t *list, const char *name )
{
	touchbutton2_t *button;

	IN_TouchEditClear();

	while( ( button = IN_TouchFindFirst( &touch.list_user, name ) ) )
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

void IN_TouchRemoveButton( const char *name )
{
	Touch_RemoveButton( &touch.list_user, name );
}

void IN_TouchRemoveButton_f( void )
{
	IN_TouchRemoveButton( Cmd_Argv( 1 ) );
}

void IN_TouchRemoveAll_f( void )
{
	IN_TouchEditClear();
	while( touch.list_user.first )
	{
		touchbutton2_t *remove = touch.list_user.first;
		touch.list_user.first = touch.list_user.first->next;
		Mem_Free ( remove );
	}
	touch.list_user.last = NULL;
}

void IN_TouchSetColor( touchbuttonlist_t *list, const char *name, byte *color )
{
	touchbutton2_t *button;
	for( button = list->first; button; button = button->next )
	{
		if( ( Q_strstr( name, "*" ) && Q_stricmpext( name, button->name ) ) || !Q_strncmp( name, button->name, 32 ) )
			MakeRGBA( button->color, color[0], color[1], color[2], color[3] );
	}
}

void IN_TouchSetTexture( touchbuttonlist_t *list, const char *name, const char *texture )
{
	touchbutton2_t *button = IN_TouchFindButton( list, name );
	if( !button )
		return;
	button->texture = -1; // mark for texture load
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
}

void IN_TouchSetCommand( touchbuttonlist_t *list, const char *name, const char *command )
{
	touchbutton2_t *button = IN_TouchFindButton( list, name );
	
	if( !button )
		return;

	if( !Q_strcmp( command, "_look" ) )
		button->type = touch_look;
	if( !Q_strcmp( command, "_move" ) )
		button->type = touch_move;
	if( !Q_strcmp( command, "_joy" ) )
		button->type = touch_joy;
	if( !Q_strcmp( command, "_dpad" ) )
		button->type = touch_dpad;

	Q_strncpy( button->command, command, sizeof( button->command ) );
}

void IN_TouchHideButtons( const char *name, qboolean hide )
{
	touchbutton2_t *button;

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
void IN_TouchHide_f( void )
{
	IN_TouchHideButtons( Cmd_Argv( 1 ), true );
}

void IN_TouchShow_f( void )
{
	IN_TouchHideButtons( Cmd_Argv( 1 ), false );
}

void IN_TouchFadeButtons( touchbuttonlist_t *list, const char *name, float speed, float end, float start  )
{
	touchbutton2_t *button;
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
void IN_TouchFade_f( void )
{
	float start = -1;
	if( Cmd_Argc() < 4 )
		return;
	if( Cmd_Argc() > 4 )
		start = Q_atof( Cmd_Argv( 4 ) );
	IN_TouchFadeButtons( &touch.list_user, Cmd_Argv( 1 ), Q_atof( Cmd_Argv( 2 )), Q_atof( Cmd_Argv( 3 )), start );
}

void IN_TouchSetColor_f( void )
{
	rgba_t color;
	if( Cmd_Argc() == 6 )
	{
		MakeRGBA( color,  Q_atoi( Cmd_Argv(2) ), Q_atoi( Cmd_Argv(3) ), Q_atoi( Cmd_Argv(4) ), Q_atoi( Cmd_Argv(5) ) );
		IN_TouchSetColor( &touch.list_user, Cmd_Argv(1), color );
		return;
	}
	Msg( "Usage: touch_setcolor <pattern> <r> <g> <b> <a>\n" );
}

void IN_TouchSetTexture_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		IN_TouchSetTexture( &touch.list_user, Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}
	Msg( "Usage: touch_settexture <name> <file>\n" );
}

void IN_TouchSetFlags_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		touchbutton2_t *button = IN_TouchFindButton( &touch.list_user, Cmd_Argv( 1 ) );
		if( button )
			button->flags = Q_atoi( Cmd_Argv( 2 ) );
		return;
	}
	Msg( "Usage: touch_setflags <name> <file>\n" );
}

void IN_TouchSetCommand_f( void )
{
	if( Cmd_Argc() == 3 )
	{
		IN_TouchSetCommand( &touch.list_user, Cmd_Argv( 1 ), Cmd_Argv( 2 ) );
		return;
	}
	Msg( "Usage: touch_command <name> <command>\n" );
}
void IN_TouchReloadConfig_f( void )
{
	Cbuf_AddText( va("exec %s\n", touch_config_file->string ) );
}

touchbutton2_t *IN_TouchAddButton( touchbuttonlist_t *list, const char *name,  const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color )
{
	touchbutton2_t *button = Mem_Alloc( touch.mempool, sizeof( touchbutton2_t ) );
	button->texture = -1;
	Q_strncpy( button->texturefile, texture, sizeof( button->texturefile ) );
	Q_strncpy( button->name, name, 32 );
	Touch_RemoveButton( list, name ); //replace if exist
	button->x1 = x1;
	button->y1 = y1;
	button->x2 = x2;
	button->y2 = y2;
	MakeRGBA( button->color, color[0], color[1], color[2], color[3] );
	button->command[0] = 0;
	button->flags = 0;
	button->fade = 1;

	// check keywords
	if( !Q_strcmp( command, "_look" ) )
		button->type = touch_look;
	if( !Q_strcmp( command, "_move" ) )
		button->type = touch_move;
	if( !Q_strcmp( command, "_joy" ) )
		button->type = touch_joy;
	if( !Q_strcmp( command, "_dpad" ) )
		button->type = touch_dpad;

	Q_strncpy( button->command, command, sizeof( button->command ) );
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

void IN_TouchAddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
{
	touchbutton2_t *button;

	if( !touch.initialized )
		return;
	if( round )
		IN_TouchCheckCoords( &x1, &y1, &x2, &y2 );
	if( round == round_aspect )
	{
		y2 = y1 + ( x2 - x1 ) * (SCR_W/SCR_H) * aspect;
	}
	button = IN_TouchAddButton( &touch.list_user, name, texture, command, x1, y1, x2, y2, color );
	button->flags |= flags | TOUCH_FL_CLIENT | TOUCH_FL_NOEDIT;
	button->aspect = aspect;
}

void IN_TouchLoadDefaults_f( void )
{
	int i;
	for( i = 0; i < g_LastDefaultButton; i++ )
	{
		touchbutton2_t *button;
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
		button = IN_TouchAddButton( &touch.list_user, g_DefaultButtons[i].name, g_DefaultButtons[i].texturefile, g_DefaultButtons[i].command, x1, y1, x2, y2, g_DefaultButtons[i].color );
		button->flags |= g_DefaultButtons[i].flags;
		button->aspect = g_DefaultButtons[i].aspect;
	}
}


// Add default button from client
void IN_TouchAddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags )
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
void IN_TouchResetDefaultButtons( void )
{
	g_LastDefaultButton = 0;
}
void IN_TouchAddButton_f( void )
{
	rgba_t color;
	int argc = Cmd_Argc( );

	if( argc >= 12 )
	{
		touchbutton2_t *button;
		MakeRGBA( color, Q_atoi( Cmd_Argv(8) ), Q_atoi( Cmd_Argv(9) ), 
			Q_atoi( Cmd_Argv(10) ), Q_atoi( Cmd_Argv(11) ) );
		button = IN_TouchAddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
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
		IN_TouchAddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3),
			Q_atof( Cmd_Argv(4) ), Q_atof( Cmd_Argv(5) ), 
			Q_atof( Cmd_Argv(6) ), Q_atof( Cmd_Argv(7) ),
			color );
		return;
	}
	if( argc == 4 )
	{
		MakeRGBA( color, 255, 255, 255, 255 );
		IN_TouchAddButton( &touch.list_user, Cmd_Argv(1), Cmd_Argv(2), Cmd_Argv(3), 0.4, 0.4, 0.6, 0.6, color );
		return;
	}
	Msg( "Usage: touch_addbutton <name> <texture> <command> [<x1> <y1> <x2> <y2> [ r g b a ] ]\n" );
}

void IN_TouchEnableEdit_f( void )
{
	if( touch.state == state_none )
		touch.state = state_edit;
	touch.resize_finger = touch.move_finger = touch.look_finger = -1;
	touch.move = NULL;
	touch.configchanged = true;
}

void IN_TouchDisableEdit_f( void )
{
	touch.state = state_none;
	if( touch.edit )
		touch.edit->finger = -1;
	if( touch.selection )
		touch.selection->finger = -1;
	touch.edit = touch.selection = NULL;
	touch.resize_finger = touch.move_finger = touch.look_finger = -1;
}

void IN_TouchDeleteProfile_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: touch_deleteprofile <name>\n" );
		return;
	}

	// delete profile
	FS_Delete( va( "touch_profiles/%s.cfg", Cmd_Argv( 1 )));
}


void IN_TouchInit( void )
{
	rgba_t color;
	if( touch.initialized )
		return;
	touch.mempool = Mem_AllocPool( "Touch" );
	//touch.first = touch.last = NULL;
	MsgDev( D_NOTE, "IN_TouchInit()\n");
	touch.move_finger = touch.resize_finger = touch.look_finger = -1;
	touch.state = state_none;
	touch.showbuttons = true;
	touch.clientonly = false;
	touch.precision = false;
	MakeRGBA( touch.scolor, 255, 255, 255, 255 );
	touch.swidth = 1;
	g_LastDefaultButton = 0;

	// fill default buttons list
	MakeRGBA( color, 255, 255, 255, 255 );
	IN_TouchAddDefaultButton( "look", "", "_look", 0.500000, 0.000000, 1.000000, 1, color, 0, 0, 0 );
	IN_TouchAddDefaultButton( "move", "", "_move", 0.000000, 0.000000, 0.500000, 1, color, 0, 0, 0 );
	IN_TouchAddDefaultButton( "invnext", "touch_default/next_weap.tga", "invnext", 0.000000, 0.530200, 0.120000, 0.757428, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "invprev", "touch_default/prev_weap.tga", "invprev", 0.000000, 0.075743, 0.120000, 0.302971, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "use", "touch_default/use.tga", "+use", 0.880000, 0.454457, 1.000000, 0.681685, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "jump", "touch_default/jump.tga", "+jump", 0.880000, 0.227228, 1.000000, 0.454457, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "attack", "touch_default/shoot.tga", "+attack", 0.760000, 0.530200, 0.880000, 0.757428, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "attack2", "touch_default/shoot_alt.tga", "+attack2", 0.760000, 0.302971, 0.880000, 0.530200, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "loadquick", "touch_default/load.tga", "loadquick", 0.760000, 0.000000, 0.840000, 0.151486, color, 2, 1, 16 );
	IN_TouchAddDefaultButton( "savequick", "touch_default/save.tga", "savequick", 0.840000, 0.000000, 0.920000, 0.151486, color, 2, 1, 16 );
	IN_TouchAddDefaultButton( "messagemode", "touch_default/keyboard.tga", "messagemode", 0.840000, 0.000000, 0.920000, 0.151486, color, 2, 1, 8 );
	IN_TouchAddDefaultButton( "reload", "touch_default/reload.tga", "+reload", 0.000000, 0.302971, 0.120000, 0.530200, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "flashlight", "touch_default/flash_light_filled.tga", "impulse 100", 0.920000, 0.000000, 1.000000, 0.151486, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "scores", "touch_default/map.tga", "+showscores", 0.760000, 0.000000, 0.840000, 0.151486, color, 2, 1, 8 );
	IN_TouchAddDefaultButton( "show_numbers", "touch_default/show_weapons.tga", "exec touch_default/numbers.cfg", 0.440000, 0.833171, 0.520000, 0.984656, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "duck", "touch_default/crouch.tga", "+duck", 0.880000, 0.757428, 1.000000, 0.984656, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "tduck", "touch_default/tduck.tga", ";+duck", 0.560000, 0.833171, 0.620000, 0.946785, color, 2, 1, 0 );
	IN_TouchAddDefaultButton( "edit", "touch_default/settings.tga", "touch_enableedit", 0.420000, 0.000000, 0.500000, 0.151486, color, 2, 1, 32 );
	IN_TouchAddDefaultButton( "menu", "touch_default/menu.tga", "escape", 0.000000, 0.833171, 0.080000, 0.984656, color, 2, 1, 0 );

	
	Cmd_AddCommand( "touch_addbutton", IN_TouchAddButton_f, "add native touch button" );
	Cmd_AddCommand( "touch_removebutton", IN_TouchRemoveButton_f, "remove native touch button" );
	Cmd_AddCommand( "touch_enableedit", IN_TouchEnableEdit_f, "enable button editing mode" );
	Cmd_AddCommand( "touch_disableedit", IN_TouchDisableEdit_f, "disable button editing mode" );
	Cmd_AddCommand( "touch_settexture", IN_TouchSetTexture_f, "change button texture" );
	Cmd_AddCommand( "touch_setcolor", IN_TouchSetColor_f, "change button color" );
	Cmd_AddCommand( "touch_setcommand", IN_TouchSetCommand_f, "change button command" );
	Cmd_AddCommand( "touch_setflags", IN_TouchSetFlags_f, "change button flags (be careful)" );
	Cmd_AddCommand( "touch_show", IN_TouchShow_f, "show button" );
	Cmd_AddCommand( "touch_hide", IN_TouchHide_f, "hide button" );
	Cmd_AddCommand( "touch_list", IN_TouchListButtons_f, "list buttons" );
	Cmd_AddCommand( "touch_removeall", IN_TouchRemoveAll_f, "remove all buttons" );
	Cmd_AddCommand( "touch_loaddefaults", IN_TouchLoadDefaults_f, "generate config from defaults" );
	Cmd_AddCommand( "touch_roundall", IN_TouchRoundAll_f, "round all buttons coordinates to grid" );
	Cmd_AddCommand( "touch_exportconfig", IN_TouchExportConfig_f, "export config keeping aspect ratio" );
	Cmd_AddCommand( "touch_set_stroke", IN_TouchStroke_f, "set global stroke width and color" );
	Cmd_AddCommand( "touch_setclientonly", IN_TouchSetClientOnly_f, "when 1, only client buttons are shown" );
	Cmd_AddCommand( "touch_reloadconfig", IN_TouchReloadConfig_f, "load config, not saving changes" );
	Cmd_AddCommand( "touch_writeconfig", IN_TouchWriteConfig, "save current config" );
	Cmd_AddCommand( "touch_deleteprofile", IN_TouchDeleteProfile_f, "delete profile by name" );
	Cmd_AddCommand( "touch_generate_code", IN_TouchGenetateCode_f, "create code sample for mobility API" );
	Cmd_AddCommand( "touch_fade", IN_TouchFade_f, "create code sample for mobility API" );

	// not saved, just runtime state for scripting
	touch_in_menu = Cvar_Get( "touch_in_menu", "0", 0, "draw touch in menu (for internal use only)" );

	// sensitivity configuration
	touch_forwardzone = Cvar_Get( "touch_forwardzone", "0.06", 0, "forward touch zone" );
	touch_sidezone = Cvar_Get( "touch_sidezone", "0.06", 0, "side touch zone" );
	touch_pitch = Cvar_Get( "touch_pitch", "90", 0, "touch pitch sensitivity" );
	touch_yaw = Cvar_Get( "touch_yaw", "120", 0, "touch yaw sensitivity" );
	touch_nonlinear_look = Cvar_Get( "touch_nonlinear_look", "0", 0, "enable nonlinear touch look" );
	touch_pow_factor = Cvar_Get( "touch_pow_factor", "1.0", 0, "set > 1 to enable" );
	touch_pow_mult = Cvar_Get( "touch_pow_mult", "300.0", 0, "power multiplier, usually 200-1000" );
	touch_exp_mult = Cvar_Get( "touch_exp_mult", "0", 0, "exponent multiplier, usually 20-200, 0 to disable" );

	// touch.cfg
	touch_grid_count = Cvar_Get( "touch_grid_count", "50", 0, "touch grid count" );
	touch_grid_enable = Cvar_Get( "touch_grid_enable", "1", 0, "enable touch grid" );
	touch_config_file = Cvar_Get( "touch_config_file", "touch.cfg", CVAR_ARCHIVE, "current touch profile file" );
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
	touch_enable = Cvar_Get( "touch_enable", DEFAULT_TOUCH_ENABLE, CVAR_ARCHIVE, "enable touch controls" );
#if defined(XASH_SDL) && defined(__ANDROID__)
	SDL_SetHint( SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1" );
#endif
	touch.initialized = true;
}
int pfnGetScreenInfo( SCREENINFO *pscrinfo );
// must be called after executing config.cfg
void IN_TouchInitConfig( void )
{
	if( !touch.initialized )
		return;

	pfnGetScreenInfo( NULL ); //HACK: update hud screen parameters like iHeight
	if( FS_FileExists( touch_config_file->string, true ) )
		Cbuf_AddText( va( "exec \"%s\"\n", touch_config_file->string ) );
	else IN_TouchLoadDefaults_f( );
	touch.closetexture = GL_LoadTexture( "touch_default/edit_close.tga", NULL, 0, TF_NOPICMIP, NULL );
	touch.hidetexture = GL_LoadTexture( "touch_default/edit_hide.tga", NULL, 0, TF_NOPICMIP, NULL );
	touch.showtexture = GL_LoadTexture( "touch_default/edit_show.tga", NULL, 0, TF_NOPICMIP, NULL );
	touch.resettexture = GL_LoadTexture( "touch_default/edit_reset.tga", NULL, 0, TF_NOPICMIP, NULL );
	touch.joytexture = GL_LoadTexture( touch_joy_texture->string, NULL, 0, TF_NOPICMIP, NULL );
	touch.configchanged = false;
}
qboolean IN_TouchIsVisible( touchbutton2_t *button )
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

	pglColor4ub( r, g, b, a );
	R_DrawStretchPic( TO_SCRN_X(x1),
		TO_SCRN_Y(y1),
		TO_SCRN_X(x2 - x1),
		TO_SCRN_Y(y2 - y1),
		0, 0, 1, 1, texture );
}

#if defined(_MSC_VER) && (_MSC_VER < 1700) 
static __inline int round(float f)
{
    return (int)(f + 0.5);

}
#endif

#define GRID_COUNT_X (touch_grid_count->integer)
#define GRID_COUNT_Y (touch_grid_count->integer * SCR_H / SCR_W)
#define GRID_X (1.0/GRID_COUNT_X)
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
	if ( touch_grid_enable->integer )
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

	R_DrawStretchPic( TO_SCRN_X(x), TO_SCRN_Y(y), TO_SCRN_X(width), TO_SCRN_X(height), s1, t1, s2, t2, cls.creditsFont.hFontTexture );
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

	GL_SetRenderMode( kRenderTransAdd );

	// text is additive and alpha does not work
	pglColor4ub( color[0] * ( (float)color[3] /255.0f ), color[1] * ( (float)color[3] /255.0f ),
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
	GL_SetRenderMode( kRenderTransTexture );
	return x1;
}

void Touch_DrawButtons( touchbuttonlist_t *list )
{
	touchbutton2_t *button;
	
	for( button = list->first; button; button = button->next )
	{
		if( IN_TouchIsVisible( button ) )
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
					button->texture = GL_LoadTexture( button->texturefile, NULL, 0, TF_NOPICMIP, NULL );
				}

				if( B(flags) & TOUCH_FL_DRAW_ADDITIVE )
					GL_SetRenderMode( kRenderTransAdd );

				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), B(texture), color[0], color[1], color[2], color[3] );

				GL_SetRenderMode( kRenderTransTexture );
			}
			if( B(flags) & TOUCH_FL_STROKE )
			{
				pglColor4ub( touch.scolor[0], touch.scolor[1], touch.scolor[2], touch.scolor[3] * B( fade ) );
				R_DrawStretchPic( TO_SCRN_X(B(x1)),
					TO_SCRN_Y(B(y1)),
					touch.swidth,
					TO_SCRN_Y(B(y2)-B(y1)) - touch.swidth,
					0, 0, 1, 1, cls.fillImage );
				R_DrawStretchPic( TO_SCRN_X(B(x1)) + touch.swidth,
					TO_SCRN_Y(B(y1)),
					TO_SCRN_X(B(x2)-B(x1)) - touch.swidth,
					touch.swidth,
					0, 0, 1, 1, cls.fillImage );
				R_DrawStretchPic( TO_SCRN_X(B(x2))-touch.swidth,
					TO_SCRN_Y(B(y1)) + touch.swidth,
					touch.swidth,
					TO_SCRN_Y(B(y2)-B(y1)) - touch.swidth,
					0, 0, 1, 1, cls.fillImage );
				R_DrawStretchPic( TO_SCRN_X(B(x1)),
					TO_SCRN_Y(B(y2))-touch.swidth,
					TO_SCRN_X(B(x2)-B(x1)) - touch.swidth,
					touch.swidth,
					0, 0, 1, 1, cls.fillImage );
				pglColor4ub( 255, 255, 255, 255 );
			}
		}
		if( touch.state >= state_edit )
		{
			rgba_t color;
			if( !( button->flags & TOUCH_FL_HIDE ) )
				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 255, 255, 0, 32 );
			else
				Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 128, 128, 128, 128 );
			MakeRGBA( color, 255, 255,127, 255 );
			Con_DrawString( TO_SCRN_X( B(x1) ), TO_SCRN_Y( B(y1) ), B(name), color );
		}
	}
	
}

void IN_TouchDraw( void )
{
	touchbutton2_t *button;

	if( !touch.initialized || (!touch_enable->integer && !touch.clientonly) )
		return;

	if( cls.key_dest != key_game && touch_in_menu->integer == 0 )
		return;

	GL_SetRenderMode( kRenderTransTexture );

	if( touch.state >= state_edit && touch_grid_enable->integer )
	{
		float x;
		if( touch_in_menu->integer )
			Touch_DrawTexture( 0, 0, 1, 1, cls.fillImage, 32, 32, 32, 255 );
		else
			Touch_DrawTexture( 0, 0, 1, 1, cls.fillImage, 0, 0, 0, 112 );
		pglColor4ub( 0, 224, 224, 112 );
		for ( x = 0; x < 1 ; x += GRID_X )
			R_DrawStretchPic( TO_SCRN_X(x),
				0,
				1,
				TO_SCRN_Y(1),
				0, 0, 1, 1, cls.fillImage );
		for ( x = 0; x < 1 ; x += GRID_Y )
			R_DrawStretchPic( 0,
				TO_SCRN_Y(x),
				TO_SCRN_X(1),
				1,
				0, 0, 1, 1, cls.fillImage );
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
			Touch_DrawTexture( x1, y1, x2, y2, cls.fillImage, 0, 255, 0, 32 );
		}
		Touch_DrawTexture( 0, 0, GRID_X, GRID_Y, cls.fillImage, 255, 255, 255, 64 );
		if( touch.selection )
		{
			button = touch.selection;
			Touch_DrawTexture( B(x1), B(y1), B(x2), B(y2), cls.fillImage, 255, 0, 0, 64 );
			if( touch.showbuttons )
			{
				if( button->flags & TOUCH_FL_HIDE )
				{
					Touch_DrawTexture( 0, GRID_Y * 8, GRID_X * 2, GRID_Y * 10, touch.showtexture, 255, 255, 255, 255 );
					Touch_DrawText( GRID_X * 2.5, GRID_Y * 8.5, 0, 0, "Show", color, 1.5 );
				}
				else
				{
					Touch_DrawTexture( 0, GRID_Y * 8, GRID_X * 2, GRID_Y * 10, touch.hidetexture, 255, 255, 255, 255 );
					Touch_DrawText( GRID_X * 2.5, GRID_Y * 8.5, 0, 0, "Hide", color, 1.5 );
				}
			}
			Con_DrawString( 0, TO_SCRN_Y(GRID_Y * 11), "Selection:", color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*12), "Name: ", color ),
											   TO_SCRN_Y(GRID_Y*12), B(name), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*13), "Texture: ", color ),
											   TO_SCRN_Y(GRID_Y*13), B(texturefile), color );
			Con_DrawString( Con_DrawString( 0, TO_SCRN_Y(GRID_Y*14), "Command: ", color ),
											   TO_SCRN_Y(GRID_Y*14), B(command), color );
		}
		if( touch.showbuttons )
		{
			// close
			Touch_DrawTexture( 0, GRID_Y * 2, GRID_X * 2, GRID_Y * 4, touch.closetexture, 255, 255, 255, 255 );
			//Con_DrawString( TO_SCRN_X( GRID_X * 2.5 ), TO_SCRN_Y( GRID_Y * 2.5 ), "Close", color );
			Touch_DrawText( GRID_X * 2.5, GRID_Y * 2.5, 0, 0, "Close", color, 1.5 );
			// reset
			Touch_DrawTexture( 0, GRID_Y * 5, GRID_X * 2, GRID_Y * 7, touch.resettexture, 255, 255, 255, 255 );
			//Con_DrawString( TO_SCRN_X( GRID_X * 2.5 ), TO_SCRN_Y( GRID_Y * 5.5 ), "Reset", color );
			Touch_DrawText( GRID_X * 2.5, GRID_Y * 5.5, 0, 0, "Reset", color, 1.5 );
		}
	}
	pglColor4ub( 255, 255, 255, 255 );
	if( ( touch.move_finger != -1 ) && touch.move && touch_move_indicator->value )
	{
		float width;
		float height;
		if( touch_joy_texture->modified )
		{
			touch_joy_texture->modified = false;
			touch.joytexture = GL_LoadTexture( touch_joy_texture->string, NULL, 0, TF_NOPICMIP, NULL );
		}
		if( touch.move->type == touch_move )
		{
			width =  touch_sidezone->value;
			height = touch_forwardzone->value;
		}
		else
		{
			width = (touch.move->x2 - touch.move->x1)/2;
			height = (touch.move->y2 - touch.move->y1)/2;
		}
		pglColor4ub( 255, 255, 255, 128 );
		R_DrawStretchPic( TO_SCRN_X( touch.move_start_x - GRID_X * touch_move_indicator->value ),
						  TO_SCRN_Y( touch.move_start_y - GRID_Y * touch_move_indicator->value ),
						  TO_SCRN_X( GRID_X * 2 * touch_move_indicator->value ), TO_SCRN_Y( GRID_Y * 2 * touch_move_indicator->value ), 0, 0, 1, 1, touch.joytexture );
		pglColor4ub( 255, 255, 255, 255 );
		R_DrawStretchPic( TO_SCRN_X( touch.move_start_x + touch.side * width - GRID_X * touch_move_indicator->value ),
						  TO_SCRN_Y( touch.move_start_y - touch.forward * height - GRID_Y * touch_move_indicator->value ),
						  TO_SCRN_X( GRID_X * 2 * touch_move_indicator->value ), TO_SCRN_Y( GRID_Y * 2 * touch_move_indicator->value ), 0, 0, 1, 1, touch.joytexture );

	}

}

// clear move and selection state
void IN_TouchEditClear( void )
{
	// Allow keep move/look fingers when doing touch_removeall
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
			touchbutton2_t *button = touch.edit;
			IN_TouchCheckCoords( &B(x1), &B(y1), &B(x2), &B(y2) );
			IN_TouchEditClear();
			if( button->type == touch_command )
				touch.selection = button;
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
	// walk
	if( fingerID == touch.move_finger )
	{
		// check bounds
		if( touch_forwardzone->value <= 0 )
			Cvar_SetFloat( "touch_forwardzone", 0.5 );
		if( touch_sidezone->value <= 0 )
			Cvar_SetFloat( "touch_sidezone", 0.3 );

		if( !touch.move || touch.move->type == touch_move )
		{
			// move relative to touch start
			touch.forward = ( touch.move_start_y - y ) / touch_forwardzone->value;
			touch.side = ( x - touch.move_start_x ) / touch_sidezone->value;
		}
		else if( touch.move->type == touch_joy )
		{
			// move relative to joy center
			touch.forward = ( ( touch.move->y2 + touch.move->y1 ) - y * 2 ) / ( touch.move->y2 - touch.move->y1 ) * touch_joy_radius->value;
			touch.side = ( x * 2 - ( touch.move->x2 + touch.move->x1 ) ) / ( touch.move->x2 - touch.move->x1 ) * touch_joy_radius->value;
		}
		else if( touch.move->type == touch_dpad )
		{
			// like joy, but without acceleration. useful for bhop
			touch.forward = round( ( (touch.move->y2 + touch.move->y1) - y * 2 ) / ( touch.move->y2 - touch.move->y1 ) * touch_dpad_radius->value );
			touch.side = round( ( x * 2 - (touch.move->x2 + touch.move->x1) ) / ( touch.move->x2 - touch.move->x1 ) * touch_dpad_radius->value );
		}

		touch.forward = bound( -1, touch.forward, 1 );
		touch.side = bound( -1, touch.side, 1 );
	}

	// process look
	if( fingerID == touch.look_finger )
	{
		if( touch.precision )
			dx *= touch_precise_amount->value, dy *= touch_precise_amount->value;
			
		if( touch_nonlinear_look->integer );
		{
			// save angle, modify only velocity
			float dabs = sqrt( dx*dx+dy*dy );
			float dcos = dx/dabs;
			float dsin = dy/dabs;
		
			if( touch_exp_mult->value > 1 )
				dabs = (exp(dabs*touch_exp_mult->value)-1)/touch_exp_mult->value;
			if( touch_pow_mult->value > 1 && touch_pow_factor->value > 1 )
				dabs = pow(dabs*touch_pow_mult->value,touch_pow_factor->value)/touch_pow_mult->value;

			dx = dabs * dcos;
			dy = dabs * dsin;
		}

		// accumulate
		touch.yaw -= dx * touch_yaw->value, touch.pitch += dy * touch_pitch->value;
	}
}


static qboolean Touch_ButtonPress( touchbuttonlist_t *list, touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touchbutton2_t *button;

	// run from end(front) to start(back)
	for( button = list->last; button; button = button->prev )
	{
		if( type == event_down )
		{
			if( ( x > button->x1 &&
				 x < button->x2 ) &&
				( y < button->y2 &&
				  y > button->y1 ) )
			{
				button->finger = fingerID;
				
				if( !IN_TouchIsVisible( button ) )
					continue;

				if( button->type == touch_command )
				{
					char command[256];
					Q_snprintf( command, sizeof( command ), "%s\n", button->command );
					Cbuf_AddText( command );

					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = true;
				}
				if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad  )
				{
					if( touch.move_finger !=-1 )
					{
						button->finger = touch.move_finger;
						continue;
					}
					if( touch.look_finger == fingerID )
					{
						touchbutton2_t *newbutton;
						touch.move_finger = touch.look_finger = -1;
						//touch.move_finger = button->finger = -1;
						for( newbutton = list->first; newbutton; newbutton = newbutton->next )
							if( ( newbutton->type == touch_move ) || ( newbutton->type == touch_look ) ) newbutton->finger = -1;
						MsgDev( D_NOTE, "Touch: touch_move on look finger %d!\n", fingerID );
						continue;
					}
					touch.move_finger = fingerID;
					touch.move = button;
					if( touch.move->type == touch_move )
					{
						touch.move_start_x = x;
						touch.move_start_y = y;
					}
					else if( touch.move->type == touch_joy )
					{
						touch.move_start_y = (touch.move->y2 + touch.move->y1) / 2;
						touch.move_start_x = (touch.move->x2 + touch.move->x1) / 2;
						touch.forward = ((touch.move->y2 + touch.move->y1) - y * 2) / (touch.move->y2 - touch.move->y1);
						touch.side = (x * 2 - (touch.move->x2 + touch.move->x1)) / (touch.move->x2 - touch.move->x1);
					}
					else if( touch.move->type == touch_dpad )
					{
						touch.move_start_y = (touch.move->y2 + touch.move->y1) / 2;
						touch.move_start_x = (touch.move->x2 + touch.move->x1) / 2;
						touch.forward = round(((touch.move->y2 + touch.move->y1) - y * 2) / (touch.move->y2 - touch.move->y1));
						touch.side = round((x * 2 - (touch.move->x2 + touch.move->x1)) / (touch.move->x2 - touch.move->x1));
					}
				}
				if( button->type == touch_look )
				{
					if( touch.look_finger !=-1 )
					{
						button->finger = touch.look_finger;
						continue;
					}
					if( touch.move_finger == fingerID )
					{
						touchbutton2_t *newbutton;
						// This is an error, try recover
						touch.move_finger = touch.look_finger = -1;
						for( newbutton = list->first; newbutton; newbutton = newbutton->next )
							if( ( newbutton->type == touch_move ) || ( newbutton->type == touch_look ) ) newbutton->finger = -1;
						MsgDev( D_NOTE, "Touch: touch_look on move finger %d!\n", fingerID );
						continue;
					}
					touch.look_finger = fingerID;
				}
			}
		}
		if( type == event_up )
		{
			if( fingerID == button->finger )
			{
				button->finger = -1;
				if( !IN_TouchIsVisible( button ) )
					continue;

				if( ( button->type == touch_command ) && ( button->command[0] == '+' ) )
				{
					char command[256];
					Q_snprintf( command, sizeof( command ), "%s\n", button->command );
					command[0] = '-';
					Cbuf_AddText( command );
					if( B(flags) & TOUCH_FL_PRECISION )
						touch.precision = false;
				}
				if( button->type == touch_move || button->type == touch_joy || button->type == touch_dpad )
				{
					touch.move_finger = -1;
					touch.forward = touch.side = 0;
					touch.move = NULL;
				}
				if( button->type == touch_look )
				{
					touch.look_finger = -1;
				}
			}
		}
	}

	return false;
}

static qboolean Touch_ButtonEdit( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touchbutton2_t *button;

	// edit buttons are on y1
	if( type == event_down )
	{
		if( (x < GRID_X) && (y < GRID_Y) )
		{
			touch.showbuttons ^= true;
			return true;
		}
		if( touch.showbuttons && ( x < GRID_X * 2 ) )
		{
			if( ( y > GRID_Y * 2 ) && ( y < GRID_Y * 4 )  ) // close button
			{
				IN_TouchDisableEdit_f();
				if( touch_in_menu->integer )
				{
					Cvar_Set( "touch_in_menu", "0" );
				}
				else
					IN_TouchWriteConfig();
				return true;
			}
			if( ( y > GRID_Y * 5 ) && ( y < GRID_Y * 7 ) ) // reset button
			{
				IN_TouchReloadConfig_f();
				return true;
			}
			if( ( y > GRID_Y * 8 ) && ( y < GRID_Y * 10 ) && touch.selection ) // hide button
			{
				touch.selection->flags ^= TOUCH_FL_HIDE;
				return true;
			}
		}
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
				// Make button last to bring it up
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
		touch.selection = NULL;

	return false;
}

static int Touch_ControlsEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy )
{
	touchbutton2_t *button;

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
	if( cls.key_dest != key_game && !touch_in_menu->integer )
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
				if( dy > 0.4 )
					Con_Bottom();
				if( y1 > 0.01 )
				{
					Con_PageUp( 1 );
					y1 = 0;
				}
				if( y1 < -0.01 )
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

	if( !touch.initialized || (!touch_enable->integer && !touch.clientonly) )
	{
#if 0
		if( type == event_down )
			Key_Event( K_MOUSE1, true );
		if( type == event_up )
			Key_Event( K_MOUSE1, false );
		Android_AddMove( dx * scr_width->value, dy * scr_height->value );
#endif
		return 0;
	}

	if( clgame.dllFuncs.pfnTouchEvent && clgame.dllFuncs.pfnTouchEvent( type, fingerID, x, y, dx, dy ) )
		return true;

	return Touch_ControlsEvent( type, fingerID, x, y, dx, dy );
}

void IN_TouchMove( float *forward, float *side, float *yaw, float *pitch )
{
	*forward += touch.forward;
	*side += touch.side;
	*yaw += touch.yaw;
	*pitch += touch.pitch;
	touch.yaw = touch.pitch = 0;
}

void IN_TouchKeyEvent( int key, int down )
{
	int xi, yi;
	float x, y;

	if( touch_enable->integer )
		return;

	if( !touch.clientonly )
		return;

	CL_GetMousePosition( &xi, &yi );

	x = xi/SCR_W;
	y = yi/SCR_H;

	Touch_ControlsEvent( !down, key == K_MOUSE1?0:1, x, y, 0, 0 );
}

void IN_TouchShutdown( void )
{
	if( !touch.initialized ) 
		return;
	IN_TouchRemoveAll_f();
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
#endif
