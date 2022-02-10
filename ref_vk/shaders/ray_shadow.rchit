#version 460 core
#extension GL_EXT_ray_tracing: require

#include "ray_kusochki.glsl"
#include "ray_common.glsl"

layout(location = PAYLOAD_LOCATION_SHADOW) rayPayloadInEXT RayPayloadShadow payload_shadow;

void main() {
	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;
	const uint tex_base_color = kusochki[kusok_index].tex_base_color;

	payload_shadow.hit_type = ((tex_base_color & KUSOK_MATERIAL_FLAG_SKYBOX) == 0) ? SHADOW_HIT : SHADOW_SKY ;
}
