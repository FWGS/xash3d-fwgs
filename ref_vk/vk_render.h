#pragma once
#include "vk_common.h"
#include "vk_const.h"
#include "vk_core.h"

qboolean VK_RenderInit( void );
void VK_RenderShutdown( void );

// General buffer usage pattern
// 1. alloc (allocates buffer mem, stores allocation data)
// 2. (returns void* buf and handle) write to buf
// 3. upload and lock (ensures that all this data is in gpu mem, e.g. uploads from staging)
// 4. ... use it 
// 5. free (frame/map end)

typedef struct {
	struct {
		uint32_t size; // single unit size in bytes
		uint32_t offset; // offset in units from start of vulkan buffer
		uint32_t count; // number of units in this allocation
	} unit;
} xvk_render_buffer_t;

typedef struct {
	void *ptr;
	xvk_render_buffer_t buffer;
} xvk_render_buffer_allocation_t;

xvk_render_buffer_allocation_t XVK_RenderBufferAllocAndLock( uint32_t unit_size, uint32_t count );
void XVK_RenderBufferUnlock( xvk_render_buffer_t handle );
void XVK_RenderBufferMapFreeze( void ); // Permanently freeze all allocations as map-permanent
void XVK_RenderBufferMapClear( void ); // Free the entire buffer for a new map

void XVK_RenderBufferFrameClear( /*int frame_id*/void ); // mark data for frame with given id as free (essentially, just forward the ring buffer)

void XVK_RenderBufferPrintStats( void );

// Set UBO state for next VK_RenderScheduleDraw calls
// Why? Xash Ref code is organized in a way where we can't reliably pass this info with
// ScheduleDraw itself, so we need to either set up per-submodule global state, or
// centralize this global state in here
void VK_RenderStateSetColor( float r, float g, float b, float a );
// TODO void VK_RenderStateGetColor( vec4_t color );

void VK_RenderStateSetMatrixProjection(const matrix4x4 proj, float fov_angle_y);
void VK_RenderStateSetMatrixView(const matrix4x4 view);
void VK_RenderStateSetMatrixModel(const matrix4x4 model);

// TODO: radius, intensity, style, PVS bits, etc..
void VK_RenderAddStaticLight(vec3_t origin, vec3_t color);

// TODO is this a good place?
typedef struct vk_vertex_s {
	// TODO padding needed for storage buffer reading, figure out how to fix in GLSL/SPV side
	vec3_t pos; float p0_;
	vec3_t normal; uint32_t flags;
	vec2_t gl_tc; //float p2_[2];
	vec2_t lm_tc; //float p3_[2];

	rgba_t color; // per-vertex (non-rt lighting) color, color[3] == 1(255) => use color, discard lightmap; color[3] == 0 => use lightmap, discard color
	float _padding[3];
} vk_vertex_t;

// TODO not sure how to do materials yet. Figure this out
typedef enum {
	kXVkMaterialDiffuse,
	kXVkMaterialWater,
	kXVkMaterialSky,
} XVkMaterialType;

typedef struct {
	int index_offset, vertex_offset;

	// TODO can be dynamic
	int texture;
	XVkMaterialType material;

	uint32_t element_count;
	uint32_t vertex_count;

	// Non-null only for brush models
	// Used for:
	// - updating animated textures for brush models
	// - updating dynamic lights (TODO: can decouple from surface/brush models by providing texture_id and aabb directly here)
	struct msurface_s *surf;

	// Index into kusochki buffer for current frame
	uint32_t kusok_index;
} vk_render_geometry_t;

struct vk_ray_model_s;

typedef struct vk_render_model_s {
	const char *debug_name;
	int render_mode;
	int num_geometries;
	vk_render_geometry_t *geometries;

	// TODO potentially dynamic data: textures

	// This model will be one-frame only, its buffers are not preserved between frames
	qboolean dynamic;

	struct vk_ray_model_s *ray_model;
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

void VK_RenderDebugLabelBegin( const char *label );
void VK_RenderDebugLabelEnd( void );

void VK_RenderBegin( qboolean ray_tracing );
void VK_RenderEnd( VkCommandBuffer cmdbuf );
void VK_RenderEndRTX( VkCommandBuffer cmdbuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h );
