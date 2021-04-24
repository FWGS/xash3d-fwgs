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
uint32_t VK_RenderBufferGetOffsetInUnits( vk_buffer_handle_t handle );

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

void VK_RenderStateSetMatrixProjection(const matrix4x4 proj);
void VK_RenderStateSetMatrixView(const matrix4x4 view);
void VK_RenderStateSetMatrixModel(const matrix4x4 model);

// TODO: radius, intensity, style, PVS bits, etc..
void VK_RenderAddStaticLight(vec3_t origin, vec3_t color);

// TODO is this a good place?
typedef struct vk_vertex_s {
	// TODO padding needed for storage buffer reading, figure out how to fix in GLSL/SPV side
	vec3_t pos; float p0_;
	vec3_t normal; float p1_;
	vec2_t gl_tc; //float p2_[2];
	vec2_t lm_tc; //float p3_[2];
} vk_vertex_t;

typedef struct {
	vk_buffer_handle_t index_buffer, vertex_buffer;
	uint32_t index_offset, vertex_offset;

	// TODO can be dynamic
	int texture;

	uint32_t element_count;
	uint32_t vertex_count;

	// TODO we don't really need this here
	// as it's used to tie emissive surfaces to geometry in RTX renderer
	// we can and should infer this dynamically (from texture) when building dynamic light clusters
	int surface_index;
} vk_render_geometry_t;

typedef struct vk_render_model_s {
	const char *debug_name;
	int render_mode;
	int num_geometries;
	vk_render_geometry_t *geometries;

	// TODO potentially dynamic data: textures

	// This model will be one-frame only, its buffers are not preserved between frames
	qboolean dynamic;

	struct {
		VkAccelerationStructureKHR blas;
		uint32_t kusochki_offset;
	} rtx;
} vk_render_model_t;

qboolean VK_RenderModelInit( vk_render_model_t* model );
void VK_RenderModelDestroy( vk_render_model_t* model );
void VK_RenderModelDraw( vk_render_model_t* model );

void VK_RenderFrameBegin( void );

void VK_RenderModelDynamicBegin( const char *debug_name, int render_mode );
void VK_RenderModelDynamicAddGeometry( const vk_render_geometry_t *geom );
void VK_RenderModelDynamicCommit( void );

void VK_RenderFrameEnd( VkCommandBuffer cmdbuf );
void VK_RenderFrameEndRTX( VkCommandBuffer cmdbuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h );

// void VK_RenderDebugLabelBegin( const char *label );
// void VK_RenderDebugLabelEnd( void );
