#define MAX_POLYGON_VERTEX_COUNT 4
#define MIN_POLYGON_VERTEX_COUNT_BEFORE_CLIPPING 3
#include "peters2021-sampling/polygon_clipping.glsl"
#include "peters2021-sampling/polygon_sampling.glsl"

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

float triangleSolidAngle(vec3 p, vec3 a, vec3 b, vec3 c) {
	a = normalize(a - p);
	b = normalize(b - p);
	c = normalize(c - p);

	// TODO horizon culling
	const float tanHalfOmega = dot(a, cross(b,c)) / (1. + dot(b,c) + dot(c,a) + dot(a,b));

	return atan(tanHalfOmega) * 2.;
}

vec3 baryMix(vec3 v1, vec3 v2, vec3 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

vec2 baryMix(vec2 v1, vec2 v2, vec2 v3, vec2 bary) {
	return v1 * (1. - bary.x - bary.y) + v2 * bary.x + v3 * bary.y;
}

float computeTriangleContrib(uint triangle_index, uint index_offset, uint vertex_offset, mat4x3 xform, vec3 pos) {
	const uint first_index_offset = index_offset + triangle_index * 3;

	const uint vi1 = uint(indices[first_index_offset+0]) + vertex_offset;
	const uint vi2 = uint(indices[first_index_offset+1]) + vertex_offset;
	const uint vi3 = uint(indices[first_index_offset+2]) + vertex_offset;

	const vec3 v1 = (xform * vec4(vertices[vi1].pos, 1.)).xyz;
	const vec3 v2 = (xform * vec4(vertices[vi2].pos, 1.)).xyz;
	const vec3 v3 = (xform * vec4(vertices[vi3].pos, 1.)).xyz;

	return triangleSolidAngle(pos, v1, v2, v3) / TWO_PI;
}

void sampleSurfaceTriangle(
	vec3 color, vec3 view_dir, MaterialProperties material /* TODO BrdfData instead is supposedly more efficient */,
	mat4x3 emissive_transform,
	uint triangle_index, uint index_offset, uint vertex_offset,
	uint kusok_index,
	out vec3 diffuse, out vec3 specular)
{
	diffuse = specular = vec3(0.);
	const uint first_index_offset = index_offset + triangle_index * 3;

	// TODO this is not entirely correct -- need to mix between all normals, or have this normal precomputed
	const uint vi1 = uint(indices[first_index_offset+0]) + vertex_offset;
	const uint vi2 = uint(indices[first_index_offset+1]) + vertex_offset;
	const uint vi3 = uint(indices[first_index_offset+2]) + vertex_offset;

	const vec3 v1 = (emissive_transform * vec4(vertices[vi1].pos, 1.)).xyz;
	const vec3 v2 = (emissive_transform * vec4(vertices[vi2].pos, 1.)).xyz;
	const vec3 v3 = (emissive_transform * vec4(vertices[vi3].pos, 1.)).xyz;

	// TODO projected uniform sampling
	vec2 bary = vec2(sqrt(rand01()), rand01());
	bary.y *= bary.x;
	bary.x = 1. - bary.x;
	const vec3 sample_pos = baryMix(v1, v2, v3, bary);

	vec3 light_dir = sample_pos - payload_opaque.hit_pos_t.xyz;
	const float light_dir_normal_dot = dot(light_dir, payload_opaque.normal);
	if (light_dir_normal_dot <= 0.)
#ifdef DEBUG_LIGHT_CULLING
		return vec3(1., 0., 1.) * color_factor;
#else
		return;
#endif


	const float light_dist2 = dot(light_dir, light_dir);
	float pdf = TWO_PI / triangleSolidAngle(payload_opaque.hit_pos_t.xyz, v1, v2, v3);

	// Cull back facing
	if (pdf <= 0.)
		return;

	if (pdf > pdf_culling_threshold)
#ifdef DEBUG_LIGHT_CULLING
		return vec3(0., 1., 0.) * color_factor;
#else
		return;
#endif

#if 1
	{
		const uint tex_index = kusochki[kusok_index].tex_base_color;
		if ((KUSOK_MATERIAL_FLAG_SKYBOX & tex_index) == 0) {
			const vec2 uv1 = vertices[vi1].gl_tc;
			const vec2 uv2 = vertices[vi2].gl_tc;
			const vec2 uv3 = vertices[vi3].gl_tc;
			const vec2 uv = baryMix(uv1, uv2, uv3, bary);

			color *= texture(textures[nonuniformEXT(tex_index)], uv).rgb;
		}
	}
#endif

	color /= pdf;

	if (dot(color,color) < color_culling_threshold)
#ifdef DEBUG_LIGHT_CULLING
		return vec3(0., 1., 0.) * color_factor;
#else
		return;
#endif

	light_dir = normalize(light_dir);

	// TODO sample emissive texture
	evalSplitBRDF(payload_opaque.normal, light_dir, view_dir, material, diffuse, specular);
	diffuse *= color;
	specular *= color;

	vec3 combined = diffuse + specular;

	if (dot(combined,combined) < color_culling_threshold)
#ifdef DEBUG_LIGHT_CULLING
		return vec3(1., 1., 0.) * color_factor;
#else
		return;
#endif

	if (shadowed(payload_opaque.hit_pos_t.xyz, light_dir, sqrt(light_dist2))) {
		diffuse = specular = vec3(0.);
	}

	if (pdf < 0.) { //any(lessThan(diffuse, vec3(0.)))) {
		diffuse = vec3(1., 0., 0.);
	}
}

void sampleEmissiveSurface(vec3 throughput, vec3 view_dir, MaterialProperties material, SampleContext ctx, uint ekusok_index, out vec3 out_diffuse, out vec3 out_specular) {
	const EmissiveKusok ek = lights.kusochki[ekusok_index];
	const uint emissive_kusok_index = lights.kusochki[ekusok_index].kusok_index;
	const Kusok kusok = kusochki[emissive_kusok_index];

	// TODO streamline matrices layouts
	const mat4x3 to_shading = ctx.world_to_shading * mat4(
		vec4(ek.tx_row_x.x, ek.tx_row_y.x, ek.tx_row_z.x, 0),
		vec4(ek.tx_row_x.y, ek.tx_row_y.y, ek.tx_row_z.y, 0),
		vec4(ek.tx_row_x.z, ek.tx_row_y.z, ek.tx_row_z.z, 0),
		vec4(ek.tx_row_x.w, ek.tx_row_y.w, ek.tx_row_z.w, 1)
	);

	const mat4x3 emissive_transform = mat4x3(
		vec3(ek.tx_row_x.x, ek.tx_row_y.x, ek.tx_row_z.x),
		vec3(ek.tx_row_x.y, ek.tx_row_y.y, ek.tx_row_z.y),
		vec3(ek.tx_row_x.z, ek.tx_row_y.z, ek.tx_row_z.z),
		vec3(ek.tx_row_x.w, ek.tx_row_y.w, ek.tx_row_z.w)
	);


	if (emissive_kusok_index == uint(payload_opaque.kusok_index))
		return;

	// Taken from Ray Tracing Gems II, ch.47, p.776, listing 47-2
	int selected = -1;
	float selected_contrib = 0.;
	float total_contrib = 0.;
	float eps1 = rand01();

	solid_angle_polygon_t poly_angle;
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

		// Clip
		const uint vertex_count = clip_polygon(3, v);

		// poly_angle
		poly_angle = prepare_solid_angle_polygon_sampling(vertex_count, v, payload_opaque.hit_pos_t.xyz);
		const float tri_contrib = poly_angle.solid_angle;

		if (tri_contrib <= 0.)
			continue;

		const float tau = total_contrib / (total_contrib + tri_contrib);
		total_contrib += tri_contrib;

		if (eps1 < tau) {
			eps1 /= tau;
		} else {
			selected = int(i);
			selected_contrib = tri_contrib;
			eps1 = (eps1 - tau) / (1. - tau);
		}

#define MAX_BELOW_ONE .99999 // FIXME what's the correct way to do this
		eps1 = clamp(eps1, 0., MAX_BELOW_ONE); // Numerical stability (?)
	}

	if (selected >= 0) {
		sampleSurfaceTriangle(throughput * ek.emissive, view_dir, material, emissive_transform, selected, kusok.index_offset, kusok.vertex_offset, emissive_kusok_index, out_diffuse, out_specular);

		const float tri_factor = total_contrib / selected_contrib;
		out_diffuse *= tri_factor;
		out_specular *= tri_factor;
	}
}

void sampleEmissiveSurfaces(vec3 throughput, vec3 view_dir, MaterialProperties material, uint cluster_index, inout vec3 diffuse, inout vec3 specular) {

	const SampleContext ctx = buildSampleContext(payload_opaque.hit_pos_t.xyz, payload_opaque.normal, view_dir);

	const uint num_emissive_kusochki = uint(light_grid.clusters[cluster_index].num_emissive_surfaces);
	float sampling_light_scale = 1.;
#if 1
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

		if (push_constants.debug_light_index_begin < push_constants.debug_light_index_end) {
			if (index_into_emissive_kusochki < push_constants.debug_light_index_begin || index_into_emissive_kusochki >= push_constants.debug_light_index_end)
				continue;
		}

		vec3 ldiffuse, lspecular;
		sampleEmissiveSurface(throughput, view_dir, material, ctx, index_into_emissive_kusochki, ldiffuse, lspecular);

		diffuse += ldiffuse * sampling_light_scale;
		specular += lspecular * sampling_light_scale;
	} // for all emissive kusochki
}
