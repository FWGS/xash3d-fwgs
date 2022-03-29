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

#define PSP_MAX_KEYS 12

extern convar_t *psp_deadzone_min;
extern convar_t *psp_deadzone_max;

static struct psp_keymap_s
{
	unsigned int	srckey;
	int				dstkey;
}psp_keymap[] =
{
#if 1
	{ PSP_CTRL_SELECT,   '~'          },
	{ PSP_CTRL_START,    K_ESCAPE     },
	{ PSP_CTRL_UP,       K_UPARROW    },
	{ PSP_CTRL_RIGHT,    K_RIGHTARROW },
	{ PSP_CTRL_DOWN,     K_DOWNARROW  },
	{ PSP_CTRL_LEFT,     K_LEFTARROW  },
	{ PSP_CTRL_LTRIGGER, K_JOY1       },
	{ PSP_CTRL_RTRIGGER, K_JOY2       },
	{ PSP_CTRL_TRIANGLE, K_SHIFT      },
	{ PSP_CTRL_CIRCLE,   K_SPACE      },
	{ PSP_CTRL_CROSS,    K_ENTER      },
	{ PSP_CTRL_SQUARE,   K_BACKSPACE  },
#else
	{ PSP_CTRL_SELECT,   K_MODE_BUTTON  },
	{ PSP_CTRL_START,    K_START_BUTTON },
	{ PSP_CTRL_UP,       K_UPARROW      },
	{ PSP_CTRL_RIGHT,    K_RIGHTARROW   },
	{ PSP_CTRL_DOWN,     K_DOWNARROW    },
	{ PSP_CTRL_LEFT,     K_LEFTARROW    },
	{ PSP_CTRL_LTRIGGER, K_L1_BUTTON    },
	{ PSP_CTRL_RTRIGGER, K_R1_BUTTON    },
	{ PSP_CTRL_TRIANGLE, K_Y_BUTTON     },
	{ PSP_CTRL_CIRCLE,   K_B_BUTTON     },
	{ PSP_CTRL_CROSS,    K_A_BUTTON     },
	{ PSP_CTRL_SQUARE,   K_X_BUTTON     },
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

static float Platform_JoyRescale( float axis, float deadzone_min, float deadzone_max )
{
	float abs_axis, scale, r_deadzone_max;
	int flip_axis = 0;
	
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
	
	return ( flip_axis ? -abs_axis :  abs_axis );
}

void Platform_RunEvents( void )
{	
	int 				i;
	SceCtrlData			buf;
	signed short 		curr_X, curr_Y;
	static unsigned int	last_buttons;
	static signed short	last_X, last_Y;

	sceCtrlReadBufferPositive( &buf, 1 );

	for( i = 0; i < PSP_MAX_KEYS; i++ )
	{
		if( ( last_buttons ^ buf.Buttons ) &  psp_keymap[i].srckey )
			Key_Event( psp_keymap[i].dstkey, buf.Buttons & psp_keymap[i].srckey );		
	}
	last_buttons = buf.Buttons;

	if( FBitSet( psp_deadzone_min->flags, FCVAR_CHANGED ) || FBitSet( psp_deadzone_max->flags, FCVAR_CHANGED ) )
	{
		for ( i = 0; i < 256; i++ )
		{
			psp_joymap[i] = Platform_JoyRescale( i, psp_deadzone_min->value, psp_deadzone_max->value );
			psp_joymap[i] *= 256;
		}

		ClearBits( psp_deadzone_min->flags, FCVAR_CHANGED );
		ClearBits( psp_deadzone_max->flags, FCVAR_CHANGED );
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
	
	// set up the controller.
	sceCtrlSetSamplingCycle( 0 );
	sceCtrlSetSamplingMode( PSP_CTRL_MODE_ANALOG );

	// building a joystick map
	for ( i = 0; i < 256; i++ )
	{
		psp_joymap[i] = Platform_JoyRescale( i, psp_deadzone_min->value, psp_deadzone_max->value );
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