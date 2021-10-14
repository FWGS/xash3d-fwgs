// Common definitions for both shaders and native code

#ifndef GLSL
#define uint uint32_t
#define vec3 vec3_t
#define vec4 vec4_t
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define PAD(x) float TOKENPASTE2(pad_, __LINE__)[x];
#define STRUCT struct
#else
#define PAD(x)
#define STRUCT

layout (constant_id = 0) const uint MAX_POINT_LIGHTS = 32;
layout (constant_id = 1) const uint MAX_EMISSIVE_KUSOCHKI = 256;
#endif

#define GEOMETRY_BIT_ANY 0x01
#define GEOMETRY_BIT_OPAQUE 0x02

struct Kusok {
	uint index_offset;
	uint vertex_offset;
	uint triangles;

	// Material
	uint texture;

	vec4 color;

	vec3 emissive;
	//PAD(1)

	float roughness;
};

struct PointLight {
	vec4 position;
	vec4 color;
};

struct EmissiveKusok {
	uint kusok_index;
	PAD(3)
	vec4 tx_row_x, tx_row_y, tx_row_z;
};

struct Lights {
	uint num_kusochki;
	uint num_point_lights;
	PAD(2)
	STRUCT EmissiveKusok kusochki[MAX_EMISSIVE_KUSOCHKI];
	STRUCT PointLight point_lights[MAX_POINT_LIGHTS];
};

struct PushConstants {
	uint random_seed;
	int bounces;
	float prev_frame_blend_factor;
	float pixel_cone_spread_angle;
	uint debug_light_index_begin, debug_light_index_end;
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
