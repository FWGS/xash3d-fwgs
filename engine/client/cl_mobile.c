/*
cl_mobile.c - common mobile interface
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

#include "common.h"
#include "client.h"
#include "mobility_int.h"
#include "library.h"
#include "input.h"
#include "platform/platform.h"

mobile_engfuncs_t *gMobileEngfuncs;

convar_t *vibration_length;
convar_t *vibration_enable;

static cl_font_t g_scaled_font;
static float g_font_scale;

static void pfnVibrate( float life, char flags )
{
	if( !vibration_enable->value )
		return;

	if( life < 0.0f )
	{
		Con_Reportf( S_WARN "Negative vibrate time: %f\n", life );
		return;
	}

	//Con_Reportf( "Vibrate: %f %d\n", life, flags );

	// here goes platform-specific backends
	Platform_Vibrate( life * vibration_length->value, flags );
}

static void Vibrate_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Msg( S_USAGE "vibrate <time>\n" );
		return;
	}

	pfnVibrate( Q_atof( Cmd_Argv(1) ), VIBRATE_NORMAL );
}

static void pfnEnableTextInput( int enable )
{
	Key_EnableTextInput( enable, false );
}

static int pfnDrawScaledCharacter( int x, int y, int number, int r, int g, int b, float scale )
{
	// this call is very ineffective and possibly broken!
	rgba_t color = { r, g, b, 255 };
	int flags = FONT_DRAW_HUD;

	if( hud_utf8->value )
		SetBits( flags, FONT_DRAW_UTF8 );

	if( fabs( g_font_scale - scale ) > 0.1f ||
		g_scaled_font.hFontTexture != cls.creditsFont.hFontTexture )
	{
		int i;

		g_scaled_font = cls.creditsFont;
		g_scaled_font.scale *= scale;
		g_scaled_font.charHeight *= scale;
		for( i = 0; i < ARRAYSIZE( g_scaled_font.charWidths ); i++ )
			g_scaled_font.charWidths[i] *= scale;

		g_font_scale = scale;
	}

	return CL_DrawCharacter( x, y, number, color, &g_scaled_font, flags );
}

static void *pfnGetNativeObject( const char *obj )
{
	if( !obj )
		return NULL;

	// Backend should consider that obj is case-sensitive
	return Platform_GetNativeObject( obj );
}

static void pfnTouch_HideButtons( const char *name, byte state )
{
	Touch_HideButtons( name, state, true );
}

static void pfnTouch_RemoveButton( const char *name )
{
	Touch_RemoveButton( name, true );
}

static char *pfnParseFileSafe( char *data, char *buf, const int size, unsigned int flags, int *len )
{
	return COM_ParseFileSafe( data, buf, size, flags, len, NULL );
}

static mobile_engfuncs_t gpMobileEngfuncs =
{
	MOBILITY_API_VERSION,
	pfnVibrate,
	pfnEnableTextInput,
	Touch_AddClientButton,
	Touch_AddDefaultButton,
	pfnTouch_HideButtons,
	pfnTouch_RemoveButton,
	Touch_SetClientOnly,
	Touch_ResetDefaultButtons,
	pfnDrawScaledCharacter,
	Sys_Warn,
	pfnGetNativeObject,
	ID_SetCustomClientID,
	pfnParseFileSafe
};

qboolean Mobile_Init( void )
{
	qboolean success = false;
	pfnMobilityInterface ExportToClient;

	// find a mobility interface
	ExportToClient = COM_GetProcAddress( clgame.hInstance, MOBILITY_CLIENT_EXPORT );
	gMobileEngfuncs = &gpMobileEngfuncs;

	if( ExportToClient && !ExportToClient( gMobileEngfuncs ) )
		success = true;

	Cmd_AddCommand( "vibrate", (xcommand_t)Vibrate_f, "Vibrate for specified time");
	vibration_length = Cvar_Get( "vibration_length", "1.0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "Vibration length");
	vibration_enable = Cvar_Get( "vibration_enable", "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "Enable vibration");

	return success;
}

void Mobile_Shutdown( void )
{
	Cmd_RemoveCommand( "vibrate" );
}
