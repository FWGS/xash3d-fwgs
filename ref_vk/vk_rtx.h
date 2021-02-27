#pragma once

#include "vk_core.h"

typedef struct {
	//int lightmap, texture;
	//int render_mode;
	uint32_t element_count, vertex_count;
	uint32_t index_offset, vertex_offset;
	VkBuffer buffer;
} vk_ray_model_create_t;

typedef int vk_ray_model_handle_t;
enum { InvalidRayModel = -1 };

vk_ray_model_handle_t VK_RayModelCreate( const vk_ray_model_create_t *args );

void VK_RaySceneBegin( void );
void VK_RayScenePushModel( VkCommandBuffer cmdbuf, vk_ray_model_handle_t model );
void VK_RaySceneEnd( VkCommandBuffer cmdbuf );

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

