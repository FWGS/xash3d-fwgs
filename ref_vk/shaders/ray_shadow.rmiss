#version 460 core
#extension GL_EXT_ray_tracing: require

#include "ray_shadow_interface.glsl"

layout(location = 0) rayPayloadInEXT RayPayloadShadow payload_shadow;

void main() {
    payload_shadow.hit_type = SHADOW_MISS;
}
