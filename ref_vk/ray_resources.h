#pragma once

#include "vk_core.h"
#include "vk_descriptor.h"

typedef struct {
	VkAccessFlags access_mask;
	VkImageLayout image_layout;
	VkPipelineStageFlagBits pipelines;
} ray_resource_state_t;

struct xvk_image_s;
typedef struct vk_resource_s {
	VkDescriptorType type;
	ray_resource_state_t write, read;
	vk_descriptor_value_t value;
} vk_resource_t;

typedef struct vk_resource_s *vk_resource_p;

typedef struct {
	VkPipelineStageFlagBits pipeline;
	vk_resource_p resource;
	vk_descriptor_value_t* value;
	qboolean write;
} vk_resource_write_descriptor_args_t;

void R_VkResourceWriteToDescriptorValue(VkCommandBuffer cmdbuf, vk_resource_write_descriptor_args_t args);
