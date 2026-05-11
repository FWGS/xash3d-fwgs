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

#ifndef IVOICETWEAK_H
#define IVOICETWEAK_H

// These provide access to the voice controls.
typedef enum
{
	MicrophoneVolume = 0,		// values 0-1.
	OtherSpeakerScale			// values 0-1. Scales how loud other players are.
} VoiceTweakControl;

typedef struct IVoiceTweak_s
{
	// These turn voice tweak mode on and off. While in voice tweak mode, the user's voice is echoed back
	// without sending to the server.
	int	(*StartVoiceTweakMode)( void );	// Returns 0 on error.
	void	(*EndVoiceTweakMode)( void );

	// Get/set control values.
	void	(*SetControlFloat)( VoiceTweakControl iControl, float value );
	float	(*GetControlFloat)( VoiceTweakControl iControl );
} IVoiceTweak;

#endif//IVOICETWEAK_H
