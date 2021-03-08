#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

typedef int vk_buffer_handle_t; // -1 == invalid handle
enum { InvalidHandle = -1 };

typedef struct {
	void *ptr;
	uint32_t unit_size, count;
} vk_buffer_lock_t;

typedef enum {
	LifetimeLong,
	LifetimeMap,
	LifetimeSingleFrame,
} vk_lifetime_t;

// TODO: allocation lifetime with contents validity lifetime?

vk_buffer_handle_t VK_RenderBufferAlloc( uint32_t unit_size, uint32_t count, vk_lifetime_t lifetime );
vk_buffer_lock_t VK_RenderBufferLock( vk_buffer_handle_t handle );
void VK_RenderBufferUnlock( vk_buffer_handle_t handle );

// TODO buffer refcount when doing RTX AS updates? need to store buffer handles somewhere between frames

// Free all LifetimeSingleFrame resources
void VK_RenderBufferClearFrame( void );

// Free all LifetimeMap resources
void VK_RenderBufferClearMap( void );

// TODO uploading to GPU mem interface
void VK_RenderBufferPrintStats( void );

// Set UBO state for next VK_RenderScheduleDraw calls
// Why? Xash Ref code is organized in a way where we can't reliably pass this info with
// ScheduleDraw itself, so we need to either set up per-submodule global state, or
// centralize this global state in here
void VK_RenderStateSetColor( float r, float g, float b, float a );
// TODO void VK_RenderStateGetColor( vec4_t color );
void VK_RenderStateSetMatrix( const matrix4x4 mvp );
// TODO: set projection and mv matrices separately

void VK_RenderStateSetProjectionMatrix(const matrix4x4 proj);
void VK_RenderStateSetViewMatrix(const matrix4x4 view);

// TODO is this a good place?
typedef struct vk_vertex_s {
	// TODO padding needed for storage buffer reading, figure out how to fix in GLSL/SPV side
	vec3_t pos; float p0_;
	vec3_t normal; float p1_;
	vec2_t gl_tc; //float p2_[2];
	vec2_t lm_tc; //float p3_[2];
} vk_vertex_t;

typedef struct render_draw_s {
	int lightmap, texture;
	int render_mode;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	vk_buffer_handle_t index_buffer, vertex_buffer;
} render_draw_t;

void VK_RenderBegin( void );
void VK_RenderScheduleDraw( const render_draw_t *draw );
void VK_RenderEnd( VkCommandBuffer cmdbuf );
void VK_RenderEndRTX( VkCommandBuffer cmdbuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h );

void VK_RenderDebugLabelBegin( const char *label );
void VK_RenderDebugLabelEnd( void );
