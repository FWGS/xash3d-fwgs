#pragma once

#include "vk_core.h"
#include "vk_buffer.h"

#define MAX_ACCELS 1024
#define MAX_KUSOCHKI 8192
#define MAX_EMISSIVE_KUSOCHKI 256
#define MODEL_CACHE_SIZE 1024

typedef struct vk_ray_model_s {
	VkAccelerationStructureKHR as;
	VkAccelerationStructureGeometryKHR *geoms;
	int max_prims;
	int num_geoms;
	int size;
	uint32_t kusochki_offset;
	qboolean dynamic;
	qboolean taken;

	struct {
		uint32_t as_offset;
	} debug;
} vk_ray_model_t;

typedef struct {
	uint32_t index_offset;
	uint32_t vertex_offset;
	uint32_t triangles;

	// Material parameters
	uint32_t texture;
	float roughness;
	uint32_t flags; // 0 -- opaque, 1 -- alpha mix, 2 -- additive, 3 -- alpha test
} vk_kusok_data_t;

typedef struct {
	uint32_t num_kusochki;
	uint32_t padding__[3];
	struct {
		uint32_t kusok_index;
		uint32_t padding__[3];
		vec3_t emissive_color;
		uint32_t padding___;
		matrix3x4 transform;
	} kusochki[MAX_EMISSIVE_KUSOCHKI];
} vk_emissive_kusochki_t;

typedef struct {
	matrix3x4 transform_row;
	vk_ray_model_t *model;
	int render_mode;
} vk_ray_draw_model_t;

typedef struct {
	const char *debug_name;
	VkAccelerationStructureKHR *p_accel;
	const VkAccelerationStructureGeometryKHR *geoms;
	const uint32_t *max_prim_counts;
	const VkAccelerationStructureBuildRangeInfoKHR *build_ranges;
	uint32_t n_geoms;
	VkAccelerationStructureTypeKHR type;
	qboolean dynamic;
} as_build_args_t;

qboolean createOrUpdateAccelerationStructure(VkCommandBuffer cmdbuf, const as_build_args_t *args, vk_ray_model_t *model);

typedef struct {
	// Geometry metadata. Lifetime is similar to geometry lifetime itself.
	// Semantically close to render buffer (describes layout for those objects)
	// TODO unify with render buffer
	// Needs: STORAGE_BUFFER
	vk_buffer_t kusochki_buffer;
	vk_ring_buffer_t kusochki_alloc;

	// TODO this should really be a single uniform buffer for matrices and light data

	// Expected to be small (qualifies for uniform buffer)
	// Two distinct modes: (TODO which?)
	// - static map-only lighting: constant for the entire map lifetime.
	//   Could be joined with render buffer, if not for possible uniform buffer binding optimization.
	//   This is how it operates now.
	// - fully dynamic lights: re-built each frame, so becomes similar to scratch_buffer and could be unified (same about uniform binding opt)
	//   This allows studio and other non-brush model to be emissive.
	// Needs: STORAGE/UNIFORM_BUFFER
	vk_buffer_t emissive_kusochki_buffer;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		int num_models;
		int num_lighttextures;
		vk_ray_draw_model_t models[MAX_ACCELS];
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	vk_ray_model_t models_cache[MODEL_CACHE_SIZE];

	qboolean freeze_models;
} xvk_ray_model_state_t;

extern xvk_ray_model_state_t g_ray_model_state;
	
void XVK_RayModel_ClearForNextFrame( void );
void XVK_RayModel_Validate(void);

VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer);