#pragma once

#include "vk_core.h"

#include "vk_const.h"

typedef struct descriptor_pool_s
{
	VkDescriptorPool pool;

	// TODO don't expose this, make a function to alloc desc set with given layout instead
	int next_free;
	//uint32_t *free_set;

	VkDescriptorSet sets[MAX_TEXTURES];
	VkDescriptorSetLayout one_texture_layout;

	// FIXME HOW THE F
	VkDescriptorSet ubo_sets[2];
	VkDescriptorSetLayout one_uniform_buffer_layout;
} descriptor_pool_t;

extern descriptor_pool_t vk_desc;

qboolean VK_DescriptorInit( void );
void VK_DescriptorShutdown( void );

typedef union {
	VkDescriptorBufferInfo buffer;
	VkDescriptorImageInfo image;
	VkDescriptorImageInfo *image_array;
	VkWriteDescriptorSetAccelerationStructureKHR accel;
} vk_descriptor_value_t;

typedef struct {
	int num_bindings;
	const VkDescriptorSetLayoutBinding *bindings;

	// Used in Write only
	vk_descriptor_value_t *values;

	VkPushConstantRange push_constants;

	VkPipelineLayout pipeline_layout;
	VkDescriptorSetLayout desc_layout;
	VkDescriptorPool desc_pool;

	int num_sets;
	VkDescriptorSet *desc_sets;
} vk_descriptors_t;

void VK_DescriptorsCreate(vk_descriptors_t *desc);
void VK_DescriptorsWrite(const vk_descriptors_t *desc, int set_slot);
void VK_DescriptorsDestroy(const vk_descriptors_t *desc);

// typedef enum {
// 	VK_DescType_SingleTexture,
// } vk_desc_type_t;
// VkDescriptorSet VK_DescriptorGetSet( vk_desc_type_t type );
