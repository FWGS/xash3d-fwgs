#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ray_common.glsl"
#include "ray_kusochki.glsl"

layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
layout (set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];

layout(location = 0) rayPayloadInEXT RayPayload payload;

hitAttributeEXT vec2 bary;

float hash(float f) { return fract(sin(f)*53478.4327); }
vec3 hashUintToVec3(uint i) { return vec3(hash(float(i)), hash(float(i)+15.43), hash(float(i)+34.)); }

const float normal_offset_fudge = .01;

void main() {
    //const float l = gl_HitTEXT;
    //ray_result.color = vec3(fract(l / 10.));
    //return;

    //ray_result.color = vec3(.5);

    vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
	// ray_result.color = fract((hit_pos + .5) / 10.);
    //ray_result.color = vec3(1.);
    //return;
    //ray_result.color = hashUintToVec3(gl_GeometryIndexEXT);

    const int instance_kusochki_offset = gl_InstanceCustomIndexEXT;
    //ray_result.color = hashUintToVec3(uint(instance_kusochki_offset));
    const int kusok_index = instance_kusochki_offset + gl_GeometryIndexEXT;

    const uint first_index_offset = kusochki[kusok_index].index_offset + gl_PrimitiveID * 3;
    const uint vi1 = uint(indices[first_index_offset+0]) + kusochki[kusok_index].vertex_offset;
    const uint vi2 = uint(indices[first_index_offset+1]) + kusochki[kusok_index].vertex_offset;
    const uint vi3 = uint(indices[first_index_offset+2]) + kusochki[kusok_index].vertex_offset;
    const vec3 n1 = vertices[vi1].normal;
    const vec3 n2 = vertices[vi2].normal;
    const vec3 n3 = vertices[vi3].normal;

    // TODO use already inverse gl_WorldToObject ?
    const vec3 normal = normalize(transpose(inverse(mat3(gl_ObjectToWorldEXT))) * (n1 * (1. - bary.x - bary.y) + n2 * bary.x + n3 * bary.y));
    hit_pos += normal * normal_offset_fudge;

    // //C = normal * .5 + .5; break;

    const vec2 texture_uv = vertices[vi1].gl_tc * (1. - bary.x - bary.y) + vertices[vi2].gl_tc * bary.x + vertices[vi3].gl_tc * bary.y;
    const uint tex_index = kusochki[kusok_index].texture;
    const vec3 base_color = pow(texture(textures[nonuniformEXT(tex_index)], texture_uv).rgb, vec3(2.));

    payload.hit_pos_t = vec4(hit_pos, gl_HitTEXT);
    payload.albedo = base_color;
    payload.normal = normal;
    payload.roughness = kusochki[kusok_index].roughness;
    payload.kusok_index = kusok_index;
}