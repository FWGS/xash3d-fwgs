#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

typedef struct vk_buffer_alloc_s {
	uint32_t buffer_offset_in_units;
	void *ptr;
} vk_buffer_alloc_t;

// TODO uploading to GPU mem interface
vk_buffer_alloc_t VK_RenderBufferAlloc( uint32_t unit_size, uint32_t count );
void VK_RenderBufferClearAll( void );
void VK_RenderBufferPrintStats( void );

// TODO address cringiness of this when doing buffer upload to GPU RAM properly
void VK_RenderTempBufferBegin( void );
vk_buffer_alloc_t VK_RenderTempBufferAlloc( uint32_t unit_size, uint32_t count );
void VK_RenderTempBufferEnd( void );

// Set UBO state for next VK_RenderScheduleDraw calls
// Why? Xash Ref code is organized in a way where we can't reliably pass this info with
// ScheduleDraw itself, so we need to either set up per-submodule global state, or
// centralize this global state in here
void VK_RenderStateSetColor( float r, float g, float b, float a );
// TODO void VK_RenderStateGetColor( vec4_t color );
void VK_RenderStateSetMatrix( const matrix4x4 mvp );
// TODO: set projection and mv matrices separately

// TODO is this a good place?
typedef struct vk_vertex_s {
	vec3_t pos;
	vec2_t gl_tc;
	vec2_t lm_tc;
} vk_vertex_t;

typedef struct render_draw_s {
	int lightmap, texture;
	int render_mode;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
} render_draw_t;

void VK_RenderBegin( void );
void VK_RenderScheduleDraw( const render_draw_t *draw );
void VK_RenderEnd( VkCommandBuffer cmdbuf );

void VK_RenderDebugLabelBegin( const char *label );
void VK_RenderDebugLabelEnd( void );
