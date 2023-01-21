#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "utils.glsl"

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

#include "color_spaces.glsl"

layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 7) uniform samplerCube skybox;

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;
hitAttributeEXT vec2 bary;

#include "rt_geometry.glsl"

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
}

void main() {
	Geometry geom = readHitGeometry();

	payload.hit_t = vec4(geom.pos, gl_HitTEXT);
	payload.prev_pos_t = vec4(geom.prev_pos, 0.);

	const Kusok kusok = getKusok(geom.kusok_index);
	const uint tex_base_color = kusok.tex_base_color;

	if ((tex_base_color & KUSOK_MATERIAL_FLAG_SKYBOX) != 0) {
		payload.emissive.rgb = SRGBtoLINEAR(texture(skybox, gl_WorldRayDirectionEXT).rgb);
		return;
	} else {
		payload.base_color_a = sampleTexture(tex_base_color, geom.uv, geom.uv_lods) * kusok.color;
		payload.material_rmxx.r = (kusok.tex_roughness > 0) ? sampleTexture(kusok.tex_roughness, geom.uv, geom.uv_lods).r : kusok.roughness;
		payload.material_rmxx.g = (kusok.tex_metalness > 0) ? sampleTexture(kusok.tex_metalness, geom.uv, geom.uv_lods).r : kusok.metalness;

		const uint tex_normal = kusok.tex_normalmap;
		vec3 T = geom.tangent;
		if (tex_normal > 0 && dot(T,T) > .5) {
			T = normalize(T - dot(T, geom.normal_shading) * geom.normal_shading);
			const vec3 B = normalize(cross(geom.normal_shading, T));
			const mat3 TBN = mat3(T, B, geom.normal_shading);
			const vec3 tnorm = sampleTexture(tex_normal, geom.uv, geom.uv_lods).xyz * 2. - 1.; // TODO is this sampling correct for normal data?
			geom.normal_shading = normalize(TBN * tnorm);
		}
	}

	payload.normals_gs.xy = normalEncode(geom.normal_geometry);
	payload.normals_gs.zw = normalEncode(geom.normal_shading);

#if 1
	// Real correct emissive color
	//payload.emissive.rgb = kusok.emissive;
	payload.emissive.rgb = clamp(kusok.emissive / (1.0/3.0) / 25, 0, 1.5) * SRGBtoLINEAR(payload.base_color_a.rgb);
#else
	// Fake texture color
	if (any(greaterThan(kusok.emissive, vec3(0.))))
		payload.emissive.rgb = payload.base_color_a.rgb;
#endif
}
