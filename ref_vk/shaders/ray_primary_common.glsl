#extension GL_EXT_ray_tracing: require

#define GLSL
#include "ray_interop.h"
#undef GLSL

struct RayPayloadPrimary {
	vec4 hit_t;
};

#define PAYLOAD_LOCATION_PRIMARY 0
