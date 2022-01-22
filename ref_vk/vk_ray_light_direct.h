#pragma once
#include "vk_ray_resources.h"

qboolean XVK_RayTraceLightDirectInit( void );
void XVK_RayTraceLightDirectDestroy( void );
void XVK_RayTraceLightDirectReloadPipeline( void );

void XVK_RayTraceLightDirect( VkCommandBuffer cmdbuf, const vk_ray_resources_t *res );
