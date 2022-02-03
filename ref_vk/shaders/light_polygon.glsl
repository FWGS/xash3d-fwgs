#define MAX_POLYGON_VERTEX_COUNT 8
#define MIN_POLYGON_VERTEX_COUNT_BEFORE_CLIPPING 3
#include "peters2021-sampling/polygon_clipping.glsl"
#include "peters2021-sampling/polygon_sampling.glsl"

#include "noise.glsl"

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

#if 0
#define SAMPLE_TYPE_T projected_solid_angle_polygon_t
#define SAMPLE_PREPARE_FUNC(vertex_count, vertices) prepare_projected_solid_angle_polygon_sampling(vertex_count, vertices)
#define SAMPLE_FUNC sample_projected_solid_angle_polygon
#define SAMPLE_CONTRIB(sap) sap.projected_solid_angle
#else
#define SAMPLE_TYPE_T solid_angle_polygon_t
#define SAMPLE_PREPARE_FUNC(vertex_count, vertices) prepare_solid_angle_polygon_sampling(vertex_count, vertices, vec3(0.))
#define SAMPLE_FUNC sample_solid_angle_polygon
#define SAMPLE_CONTRIB(sap) sap.solid_angle
#endif

vec4 getPolygonLightSampleProjected(vec3 P, vec3 N, vec3 view_dir, SampleContext ctx, const PolygonLight poly) {
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

vec4 getPolygonLightSampleSolid(vec3 P, vec3 N, vec3 view_dir, SampleContext ctx, const PolygonLight poly) {
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

#if 0
void sampleEmissiveSurface(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, SampleContext ctx, uint ekusok_index, out vec3 out_diffuse, out vec3 out_specular) {
	out_diffuse = out_specular = vec3(0.);

	const EmissiveKusok ek = lights.kusochki[ekusok_index];
	const uint emissive_kusok_index = lights.kusochki[ekusok_index].kusok_index;

	// FIXME
	// if (emissive_kusok_index == uint(payload_opaque.kusok_index))
	// 	return;

	const Kusok kusok = kusochki[emissive_kusok_index];

	// TODO streamline matrices layouts
	const mat4x3 to_world = mat4x3(
		vec3(ek.tx_row_x.x, ek.tx_row_y.x, ek.tx_row_z.x),
		vec3(ek.tx_row_x.y, ek.tx_row_y.y, ek.tx_row_z.y),
		vec3(ek.tx_row_x.z, ek.tx_row_y.z, ek.tx_row_z.z),
		vec3(ek.tx_row_x.w, ek.tx_row_y.w, ek.tx_row_z.w)
	);

	const mat4x3 to_shading = ctx.world_to_shading * mat4(
		vec4(ek.tx_row_x.x, ek.tx_row_y.x, ek.tx_row_z.x, 0),
		vec4(ek.tx_row_x.y, ek.tx_row_y.y, ek.tx_row_z.y, 0),
		vec4(ek.tx_row_x.z, ek.tx_row_y.z, ek.tx_row_z.z, 0),
		vec4(ek.tx_row_x.w, ek.tx_row_y.w, ek.tx_row_z.w, 1)
	);

	// Picking a triangle is taken from Ray Tracing Gems II, ch.47, p.776, listing 47-2
	int selected = -1;
	float total_contrib = 0.;
	float eps1 = rand01();

	projected_solid_angle_polygon_t selected_angle;
	vec4 selected_plane;
	for (uint i = 0; i < kusok.triangles; ++i) {
		const uint first_index_offset = kusok.index_offset + i * 3;
		const uint vi1 = uint(indices[first_index_offset+0]) + kusok.vertex_offset;
		const uint vi2 = uint(indices[first_index_offset+1]) + kusok.vertex_offset;
		const uint vi3 = uint(indices[first_index_offset+2]) + kusok.vertex_offset;

		// Transform to shading space
		vec3 v[MAX_POLYGON_VERTEX_COUNT];
		v[0] = to_shading * vec4(vertices[vi1].pos, 1.);
		v[1] = to_shading * vec4(vertices[vi2].pos, 1.);
		v[2] = to_shading * vec4(vertices[vi3].pos, 1.);

		// cull by triangle orientation
		const vec3 tri_normal_dir = cross(v[1] - v[0], v[2] - v[0]);
		if (dot(tri_normal_dir, v[0]) <= 0.)
			continue;

		// Clip
		const uint vertex_count = clip_polygon(3, v);
		if (vertex_count == 0)
			continue;

		// poly_angle
		const projected_solid_angle_polygon_t sap = prepare_projected_solid_angle_polygon_sampling(vertex_count, v);
		const float tri_contrib = sap.projected_solid_angle;

		if (tri_contrib <= 0.)
			continue;

		const float tau = total_contrib / (total_contrib + tri_contrib);
		total_contrib += tri_contrib;

#if 0
		if (false) {
#else
		if (eps1 < tau) {
#endif
			eps1 /= tau;
		} else {
			selected = int(i);
			eps1 = (eps1 - tau) / (1. - tau);
			selected_angle = sap;

			vec3 vw[MAX_POLYGON_VERTEX_COUNT];
			vw[0] = to_world * vec4(vertices[vi1].pos, 1.);
			vw[1] = to_world * vec4(vertices[vi2].pos, 1.);
			vw[2] = to_world * vec4(vertices[vi3].pos, 1.);

			selected_plane.xyz = cross(vw[1] - vw[0], vw[2] - vw[0]);
			selected_plane.w = -dot(vw[0], selected_plane.xyz);
		}

#define MAX_BELOW_ONE .99999 // FIXME what's the correct way to do this
		eps1 = clamp(eps1, 0., MAX_BELOW_ONE); // Numerical stability (?)
	}

	if (selected < 0 || selected_angle.projected_solid_angle <= 0.)
		return;

	//sampleSurfaceTriangle(throughput * ek.emissive, view_dir, material, emissive_transform, selected, kusok.index_offset, kusok.vertex_offset, emissive_kusok_index, out_diffuse, out_specular);

	vec2 rnd = vec2(rand01(), rand01());
	const vec3 light_dir = (transpose(ctx.world_to_shading) * sample_projected_solid_angle_polygon(selected_angle, rnd)).xyz;
	//const vec3 light_dir = sample_solid_angle_polygon(selected_angle, rnd);

	vec3 tri_diffuse = vec3(0.), tri_specular = vec3(0.);
#if 1
	evalSplitBRDF(N, light_dir, view_dir, material, tri_diffuse, tri_specular);
	tri_diffuse *= throughput * ek.emissive;
	tri_specular *= throughput * ek.emissive;
#else
	tri_diffuse = vec3(selected_angle.solid_angle);
#endif

#if 1
	vec3 combined = tri_diffuse + tri_specular;
	if (dot(combined,combined) > color_culling_threshold) {
		const float dist = -dot(vec4(P, 1.f), selected_plane) / dot(light_dir, selected_plane.xyz);
		if (!shadowed(P, light_dir, dist)) {
			const float tri_factor = total_contrib; // / selected_angle.solid_angle;
			out_diffuse += tri_diffuse * tri_factor;
			out_specular += tri_specular * tri_factor;
		}
	} else {
#ifdef DEBUG_LIGHT_CULLING
		return vec3(1., 1., 0.) * color_factor;
#else
		return;
#endif
	}
#else
	const float tri_factor = total_contrib;
	out_diffuse += tri_diffuse * tri_factor;
	out_specular += tri_specular * tri_factor;
#endif
}

void sampleEmissiveSurfaces(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, uint cluster_index, inout vec3 diffuse, inout vec3 specular) {

	const SampleContext ctx = buildSampleContext(P, N, view_dir);

	const uint num_emissive_kusochki = uint(light_grid.clusters[cluster_index].num_emissive_surfaces);
	float sampling_light_scale = 1.;
#if 0
	const uint max_lights_per_frame = 4;
	uint begin_i = 0, end_i = num_emissive_kusochki;
	if (end_i > max_lights_per_frame) {
		begin_i = rand() % (num_emissive_kusochki - max_lights_per_frame);
		end_i = begin_i + max_lights_per_frame;
		sampling_light_scale = float(num_emissive_kusochki) / float(max_lights_per_frame);
	}
	for (uint i = begin_i; i < end_i; ++i) {
#else

	for (uint i = 0; i < num_emissive_kusochki; ++i) {
#endif
		const uint index_into_emissive_kusochki = uint(light_grid.clusters[cluster_index].emissive_surfaces[i]);

#if 0
		if (push_constants.debug_light_index_begin < push_constants.debug_light_index_end) {
			if (index_into_emissive_kusochki < push_constants.debug_light_index_begin || index_into_emissive_kusochki >= push_constants.debug_light_index_end)
				continue;
		}
#endif

		vec3 ldiffuse, lspecular;
		sampleEmissiveSurface(P, N, throughput, view_dir, material, ctx, index_into_emissive_kusochki, ldiffuse, lspecular);

		diffuse += ldiffuse * sampling_light_scale;
		specular += lspecular * sampling_light_scale;
	} // for all emissive kusochki
}
#endif

void sampleEmissiveSurfaces(vec3 P, vec3 N, vec3 throughput, vec3 view_dir, MaterialProperties material, uint cluster_index, inout vec3 diffuse, inout vec3 specular) {

#define DO_ALL_IN_CLUSTER 1
#if 0
	{
		const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);
		//diffuse = vec3(float(num_polygons) / MAX_VISIBLE_SURFACE_LIGHTS);
		if (num_polygons > 0) {
			const PolygonLight poly = lights.polygons[0];

			const vec3 dir = poly.center - P;
			const vec3 light_dir = normalize(dir);
			if (dot(-light_dir, poly.plane.xyz) <= 0.f) // || dot(light_dir, N) <= 0.)
				return;

			const SampleContext ctx = buildSampleContext(P, N, view_dir);
			const vec4 light_sample_dir = getPolygonLightSampleSolid(P, N, view_dir, ctx, poly);
			if (light_sample_dir.w <= 0.)
				return;
			const float estimate = 1.f;// light_sample_dir.w;
			vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
			evalSplitBRDF(N, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
			diffuse += throughput * poly.emissive * estimate * poly_diffuse;
			specular += throughput * poly.emissive * estimate * poly_specular;
		}
		return;
	}

#elif DO_ALL_IN_CLUSTER
	const SampleContext ctx = buildSampleContext(P, N, view_dir);
	const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);
	for (uint i = 0; i < num_polygons; ++i) {
		const uint index = uint(light_grid.clusters[cluster_index].polygons[i]);
		const PolygonLight poly = lights.polygons[index];

		if (false)
		{
			const vec3 dir = poly.center - P;
			const vec3 light_dir = normalize(dir);
			if (dot(-light_dir, poly.plane.xyz) <= 0.f)
				continue;
		}

#define PROJECTED
#ifdef PROJECTED
		const vec4 light_sample_dir = getPolygonLightSampleProjected(P, N, view_dir, ctx, poly);
#else
		const vec4 light_sample_dir = getPolygonLightSampleSolid(P, N, view_dir, ctx, poly);
#endif
		if (light_sample_dir.w <= 0.)
			continue;

		const float dist = - dot(vec4(P, 1.f), poly.plane) / dot(light_sample_dir.xyz, poly.plane.xyz);
		const vec3 emissive = poly.emissive;

#if 0
		const float estimate = light_sample_dir.w;
		diffuse += throughput * emissive * estimate;
		specular += throughput * emissive * estimate;
#else
		//if (true) {//!shadowed(P, light_sample_dir.xyz, dist)) {
		if (!shadowed(P, light_sample_dir.xyz, dist)) {
			//const float estimate = total_contrib;
			const float estimate = light_sample_dir.w;
			vec3 poly_diffuse = vec3(0.), poly_specular = vec3(0.);
			evalSplitBRDF(N, light_sample_dir.xyz, view_dir, material, poly_diffuse, poly_specular);
			diffuse += throughput * emissive * estimate * poly_diffuse;
			specular += throughput * emissive * estimate * poly_specular;
		}
#endif
	}
#else
	// TODO move this to pickPolygonLight function
	//const uint num_polygons = uint(light_grid.clusters[cluster_index].num_polygons);
	const uint num_polygons = lights.num_polygons;

	uint selected = 0;
	float total_contrib = 0.;
	float eps1 = rand01();
	for (uint i = 0; i < num_polygons; ++i) {
		//const uint index = uint(light_grid.clusters[cluster_index].polygons[i]);
		const uint index = i;

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
	const vec4 light_sample_dir = getPolygonLightSample(P, N, view_dir, ctx, selected - 1);
	if (light_sample_dir.w <= 0.)
		return;

	const PolygonLight poly = lights.polygons[selected - 1];
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
