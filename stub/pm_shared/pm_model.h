#pragma once
#ifndef PM_MODEL_H
#define PM_MODEL_H

#include "xash3d_types.h"

typedef enum
{
	mod_bad = -1,
	mod_brush,
	mod_sprite,
	mod_alias,
	mod_studio
} modtype_t;

typedef struct model_s model_t;

typedef struct hull_s
{
	void *clipnodes;
	void *planes;
	int firstclipnode;
	int lastclipnode;
	vec3_t clip_mins;
	vec3_t clip_maxs;
} hull_t;

#endif /* PM_MODEL_H */
