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

// typedef enum {
// 	VK_DescType_SingleTexture,
// } vk_desc_type_t;
// VkDescriptorSet VK_DescriptorGetSet( vk_desc_type_t type );
