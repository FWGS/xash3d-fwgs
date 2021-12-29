bool shadowed(vec3 pos, vec3 dir, float dist) {
	payload_shadow.hit_type = SHADOW_HIT;
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		| gl_RayFlagsTerminateOnFirstHitEXT
		| gl_RayFlagsSkipClosestHitShaderEXT
		;
	traceRayEXT(tlas,
		flags,
		GEOMETRY_BIT_OPAQUE,
		SHADER_OFFSET_HIT_SHADOW_BASE, SBT_RECORD_SIZE, SHADER_OFFSET_MISS_SHADOW,
		pos, 0., dir, dist - shadow_offset_fudge, PAYLOAD_LOCATION_SHADOW);
	return payload_shadow.hit_type == SHADOW_HIT;
}

// TODO join with just shadowed()
bool shadowedSky(vec3 pos, vec3 dir, float dist) {
	payload_shadow.hit_type = SHADOW_HIT;
	const uint flags =  0
		//| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsOpaqueEXT
		//| gl_RayFlagsTerminateOnFirstHitEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		;
	traceRayEXT(tlas,
		flags,
		GEOMETRY_BIT_OPAQUE,
		SHADER_OFFSET_HIT_SHADOW_BASE, SBT_RECORD_SIZE, SHADER_OFFSET_MISS_SHADOW,
		pos, 0., dir, dist - shadow_offset_fudge, PAYLOAD_LOCATION_SHADOW);
	return payload_shadow.hit_type != SHADOW_SKY;
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
