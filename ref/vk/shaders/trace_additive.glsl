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
		const Kusok kusok = getKusok(geom.kusok_index);

		const vec4 texture_color = texture(textures[nonuniformEXT(kusok.tex_base_color)], geom.uv);
		const vec3 color = texture_color.rgb * kusok.emissive * texture_color.a * kusok.color.a * SRGBtoLINEAR(geom.color.rgb * geom.color.a);

		const float hit_t = rayQueryGetIntersectionTEXT(rq, false);
		const float overshoot = hit_t - L;
		ret += color * smoothstep(additive_soft_overshoot, 0., overshoot);
	}
	return ret;
}

#endif //ifndef TRACE_ADDITIVE_GLSL_INCLUDED
