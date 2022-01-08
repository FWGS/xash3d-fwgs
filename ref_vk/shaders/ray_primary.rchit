#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

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
};

Geometry readHitGeometry() {
	Geometry geom;

	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;
	const Kusok kusok = kusochki[kusok_index];

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
	payload.uv = geom.uv;
}
