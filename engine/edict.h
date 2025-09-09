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

#ifndef EDICT_H
#define EDICT_H

#define MAX_ENT_LEAFS_32	24		// Orignally was 16
#define MAX_ENT_LEAFS_16	48

#include "progdefs.h"

struct edict_s
{
	qboolean		free;
	int		serialnumber;

	link_t		area;		// linked to a division node or leaf
	int		headnode;		// -1 to use normal leaf check

	int		num_leafs;
	union
	{
		int   leafnums32[MAX_ENT_LEAFS_32];
		short leafnums16[MAX_ENT_LEAFS_16];
	};

	float		freetime;		// sv.time when the object was freed

	void*		pvPrivateData;	// Alloced and freed by engine, used by DLLs
	entvars_t		v;		// C exported fields from progs

	// other fields from progs come immediately after
};

#endif//EDICT_H
