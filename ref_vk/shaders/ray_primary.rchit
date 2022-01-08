#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;
hitAttributeEXT vec2 bary;

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec2 baryMix(vec2 v1, vec2 v2, vec2 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

struct Geometry {
	vec3 pos;
	vec2 uv;

	int kusok_index;
};

Geometry readHitGeometry() {
	Geometry geom;

	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	geom.kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;
	const Kusok kusok = kusochki[geom.kusok_index];

	const uint first_index_offset = kusok.index_offset + gl_PrimitiveID * 3;
	const uint vi1 = uint(indices[first_index_offset+0]) + kusok.vertex_offset;
	const uint vi2 = uint(indices[first_index_offset+1]) + kusok.vertex_offset;
	const uint vi3 = uint(indices[first_index_offset+2]) + kusok.vertex_offset;

	geom.pos = (gl_ObjectToWorldEXT * vec4(baryMix(
		vertices[vi1].pos,
		vertices[vi2].pos,
		vertices[vi3].pos, bary), 1.f)).xyz;

	geom.uv = baryMix(vertices[vi1].gl_tc, vertices[vi2].gl_tc, vertices[vi3].gl_tc, bary);
	//TODO or not TODO? const vec2 texture_uv = texture_uv_stationary + push_constants.time * kusok.uv_speed;

	return geom;
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
		// FIXME mips
		payload.base_color_a = texture(textures[nonuniformEXT(tex_base_color)], geom.uv) * kusok.color;
	}
}
