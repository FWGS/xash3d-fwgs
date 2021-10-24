#pragma once

#include "const.h" // required for mod_local.h
#include "mod_local.h"
#include "xash3d_mathlib.h"

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

typedef enum {
	Unknown = 0,
	Light,
	LightSpot,
	LightEnvironment,
	Worldspawn,
	Ignored,
} class_name_e;

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
	LightType type;

	vec3_t origin;
	vec3_t color;
	vec3_t dir;

	//int style;
	int flags;
	float stopdot, stopdot2;
	//char pattern[64];
	//int dark;
} vk_light_entity_t;

typedef struct {
	int num_lights;
	vk_light_entity_t lights[256];

	int single_environment_index;

	string wadlist;
} xvk_map_entities_t;

extern xvk_map_entities_t g_map_entities;

enum { NoEnvironmentLights = -1, MoreThanOneEnvironmentLight = -2 };

void XVK_ParseMapEntities( void );
