#pragma once

#include "vk_core.h"

struct vk_render_model_s;
struct vk_ray_model_s;
struct model_s;

typedef struct {
	struct vk_render_model_s *model;
	VkBuffer buffer; // TODO must be uniform for all models. Shall we read it directly from vk_render?
} vk_ray_model_init_t;

struct vk_ray_model_s *VK_RayModelCreate( vk_ray_model_init_t model_init );
void VK_RayModelDestroy( struct vk_ray_model_s *model );

void VK_RayFrameBegin( void );

// TODO how to improve this render vs ray model storage/interaction?
void VK_RayFrameAddModel(struct vk_ray_model_s *model, const struct vk_render_model_s *render_model, const matrix3x4 *transform_row);

typedef struct {
	VkBuffer buffer;
	uint32_t offset;
	uint64_t size;
} vk_buffer_region_t;

typedef struct {
	struct vk_combuf_s *combuf;

	struct {
		VkImageView image_view;
		VkImage image;
		uint32_t width, height;
	} dst;

	const matrix4x4 *projection, *view;

	// Buffer holding vertex and index data
	struct {
		VkBuffer buffer; // must be the same as in vk_ray_model_create_t TODO: validate or make impossible to specify incorrectly
		uint64_t size;
	} geometry_data;

	float fov_angle_y;
} vk_ray_frame_render_args_t;
void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args);

void VK_RayNewMap( void );
void VK_RayMapLoadEnd( void );

qboolean VK_RayInit( void );
void VK_RayShutdown( void );

