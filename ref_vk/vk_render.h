#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

uniform_data_t *VK_RenderGetUniformSlot(int index);

typedef struct vk_buffer_alloc_s {
	uint32_t buffer_offset_in_units;
	void *ptr;
} vk_buffer_alloc_t;

// TODO uploading to GPU mem interface
vk_buffer_alloc_t VK_RenderBufferAlloc( uint32_t unit_size, uint32_t count );

void VK_RenderBufferClearAll( void );

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

// TODO should this not be global?
void VK_RenderBindBuffers( void );
void VK_RenderBindUniformBufferWithIndex( VkPipelineLayout pipeline_layout, int index );

void VK_RenderBufferPrintStats( void );
