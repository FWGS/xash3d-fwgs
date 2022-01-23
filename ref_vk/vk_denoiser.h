#pragma once

#include "vk_ray_resources.h"
#include "vk_core.h"

qboolean XVK_DenoiserInit( void );
void XVK_DenoiserDestroy( void );

void XVK_DenoiserReloadPipeline( void );

void XVK_DenoiserDenoise( VkCommandBuffer cmdbuf, const vk_ray_resources_t* res );
