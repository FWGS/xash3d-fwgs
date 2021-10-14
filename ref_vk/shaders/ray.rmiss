#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "ray_common.glsl"
#include "ray_kusochki.glsl"

layout (set = 0, binding = 7/*, align=4*/) uniform UBOLights { Lights lights; };

layout(location = 0) rayPayloadInEXT RayPayload payload;

void main() {
    payload.hit_pos_t = vec4(-1.);
    payload.geometry_normal = payload.normal = vec3(0., 1., 0.);
	payload.reflection = 0.;
    payload.roughness = 0.;
    payload.base_color = vec3(0.);//mix(vec3(.1, .2, .7), lights.sun_color, pow(sun_dot, 100.));
	//vec3(1., 0., 1.);
    payload.kusok_index = -1;
}
