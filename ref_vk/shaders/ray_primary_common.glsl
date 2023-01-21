#extension GL_EXT_ray_tracing: require

#define GLSL
#include "ray_interop.h"
#undef GLSL

struct RayPayloadPrimary {
	vec4 hit_t;
	vec4 prev_pos_t;
	vec4 base_color_a;
	vec4 normals_gs;
	vec4 material_rmxx;
	vec4 emissive;
};

#define PAYLOAD_LOCATION_PRIMARY 0
