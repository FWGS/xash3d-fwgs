#pragma once
#include "vk_common.h"
#include "vk_core.h"

#include <stdint.h>

// General buffer usage pattern
// 1. alloc (allocates buffer mem, stores allocation data)
// 2. (returns void* buf and handle) write to buf
// 3. upload and lock (ensures that all this data is in gpu mem, e.g. uploads from staging)
// 4. ... use it
// 5. free (frame/map end)

// TODO is this a good place?
typedef struct vk_vertex_s {
	// TODO padding needed for storage buffer reading, figure out how to fix in GLSL/SPV side
	vec3_t pos; float p0_;
	vec3_t prev_pos; float p01_;
	vec3_t normal; uint32_t flags;
	vec3_t tangent; uint32_t p1_;
	vec2_t gl_tc; //float p2_[2];
	vec2_t lm_tc; //float p3_[2];

	rgba_t color; // per-vertex (non-rt lighting) color, color[3] == 1(255) => use color, discard lightmap; color[3] == 0 => use lightmap, discard color
	float _padding[3];
} vk_vertex_t;

typedef struct {
	struct {
		vk_vertex_t *ptr;
		int count;
		int unit_offset;
	} vertices;

	struct {
		uint16_t *ptr;
		int count;
		int unit_offset;
	} indices;

	struct {
		int staging_handle;
	} impl_;
} r_geometry_buffer_lock_t;

typedef enum {
	LifetimeLong,
	LifetimeSingleFrame
} r_geometry_lifetime_t;

qboolean R_GeometryBufferAllocAndLock( r_geometry_buffer_lock_t *lock, int vertex_count, int index_count, r_geometry_lifetime_t lifetime );
void R_GeometryBufferUnlock( const r_geometry_buffer_lock_t *lock );
//void R_VkGeometryBufferFree( int handle );

void R_GeometryBuffer_MapClear( void ); // Free the entire buffer for a new map

qboolean R_GeometryBuffer_Init(void);
void R_GeometryBuffer_Shutdown(void);

void R_GeometryBuffer_Flip(void);

// FIXME is there a better way?
VkBuffer R_GeometryBuffer_Get(void);

