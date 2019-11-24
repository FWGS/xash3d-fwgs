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
	int width  = clgame.scrInfo.charWidths[number] * scale * hud_scale->value;
	int height = clgame.scrInfo.iCharHeight        * scale * hud_scale->value;

	if( !cls.creditsFont.valid )
		return 0;

	x *= hud_scale->value;
	y *= hud_scale->value;

	number &= 255;
	number = Con_UtfProcessChar( number );

	if( number < 32 )
		return 0;

	if( y < -height )
		return 0;

	pfnPIC_Set( cls.creditsFont.hFontTexture, r, g, b, 255 );
	pfnPIC_DrawAdditive( x, y, width, height, &cls.creditsFont.fontRc[number] );

	return width;
}

static void *pfnGetNativeObject( const char *obj )
{
	if( !obj )
		return NULL;

	// Backend should consider that obj is case-sensitive
	return Platform_GetNativeObject( obj );
}


static mobile_engfuncs_t gpMobileEngfuncs =
{
	MOBILITY_API_VERSION,
	pfnVibrate,
	pfnEnableTextInput,
	Touch_AddClientButton,
	Touch_AddDefaultButton,
	Touch_HideButtons,
	Touch_RemoveButton,
	(void*)Touch_SetClientOnly,
	Touch_ResetDefaultButtons,
	pfnDrawScaledCharacter,
	Sys_Warn,
	pfnGetNativeObject,
	ID_SetCustomClientID
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
	vibration_length = Cvar_Get( "vibration_length", "1.0", FCVAR_ARCHIVE, "Vibration length");
	vibration_enable = Cvar_Get( "vibration_enable", "1", FCVAR_ARCHIVE, "Enable vibration");

	return success;
}

void Mobile_Shutdown( void )
{
	Cmd_RemoveCommand( "vibrate" );
}
