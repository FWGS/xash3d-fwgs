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

static CVAR_DEFINE_AUTO( vibration_length, "1.0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "vibration length" );
static CVAR_DEFINE_AUTO( vibration_enable, "1", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "enable vibration" );
static CVAR_DEFINE_AUTO( vibration_shake, "1.0", FCVAR_ARCHIVE | FCVAR_PRIVILEGED, "screen shake vibration strength, 0 to disable" );

static cl_font_t g_scaled_font;
static float g_font_scale;

static void pfnVibrate( float life, char flags )
{
	if( !vibration_enable.value || life < 0.0f )
		return;

	// here goes platform-specific backends
	Platform_Vibrate( life * vibration_length.value, flags );
}

/*
=============
Mobile_ShakeVibrate

=============
*/
void Mobile_ShakeVibrate( float amplitude, float frequency, float time )
{
	if( !vibration_enable.value || vibration_shake.value <= 0.0f )
		return;

	// screen shake amplitude is 4.12 fixed point, frequency is 8.8 fixed point
	float strength = bound( 0.0f, amplitude * ( 1.0f / 16.0f ) * vibration_shake.value, 1.0f );
	float mix = bound( 0.0f, frequency * ( 1.0f / 256.0f ), 1.0f );

	// low frequency shakes are jerks for the heavy motor, high frequency shakes are rumbles for the light motor
	int low_freq = strength * ( 1.0f - mix ) * 0xFFFF;
	int high_freq = strength * mix * 0xFFFF;

	Platform_Vibrate2( time, low_freq, high_freq, 0 );
}

/*
=============
Mobile_StopVibration

=============
*/
void Mobile_StopVibration( void )
{
	Platform_Vibrate2( 0.0f, 0, 0, 0 );
}

static void Vibrate_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Msg( S_USAGE "vibrate <time>\n" );
		return;
	}

	pfnVibrate( Q_atof( Cmd_Argv( 1 )), VIBRATE_NORMAL );
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

	if( hud_utf8.value )
		SetBits( flags, FONT_DRAW_UTF8 );

	if( fabs( g_font_scale - scale ) > 0.1f ||
		g_scaled_font.hFontTexture != cls.creditsFont.hFontTexture )
	{
		g_scaled_font = cls.creditsFont;
		g_scaled_font.scale *= scale;
		g_scaled_font.charHeight *= scale;
		for( int i = 0; i < ARRAYSIZE( g_scaled_font.charWidths ); i++ )
			g_scaled_font.charWidths[i] *= scale;

		g_font_scale = scale;
	}

	return CL_DrawCharacter( x, y, number, color, &g_scaled_font, flags );
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

static void GAME_EXPORT pfnSetCustomClientID( const char *id )
{
	// deprecated
}

static const mobile_engfuncs_t gMobileEngfuncs =
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
	Sys_GetNativeObject,
	pfnSetCustomClientID,
	pfnParseFileSafe
};

qboolean Mobile_Init( void )
{
	pfnMobilityInterface ExportToClient;

	Cmd_AddCommand( "vibrate", Vibrate_f, "Vibrate for specified time");
	Cvar_RegisterVariable( &vibration_length );
	Cvar_RegisterVariable( &vibration_enable );
	Cvar_RegisterVariable( &vibration_shake );

	// find mobility interface
	if(( ExportToClient = COM_GetProcAddress( clgame.hInstance, MOBILITY_CLIENT_EXPORT )))
	{
		static mobile_engfuncs_t mobile_engfuncs; // keep a copy, don't let user change engine pointers

		mobile_engfuncs = gMobileEngfuncs;

		if( !ExportToClient( &mobile_engfuncs ))
		{
			Con_Reportf( "%s: ^2initailized extended MobilityAPI ^7ver. %i\n", __func__, MOBILITY_API_VERSION );
			return true;
		}

		// make sure that mobile functions are cleared
#if 1
		// some SDKs define export as returning void, breaking the contract
		// ignore result for now...
		return true;
#else
		memset( &mobile_engfuncs, 0, sizeof( mobile_engfuncs ));

		return false; // just tell user about problems
#endif
	}

	return true; // mobile interface is missed
}

void Mobile_Shutdown( void )
{
	Cmd_RemoveCommand( "vibrate" );
}
