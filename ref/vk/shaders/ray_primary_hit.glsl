#ifndef RAY_PRIMARY_HIT_GLSL_INCLUDED
#define RAY_PRIMARY_HIT_GLSL_INCLUDED
#extension GL_EXT_nonuniform_qualifier : enable

#include "utils.glsl"
#include "ray_primary_common.glsl"
#include "ray_kusochki.glsl"
#include "rt_geometry.glsl"
#include "color_spaces.glsl"

layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;
layout(set = 0, binding = 7) uniform samplerCube skybox;

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
#ifndef RAY_BOUNCE
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
#else
	return textureLod(textures[nonuniformEXT(tex_index)], uv, 2.);
#endif
}

void primaryRayHit(rayQueryEXT rq, inout RayPayloadPrimary payload) {
	Geometry geom = readHitGeometry(rq, ubo.ubo.ray_cone_width, rayQueryGetIntersectionBarycentricsEXT(rq, true));
	const float hitT = rayQueryGetIntersectionTEXT(rq, true);  //gl_HitTEXT;
	const vec3 rayDirection = rayQueryGetWorldRayDirectionEXT(rq); //gl_WorldRayDirectionEXT
	payload.hit_t = vec4(geom.pos, hitT);
	payload.prev_pos_t = vec4(geom.prev_pos, 0.);

	const Kusok kusok = getKusok(geom.kusok_index);

	if ((kusok.flags & KUSOK_MATERIAL_FLAG_SKYBOX) != 0) {
		payload.emissive.rgb = SRGBtoLINEAR(texture(skybox, rayDirection).rgb);
		return;
	} else {
		payload.base_color_a = sampleTexture(kusok.tex_base_color, geom.uv, geom.uv_lods);
		payload.material_rmxx.r = (kusok.tex_roughness > 0) ? sampleTexture(kusok.tex_roughness, geom.uv, geom.uv_lods).r : kusok.roughness;
		payload.material_rmxx.g = (kusok.tex_metalness > 0) ? sampleTexture(kusok.tex_metalness, geom.uv, geom.uv_lods).r : kusok.metalness;

#ifndef RAY_BOUNCE
		const uint tex_normal = kusok.tex_normalmap;
		vec3 T = geom.tangent;
		if (tex_normal > 0 && dot(T,T) > .5) {
			T = normalize(T - dot(T, geom.normal_shading) * geom.normal_shading);
			const vec3 B = normalize(cross(geom.normal_shading, T));
			const mat3 TBN = mat3(T, B, geom.normal_shading);
			const vec3 tnorm = sampleTexture(tex_normal, geom.uv, geom.uv_lods).xyz * 2. - 1.; // TODO is this sampling correct for normal data?
			geom.normal_shading = normalize(TBN * tnorm);
		}
#endif
	}

	payload.normals_gs.xy = normalEncode(geom.normal_geometry);
	payload.normals_gs.zw = normalEncode(geom.normal_shading);

#if 1
	// Real correct emissive color
	//payload.emissive.rgb = kusok.emissive;
	//payload.emissive.rgb = kusok.emissive * SRGBtoLINEAR(payload.base_color_a.rgb);
	//payload.emissive.rgb = clamp((kusok.emissive * (1.0/3.0) / 20), 0, 1.0) * SRGBtoLINEAR(payload.base_color_a.rgb);
	//payload.emissive.rgb = (sqrt(sqrt(kusok.emissive)) * (1.0/3.0)) * SRGBtoLINEAR(payload.base_color_a.rgb);
	payload.emissive.rgb = (sqrt(kusok.emissive) / 8) * SRGBtoLINEAR(payload.base_color_a.rgb);
#else
	// Fake texture color
	if (any(greaterThan(kusok.emissive, vec3(0.))))
		payload.emissive.rgb = payload.base_color_a.rgb;
#endif

	payload.base_color_a *= kusok.color;
}

#endif // ifndef RAY_PRIMARY_HIT_GLSL_INCLUDED
