#version 460 core
#extension GL_EXT_ray_tracing: require

layout(location = 0) rayPayloadInEXT vec4 ray_result;

void main() {
    vec3 hit_pos = gl_HitTEXT * gl_WorldRayDirectionEXT + gl_WorldRayOriginEXT;
	ray_result = vec4(fract(hit_pos / 1.), 1.);
}