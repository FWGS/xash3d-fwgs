#pragma once

#include "const.h"
#include "com_model.h"
#include "protocol.h"

#define BLOCK_SIZE_MAX	1024
#define BLOCK_SIZE BLOCK_SIZE_MAX

typedef struct {
	int lightstylevalue[MAX_LIGHTSTYLES];	// value 0 - 65536
} xvk_lightmap_state_t;

extern xvk_lightmap_state_t g_lightmap;

void VK_ClearLightmap( void );
void VK_CreateSurfaceLightmap( msurface_t *surf, const model_t *loadmodel );
void VK_UploadLightmap( void );
void VK_RunLightStyles( void );
