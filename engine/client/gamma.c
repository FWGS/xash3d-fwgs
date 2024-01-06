/*
gamma.c - gamma routines
Copyright (C) 2011 Uncle Mike

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
#include "xash3d_mathlib.h"
#include "enginefeatures.h"

//-----------------------------------------------------------------------------
// Gamma conversion support
//-----------------------------------------------------------------------------
static byte	texgammatable[256];
static uint	lightgammatable[1024];
static uint	lineargammatable[1024];
static uint	screengammatable[1024];
static CVAR_DEFINE( v_direct, "direct", "0.9", 0, "direct studio lighting" );
static CVAR_DEFINE( v_texgamma, "texgamma", "2.0", 0, "texgamma amount" );
static CVAR_DEFINE( v_lightgamma, "lightgamma", "2.5", 0, "lightgamma amount" );
static CVAR_DEFINE( v_brightness, "brightness", "0.0", FCVAR_ARCHIVE, "brightness factor" );
static CVAR_DEFINE( v_gamma, "gamma", "2.5", FCVAR_ARCHIVE, "gamma amount" );
static CVAR_DEFINE( gl_overbright, "gl_overbright", "1", FCVAR_ARCHIVE, "overbrights (ref_gl only)" );

static void BuildGammaTable( const float gamma, const float brightness, const float texgamma, const float lightgamma )
{
	float g1, g2, g3;
	int i;

	if( gamma != 0.0 )
		g1 = 1.0 / gamma;
	else g1 = 0.4;

	g2 = g1 * texgamma;

	if( brightness <= 0.0 )
		g3 = 0.125;
	else if( brightness <= 1.0 )
		g3 = 0.125 - brightness * brightness * 0.075;
	else
		g3 = 0.05;

	for( i = 0; i < 256; i++ )
	{
		double d = pow( i / 255.0, (double)g2 );
		int inf = d * 255.0;
		texgammatable[i] = bound( 0, inf, 255 );
	}

	for( i = 0; i < 1024; i++ )
	{
		double d;
		float f = pow( i / 1023.0, (double)lightgamma );
		int inf;

		if( brightness > 1.0 )
			f *= brightness;

		if( f <= g3 )
			f = ( f / g3 ) * 0.125;
		else
			f = (( f - g3 ) / ( 1.0 - g3 )) * 0.875 + 0.125;

		d = pow( (double)f, (double)g1 ); // do not remove the cast, or tests fail
		inf = d * 1023.0;
		lightgammatable[i] = bound( 0, inf, 1023 );

		// do these calculations in the same loop...
		lineargammatable[i] = pow( i / 1023.0, (double)gamma ) * 1023.0;
		screengammatable[i] = pow( i / 1023.0, 1.0 / gamma ) * 1023.0;
	}
}

static void V_ValidateGammaCvars( void )
{
	if( Host_IsLocalGame( ))
		return;

	if( v_gamma.value < 1.8f )
		Cvar_DirectSet( &v_gamma, "1.8" );
	else if( v_gamma.value > 3.0f )
		Cvar_DirectSet( &v_gamma, "3" );

	if( v_texgamma.value < 1.8f )
		Cvar_DirectSet( &v_texgamma, "1.8" );
	else if( v_texgamma.value > 3.0f )
		Cvar_DirectSet( &v_texgamma, "3" );

	if( v_lightgamma.value < 1.8f )
		Cvar_DirectSet( &v_lightgamma, "1.8" );
	else if( v_lightgamma.value > 3.0f )
		Cvar_DirectSet( &v_lightgamma, "3" );

	if( v_brightness.value < 0.0f )
		Cvar_DirectSet( &v_brightness, "0" );
	else if( v_brightness.value > 2.0f )
		Cvar_DirectSet( &v_brightness, "2" );
}

void V_CheckGamma( void )
{
	static qboolean dirty = false;
	qboolean notify_refdll = false;

	if( cls.scrshot_action == scrshot_envshot || cls.scrshot_action == scrshot_skyshot )
	{
		dirty = true; // force recalculate next normal frame
		BuildGammaTable( 1.8f, 0.0f, 2.0f, 2.5f );
		if( ref.initialized )
			ref.dllFuncs.R_GammaChanged( true );
		return;
	}

	if( FBitSet( gl_overbright.flags, FCVAR_CHANGED ))
	{
		// nothing to recalculate so far, just notify the refdll
		notify_refdll = true;
		ClearBits( gl_overbright.flags, FCVAR_CHANGED );
	}

	if( dirty || FBitSet( v_texgamma.flags|v_lightgamma.flags|v_brightness.flags|v_gamma.flags, FCVAR_CHANGED ))
	{
		V_ValidateGammaCvars();

		BuildGammaTable( v_gamma.value, v_brightness.value, v_texgamma.value, v_lightgamma.value );

		// force refdll to recalculate lightmaps
		notify_refdll = true;

		// unfortunately, recalculating textures isn't possible yet
		ClearBits( v_texgamma.flags, FCVAR_CHANGED );
		ClearBits( v_lightgamma.flags, FCVAR_CHANGED );
		ClearBits( v_brightness.flags, FCVAR_CHANGED );
		ClearBits( v_gamma.flags, FCVAR_CHANGED );
	}

	if( notify_refdll && ref.initialized )
		ref.dllFuncs.R_GammaChanged( false );
}

void V_Init( void )
{
	Cvar_RegisterVariable( &v_texgamma );
	Cvar_RegisterVariable( &v_lightgamma );
	Cvar_RegisterVariable( &v_brightness );
	Cvar_RegisterVariable( &v_gamma );
	Cvar_RegisterVariable( &v_direct );
	Cvar_RegisterVariable( &gl_overbright );

	// force gamma init
	SetBits( v_gamma.flags, FCVAR_CHANGED );
	V_CheckGamma();
}

byte TextureToGamma( byte b )
{
	if( FBitSet( host.features, ENGINE_LINEAR_GAMMA_SPACE ))
		return b;

	return texgammatable[b];
}

byte LightToTexGamma( byte b )
{
	if( FBitSet( host.features, ENGINE_LINEAR_GAMMA_SPACE ))
		return b;

	// 255 << 2 is 1020, impossible to overflow
	return lightgammatable[b << 2] >> 2;
}

uint LightToTexGammaEx( uint b )
{
	if( FBitSet( host.features, ENGINE_LINEAR_GAMMA_SPACE ))
		return b;

	if( unlikely( b > ARRAYSIZE( lightgammatable )))
		return 0;

	return lightgammatable[b];
}

uint ScreenGammaTable( uint b )
{
	if( FBitSet( host.features, ENGINE_LINEAR_GAMMA_SPACE ))
		return b;

	if( unlikely( b > ARRAYSIZE( screengammatable )))
		return 0;

	return screengammatable[b];
}

uint LinearGammaTable( uint b )
{
	if( FBitSet( host.features, ENGINE_LINEAR_GAMMA_SPACE ))
		return b;

	if( unlikely( b > ARRAYSIZE( lineargammatable )))
		return 0;
	return lineargammatable[b];
}
