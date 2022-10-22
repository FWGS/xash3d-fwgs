#pragma once

#include "ray_pass.h"
#include "xash3d_types.h"

typedef struct {
	int passes_count;
	ray_pass_p *passes;
} vk_meatpipe_t;

qboolean R_VkMeatpipeLoad(vk_meatpipe_t *out, const char *filename);
void R_VkMeatpipeDestroy(vk_meatpipe_t *mp);
