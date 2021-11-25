#version 460 core
#extension GL_EXT_ray_tracing: require

#include "ray_common.glsl"

layout(location = PAYLOAD_LOCATION_SHADOW) rayPayloadInEXT RayPayloadShadow payload_shadow;

void main() {
    payload_shadow.hit_type = SHADOW_MISS;
}
