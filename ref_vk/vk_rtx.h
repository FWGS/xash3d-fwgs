#pragma once

#include "vk_core.h"

typedef struct {
	//int lightmap, texture;
	//int render_mode;
	uint32_t max_vertex;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	VkBuffer buffer;
} vk_ray_model_create_t;

typedef int vk_ray_model_handle_t;
enum { InvalidRayModel = -1 };

vk_ray_model_handle_t VK_RayModelCreate( const vk_ray_model_create_t *args );

void VK_RaySceneBegin( void );
void VK_RayScenePushModel(VkCommandBuffer cmdbuf, const vk_ray_model_create_t* model); // vk_ray_model_handle_t model );
void VK_RaySceneEnd( VkCommandBuffer cmdbuf, VkImageView img_dst, uint32_t w, uint32_t h );

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

