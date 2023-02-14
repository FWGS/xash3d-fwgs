#ifndef LIGHT_COMMON_GLSL_INCLUDED
#define LIGHT_COMMON_GLSL_INCLUDED
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_kusochki.glsl"

#ifndef TEXTURES_INCLUDED_ALREADY_FIXME
layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
#endif

#ifdef RAY_TRACE2
#include "ray_shadow_interface.glsl"
layout(location = PAYLOAD_LOCATION_SHADOW) rayPayloadEXT RayPayloadShadow payload_shadow;
#endif

#ifdef RAY_TRACE
uint traceShadowRay(vec3 pos, vec3 dir, float dist, uint flags) {
	payload_shadow.hit_type = SHADOW_HIT;
	traceRayEXT(tlas,
		flags,
		GEOMETRY_BIT_OPAQUE,
		SHADER_OFFSET_HIT_SHADOW_BASE, SBT_RECORD_SIZE, SHADER_OFFSET_MISS_SHADOW,
		pos, 0., dir, dist - shadow_offset_fudge, PAYLOAD_LOCATION_SHADOW);
	return payload_shadow.hit_type;
}
#endif

#if defined(RAY_QUERY)
bool shadowTestAlphaMask(vec3 pos, vec3 dir, float dist) {
	rayQueryEXT rq;
	const uint flags =  0
		| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsNoOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_ALPHA_TEST, pos, 0., dir, dist);

	while (rayQueryProceedEXT(rq)) {
		// Alpha test, takes 10ms
		// TODO check other possible ways of doing alpha test. They might be more efficient:
		// 1. Do a separate ray query for alpha masked geometry. Reason: here we might accidentally do the expensive
		//    texture sampling for geometry that's ultimately invisible (i.e. behind walls). Also, shader threads congruence.
		//    Separate pass could be more efficient as it'd be doing the same thing for every invocation.
		// 2. Same as the above, but also with a completely independent TLAS. Why: no need to mask-check geometry for opaque-vs-alpha
		const uint instance_kusochki_offset = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
		const uint geometry_index = rayQueryGetIntersectionGeometryIndexEXT(rq, false);
		const uint kusok_index = instance_kusochki_offset + geometry_index;
		const Kusok kusok = getKusok(kusok_index);

		const uint primitive_index = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
		const uint first_index_offset = kusok.index_offset + primitive_index * 3;
		const uint vi1 = uint(getIndex(first_index_offset+0)) + kusok.vertex_offset;
		const uint vi2 = uint(getIndex(first_index_offset+1)) + kusok.vertex_offset;
		const uint vi3 = uint(getIndex(first_index_offset+2)) + kusok.vertex_offset;
		const vec2 uvs[3] = {
			getVertex(vi1).gl_tc,
			getVertex(vi2).gl_tc,
			getVertex(vi3).gl_tc,
		};
		const vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, false);
		const vec2 uv = baryMix(uvs[0], uvs[1], uvs[2], bary);
		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.tex_base_color)], uv);

		const float alpha_mask_threshold = .1f;
		if (texture_color.a >= alpha_mask_threshold) {
			rayQueryConfirmIntersectionEXT(rq);
		}
	}

	return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
}
#endif

bool shadowed(vec3 pos, vec3 dir, float dist, bool check_sky) {
#ifdef RAY_TRACE
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const uint hit_type = traceShadowRay(pos, dir, dist, flags);
	return check_sky ? payload_shadow.hit_type != SHADOW_SKY : payload_shadow.hit_type == SHADOW_HIT;
#elif defined(RAY_QUERY)
	{
		const uint flags =  0
			| gl_RayFlagsCullFrontFacingTrianglesEXT
			| gl_RayFlagsOpaqueEXT
			| gl_RayFlagsTerminateOnFirstHitEXT
			;
		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_OPAQUE, pos, 0., dir, dist - shadow_offset_fudge);
		while (rayQueryProceedEXT(rq)) {}

		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
			if (!check_sky)
				return true;

			const int instance_kusochki_offset = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
			const int kusok_index = instance_kusochki_offset + rayQueryGetIntersectionGeometryIndexEXT(rq, true);
			const uint tex_base_color = getKusok(kusok_index).tex_base_color;
			if ((tex_base_color & KUSOK_MATERIAL_FLAG_SKYBOX) == 0)
				return true;
		}
	}

	return shadowTestAlphaMask(pos, dir, dist);

#else
#error RAY_TRACE or RAY_QUERY
#endif
}

// This is an entry point for evaluation of all other BRDFs based on selected configuration (for direct light)
void evalSplitBRDF(vec3 N, vec3 L, vec3 V, MaterialProperties material, out vec3 diffuse, out vec3 specular) {
	// Prepare data needed for BRDF evaluation - unpack material properties and evaluate commonly used terms (e.g. Fresnel, NdotL, ...)
	const BrdfData data = prepareBRDFData(N, L, V, material);

	// Ignore V and L rays "below" the hemisphere
	//if (data.Vbackfacing || data.Lbackfacing) return vec3(0.0f, 0.0f, 0.0f);

	// Eval specular and diffuse BRDFs
	specular = evalSpecular(data);
	diffuse = evalDiffuse(data);

	// Combine specular and diffuse layers
#if COMBINE_BRDFS_WITH_FRESNEL
	// Specular is already multiplied by F, just attenuate diffuse
	diffuse *= vec3(1.) - data.F;
#endif
}

#endif //ifndef LIGHT_COMMON_GLSL_INCLUDED
