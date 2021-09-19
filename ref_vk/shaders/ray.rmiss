#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "ray_common.glsl"
#include "ray_kusochki.glsl"

layout (constant_id = 0) const uint MAX_POINT_LIGHTS = 32;
layout (constant_id = 1) const uint MAX_EMISSIVE_KUSOCHKI = 256;

// TODO #include, use from here and regular shader
struct EmissiveKusok {
	uint kusok_index;
	vec4 tx_row_x, tx_row_y, tx_row_z;
};

layout (set = 0, binding = 7/*, align=4*/) uniform Lights {
	uint num_kusochki;
	uint num_point_lights;
	vec3 sun_dir, sun_color;
	EmissiveKusok kusochki[MAX_EMISSIVE_KUSOCHKI];
	PointLight point_lights[MAX_POINT_LIGHTS];
} lights;

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
	const float sun_dot = max(0., dot(gl_WorldRayDirectionEXT, lights.sun_dir));
    payload.hit_pos_t = vec4(-1.);
    payload.geometry_normal = payload.normal = vec3(0., 1., 0.);
	payload.reflection = 0.;
    payload.roughness = 0.;
    payload.base_color = vec3(0.);//mix(vec3(.1, .2, .7), lights.sun_color, pow(sun_dot, 100.));
	//vec3(1., 0., 1.);
    payload.kusok_index = -1;
}
