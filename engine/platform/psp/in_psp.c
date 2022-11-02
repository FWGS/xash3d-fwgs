/*
in_psp.c - PSP input component
Copyright (C) 2021 Sergey Galushko

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
#include "keydefs.h"
#include "input.h"
#include "client.h"

#if XASH_INPUT == INPUT_PSP
#include <pspctrl.h>

#define PSP_MAX_KEYS	sizeof(psp_keymap) / sizeof(struct psp_keymap_s)
#define PSP_EXT_KEY	PSP_CTRL_HOME

convar_t *psp_joy_dz_min;
convar_t *psp_joy_dz_max;
convar_t *psp_joy_cv_power;
convar_t *psp_joy_cv_expo;

static struct psp_keymap_s
{
	int		srckey;
	int		dstkey;
	qboolean	stdpressed;
	qboolean	extpressed;
}psp_keymap[] =
{
#if 0
	{ PSP_CTRL_SELECT,   '~'         , false, false },
	{ PSP_CTRL_START,    K_ESCAPE    , false, false },
	{ PSP_CTRL_UP,       K_UPARROW   , false, false },
	{ PSP_CTRL_RIGHT,    K_RIGHTARROW, false, false },
	{ PSP_CTRL_DOWN,     K_DOWNARROW , false, false },
	{ PSP_CTRL_LEFT,     K_LEFTARROW , false, false },
	{ PSP_CTRL_LTRIGGER, K_JOY1      , false, false },
	{ PSP_CTRL_RTRIGGER, K_JOY2      , false, false },
	{ PSP_CTRL_TRIANGLE, K_SHIFT     , false, false },
	{ PSP_CTRL_CIRCLE,   K_SPACE     , false, false },
	{ PSP_CTRL_CROSS,    K_ENTER     , false, false },
	{ PSP_CTRL_SQUARE,   K_BACKSPACE , false, false },
#else
	{ PSP_CTRL_SELECT  , K_MODE_BUTTON , false, false },
	{ PSP_CTRL_START   , K_START_BUTTON, false, false },
	{ PSP_CTRL_UP      , K_UPARROW     , false, false },
	{ PSP_CTRL_RIGHT   , K_RIGHTARROW  , false, false },
	{ PSP_CTRL_DOWN    , K_DOWNARROW   , false, false },
	{ PSP_CTRL_LEFT    , K_LEFTARROW   , false, false },
	{ PSP_CTRL_LTRIGGER, K_L1_BUTTON   , false, false },
	{ PSP_CTRL_RTRIGGER, K_R1_BUTTON   , false, false },
	{ PSP_CTRL_TRIANGLE, K_Y_BUTTON    , false, false },
	{ PSP_CTRL_CIRCLE  , K_B_BUTTON    , false, false },
	{ PSP_CTRL_CROSS   , K_A_BUTTON    , false, false },
	{ PSP_CTRL_SQUARE  , K_X_BUTTON    , false, false },
#endif
#if 0
	PSP_CTRL_HOME,
	PSP_CTRL_HOLD,
	PSP_CTRL_NOTE,
	PSP_CTRL_SCREEN,
	PSP_CTRL_VOLUP,
	PSP_CTRL_VOLDOWN,
	PSP_CTRL_WLAN_UP,
	PSP_CTRL_REMOTE,
	PSP_CTRL_DISC,
	PSP_CTRL_MS,
#endif
};
static signed short psp_joymap[256]; /* -32768 <> 32767 */

static float Platform_JoyAxisCompute( float axis, float deadzone_min, float deadzone_max, float power, float expo )
{
	float		abs_axis, fabs_axis, fcurve;
	float		scale, r_deadzone_max;
	qboolean	flip_axis = 0;

	expo = bound( 0.0f, expo, 1.0f );
	power = bound( 0.0f, power, 10.0f );
	deadzone_min = bound( 0.0f, deadzone_min, 127.0f );
	deadzone_max = bound( 0.0f, deadzone_max, 127.0f );

	// (-127) - (0) - (+127)
	abs_axis = axis - 128.0f;
	if( abs_axis < 0.0f )
	{
		abs_axis = -abs_axis - 1.0f;
		flip_axis = 1;
	}
	if( abs_axis <= deadzone_min ) return 0.0f;
	r_deadzone_max = 127.0f - deadzone_max;
	if( abs_axis >= r_deadzone_max ) return ( flip_axis ? -127.0f :  127.0f );

	scale = 127.0f / ( r_deadzone_max - deadzone_min );
	abs_axis -= deadzone_min;
	abs_axis *= scale;

	if( expo )
	{
		// x * ( x^power * expo + x * ( 1.0 - expo ))
		fabs_axis = abs_axis / 127.0f; // 0.0f - 1.0f
		fcurve = powf(fabs_axis, power) * expo + fabs_axis * (1.0f - expo);
		abs_axis = fabs_axis * fcurve * 127.0f;
	}

	return ( flip_axis ? -abs_axis :  abs_axis );
}

void Platform_RunEvents( void )
{
	int			i;
	SceCtrlData		buf;
	signed short 		curr_X, curr_Y;
	static unsigned int	last_buttons;
	static signed short	last_X, last_Y;

	sceCtrlPeekBufferPositive( &buf, 1 );

	for( i = 0; i < PSP_MAX_KEYS; i++ )
	{
		if( buf.Buttons & PSP_EXT_KEY )
		{
			if(( last_buttons ^ buf.Buttons ) & psp_keymap[i].srckey )
			{
				if( psp_keymap[i].stdpressed )
				{
					psp_keymap[i].stdpressed = false;
					Key_Event( psp_keymap[i].dstkey, psp_keymap[i].stdpressed );
				}
				else
				{
					psp_keymap[i].extpressed = buf.Buttons & psp_keymap[i].srckey;
					Key_Event( K_AUX16 + i, psp_keymap[i].extpressed);
				}
			}
		}
		else
		{
			// release
			if( psp_keymap[i].extpressed )
			{
				psp_keymap[i].extpressed = false;
				Key_Event( K_AUX16 + i, psp_keymap[i].extpressed);
			}

			if(( last_buttons ^ buf.Buttons ) & psp_keymap[i].srckey )
			{
				psp_keymap[i].stdpressed = buf.Buttons & psp_keymap[i].srckey;
				Key_Event( psp_keymap[i].dstkey, psp_keymap[i].stdpressed );
			}
		}
	}
	last_buttons = buf.Buttons;

	if( FBitSet( psp_joy_dz_min->flags, FCVAR_CHANGED ) || FBitSet( psp_joy_dz_max->flags, FCVAR_CHANGED ) ||
		FBitSet( psp_joy_cv_power->flags, FCVAR_CHANGED ) || FBitSet( psp_joy_cv_expo->flags, FCVAR_CHANGED ))
	{
		for ( i = 0; i < 256; i++ )
		{
			psp_joymap[i] = Platform_JoyAxisCompute( i, psp_joy_dz_min->value, psp_joy_dz_max->value, psp_joy_cv_power->value, psp_joy_cv_expo->value );
			psp_joymap[i] *= 256;
		}

		ClearBits( psp_joy_dz_min->flags, FCVAR_CHANGED );
		ClearBits( psp_joy_dz_max->flags, FCVAR_CHANGED );
		ClearBits( psp_joy_cv_power->flags, FCVAR_CHANGED );
		ClearBits( psp_joy_cv_expo->flags, FCVAR_CHANGED );
	}

	curr_X = psp_joymap[buf.Lx];
	curr_Y = psp_joymap[buf.Ly];

	if( last_X != curr_X )
		Joy_AxisMotionEvent( 2, -curr_X );

	if( last_Y != curr_Y )
		Joy_AxisMotionEvent( 3, -curr_Y );

	last_X = curr_X;
	last_Y = curr_Y;
}

void Platform_GetMousePos( int *x, int *y )
{
	*x = *y = 0;
}

void Platform_SetMousePos( int x, int y )
{

}

void Platform_EnableTextInput( qboolean enable )
{

}

int Platform_JoyInit( int numjoy )
{
	int i;

	// set up cvars
	psp_joy_dz_min = Cvar_Get( "psp_joy_dz_min", "15", FCVAR_ARCHIVE, "joy deadzone min (0 - 127)" );
	psp_joy_dz_max = Cvar_Get( "psp_joy_dz_max", "0",  FCVAR_ARCHIVE, "joy deadzone max (0 - 127)" );
	psp_joy_cv_power = Cvar_Get( "psp_joy_cv_power", "2",  FCVAR_ARCHIVE, "joy curve power (0 - 10)" );
	psp_joy_cv_expo = Cvar_Get( "psp_joy_cv_expo", "0.5",  FCVAR_ARCHIVE, "joy curve expo (0 - 1.0)" );

	// set up the controller.
	sceCtrlSetSamplingCycle( 0 );
	sceCtrlSetSamplingMode( PSP_CTRL_MODE_ANALOG );

	// building a joystick map
	for ( i = 0; i < 256; i++ )
	{
		psp_joymap[i] = Platform_JoyAxisCompute( i, psp_joy_dz_min->value, psp_joy_dz_max->value, psp_joy_cv_power->value, psp_joy_cv_expo->value );
		psp_joymap[i] *= 256;
	}
	return 1;
}

void Platform_MouseMove( float *x, float *y )
{

}

void Platform_PreCreateMove( void )
{

}
#endif /* XASH_INPUT */
