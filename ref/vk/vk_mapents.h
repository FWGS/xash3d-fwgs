#pragma once
#include "xash3d_types.h"

#define ENT_PROP_LIST(X) \
	X(0, vec3_t, origin, Vec3) \
	X(1, vec3_t, angles, Vec3) \
	X(2, float, pitch, Float) \
	X(3, vec3_t, _light, Rgbav) \
	X(4, class_name_e, classname, Classname) \
	X(5, float, angle, Float) \
	X(6, float, _cone, Float) \
	X(7, float, _cone2, Float) \
	X(8, int, _sky, Int) \
	X(9, string, wad, WadList) \
	X(10, string, targetname, String) \
	X(11, string, target, String) \
	X(12, int, style, Int) \
	X(13, int_array_t, _xvk_surface_id, IntArray) \
	X(14, string, _xvk_texture, String) \
	X(15, int_array_t, _xvk_ent_id, IntArray) \
	X(16, float, _xvk_radius, Float) \

typedef enum {
	Unknown = 0,
	Light,
	LightSpot,
	LightEnvironment,
	Worldspawn,
	Ignored,
} class_name_e;

#define MAX_INT_ARRAY_SIZE 64

typedef struct {
	int num;
	int values[MAX_INT_ARRAY_SIZE];
} int_array_t;

typedef struct {
#define DECLARE_FIELD(num, type, name, kind) type name;
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} entity_props_t;

typedef enum {
	None = 0,
#define DECLARE_FIELD(num, type, name, kind) Field_##name = (1<<num),
	ENT_PROP_LIST(DECLARE_FIELD)
#undef DECLARE_FIELD
} fields_read_e;

typedef enum { LightTypePoint, LightTypeSurface, LightTypeSpot, LightTypeEnvironment} LightType;

typedef struct {
	int entity_index;
	LightType type;

	vec3_t origin;
	vec3_t color;
	vec3_t dir;

	float radius;

	int style;
	float stopdot, stopdot2;

	string target_entity;
} vk_light_entity_t;

typedef struct {
	string targetname;
	vec3_t origin;
} xvk_mapent_target_t;

enum {
	Patch_Surface_NoPatch = 0,
	Patch_Surface_Delete = 0x01,
	Patch_Surface_Texture = 0x02,
	Patch_Surface_Emissive = 0x04,
};

struct texture_s;

typedef struct {
	uint32_t flags;

	// Static texture index in case there's no texture_s pointer
	int tex_id;

	// Pointer to texture_s data (which also may include animation)
	const struct texture_s *tex;

	vec3_t emissive;
} xvk_patch_surface_t;

typedef struct {
	xvk_patch_surface_t *surfaces;
} xvk_patch_t;

#define MAX_MAPENT_TARGETS 256

typedef struct {
	int num_lights;
	vk_light_entity_t lights[256];

	int single_environment_index;
	int entity_count;

	string wadlist;

	int num_targets;
	xvk_mapent_target_t targets[MAX_MAPENT_TARGETS];

	xvk_patch_t patch;
} xvk_map_entities_t;

extern xvk_map_entities_t g_map_entities;

enum { NoEnvironmentLights = -1, MoreThanOneEnvironmentLight = -2 };

void XVK_ParseMapEntities( void );
void XVK_ParseMapPatches( void );
