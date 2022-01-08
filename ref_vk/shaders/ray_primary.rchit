#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "ray_primary_common.glsl"

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;

void main() {
	payload.hit_t.w = gl_HitTEXT;
	payload.hit_t.xyz = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
}
