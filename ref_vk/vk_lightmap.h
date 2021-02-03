#pragma once

#include "const.h"
#include "com_model.h"

#define BLOCK_SIZE_MAX	1024
#define BLOCK_SIZE BLOCK_SIZE_MAX

void VK_ClearLightmap( void );
void VK_CreateSurfaceLightmap( msurface_t *surf, const model_t *loadmodel );
void VK_UploadLightmap( void );
void VK_RunLightStyles( void );
