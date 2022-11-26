#ifndef LIGHT_COMMON_GLSL_INCLUDED
#define LIGHT_COMMON_GLSL_INCLUDED

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

bool shadowed(vec3 pos, vec3 dir, float dist) {
#ifdef RAY_TRACE
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const uint hit_type = traceShadowRay(pos, dir, dist, flags);
	return payload_shadow.hit_type == SHADOW_HIT;
#elif defined(RAY_QUERY)
	rayQueryEXT rq;
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_OPAQUE, pos, 0., dir, dist - shadow_offset_fudge);
	while (rayQueryProceedEXT(rq)) { }
	return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT;
#else
	return false;
#endif
}

// TODO join with just shadowed()
bool shadowedSky(vec3 pos, vec3 dir, float dist) {
#ifdef RAY_TRACE
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		//| gl_RayFlagsTerminateOnFirstHitEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		;
	const uint hit_type = traceShadowRay(pos, dir, dist, flags);
	return payload_shadow.hit_type != SHADOW_SKY;
#else
	// FIXME ray query
	return false;
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
