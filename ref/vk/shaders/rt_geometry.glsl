#ifndef RT_GEOMETRY_GLSL_INCLUDED
#define RT_GEOMETRY_GLSL_INCLUDED
#include "utils.glsl"

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
	vec3 prev_pos;

	vec2 uv;
	vec4 uv_lods;

	vec3 normal_geometry;
	vec3 normal_shading;
	vec3 tangent;

	int kusok_index;
};

#ifdef RAY_QUERY
Geometry readHitGeometry(rayQueryEXT rq, float ray_cone_width, vec2 bary) {
	const int instance_kusochki_offset = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
	const int geometry_index = rayQueryGetIntersectionGeometryIndexEXT(rq, true);
	const int primitive_index = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
	const mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(rq, true);
	const vec3 ray_direction = rayQueryGetWorldRayDirectionEXT(rq);
	const float hit_t = rayQueryGetIntersectionTEXT(rq, true);
#else
Geometry readHitGeometry(vec2 bary, float ray_cone_width) {
	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	const int geometry_index = gl_GeometryIndexEXT;
	const int primitive_index = gl_PrimitiveID;
	const mat4x3 objectToWorld = gl_ObjectToWorldEXT;
	const vec3 ray_direction = gl_WorldRayDirectionEXT;
	const float hit_t = gl_HitTEXT;
#endif

	Geometry geom;

	geom.kusok_index = instance_kusochki_offset + geometry_index;
	const Kusok kusok = getKusok(geom.kusok_index);

	const uint first_index_offset = kusok.index_offset + primitive_index * 3;
	const uint vi1 = uint(getIndex(first_index_offset+0)) + kusok.vertex_offset;
	const uint vi2 = uint(getIndex(first_index_offset+1)) + kusok.vertex_offset;
	const uint vi3 = uint(getIndex(first_index_offset+2)) + kusok.vertex_offset;

	const vec3 pos[3] = {
		objectToWorld * vec4(getVertex(vi1).pos, 1.f),
		objectToWorld * vec4(getVertex(vi2).pos, 1.f),
		objectToWorld * vec4(getVertex(vi3).pos, 1.f),
	};

	const vec3 prev_pos[3] = {
		(kusok.prev_transform * vec4(getVertex(vi1).prev_pos, 1.f)).xyz,
		(kusok.prev_transform * vec4(getVertex(vi2).prev_pos, 1.f)).xyz,
		(kusok.prev_transform * vec4(getVertex(vi3).prev_pos, 1.f)).xyz,
	};

	const vec2 uvs[3] = {
		getVertex(vi1).gl_tc,
		getVertex(vi2).gl_tc,
		getVertex(vi3).gl_tc,
	};

	geom.pos = baryMix(pos[0], pos[1], pos[2], bary);
	geom.prev_pos = baryMix(prev_pos[0], prev_pos[1], prev_pos[2], bary);
	geom.uv = baryMix(uvs[0], uvs[1], uvs[2], bary);
	//TODO or not TODO? const vec2 texture_uv = texture_uv_stationary + push_constants.time * kusok.uv_speed;

	// NOTE: need to flip if back-facing
	geom.normal_geometry = normalize(cross(pos[2]-pos[0], pos[1]-pos[0]));

	// NOTE: only support rotations, for arbitrary transform would need to do transpose(inverse(mat3(gl_ObjectToWorldEXT)))
	const mat3 normalTransform = mat3(objectToWorld); // mat3(gl_ObjectToWorldEXT);
	geom.normal_shading = normalize(normalTransform * baryMix(
		getVertex(vi1).normal,
		getVertex(vi2).normal,
		getVertex(vi3).normal,
		bary));
	geom.tangent = normalize(normalTransform * baryMix(
		getVertex(vi1).tangent,
		getVertex(vi2).tangent,
		getVertex(vi3).tangent,
		bary));

	geom.uv_lods = computeAnisotropicEllipseAxes(geom.pos, geom.normal_geometry, ray_direction, ray_cone_width * hit_t, pos, uvs, geom.uv);

	return geom;
}

#ifdef RAY_QUERY
struct MiniGeometry {
	vec2 uv;
	uint kusok_index;
};

MiniGeometry readCandidateMiniGeometry(rayQueryEXT rq) {
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

		MiniGeometry ret;
		ret.uv = uv;
		ret.kusok_index = kusok_index;
		return ret;
}
#endif // #ifdef RAY_QUERY

#endif // RT_GEOMETRY_GLSL_INCLUDED
