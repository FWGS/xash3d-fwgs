#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ray_kusochki.glsl"
#include "ray_common.glsl"

layout (set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];
layout (set = 0, binding = 13) uniform samplerCube skybox;

layout(location = PAYLOAD_LOCATION_OPAQUE) rayPayloadInEXT RayPayloadOpaque payload;

layout (push_constant) uniform PC_ {
	PushConstants push_constants;
};

hitAttributeEXT vec2 bary;

// FIXME implement more robust self-intersection avoidance (see chap 6 of "Ray Tracing Gems")
const float normal_offset_fudge = .001;

// Taken from Journal of Computer Graphics Techniques, Vol. 10, No. 1, 2021.
// Improved Shader and Texture Level of Detail Using Ray Cones,
// by T. Akenine-Moller, C. Crassin, J. Boksansky, L. Belcour, A. Panteleev, and O. Wright
// https://jcgt.org/published/0010/01/01/
// P is the intersection point
// f is the triangle normal
// d is the ray cone direction
void computeAnisotropicEllipseAxes(in vec3 P, in vec3 f,
	in vec3 d, in float rayConeRadiusAtIntersection,
	in vec3 positions[3], in vec2 txcoords[3],
	in vec2 interpolatedTexCoordsAtIntersection,
	out vec2 texGradient1, out vec2 texGradient2)
{
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
	texGradient1 = (1.0-u1-v1) * txcoords[0] + u1 * txcoords[1] +
	v1 * txcoords[2] - interpolatedTexCoordsAtIntersection;
	eP = delta + a2;
	float u2 = dot(f, cross(eP, e2)) * oneOverAreaTriangle;
	float v2 = dot(f, cross(e1, eP)) * oneOverAreaTriangle;
	texGradient2 = (1.0-u2-v2) * txcoords[0] + u2 * txcoords[1] +
	v2 * txcoords[2] - interpolatedTexCoordsAtIntersection;
}

vec4 sampleTexture(uint tex_index, vec2 uv, vec4 uv_lods) {
	return textureGrad(textures[nonuniformEXT(tex_index)], uv, uv_lods.xy, uv_lods.zw);
}

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

void main() {
	payload.t_offset += gl_HitTEXT;

	const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
	const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;
	const Kusok kusok = kusochki[kusok_index];

	const uint first_index_offset = kusok.index_offset + gl_PrimitiveID * 3;

	const uint vi1 = uint(indices[first_index_offset+0]) + kusok.vertex_offset;
	const uint vi2 = uint(indices[first_index_offset+1]) + kusok.vertex_offset;
	const uint vi3 = uint(indices[first_index_offset+2]) + kusok.vertex_offset;

	const vec3 pos[3] = {
		gl_ObjectToWorldEXT * vec4(vertices[vi1].pos, 1.),
		gl_ObjectToWorldEXT * vec4(vertices[vi2].pos, 1.),
		gl_ObjectToWorldEXT * vec4(vertices[vi3].pos, 1.),
	};
	// This one is supposed to be numerically better, but I can't see why
	const vec3 hit_pos = pos[0] * (1. - bary.x - bary.y) + pos[1] * bary.x + pos[2] * bary.y;
	//const vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT

	const uint tex_base_color = kusok.tex_base_color;
	if ((tex_base_color & KUSOK_MATERIAL_FLAG_SKYBOX) != 0) {
		payload.hit_pos_t = vec4(hit_pos, gl_HitTEXT);
		payload.base_color = vec3(0.);
		payload.transmissiveness = 0.;
		payload.normal = vec3(0.);
		payload.geometry_normal = vec3(0.);
		payload.kusok_index = -1;
		payload.roughness = 0.;
		payload.metalness = 0.;
		payload.material_index = tex_base_color;

		// HACK: skyboxes are LDR now. They will look really dull after tonemapping
		// We need to remaster them into HDR. While that is not done, we just tune them with pow(x, 2.2) which looks okay-ish
		// See #230
		payload.emissive = pow(texture(skybox, gl_WorldRayDirectionEXT).rgb, vec3(2.2));
		return;
	}

	const vec3 n1 = vertices[vi1].normal;
	const vec3 n2 = vertices[vi2].normal;
	const vec3 n3 = vertices[vi3].normal;

	// TODO use already inverse gl_WorldToObject ?
	const mat3 matWorldRotation = mat3(gl_ObjectToWorldEXT);
	const mat3 normalTransformMat = transpose(inverse(matWorldRotation));
	vec3 normal = normalize(normalTransformMat * (n1 * (1. - bary.x - bary.y) + n2 * bary.x + n3 * bary.y));

	const vec2 uvs[3] = {
		vertices[vi1].gl_tc,
		vertices[vi2].gl_tc,
		vertices[vi3].gl_tc,
	};

	const vec2 texture_uv_stationary = vertices[vi1].gl_tc * (1. - bary.x - bary.y) + vertices[vi2].gl_tc * bary.x + vertices[vi3].gl_tc * bary.y;
	const vec2 texture_uv = texture_uv_stationary + push_constants.time * kusok.uv_speed;

	const vec3 real_geom_normal = normalize(cross(pos[2]-pos[0], pos[1]-pos[0]));
	const float geom_normal_sign = sign(dot(real_geom_normal, -gl_WorldRayDirectionEXT));
	const vec3 geom_normal = geom_normal_sign * real_geom_normal;

	const float ray_cone_width = payload.pixel_cone_spread_angle * payload.t_offset;
	vec4 uv_lods;
	computeAnisotropicEllipseAxes(hit_pos, /* TODO geom_?*/ normal, gl_WorldRayDirectionEXT, ray_cone_width, pos, uvs, texture_uv_stationary, uv_lods.xy, uv_lods.zw);

	const uint tex_index = tex_base_color;
	const vec4 tex_color = sampleTexture(tex_index, texture_uv, uv_lods);
	//const vec3 base_color = pow(tex_color.rgb, vec3(2.));
	const vec3 base_color = ((push_constants.flags & PUSH_FLAG_LIGHTMAP_ONLY) != 0) ? vec3(1.) : tex_color.rgb;// pow(tex_color.rgb, vec3(2.));
	/* tex_color = pow(tex_color, vec4(2.)); */
	/* const vec3 base_color = tex_color.rgb; */

	normal *= geom_normal_sign;
	const uint tex_normal = kusok.tex_normalmap;
	vec3 T = baryMix(vertices[vi1].tangent, vertices[vi2].tangent, vertices[vi3].tangent, bary);
	if (tex_normal > 0 && dot(T,T) > .5) {
		T = normalize(normalTransformMat * T);
		T = normalize(T - dot(T, normal) * normal);
		const vec3 B = normalize(cross(normal, T));
		const mat3 TBN = mat3(T, B, normal);
		const vec3 tnorm = sampleTexture(tex_normal, texture_uv, uv_lods).xyz * 2. - 1.; // TODO is this sampling correct for normal data?
		normal = normalize(TBN * tnorm);
	}

	// FIXME read alpha from texture

	payload.hit_pos_t = vec4(hit_pos + geom_normal * normal_offset_fudge, gl_HitTEXT);
	payload.base_color = base_color * kusok.color.rgb;
	payload.transmissiveness = (1. - tex_color.a * kusok.color.a);
	payload.normal = normal;
	payload.geometry_normal = geom_normal;

	payload.emissive = vec3(0.);
	if (any(greaterThan(kusok.emissive, vec3(0.)))) {
		//const vec3 emissive_color = base_color;
		//const vec3 emissive_color = pow(base_color, vec3(2.2));
		//const float max_color = max(max(emissive_color.r, emissive_color.g), emissive_color.b);
		//payload.emissive = normalize(kusok.emissive) * emissive_color;// * mix(vec3(1.), kusok.emissive, smoothstep(.3, .6, max_color));
		payload.emissive = kusok.emissive * base_color;
	}

	payload.kusok_index = kusok_index;

	payload.roughness = (kusok.tex_roughness > 0) ? sampleTexture(kusok.tex_roughness, texture_uv, uv_lods).r : kusok.roughness;
	payload.metalness = (kusok.tex_metalness > 0) ? sampleTexture(kusok.tex_metalness, texture_uv, uv_lods).r : kusok.metalness;
	payload.material_index = tex_index;
	//payload.debug = vec4(texture_uv, uv_lods);

	T = baryMix(vertices[vi1].tangent, vertices[vi2].tangent, vertices[vi3].tangent, bary);
	T = normalize(normalTransformMat * T);
	payload.debug = vec4(bary, 0., 0.);
}
