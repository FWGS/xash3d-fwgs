#pragma once

#include "vk_const.h"
#include "vk_core.h"

#include "xash3d_types.h"

typedef struct {
	uint8_t num_point_lights;
	uint8_t num_polygons;

	uint8_t point_lights[MAX_VISIBLE_POINT_LIGHTS];
	uint8_t polygons[MAX_VISIBLE_SURFACE_LIGHTS];

	struct {
		uint8_t point_lights;
		uint8_t polygons;
	} num_static;

	uint32_t frame_sequence;
} vk_lights_cell_t;

typedef struct {
	vec4_t plane;
	vec3_t center;
	float area;

	vec3_t emissive;

	struct {
		int offset, count; // reference g_light.polygon_vertices
	} vertices;

	// uint32_t kusok_index;
} rt_light_polygon_t;

enum {
	LightFlag_Environment = 0x1,
};

typedef struct {
	vec3_t origin;
	vec3_t color;
	vec3_t dir;
	float stopdot, stopdot2;
	float radius;
	int flags;

	int lightstyle;
	vec3_t base_color;
} vk_point_light_t;

// Used by infotool
typedef struct {
	struct {
		int grid_min_cell[3];
		int grid_size[3];
		int grid_cells;
	} map;

	vk_lights_cell_t cells[MAX_LIGHT_CLUSTERS];
} vk_lights_t;

extern vk_lights_t g_lights;

qboolean VK_LightsInit( void );
void VK_LightsShutdown( void );

// Allocate clusters and vis data for the new map
struct model_s;
void RT_LightsNewMap( const struct model_s *map );

// Clear light data and prepare for loading
// RT_LightsNewMap should have been already called for current map
void RT_LightsLoadBegin( const struct model_s *map );
// Finalize loading light data, i.e. mark everything loaded so far as static light data
void RT_LightsLoadEnd( void );

void RT_LightsFrameBegin( void );
void RT_LightsFrameEnd( void );

typedef struct {
	VkBuffer buffer;
	struct {
		uint32_t offset, size;
	} metadata, grid;
} vk_lights_bindings_t;
vk_lights_bindings_t VK_LightsUpload( void );

qboolean RT_GetEmissiveForTexture( vec3_t out, int texture_id );

int RT_LightCellIndex( const int light_cell[3] );

struct cl_entity_s;
void RT_LightAddFlashlight( const struct cl_entity_s *ent, qboolean local_player );

struct msurface_s;
typedef struct rt_light_add_polygon_s {
	int num_vertices;
	vec3_t vertices[7];

	vec3_t emissive;

	// Needed for BSP visibilty purposes
	// TODO can we layer light code? like:
	// - bsp/xash/rad/patch-specific stuff
	// - mostly engine-agnostic light clusters
	const struct msurface_s *surface;

	qboolean dynamic;
	const matrix3x4 *transform_row;
} rt_light_add_polygon_t;
int RT_LightAddPolygon(const rt_light_add_polygon_t *light);
