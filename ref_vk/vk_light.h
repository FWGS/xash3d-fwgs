#pragma once

#include "vk_const.h"

#include "xash3d_types.h"
#include "protocol.h"
#include "const.h"
#include "bspfile.h"

// TODO this should not be visible to outside
typedef struct {
	vec3_t emissive;
	qboolean set;
} vk_emissive_texture_table_t;

extern vk_emissive_texture_table_t g_emissive_texture_table[MAX_TEXTURES];

void VK_LightsLoadMap( void );

typedef struct {
	uint8_t num_dlights;
	uint8_t num_slights;
	uint8_t dlights[MAX_VISIBLE_DLIGHTS];
	uint8_t slights[MAX_VISIBLE_SURFACE_LIGHTS];
} vk_light_leaf_t;

typedef struct {
	vec3_t emissive;
	int surface_index;
} vk_emissive_surface_t;

typedef struct {
	// TODO make this opaque light clusters
	int num_leaves; // same as worldmodel->numleaves
	vk_light_leaf_t *leaves;

	int num_emissive_surfaces;
	vk_emissive_surface_t emissive_surfaces[256]; // indexed by uint8_t

	int num_surfaces;
	struct {
		int leaf;
	} *surfaces;
} vk_potentially_visible_lights_t;

extern vk_potentially_visible_lights_t g_lights;

void VK_LightsShutdown( void );
