#pragma once

#include "vk_core.h"

struct vk_render_model_s;
struct vk_ray_model_s;

typedef struct {
	struct vk_render_model_s *model;
	VkBuffer buffer; // TODO must be uniform for all models. Shall we read it directly from vk_render?
} vk_ray_model_init_t;

struct vk_ray_model_s *VK_RayModelInit( vk_ray_model_init_t model_init );
void VK_RayModelDestroy( struct vk_ray_model_s *model );

void VK_RayFrameBegin( void );
void VK_RayFrameAddModel( struct vk_ray_model_s *model, const matrix3x4 *transform_row );

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

