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

void VK_RenderBufferPrintStats( void );

// TODO address cringiness of this when doing buffer upload to GPU RAM properly
void VK_RenderTempBufferBegin( void );
vk_buffer_alloc_t VK_RenderTempBufferAlloc( uint32_t unit_size, uint32_t count );
void VK_RenderTempBufferEnd( void );

// TODO is this a good place?
typedef struct brush_vertex_s {
	vec3_t pos;
	vec2_t gl_tc;
	vec2_t lm_tc;
} brush_vertex_t;

typedef struct render_draw_s {
	int ubo_index;
	int lightmap, texture;
	int render_mode;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
} render_draw_t;

void VK_RenderBegin( void );
void VK_RenderDraw( const render_draw_t *draw );
void VK_RenderEnd( void );
