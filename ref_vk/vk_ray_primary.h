#pragma once

#include "vk_ray_resources.h"

qboolean XVK_RayTracePrimaryInit( void );
void XVK_RayTracePrimaryDestroy( void );
void XVK_RayTracePrimaryReloadPipeline( void );

void XVK_RayTracePrimary( VkCommandBuffer cmdbuf, const vk_ray_resources_t *res );
