layout (set = 0, binding = BINDING_LIGHTS) readonly buffer UBOLights { LightsMetadata lights; }; // TODO this is pretty much static and should be a buffer, not UBO
layout (set = 0, binding = BINDING_LIGHT_CLUSTERS, align = 1) readonly buffer UBOLightClusters {
	ivec3 grid_min, grid_size;
	//uint8_t clusters_data[MAX_LIGHT_CLUSTERS * LIGHT_CLUSTER_SIZE + HACK_OFFSET];
	LightCluster clusters[MAX_LIGHT_CLUSTERS];
} light_grid;

const float color_culling_threshold = 0;//600./color_factor;
const float shadow_offset_fudge = .1;

#include "brdf.h"
#include "light_common.glsl"

#if LIGHT_POLYGON
#include "light_polygon.glsl"
#endif

#if LIGHT_POINT
void computePointLights(vec3 P, vec3 N, uint cluster_index, vec3 throughput, vec3 view_dir, MaterialProperties material, out vec3 diffuse, out vec3 specular) {
	diffuse = specular = vec3(0.);
	const uint num_point_lights = uint(light_grid.clusters[cluster_index].num_point_lights);
	for (uint j = 0; j < num_point_lights; ++j) {
		const uint i = uint(light_grid.clusters[cluster_index].point_lights[j]);

		vec3 color = lights.point_lights[i].color_stopdot.rgb * throughput;
		if (dot(color,color) < color_culling_threshold)
			continue;

		const vec4 origin_r = lights.point_lights[i].origin_r;
		const float stopdot = lights.point_lights[i].color_stopdot.a;
		const vec3 dir = lights.point_lights[i].dir_stopdot2.xyz;
		const float stopdot2 = lights.point_lights[i].dir_stopdot2.a;
		const bool not_environment = (lights.point_lights[i].environment == 0);

		const vec3 light_dir = not_environment ? (origin_r.xyz - P) : -dir; // TODO need to randomize sampling direction for environment soft shadow
		const float radius = origin_r.w;

		const vec3 light_dir_norm = normalize(light_dir);
		const float light_dot = dot(light_dir_norm, N);
		if (light_dot < 1e-5)
			continue;

		const float spot_dot = -dot(light_dir_norm, dir);
		if (spot_dot < stopdot2)
			continue;

		float spot_attenuation = 1.f;
		if (spot_dot < stopdot)
			spot_attenuation = (spot_dot - stopdot2) / (stopdot - stopdot2);

		//float fdist = 1.f;
		float light_dist = 1e5; // TODO this is supposedly not the right way to do shadows for environment lights. qrad checks for hitting SURF_SKY, and maybe we should too?
		const float d2 = dot(light_dir, light_dir);
		const float r2 = origin_r.w * origin_r.w;
		if (not_environment) {
			if (radius < 1e-3)
				continue;

			const float dist = length(light_dir);
			if (radius > dist)
				continue;
#if 1
			//light_dist = sqrt(d2);
			light_dist = dist - radius;
			//fdist = 2.f / (r2 + d2 + light_dist * sqrt(d2 + r2));
#else
			light_dist = dist;
			//const float fdist = 2.f / (r2 + d2 + light_dist * sqrt(d2 + r2));
			//const float fdist = 2.f / (r2 + d2 + light_dist * sqrt(d2 + r2));
			//fdist = (light_dist > 1.) ? 1.f / d2 : 1.f; // qrad workaround
#endif

			//const float pdf = 1.f / (fdist * light_dot * spot_attenuation);
			//const float pdf = TWO_PI / asin(radius / dist);
			const float pdf = 1. / ((1. - sqrt(d2 - r2) / dist) * spot_attenuation);
			color /= pdf;
		} else {
			color *= 2;
		}

		// if (dot(color,color) < color_culling_threshold)
		// 	continue;

		vec3 ldiffuse, lspecular;
		evalSplitBRDF(N, light_dir_norm, view_dir, material, ldiffuse, lspecular);
		ldiffuse *= color;
		lspecular *= color;

		vec3 combined = ldiffuse + lspecular;

		if (dot(combined,combined) < color_culling_threshold)
			continue;

		// FIXME split environment and other lights
		if (not_environment) {
			if (shadowed(P, light_dir_norm, light_dist + shadow_offset_fudge))
				continue;
		} else {
			// for environment light check that we've hit SURF_SKY
			if (shadowedSky(P, light_dir_norm, light_dist + shadow_offset_fudge))
				continue;
		}

		diffuse += ldiffuse;
		specular += lspecular;
	} // for all lights
}
#endif

void computeLighting(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, out vec3 diffuse, out vec3 specular) {
	diffuse = specular = vec3(0.);
	const ivec3 light_cell = ivec3(floor(P / LIGHT_GRID_CELL_SIZE)) - light_grid.grid_min;
	const uint cluster_index = uint(dot(light_cell, ivec3(1, light_grid.grid_size.x, light_grid.grid_size.x * light_grid.grid_size.y)));

	if (any(greaterThanEqual(light_cell, light_grid.grid_size)) || cluster_index >= MAX_LIGHT_CLUSTERS)
		return; // throughput * vec3(1., 0., 0.);

	// const uint cluster_offset = cluster_index * LIGHT_CLUSTER_SIZE + HACK_OFFSET;
	// const int num_dlights = int(light_grid.clusters_data[cluster_offset + LIGHT_CLUSTER_NUM_DLIGHTS_OFFSET]);
	// const int num_emissive_surfaces = int(light_grid.clusters_data[cluster_offset + LIGHT_CLUSTER_NUM_EMISSIVE_SURFACES_OFFSET]);
	// const uint emissive_surfaces_offset = cluster_offset + LIGHT_CLUSTER_EMISSIVE_SURFACES_DATA_OFFSET;
	//C = vec3(float(num_emissive_surfaces));

	//C = vec3(float(int(light_grid.clusters[cluster_index].num_emissive_surfaces)));
	//C += .3 * fract(vec3(light_cell) / 4.);

#if LIGHT_POLYGON
	sampleEmissiveSurfaces(P, N, throughput, view_dir, material, cluster_index, diffuse, specular);
#endif

#if LIGHT_POINT
	vec3 ldiffuse = vec3(0.), lspecular = vec3(0.);
	computePointLights(P, N, cluster_index, throughput, view_dir, material, ldiffuse, lspecular);
	diffuse += ldiffuse;
	specular += lspecular;
#endif
}
