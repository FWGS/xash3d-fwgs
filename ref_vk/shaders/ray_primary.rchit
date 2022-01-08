#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;
hitAttributeEXT vec2 bary;

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

struct Geometry {
	vec3 pos;
	//vec2 uv;
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

	return geom;
}

void main() {
	payload.hit_t.w = gl_HitTEXT;
	//payload.hit_t.xyz = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	payload.hit_t.xyz = readHitGeometry().pos;
}
