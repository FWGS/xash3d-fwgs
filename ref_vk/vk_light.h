#pragma once

#include "vk_const.h"

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

// TODO spotlight

typedef struct {
	struct {
		int grid_min_cell[3];
		int grid_size[3];
		int grid_cells;
	} map;

	int num_polygons;
	rt_light_polygon_t polygons[MAX_SURFACE_LIGHTS];

	int num_point_lights;
	vk_point_light_t point_lights[MAX_POINT_LIGHTS];

	int num_polygon_vertices;
	vec3_t polygon_vertices[MAX_SURFACE_LIGHTS * 7];

	struct {
		int point_lights;
		int polygons;
		int polygon_vertices;
	} num_static;

	vk_lights_cell_t cells[MAX_LIGHT_CLUSTERS];
} vk_lights_t;

extern vk_lights_t g_lights;

void VK_LightsInit( void );
void VK_LightsShutdown( void );

struct model_s;
void RT_LightsNewMapBegin( const struct model_s *map );
void RT_LightsNewMapEnd( const struct model_s *map );

void RT_LightsFrameBegin( void );
void RT_LightsFrameEnd( void );

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
