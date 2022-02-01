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

typedef struct {
	VkAccessFlags access_mask;
	VkImageLayout image_layout;
	VkPipelineStageFlagBits pipelines;
} ray_resource_state_t;

typedef struct {
	VkDescriptorType type;
	ray_resource_state_t write, read;
	vk_descriptor_value_t value;
} ray_resource_t;

#define RAY_RESOURCE_DEFAULT_STATE \
	(ray_resource_state_t) { \
		.access_mask = 0, \
		.image_layout = VK_IMAGE_LAYOUT_UNDEFINED, \
		.pipelines = 0, \
	}

typedef struct vk_ray_resources_s {
	uint32_t width, height;
	ray_resource_t values[RayResource__COUNT];
} vk_ray_resources_t;

typedef struct {
	vk_ray_resources_t *resources;
	const int *indices;
	int count;
	VkPipelineStageFlagBits dest_pipeline;

	vk_descriptor_value_t *out_values;
} ray_resources_fill_t;

void RayResourcesFill(ray_resources_fill_t fill);
