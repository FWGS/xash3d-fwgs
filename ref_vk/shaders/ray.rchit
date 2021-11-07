#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ray_kusochki.glsl"
#include "ray_common.glsl"

layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
layout (set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];

layout(location = PAYLOAD_LOCATION_OPAQUE) rayPayloadInEXT RayPayloadOpaque payload;

layout (push_constant) uniform PC_ {
	PushConstants push_constants;
};

hitAttributeEXT vec2 bary;

float hash(float f) { return fract(sin(f)*53478.4327); }
vec3 hashUintToVec3(uint i) { return vec3(hash(float(i)), hash(float(i)+15.43), hash(float(i)+34.)); }

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

void main() {
    payload.t_offset += gl_HitTEXT;

    const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
    const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;

    const uint first_index_offset = kusochki[kusok_index].index_offset + gl_PrimitiveID * 3;

    const uint vi1 = uint(indices[first_index_offset+0]) + kusochki[kusok_index].vertex_offset;
    const uint vi2 = uint(indices[first_index_offset+1]) + kusochki[kusok_index].vertex_offset;
    const uint vi3 = uint(indices[first_index_offset+2]) + kusochki[kusok_index].vertex_offset;

    const vec3 n1 = vertices[vi1].normal;
    const vec3 n2 = vertices[vi2].normal;
    const vec3 n3 = vertices[vi3].normal;

    // TODO use already inverse gl_WorldToObject ?
    const mat3 matWorldRotation = mat3(gl_ObjectToWorldEXT);
	const mat3 normalTransformMat = transpose(inverse(matWorldRotation));
    const vec3 normal = normalize(normalTransformMat * (n1 * (1. - bary.x - bary.y) + n2 * bary.x + n3 * bary.y));

    const vec2 uvs[3] = {
        vertices[vi1].gl_tc,
        vertices[vi2].gl_tc,
        vertices[vi3].gl_tc,
    };

    const vec3 pos[3] = {
        vertices[vi1].pos,
        vertices[vi2].pos,
        vertices[vi3].pos,
    };
    const vec2 texture_uv = vertices[vi1].gl_tc * (1. - bary.x - bary.y) + vertices[vi2].gl_tc * bary.x + vertices[vi3].gl_tc * bary.y + push_constants.time * kusochki[kusok_index].uv_speed;
    const uint tex_index = kusochki[kusok_index].texture;

    const vec3 real_geom_normal = normalize(normalTransformMat * cross(pos[2]-pos[0], pos[1]-pos[0]));
    const float geom_normal_sign = sign(dot(real_geom_normal, -gl_WorldRayDirectionEXT));
    const vec3 geom_normal = geom_normal_sign * real_geom_normal;

    // This one is supposed to be numerically better, but I can't see why
    const vec3 hit_pos = (gl_ObjectToWorldEXT * vec4(pos[0] * (1. - bary.x - bary.y) + pos[1] * bary.x + pos[2] * bary.y, 1.)).xyz + geom_normal * normal_offset_fudge;
    //const vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + geom_normal * normal_offset_fudge;

    const float ray_cone_width = payload.pixel_cone_spread_angle * payload.t_offset;
    vec4 uv_lods;
    computeAnisotropicEllipseAxes(hit_pos, normal, gl_WorldRayDirectionEXT, ray_cone_width, pos, uvs, texture_uv, uv_lods.xy, uv_lods.zw);
    const vec4 tex_color = textureGrad(textures[nonuniformEXT(tex_index)], texture_uv, uv_lods.xy, uv_lods.zw);
    //const vec3 base_color = pow(tex_color.rgb, vec3(2.));
    const vec3 base_color = ((push_constants.flags & PUSH_FLAG_LIGHTMAP_ONLY) != 0) ? vec3(1.) : tex_color.rgb;// pow(tex_color.rgb, vec3(2.));
    /* tex_color = pow(tex_color, vec4(2.)); */
    /* const vec3 base_color = tex_color.rgb; */

	// FIXME read alpha from texture

    payload.hit_pos_t = vec4(hit_pos, gl_HitTEXT);
    payload.base_color = base_color * kusochki[kusok_index].color.rgb;
	payload.transmissiveness = (1. - tex_color.a * kusochki[kusok_index].color.a);
    payload.normal = normal * geom_normal_sign;
	payload.geometry_normal = geom_normal;
    payload.emissive = kusochki[kusok_index].emissive * base_color; // TODO emissive should have a special texture
    payload.roughness = kusochki[kusok_index].roughness;
    payload.kusok_index = kusok_index;
	payload.material_index = nonuniformEXT(tex_index);
}
