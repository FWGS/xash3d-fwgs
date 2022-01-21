#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "utils.glsl"

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; };

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;
hitAttributeEXT vec2 bary;

#include "rt_geometry.glsl"

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
}

void main() {
	const Geometry geom = readHitGeometry();

	payload.hit_t = vec4(geom.pos, gl_HitTEXT);

	const Kusok kusok = kusochki[geom.kusok_index];
	const uint tex_base_color = kusok.tex_base_color;

	if ((tex_base_color & KUSOK_MATERIAL_FLAG_SKYBOX) != 0) {
		// FIXME read skybox
		payload.base_color_a = vec4(1.,0.,1.,1.);
	} else {
		payload.base_color_a = sampleTexture(tex_base_color, geom.uv, geom.uv_lods) * kusok.color;
	}

	payload.normals_gs.xy = normalEncode(geom.normal_geometry);
	payload.normals_gs.zw = normalEncode(geom.normal_shading);
}
