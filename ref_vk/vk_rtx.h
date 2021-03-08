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

typedef struct {
	VkCommandBuffer cmdbuf;

	struct {
		VkImageView image_view;
		VkImage image;
		uint32_t width, height;
	} dst;

	// TODO inv_view/proj matrices
	struct {
		VkBuffer buffer;
		uint32_t offset;
		uint32_t size;
	} ubo;

	// TODO dlights

	// Buffer holding vertex and index data
	struct {
		VkBuffer buffer; // must be the same as in vk_ray_model_create_t TODO: validate or make impossible to specify incorrectly
		uint32_t size;
	} geometry_data;
} vk_ray_scene_render_args_t;
void VK_RaySceneEnd(const vk_ray_scene_render_args_t* args);

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

