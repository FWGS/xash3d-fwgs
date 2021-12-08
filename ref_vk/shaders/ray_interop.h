// Common definitions for both shaders and native code

#ifndef GLSL
#define uint uint32_t
#define vec2 vec2_t
#define vec3 vec3_t
#define vec4 vec4_t
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define PAD(x) float TOKENPASTE2(pad_, __LINE__)[x];
#define STRUCT struct
#else
#extension GL_EXT_shader_8bit_storage : require

#define PAD(x)
#define STRUCT

layout (constant_id = 0) const uint MAX_POINT_LIGHTS = 32;
layout (constant_id = 1) const uint MAX_EMISSIVE_KUSOCHKI = 256;
layout (constant_id = 2) const uint MAX_VISIBLE_POINT_LIGHTS = 31;
layout (constant_id = 3) const uint MAX_VISIBLE_SURFACE_LIGHTS = 255;
#endif

#define GEOMETRY_BIT_OPAQUE 0x01
#define GEOMETRY_BIT_ADDITIVE 0x02
#define GEOMETRY_BIT_REFRACTIVE 0x04

#define SHADER_OFFSET_MISS_REGULAR 0
#define SHADER_OFFSET_MISS_SHADOW 1
#define SHADER_OFFSET_MISS_EMPTY 2

#define SHADER_OFFSET_HIT_REGULAR 0
#define SHADER_OFFSET_HIT_ALPHA_TEST 1
#define SHADER_OFFSET_HIT_ADDITIVE 2

#define SHADER_OFFSET_HIT_REGULAR_BASE 0
#define SHADER_OFFSET_HIT_SHADOW_BASE 3

#define KUSOK_MATERIAL_FLAG_SKYBOX 0x80000000

struct Kusok {
	uint index_offset;
	uint vertex_offset;
	uint triangles;

	// Material
	uint tex_base_color;

	// TODO the color is per-model, not per-kusok
	vec4 color;

	vec3 emissive;
	uint tex_roughness;

	vec2 uv_speed; // for conveyors; TODO this can definitely be done in software more efficiently (there only a handful of these per map)
	uint tex_metalness;
	uint tex_normalmap;

	float roughness;
	float metalness;
	PAD(2)
};

struct PointLight {
	vec4 origin_r;
	vec4 color_stopdot;
	vec4 dir_stopdot2;
	uint environment; // Is directional-only environment light
	PAD(3)
};

struct EmissiveKusok {
	uint kusok_index;
	PAD(3)
	vec3 emissive;
	PAD(1)
	vec4 tx_row_x, tx_row_y, tx_row_z;
};

struct Lights {
	uint num_kusochki;
	uint num_point_lights;
	PAD(2)
	STRUCT EmissiveKusok kusochki[MAX_EMISSIVE_KUSOCHKI];
	STRUCT PointLight point_lights[MAX_POINT_LIGHTS];
};

struct LightCluster {
	uint8_t num_point_lights;
	uint8_t num_emissive_surfaces;
	uint8_t point_lights[MAX_VISIBLE_POINT_LIGHTS];
	uint8_t emissive_surfaces[MAX_VISIBLE_SURFACE_LIGHTS];
};

#define PUSH_FLAG_LIGHTMAP_ONLY 0x01

struct PushConstants {
	float time;
	uint random_seed;
	int bounces;
	float prev_frame_blend_factor;
	float pixel_cone_spread_angle;
	uint debug_light_index_begin, debug_light_index_end;
	uint flags;
};

#undef PAD
#undef STRUCT

#ifndef GLSL
#undef uint
#undef vec3
#undef vec4
#undef TOKENPASTE
#undef TOKENPASTE2
#endif
