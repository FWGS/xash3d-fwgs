#version 460 core
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "ray_kusochki.glsl"
#include "ray_common.glsl"

layout (constant_id = 6) const uint MAX_TEXTURES = 4096;
layout (set = 0, binding = 6) uniform sampler2D textures[MAX_TEXTURES];

layout(location = 0) rayPayloadInEXT RayPayload payload;

hitAttributeEXT vec2 bary;

float hash(float f) { return fract(sin(f)*53478.4327); }
vec3 hashUintToVec3(uint i) { return vec3(hash(float(i)), hash(float(i)+15.43), hash(float(i)+34.)); }

const float normal_offset_fudge = .01;

// Taken from Ray Tracing Gems II, Chapter 7. Texture Coordinate Gradients Estimation for Ray Cones, by Wessam Bahnassi
// https://www.realtimerendering.com/raytracinggems/rtg2/index.html
// https://github.com/Apress/Ray-Tracing-Gems-II/blob/main/Chapter_07/Raytracing.hlsl
vec4 UVDerivsFromRayCone(vec3 vRayDir, vec3 vWorldNormal, float vRayConeWidth, vec2 aUV[3], vec3 aPos[3], mat3 matWorld)
{
	vec2 vUV10 = aUV[1]-aUV[0];
	vec2 vUV20 = aUV[2]-aUV[0];
	float fQuadUVArea = abs(vUV10.x*vUV20.y - vUV20.x*vUV10.y);

	// Since the ray cone's width is in world-space, we need to compute the quad
	// area in world-space as well to enable proper ratio calculation
	vec3 vEdge10 = (aPos[1]-aPos[0]) * matWorld;
	vec3 vEdge20 = (aPos[2]-aPos[0]) * matWorld;
	vec3 vFaceNrm = cross(vEdge10, vEdge20);
	float fQuadArea = length(vFaceNrm);

	float fDistTerm = abs(vRayConeWidth);
	float fNormalTerm = abs(dot(vRayDir,vWorldNormal));
	float fProjectedConeWidth = vRayConeWidth/fNormalTerm;
	float fVisibleAreaRatio = (fProjectedConeWidth*fProjectedConeWidth) / fQuadArea;

	float fVisibleUVArea = fQuadUVArea*fVisibleAreaRatio;
	float fULength = sqrt(fVisibleUVArea);
	return vec4(fULength,0,0,fULength);
}

void main() {
    //const float l = gl_HitTEXT;
    //ray_result.color = vec3(fract(l / 10.));
    //return;

    //ray_result.color = vec3(.5);

	// FIXME compute hit pos based on barycentric coords (better precision)
    vec3 hit_pos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    payload.t_offset += gl_HitTEXT;

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
    const mat3 matWorld = mat3(gl_ObjectToWorldEXT);
    const vec3 normal = normalize(transpose(inverse(matWorld)) * (n1 * (1. - bary.x - bary.y) + n2 * bary.x + n3 * bary.y));

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
    const vec2 texture_uv = vertices[vi1].gl_tc * (1. - bary.x - bary.y) + vertices[vi2].gl_tc * bary.x + vertices[vi3].gl_tc * bary.y;
    const uint tex_index = kusochki[kusok_index].texture;

    const float ray_cone_width = payload.pixel_cone_spread_angle * payload.t_offset;
    const vec4 uv_lods = UVDerivsFromRayCone(gl_WorldRayDirectionEXT, normal, ray_cone_width, uvs, pos, matWorld);
    const vec4 tex_color = textureGrad(textures[nonuniformEXT(tex_index)], texture_uv, uv_lods.xy, uv_lods.zw);
    const vec3 base_color = pow(tex_color.rgb, vec3(2.));
    /* tex_color = pow(tex_color, vec4(2.)); */
    /* const vec3 base_color = tex_color.rgb; */

	// FIXME read alpha from texture

    payload.hit_pos_t = vec4(hit_pos, gl_HitTEXT);
    payload.base_color = base_color * kusochki[kusok_index].color.rgb;
	payload.reflection = tex_color.a * kusochki[kusok_index].color.a;
    payload.normal = normal * sign(dot(normal, -gl_WorldRayDirectionEXT));

    payload.emissive = kusochki[kusok_index].emissive * base_color; // TODO emissive should have a special texture
    payload.roughness = kusochki[kusok_index].roughness;
    payload.kusok_index = kusok_index;
    payload.material_flags = kusochki[kusok_index].material_flags;
}
