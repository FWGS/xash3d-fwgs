#ifndef RAY_SHADOW_INTERFACE_GLSL_INCLUDED
#define RAY_SHADOW_INTERFACE_GLSL_INCLUDED

#define SHADOW_MISS 0
#define SHADOW_HIT 1
#define SHADOW_SKY 2

struct RayPayloadShadow {
	uint hit_type;
};

#endif //ifndef RAY_SHADOW_INTERFACE_GLSL_INCLUDED
