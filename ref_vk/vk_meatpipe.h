#pragma once

#include "xash3d_types.h"

typedef struct {
	int pog;
} vk_meatpipe_t;

qboolean R_VkMeatpipeLoad(vk_meatpipe_t *out, const char *filename);
void R_VkMeatpipeDestroy(vk_meatpipe_t *mp);
