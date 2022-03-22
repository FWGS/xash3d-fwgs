#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_ray_tracing: require

#include "utils.glsl"
#include "noise.glsl"

#define GLSL
#include "ray_interop.h"
#undef GLSL

#define X(index, name, format) layout(set=0,binding=index,format) uniform readonly image2D name;
RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, format) layout(set=0,binding=index,format) uniform writeonly image2D out_image_##name;
OUTPUTS(X)
#undef X

layout(set = 0, binding = 1) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 2) uniform UBO { UniformBuffer ubo; };

#include "ray_kusochki.glsl"

#define RAY_TRACE
#define RAY_TRACE2
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
	const vec2 uv = (gl_LaunchIDEXT.xy + .5) / gl_LaunchSizeEXT.xy * 2. - 1.;
	const ivec2 pix = ivec2(gl_LaunchIDEXT.xy);

	rand01_state = ubo.random_seed + gl_LaunchIDEXT.x * 1833 +  gl_LaunchIDEXT.y * 31337;

	// FIXME incorrect for reflection/refraction
	const vec4 target    = ubo.inv_proj * vec4(uv.x, uv.y, 1, 1);
	const vec3 direction = normalize((ubo.inv_view * vec4(target.xyz, 0)).xyz);

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
	imageStore(out_image_light_point_diffuse, pix, vec4(diffuse / 4.0, 0.f));
	imageStore(out_image_light_point_specular, pix, vec4(specular / 4.0, 0.f));
#else
	imageStore(out_image_light_poly_diffuse, pix, vec4(diffuse / 25.0, 0.f));
	imageStore(out_image_light_poly_specular, pix, vec4(specular/ 25.0, 0.f));
#endif
}
