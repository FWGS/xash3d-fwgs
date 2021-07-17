#version 460 core
#extension GL_EXT_ray_tracing: require

layout(location = 1) rayPayloadInEXT bool shadow;

void main() {
    shadow = false;
}