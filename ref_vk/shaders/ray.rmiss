#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "ray_common.glsl"

layout(location = 0) rayPayloadInEXT RayResult ray_result;

void main() {
    ray_result.color = vec3(1., 0., 1.);
}