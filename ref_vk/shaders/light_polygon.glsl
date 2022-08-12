#define MAX_POLYGON_VERTEX_COUNT 8
#define MIN_POLYGON_VERTEX_COUNT_BEFORE_CLIPPING 3
#include "peters2021-sampling/polygon_clipping.glsl"
#include "peters2021-sampling/polygon_sampling.glsl"

#include "noise.glsl"
#include "utils.glsl"

struct SampleContext {
	mat4x3 world_to_shading;
};

SampleContext buildSampleContext(vec3 position, vec3 normal, vec3 view_dir) {
	SampleContext ctx;
	const float normal_dot_outgoing = dot(normal, -view_dir);
	const vec3 x_axis = normalize(fma(vec3(-normal_dot_outgoing), normal, -view_dir));
	const vec3 y_axis = cross(normal, x_axis);
	const mat3 rotation = transpose(mat3(x_axis, y_axis, normal));
	ctx.world_to_shading = mat4x3(rotation[0], rotation[1], rotation[2], -rotation * position);
	return ctx;
}

vec4 getPolygonLightSampleSimple(vec3 P, vec3 view_dir, const PolygonLight poly) {
	const uint vertices_offset = poly.vertices_count_offset & 0xffffu;
	uint vertices_count = poly.vertices_count_offset >> 16;

	vec3 v[3];
	vertices_count = 3; // FIXME

	for (uint i = 0; i < vertices_count; ++i) {
		v[i] = lights.polygon_vertices[vertices_offset + i].xyz;
	}

	vec2 rnd = vec2(sqrt(rand01()), rand01());
	rnd.y *= rnd.x;
	rnd.x = 1.f - rnd.x;

	const vec3 light_dir = baryMix(v[0], v[1], v[2], rnd) - P;
	const vec3 light_dir_n = normalize(light_dir);
	const float contrib = - poly.area * dot(light_dir_n, poly.plane.xyz ) / dot(light_dir, light_dir);
	return vec4(light_dir_n, contrib);
}

vec4 getPolygonLightSampleSimpleSolid(vec3 P, vec3 view_dir, const PolygonLight poly) {
	const uint vertices_offset = poly.vertices_count_offset & 0xffffu;
	uint vertices_count = poly.vertices_count_offset >> 16;

	uint selected = 0;
	float total_contrib = 0.;
	float eps1 = rand01();
	vec3 v[3];
	v[0] = normalize(lights.polygon_vertices[vertices_offset + 0].xyz - P);
	v[1] = normalize(lights.polygon_vertices[vertices_offset + 1].xyz - P);
	const float householder_sign = (v[0].x > 0.0f) ? -1.0f : 1.0f;
	const vec2 householder_yz = v[0].yz * (1.0f / (abs(v[0].x) + 1.0f));
	for (uint i = 2; i < vertices_count; ++i) {
		v[2] = normalize(lights.polygon_vertices[vertices_offset + i].xyz - P);

		// effectively mindlessly copypasted from polygon_sampling.glsl, Peters 2021
		// https://github.com/MomentsInGraphics/vulkan_renderer/blob/main/src/shaders/polygon_sampling.glsl
		const float dot_0_1 = dot(v[0], v[1]);
		const float dot_0_2 = dot(v[1], v[2]);
		const float dot_1_2 = dot(v[0], v[2]);
		const float dot_householder_0 = fma(-householder_sign, v[1].x, dot_0_1);
		const float dot_householder_2 = fma(-householder_sign, v[2].x, dot_1_2);
		const mat2 bottom_right_minor = mat2(
			fma(vec2(-dot_householder_0), householder_yz, v[1].yz),
			fma(vec2(-dot_householder_2), householder_yz, v[2].yz));
		const float simplex_volume = abs(determinant(bottom_right_minor));
		const float dot_0_2_plus_1_2 = dot_0_2 + dot_1_2;
		const float one_plus_dot_0_1 = 1.0f + dot_0_1;
		const float tangent = simplex_volume / (one_plus_dot_0_1 + dot_0_2_plus_1_2);
		const float contrib = 2.f * (atan(tangent) + (tangent < 0.f ? M_PI : 0.));

		if (contrib < 1e-6)
			continue;

		const float tau = total_contrib / (total_contrib + contrib);
		total_contrib += contrib;

		if (eps1 < tau) {
			eps1 /= tau;
		} else {
			selected = i;
			eps1 = (eps1 - tau) / (1. - tau);
		}

		// selected = 2;
		// break;
		v[1] = v[2];
	}

	if (selected == 0)
		return vec4(0.);

	vec2 rnd = vec2(sqrt(rand01()), rand01());
	rnd.y *= rnd.x;
	rnd.x = 1.f - rnd.x;

	const vec3 light_dir = baryMix(
		lights.polygon_vertices[vertices_offset + 0].xyz,
		lights.polygon_vertices[vertices_offset + selected - 1].xyz,
		lights.polygon_vertices[vertices_offset + selected].xyz,
		rnd) - P;
	const vec3 light_dir_n = normalize(light_dir);
	return vec4(light_dir_n, total_contrib);
}

vec4 getPolygonLightSampleProjected(vec3 view_dir, SampleContext ctx, const PolygonLight poly) {
	vec3 clipped[MAX_POLYGON_VERTEX_COUNT];

	const uint vertices_offset = poly.vertices_count_offset & 0xffffu;
	uint vertices_count = poly.vertices_count_offset >> 16;

	for (uint i = 0; i < vertices_count; ++i) {
		clipped[i] = ctx.world_to_shading * vec4(lights.polygon_vertices[vertices_offset + i].xyz, 1.);
	}

	vertices_count = clip_polygon(vertices_count, clipped);
	if (vertices_count == 0)
		return vec4(0.f);

	const projected_solid_angle_polygon_t sap = prepare_projected_solid_angle_polygon_sampling(vertices_count, clipped);
	const float contrib = sap.projected_solid_angle;
	if (contrib <= 0.f)
		return vec4(0.f);

	vec2 rnd = vec2(rand01(), rand01());
	const vec3 light_dir = (transpose(ctx.world_to_shading) * sample_projected_solid_angle_polygon(sap, rnd)).xyz;

	return vec4(light_dir, contrib);
}

vec4 getPolygonLightSampleSolid(vec3 P, vec3 view_dir, SampleContext ctx, const PolygonLight poly) {
	vec3 clipped[MAX_POLYGON_VERTEX_COUNT];

	const uint vertices_offset = poly.vertices_count_offset & 0xffffu;
	uint vertices_count = poly.vertices_count_offset >> 16;

	for (uint i = 0; i < vertices_count; ++i) {
		clipped[i] = lights.polygon_vertices[vertices_offset + i].xyz;
	}

#define DONT_CLIP
#ifndef DONT_CLIP
	vertices_count = clip_polygon(vertices_count, clipped);
	if (vertices_count == 0)
		return vec4(0.f);
#endif

	const solid_angle_polygon_t sap = prepare_solid_angle_polygon_sampling(vertices_count, clipped, P);
	const float contrib = sap.solid_angle;
	if (contrib <= 0.f)
		return vec4(0.f);

	vec2 rnd = vec2(rand01(), rand01());
	const vec3 light_dir = sample_solid_angle_polygon(sap, rnd).xyz;

	return vec4(light_dir, contrib);
}

#define DO_ALL_IN_CLUSTER 1
//#define PROJECTED
//#define SOLID
#define SIMPLE_SOLID

void sampleSinglePolygonLight(in vec3 P, in vec3 N, in vec3 view_dir, in SampleContext ctx, in MaterialProperties material, in PolygonLight poly, inout vec3 diffuse, inout vec3 specular) {
	// TODO cull by poly plane

#ifdef PROJECTED
	const vec4 light_sample_dir = getPolygonLightSampleProjected(view_dir, ctx, poly);
#else
	const vec4 light_sample_dir = getPolygonLightSampleSolid(P, view_dir, ctx, poly);
#endif
	if (light_sample_dir.w <= 0.)
		return;

	const float dist = - dot(vec4(P, 1.f), poly.plane) / dot(light_sample_dir.xyz, poly.plane.xyz);

	if (shadowed(P, light_sample_dir.xyz, dist))
		return;

	vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
	evalSplitBRDF(N, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
	const float estimate = light_sample_dir.w;
	const vec3 emissive = poly.emissive * estimate;
	diffuse += emissive * poly_diffuse;
	specular += emissive * poly_specular;
}

#if 0
// Sample random one
void sampleEmissiveSurfaces(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, uint cluster_index, inout vec3 diffuse, inout vec3 specular) {
	const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);

	if (num_polygons == 0)
		return;

	const uint selected = uint(light_grid.clusters[cluster_index].polygons[rand_range(num_polygons)]);

	const PolygonLight poly = lights.polygons[selected];
	const SampleContext ctx = buildSampleContext(P, N, view_dir);
	sampleSinglePolygonLight(P, N, view_dir, ctx, material, poly, diffuse, specular);

	const float sampling_factor = float(num_polygons);
	diffuse *= sampling_factor;
	specular *= sampling_factor;
}

#elif 1
void sampleEmissiveSurfaces(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, uint cluster_index, inout vec3 diffuse, inout vec3 specular) {
#if DO_ALL_IN_CLUSTER
	const SampleContext ctx = buildSampleContext(P, N, view_dir);

//#define USE_CLUSTERS
#ifdef USE_CLUSTERS
	const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);
	for (uint i = 0; i < num_polygons; ++i) {
		const uint index = uint(light_grid.clusters[cluster_index].polygons[i]);
#else
	for (uint index = 0; index < lights.num_polygons; ++index) {
#endif

		const PolygonLight poly = lights.polygons[index];

		const float plane_dist = dot(poly.plane, vec4(P, 1.f));

		if (plane_dist < 0.)
			continue;

#ifdef PROJECTED
		const vec4 light_sample_dir = getPolygonLightSampleProjected(view_dir, ctx, poly);
#elif defined(SOLID)
		const vec4 light_sample_dir = getPolygonLightSampleSolid(P, view_dir, ctx, poly);
#elif defined(SIMPLE_SOLID)
		const vec4 light_sample_dir = getPolygonLightSampleSimpleSolid(P, view_dir, poly);
#else
		const vec4 light_sample_dir = getPolygonLightSampleSimple(P, view_dir, poly);
#endif

		if (light_sample_dir.w <= 0.)
			continue;

		const float dist = - plane_dist / dot(light_sample_dir.xyz, poly.plane.xyz);
		const vec3 emissive = poly.emissive;

		if (!shadowed(P, light_sample_dir.xyz, dist)) {
			//const float estimate = total_contrib;
			const float estimate = light_sample_dir.w;
			vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
			evalSplitBRDF(N, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
			diffuse += throughput * emissive * estimate * poly_diffuse;
			specular += throughput * emissive * estimate * poly_specular;
		}
	}
#else // DO_ALL_IN_CLUSTERS

#ifdef USE_CLUSTERS
	// TODO move this to pickPolygonLight function
	const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);
#else
	const uint num_polygons = lights.num_polygons;
#endif

	uint selected = 0;
	float total_contrib = 0.;
	float eps1 = rand01();
	for (uint i = 0; i < num_polygons; ++i) {
#ifdef USE_CLUSTERS
		const uint index = uint(light_grid.clusters[cluster_index].polygons[i]);
#else
		const uint index = i;
#endif

		const PolygonLight poly = lights.polygons[index];

		const vec3 dir = poly.center - P;
		const vec3 light_dir = normalize(dir);
		float contrib_estimate = poly.area * dot(-light_dir, poly.plane.xyz) / (1e-3 + dot(dir, dir));

		if (contrib_estimate < 1e-6)
		 	continue;

		contrib_estimate = 1.f;
		const float tau = total_contrib / (total_contrib + contrib_estimate);
		total_contrib += contrib_estimate;

		if (eps1 < tau) {
			eps1 /= tau;
		} else {
			selected = index + 1;
			eps1 = (eps1 - tau) / (1. - tau);
		}
	}

	if (selected == 0) {
		//diffuse = vec3(1., 0., 0.);
		return;
	}

#if 0
	const PolygonLight poly = lights.polygons[selected - 1];
	const vec3 emissive = poly.emissive;
	vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
	evalSplitBRDF(N, normalize(poly.center-P), view_dir, material, poly_diffuse, poly_specular);
	diffuse += throughput * emissive * total_contrib;
	specular += throughput * emissive * total_contrib;
#else
	const SampleContext ctx = buildSampleContext(P, N, view_dir);
	const PolygonLight poly = lights.polygons[selected - 1];
#ifdef PROJECTED
		const vec4 light_sample_dir = getPolygonLightSampleProjected(view_dir, ctx, poly);
#else
		const vec4 light_sample_dir = getPolygonLightSampleSolid(P, view_dir, ctx, poly);
#endif
	if (light_sample_dir.w <= 0.)
		return;

	const float dist = - dot(vec4(P, 1.f), poly.plane) / dot(light_sample_dir.xyz, poly.plane.xyz);
	const vec3 emissive = poly.emissive;

	//if (true) {//!shadowed(P, light_sample_dir.xyz, dist)) {
	if (!shadowed(P, light_sample_dir.xyz, dist)) {
		//const float estimate = total_contrib;
		const float estimate = light_sample_dir.w;
		vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
		evalSplitBRDF(N, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
		diffuse += throughput * emissive * estimate;
		specular += throughput * emissive * estimate;
	}
#endif
#endif
}
#endif
