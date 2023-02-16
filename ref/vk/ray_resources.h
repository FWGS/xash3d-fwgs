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
	const vk_resource_p *resources;
	const int *resources_map;
	vk_descriptor_value_t* values;
	int count;
	int write_begin; // Entries starting at this index are written into by the pass
} vk_resources_write_descriptors_args_t;

void R_VkResourcesPrepareDescriptorsValues(VkCommandBuffer cmdbuf, vk_resources_write_descriptors_args_t args);


