#ifndef TRACE_ADDITIVE_GLSL_INCLUDED
#define TRACE_ADDITIVE_GLSL_INCLUDED

vec3 traceAdditive(vec3 pos, vec3 dir, float L) {
	const float additive_soft_overshoot = 16.;
	vec3 ret = vec3(0., 0., 0.);
	rayQueryEXT rq;
	const uint flags = 0
		| gl_RayFlagsCullFrontFacingTrianglesEXT
		//| gl_RayFlagsSkipClosestHitShaderEXT
		| gl_RayFlagsNoOpaqueEXT // force all to be non-opaque
		;
	rayQueryInitializeEXT(rq, tlas, flags, GEOMETRY_BIT_ADDITIVE, pos, 0., dir, L + additive_soft_overshoot);
	while (rayQueryProceedEXT(rq)) {
		const MiniGeometry geom = readCandidateMiniGeometry(rq);
		const uint tex_base_color = getKusok(geom.kusok_index).tex_base_color;
		const vec4 texture_color = texture(textures[nonuniformEXT(tex_base_color)], geom.uv);
		const vec3 kusok_emissive = getKusok(geom.kusok_index).emissive;
		const vec3 color = texture_color.rgb * kusok_emissive * texture_color.a; // * kusok_color.a;

		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);
		const float overshoot = hit_t - L;
		ret += color * smoothstep(additive_soft_overshoot, 0., overshoot);
	}
	return ret;
}

#endif //ifndef TRACE_ADDITIVE_GLSL_INCLUDED
