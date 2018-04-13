/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#ifndef SCREENFADE_H
#define SCREENFADE_H

typedef struct screenfade_s
{
	float	fadeSpeed;			// How fast to fade (tics / second) (+ fade in, - fade out)
	float	fadeEnd;				// When the fading hits maximum
	float	fadeTotalEnd;			// Total End Time of the fade (used for FFADE_OUT)
	float	fadeReset;			// When to reset to not fading (for fadeout and hold)
	byte	fader, fadeg, fadeb, fadealpha;	// Fade color
	int	fadeFlags;			// Fading flags
} screenfade_t;

#endif//SCREENFADE_H