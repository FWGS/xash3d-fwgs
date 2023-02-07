#include "utils.glsl"
#include "noise.glsl"

#define GLSL
#include "ray_interop.h"
#undef GLSL

#define RAY_LIGHT_DIRECT_INPUTS(X) \
	X(10, position_t, rgba32f) \
	X(11, normals_gs, rgba16f) \
	X(12, material_rmxx, rgba8) \

#define X(index, name, format) layout(set=0,binding=index,format) uniform readonly image2D name;
RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, format) layout(set=0,binding=index,format) uniform writeonly image2D out_##name;
OUTPUTS(X)
#undef X

layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; } ubo;

#include "ray_kusochki.glsl"

#undef SHADER_OFFSET_HIT_SHADOW_BASE
#define SHADER_OFFSET_HIT_SHADOW_BASE 0
#undef SHADER_OFFSET_MISS_SHADOW
#define SHADER_OFFSET_MISS_SHADOW 0
#undef PAYLOAD_LOCATION_SHADOW
#define PAYLOAD_LOCATION_SHADOW 0

#define BINDING_LIGHTS 7
#define BINDING_LIGHT_CLUSTERS 8
#include "light.glsl"

void readNormals(ivec2 uv, out vec3 geometry_normal, out vec3 shading_normal) {
	const vec4 n = imageLoad(normals_gs, uv);
	geometry_normal = normalDecode(n.xy);
	shading_normal = normalDecode(n.zw);
}

void main() {
#ifdef RAY_TRACE
	const vec2 uv = (gl_LaunchIDEXT.xy + .5) / gl_LaunchSizeEXT.xy * 2. - 1.;
	const ivec2 pix = ivec2(gl_LaunchIDEXT.xy);
#elif defined(RAY_QUERY)
	const ivec2 pix = ivec2(gl_GlobalInvocationID);
	const ivec2 res = ivec2(imageSize(material_rmxx));
	if (any(greaterThanEqual(pix, res))) {
		return;
	}
	const vec2 uv = (gl_GlobalInvocationID.xy + .5) / res * 2. - 1.;
#else
#error You have two choices here. Ray trace, or Rake Yuri. So what it's gonna be, huh? Choose wisely.
#endif

	rand01_state = ubo.ubo.random_seed + pix.x * 1833 + pix.y * 31337;

	// FIXME incorrect for reflection/refraction
	const vec4 target    = ubo.ubo.inv_proj * vec4(uv.x, uv.y, 1, 1);
	const vec3 direction = normalize((ubo.ubo.inv_view * vec4(target.xyz, 0)).xyz);

	const vec4 material_data = imageLoad(material_rmxx, pix);

	MaterialProperties material;
	material.baseColor = vec3(1.);
	material.emissive = vec3(0.f);
	material.metalness = material_data.g;
	material.roughness = material_data.r;

	const vec3 pos = imageLoad(position_t, pix).xyz;

	vec3 geometry_normal, shading_normal;
	readNormals(pix, geometry_normal, shading_normal);

	const vec3 throughput = vec3(1.);
	vec3 diffuse = vec3(0.), specular = vec3(0.);
	computeLighting(pos + geometry_normal * .001, shading_normal, throughput, -direction, material, diffuse, specular);

#if LIGHT_POINT
	imageStore(out_light_point_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_point_specular, pix, vec4(specular, 0.f));
#endif

#if LIGHT_POLYGON
	imageStore(out_light_poly_diffuse, pix, vec4(diffuse, 0.f));
	imageStore(out_light_poly_specular, pix, vec4(specular, 0.f));
#endif
}
