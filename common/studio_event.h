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
#pragma once
#ifndef STUDIO_EVENT_H
#define STUDIO_EVENT_H

#define MAXEVENTSTRING		64

typedef struct mstudioevent_s
{
	int32_t 	frame;
	int32_t		event;
	int32_t		type;
	char		options[MAXEVENTSTRING];
} mstudioevent_t;

#endif // STUDIO_EVENT_H
