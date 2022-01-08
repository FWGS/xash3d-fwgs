#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_primary_common.glsl"

#include "ray_kusochki.glsl"

layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
layout(set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; };

layout(location = PAYLOAD_LOCATION_PRIMARY) rayPayloadInEXT RayPayloadPrimary payload;
hitAttributeEXT vec2 bary;

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec2 baryMix(vec2 v1, vec2 v2, vec2 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
}

// Taken from Journal of Computer Graphics Techniques, Vol. 10, No. 1, 2021.
// Improved Shader and Texture Level of Detail Using Ray Cones,
// by T. Akenine-Moller, C. Crassin, J. Boksansky, L. Belcour, A. Panteleev, and O. Wright
// https://jcgt.org/published/0010/01/01/
// P is the intersection point
// f is the triangle normal
// d is the ray cone direction
vec4 computeAnisotropicEllipseAxes(in vec3 P, in vec3 f,
	in vec3 d, in float rayConeRadiusAtIntersection,
	in vec3 positions[3], in vec2 txcoords[3],
	in vec2 interpolatedTexCoordsAtIntersection)
{
	vec4 texGradient;
	// Compute ellipse axes.
	vec3 a1 = d - dot(f, d) * f;
	vec3 p1 = a1 - dot(d, a1) * d;
	a1 *= rayConeRadiusAtIntersection / max(0.0001, length(p1));
	vec3 a2 = cross(f, a1);
	vec3 p2 = a2 - dot(d, a2) * d;
	a2 *= rayConeRadiusAtIntersection / max(0.0001, length(p2));
	// Compute texture coordinate gradients.
	vec3 eP, delta = P - positions[0];
	vec3 e1 = positions[1] - positions[0];
	vec3 e2 = positions[2] - positions[0];
	float oneOverAreaTriangle = 1.0 / dot(f, cross(e1, e2));
	eP = delta + a1;
	float u1 = dot(f, cross(eP, e2)) * oneOverAreaTriangle;
	float v1 = dot(f, cross(e1, eP)) * oneOverAreaTriangle;
	texGradient.xy = (1.0-u1-v1) * txcoords[0] + u1 * txcoords[1] +
	v1 * txcoords[2] - interpolatedTexCoordsAtIntersection;
	eP = delta + a2;
	float u2 = dot(f, cross(eP, e2)) * oneOverAreaTriangle;
	float v2 = dot(f, cross(e1, eP)) * oneOverAreaTriangle;
	texGradient.zw = (1.0-u2-v2) * txcoords[0] + u2 * txcoords[1] +
	v2 * txcoords[2] - interpolatedTexCoordsAtIntersection;
	return texGradient;
}

struct Geometry {
	vec3 pos;

	vec2 uv;
	vec4 uv_lods;

	vec3 normal_geometry;
	vec3 normal_shading;

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

	const vec3 pos[3] = {
		gl_ObjectToWorldEXT * vec4(vertices[vi1].pos, 1.f),
		gl_ObjectToWorldEXT * vec4(vertices[vi2].pos, 1.f),
		gl_ObjectToWorldEXT * vec4(vertices[vi3].pos, 1.f),
	};

	const vec2 uvs[3] = {
		vertices[vi1].gl_tc,
		vertices[vi2].gl_tc,
		vertices[vi3].gl_tc,
	};

	geom.pos = baryMix(pos[0], pos[1], pos[2], bary);
	geom.uv = baryMix(uvs[0], uvs[1], uvs[2], bary);
	//TODO or not TODO? const vec2 texture_uv = texture_uv_stationary + push_constants.time * kusok.uv_speed;

	// NOTE: need to flip if back-facing
	geom.normal_geometry = normalize(cross(pos[2]-pos[0], pos[1]-pos[0]));

	// NOTE: only support rotations, for arbitrary transform would need to do transpose(inverse(mat3(gl_ObjectToWorldEXT)))
	const mat3 normalTransform = mat3(gl_ObjectToWorldEXT);
	geom.normal_shading = normalTransform * baryMix(
		vertices[vi1].normal,
		vertices[vi2].normal,
		vertices[vi3].normal,
		bary);

	geom.uv_lods = computeAnisotropicEllipseAxes(geom.pos, geom.normal_geometry, gl_WorldRayDirectionEXT, ubo.ray_cone_width * gl_HitTEXT, pos, uvs, geom.uv);

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
		payload.base_color_a = sampleTexture(tex_base_color, geom.uv, geom.uv_lods) * kusok.color;
	}
}
