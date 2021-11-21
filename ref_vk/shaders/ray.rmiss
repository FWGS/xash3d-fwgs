#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ray_common.glsl"
#include "ray_kusochki.glsl"

//layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
//layout (set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout (set = 0, binding = 13) uniform samplerCube skybox;
layout (set = 0, binding = 7/*, align=4*/) uniform UBOLights { Lights lights; };

layout(location = PAYLOAD_LOCATION_OPAQUE) rayPayloadInEXT RayPayloadOpaque payload;

void main() {
	payload.hit_pos_t = vec4(-1.);
	payload.geometry_normal = payload.normal = vec3(0., 1., 0.);
	payload.transmissiveness = 0.;
	payload.roughness = 0.;
	payload.base_color = vec3(1., 0., 1.);
	payload.kusok_index = -1;
	payload.material_index = 0;
  payload.emissive = texture(skybox, gl_WorldRayDirectionEXT).rgb;
}
