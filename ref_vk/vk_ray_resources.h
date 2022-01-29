#pragma once

#include "vk_rtx.h"
#include "vk_const.h"
#include "vk_descriptor.h"

#include "shaders/ray_interop.h"

#define RAY_SCENE_RESOURCES(X) \
	X(TLAS, tlas) \
	X(Buffer, ubo) \
	X(Buffer, kusochki) \
	X(Buffer, indices) \
	X(Buffer, vertices) \
	X(Buffer, lights) \
	X(Buffer, light_clusters) \
	X(Texture, all_textures) \

enum {
#define X(type, name, ...) RayResource_##name,
	RAY_SCENE_RESOURCES(X)
	RAY_PRIMARY_OUTPUTS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
	RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
	X(-1, denoised)
#undef X
	RayResource__COUNT
};

	/* TODO
typedef struct {
	struct {
		VkAccessFlags access_mask;
		VkImageLayout image_layout;
		VkPipelineStageFlagBits pipelines;
	} state;

	union {
		VkAccelerationStructureKHR tlas;
		vk_buffer_region_t buffer;
		VkImageView image;
	} value;
} ray_resource_t;
*/

typedef struct vk_ray_resources_s {
	uint32_t width, height;
	vk_descriptor_value_t values[RayResource__COUNT];
} vk_ray_resources_t;
