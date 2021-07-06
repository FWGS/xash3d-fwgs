#version 460 core
#extension GL_EXT_ray_tracing: require

layout(location = 0) rayPayloadInEXT vec4 ray_result;

void main() {
    ray_result = vec4(1., 0., 1., 0.);
}