#pragma once

#include "vk_core.h"

struct vk_render_model_s;

typedef struct {
	struct vk_render_model_s *model;
	VkBuffer buffer;
} vk_ray_model_init_t;

qboolean VK_RayModelInit( vk_ray_model_init_t model_init);
void VK_RayModelDestroy( struct vk_render_model_s *model );

typedef struct {
	//int lightmap, texture;
	//int render_mode;
	int texture_id;
	uint32_t max_vertex;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	VkBuffer buffer;
	const matrix3x4 *transform_row;
	struct { float r,g,b; } emissive;
} vk_ray_model_dynamic_t;

void VK_RayFrameBegin( void );
void VK_RayFrameAddModel( const struct vk_render_model_s *model, const matrix3x4 *transform_row );
void VK_RayFrameAddModelDynamic(VkCommandBuffer cmdbuf, const vk_ray_model_dynamic_t* model);

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

	struct {
		VkBuffer buffer;
		uint32_t offset;
		uint32_t size;
	} dlights;

	// Buffer holding vertex and index data
	struct {
		VkBuffer buffer; // must be the same as in vk_ray_model_create_t TODO: validate or make impossible to specify incorrectly
		uint32_t size;
	} geometry_data;
} vk_ray_frame_render_args_t;
void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args);

void VK_RayNewMap( void );
void VK_RayMapLoadEnd( void );

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

