#include "vk_ray_internal.h"

#include "vk_rtx.h"
#include "vk_textures.h"
#include "vk_render.h"
#include "vk_light.h"

#include "eiface.h"
#include "xash3d_mathlib.h"

#include <string.h>

xvk_ray_model_state_t g_ray_model_state;

static void returnModelToCache(vk_ray_model_t *model) {
	ASSERT(model->taken);
	model->taken = false;
}

static vk_ray_model_t *getModelFromCache(int num_geoms, int max_prims, const VkAccelerationStructureGeometryKHR *geoms) { //}, int size) {
	vk_ray_model_t *model = NULL;
	int i;
	for (i = 0; i < ARRAYSIZE(g_ray_model_state.models_cache); ++i)
	{
		int j;
	 	model = g_ray_model_state.models_cache + i;
		if (model->taken)
			continue;

		if (!model->as)
			break;

		if (model->num_geoms != num_geoms)
			continue;

		if (model->max_prims != max_prims)
			continue;

		for (j = 0; j < num_geoms; ++j) {
			if (model->geoms[j].geometryType != geoms[j].geometryType)
				break;

			if (model->geoms[j].flags != geoms[j].flags)
				break;

			if (geoms[j].geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
				// TODO what else should we compare?
				if (model->geoms[j].geometry.triangles.maxVertex != geoms[j].geometry.triangles.maxVertex)
					break;

				ASSERT(model->geoms[j].geometry.triangles.vertexStride == geoms[j].geometry.triangles.vertexStride);
				ASSERT(model->geoms[j].geometry.triangles.vertexFormat == geoms[j].geometry.triangles.vertexFormat);
				ASSERT(model->geoms[j].geometry.triangles.indexType == geoms[j].geometry.triangles.indexType);
			} else {
				PRINT_NOT_IMPLEMENTED_ARGS("Non-tri geometries are not implemented");
				break;
			}
		}

		if (j == num_geoms)
			break;
	}

	if (i == ARRAYSIZE(g_ray_model_state.models_cache))
		return NULL;

	// if (model->size > 0)
	// 	ASSERT(model->size >= size);

	if (!model->geoms) {
		const size_t size = sizeof(*geoms) * num_geoms;
		model->geoms = Mem_Malloc(vk_core.pool, size);
		memcpy(model->geoms, geoms, size);
		model->num_geoms = num_geoms;
		model->max_prims = max_prims;
	}

	model->taken = true;
	return model;
}

static void assertNoOverlap( uint32_t o1, uint32_t s1, uint32_t o2, uint32_t s2 ) {
	uint32_t min_offset, min_size;
	uint32_t max_offset;

	if (o1 < o2) {
		min_offset = o1;
		min_size = s1;
		max_offset = o2;
	} else {
		min_offset = o2;
		min_size = s2;
		max_offset = o1;
	}

	ASSERT(min_offset + min_size <= max_offset);
}

static void validateModelPair( const vk_ray_model_t *m1, const vk_ray_model_t *m2 ) {
	if (m1 == m2) return;
	if (!m2->num_geoms) return;
	assertNoOverlap(m1->debug.as_offset, m1->size, m2->debug.as_offset, m2->size);
	if (m1->taken && m2->taken)
		assertNoOverlap(m1->kusochki_offset, m1->num_geoms, m2->kusochki_offset, m2->num_geoms);
}

static void validateModel( const vk_ray_model_t *model ) {
	for (int j = 0; j < ARRAYSIZE(g_ray_model_state.models_cache); ++j) {
		validateModelPair(model, g_ray_model_state.models_cache + j);
	}
}

static void validateModels( void ) {
	for (int i = 0; i < ARRAYSIZE(g_ray_model_state.models_cache); ++i) {
		validateModel(g_ray_model_state.models_cache + i);
	}
}

void XVK_RayModel_Validate( void ) {
	const vk_kusok_data_t* kusochki = g_ray_model_state.kusochki_buffer.mapped;
	ASSERT(g_ray_model_state.frame.num_models <= ARRAYSIZE(g_ray_model_state.frame.models));
	for (int i = 0; i < g_ray_model_state.frame.num_models; ++i) {
		const vk_ray_draw_model_t *draw_model = g_ray_model_state.frame.models + i;
		const vk_ray_model_t *model = draw_model->model;
		int num_geoms = 1; // TODO can't validate non-dynamic models because this info is lost
		ASSERT(model);
		ASSERT(model->as != VK_NULL_HANDLE);
		ASSERT(model->kusochki_offset < MAX_KUSOCHKI);
		ASSERT(model->geoms);
		ASSERT(model->num_geoms > 0);
		ASSERT(model->taken);
		num_geoms = model->num_geoms;

		for (int j = 0; j < num_geoms; j++) {
			const vk_kusok_data_t *kusok = kusochki + j;
			const vk_texture_t *tex = findTexture(kusok->texture);
			ASSERT(tex);
			ASSERT(tex->vk.image_view != VK_NULL_HANDLE);

			// uint32_t index_offset;
			// uint32_t vertex_offset;
			// uint32_t triangles;
		}

		// Check for as model memory aliasing
		for (int j = 0; j < g_ray_model_state.frame.num_models; ++j) {
			const vk_ray_model_t *model2 = g_ray_model_state.frame.models[j].model;
			validateModelPair(model, model2);
		}
	}
}

vk_ray_model_t* VK_RayModelCreate( vk_ray_model_init_t args ) {
	VkAccelerationStructureGeometryKHR *geoms;
	uint32_t *geom_max_prim_counts;
	VkAccelerationStructureBuildRangeInfoKHR *geom_build_ranges;
	const VkDeviceAddress buffer_addr = getBufferDeviceAddress(args.buffer);
	vk_kusok_data_t *kusochki;
	const uint32_t kusochki_count_offset = VK_RingBuffer_Alloc(&g_ray_model_state.kusochki_alloc, args.model->num_geometries, 1);
	vk_ray_model_t *ray_model;
	int max_prims = 0;

	ASSERT(vk_core.rtx);

	if (g_ray_model_state.freeze_models)
		return args.model->ray_model;

	if (kusochki_count_offset == AllocFailed) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded on model %s\n", args.model->debug_name);
		return NULL;
	}

	// FIXME don't touch allocator each frame many times pls
	geoms = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geoms));
	geom_max_prim_counts = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_max_prim_counts));
	geom_build_ranges = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));

	kusochki = (vk_kusok_data_t*)(g_ray_model_state.kusochki_buffer.mapped) + kusochki_count_offset;

	for (int i = 0; i < args.model->num_geometries; ++i) {
		vk_render_geometry_t *mg = args.model->geometries + i;
		const uint32_t prim_count = mg->element_count / 3;

		max_prims += prim_count;
		geom_max_prim_counts[i] = prim_count;
		geoms[i] = (VkAccelerationStructureGeometryKHR)
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR, // FIXME this is not true. incoming mode might have transparency eventually (and also dynamically)
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = VK_INDEX_TYPE_UINT16,
						.maxVertex = mg->max_vertex,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr,
						.indexData.deviceAddress = buffer_addr,
					},
			};

#if 0
		gEngine.Con_Reportf("  g%d: v(%#x %d %#x) V%d i(%#x %d %#x) I%d\n", i,
			vertex_offset*sizeof(vk_vertex_t), mg->vertex_count * sizeof(vk_vertex_t), (vertex_offset + mg->vertex_count) * sizeof(vk_vertex_t), mg->vertex_count,
			index_offset*sizeof(uint16_t), mg->element_count * sizeof(uint16_t), (index_offset + mg->element_count) * sizeof(uint16_t), mg->element_count);
#endif

		geom_build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR) {
			.primitiveCount = prim_count,
			.primitiveOffset = mg->index_offset * sizeof(uint16_t),
			.firstVertex = mg->vertex_offset,
		};

		kusochki[i].vertex_offset = mg->vertex_offset;
		kusochki[i].index_offset = mg->index_offset;
		kusochki[i].triangles = prim_count;

		kusochki[i].texture = mg->texture;
		kusochki[i].roughness = mg->material == kXVkMaterialWater ? 0. : 1.; // FIXME
		VectorSet(kusochki[i].emissive, 0, 0, 0 );

		mg->kusok_index = i + kusochki_count_offset;
	}

	{
		as_build_args_t asrgs = {
			.geoms = geoms,
			.max_prim_counts = geom_max_prim_counts,
			.build_ranges = geom_build_ranges,
			.n_geoms = args.model->num_geometries,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.dynamic = args.model->dynamic,
			.debug_name = args.model->debug_name,
		};
		ray_model = getModelFromCache(args.model->num_geometries, max_prims, geoms); //, build_size.accelerationStructureSize);
		if (!ray_model) {
			gEngine.Con_Printf(S_ERROR "Ran out of model cache slots\n");
		} else {
			qboolean result;
			asrgs.p_accel = &ray_model->as;
			result = createOrUpdateAccelerationStructure(vk_core.cb, &asrgs, ray_model);

			if (!result)
			{
				gEngine.Con_Printf(S_ERROR "Could not build BLAS for %s\n", args.model->debug_name);
				returnModelToCache(ray_model);
				ray_model = NULL;
			} else {
				ray_model->kusochki_offset = kusochki_count_offset;
				ray_model->dynamic = args.model->dynamic;

				if (vk_core.debug)
					validateModel(ray_model);
			}
		}
	}

	Mem_Free(geom_build_ranges);
	Mem_Free(geom_max_prim_counts);
	Mem_Free(geoms); // TODO this can be cached within models_cache ??

	//gEngine.Con_Reportf("Model %s (%p) created blas %p\n", args.model->debug_name, args.model, args.model->rtx.blas);

	return ray_model;
}

void VK_RayModelDestroy( struct vk_ray_model_s *model ) {
	ASSERT(!g_ray_model_state.freeze_models);

	ASSERT(vk_core.rtx);
	if (model->as != VK_NULL_HANDLE) {
		//gEngine.Con_Reportf("Model %s destroying AS=%p blas_index=%d\n", model->debug_name, model->rtx.blas, blas_index);

		vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		Mem_Free(model->geoms);
		memset(model, 0, sizeof(*model));
	}
}

void VK_RayFrameAddModel( vk_ray_model_t *model, const vk_render_model_t *render_model, const matrix3x4 *transform_row, const vec4_t color) {
	qboolean reflective = false;
	qboolean force_emissive = false;
	qboolean additive = false;
	vk_ray_draw_model_t* draw_model = g_ray_model_state.frame.models + g_ray_model_state.frame.num_models;

	ASSERT(vk_core.rtx);
	ASSERT(g_ray_model_state.frame.num_models <= ARRAYSIZE(g_ray_model_state.frame.models));

	if (g_ray_model_state.freeze_models)
		return;

	if (g_ray_model_state.frame.num_models == ARRAYSIZE(g_ray_model_state.frame.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	{
		ASSERT(model->as != VK_NULL_HANDLE);
		draw_model->alpha_test = false;
		draw_model->model = model;
		draw_model->render_mode = render_model->render_mode;
		memcpy(draw_model->transform_row, *transform_row, sizeof(draw_model->transform_row));
		g_ray_model_state.frame.num_models++;
	}

	switch (render_model->render_mode) {
		case kRenderNormal:
			break;

		// C = (1 - alpha) * DST + alpha * SRC (TODO is this right?)
		case kRenderTransColor:
		case kRenderTransTexture:
			reflective = true;
			draw_model->alpha_test = true;
			break;

		// Additive blending: C = SRC * alpha + DST
		case kRenderGlow:
		case kRenderTransAdd:
			additive = true;
			force_emissive = true;
			draw_model->alpha_test = true;
			break;

		// Alpha test (TODO additive? mixing?)
		case kRenderTransAlpha:
			draw_model->alpha_test = true;
			break;

		default:
			gEngine.Host_Error("Unexpected render mode %d\n", render_model->render_mode);
	}

	draw_model->translucent = false;
	if (additive || color[3] < 1.f) {
		draw_model->translucent = true;
	}

	for (int i = 0; i < render_model->num_geometries; ++i) {
		const vk_render_geometry_t *geom = render_model->geometries + i;
		const vk_emissive_surface_t *esurf = VK_LightsAddEmissiveSurface( geom, transform_row, render_model->static_map );
		vk_kusok_data_t *kusok = (vk_kusok_data_t*)(g_ray_model_state.kusochki_buffer.mapped) + geom->kusok_index;
		kusok->texture = geom->texture;

		// HACK until there is proper specular
		// FIXME also this erases previour roughness unconditionally
		if (reflective)
			kusok->roughness = 0.f;
		else
			kusok->roughness = 1.f;

		Vector4Copy(color, kusok->color);

		if (additive) {
			// Alpha zero means fully transparent -- no reflections, full refraction
			// Together with force_emissive, this results in just adding emissive color
			kusok->color[3] = 0.f;
		}

		if (esurf) {
			VectorCopy(esurf->emissive, kusok->emissive);
		} else if (force_emissive) {
			VectorSet(kusok->emissive, 1.f, 1.f, 1.f);
		}
	}
}

void XVK_RayModel_ClearForNextFrame( void )
{
	// FIXME we depend on the fact that only a single frame can be in flight
	// currently framectl waits for the queue to complete before returning
	// so we can be sure here that previous frame is complete and we're free to
	// destroy/reuse dynamic ASes from previous frame
	for (int i = 0; i < g_ray_model_state.frame.num_models; ++i) {
		vk_ray_draw_model_t *model = g_ray_model_state.frame.models + i;
		ASSERT(model->model);
		if (!model->model->dynamic)
			continue;

		returnModelToCache(model->model);
		model->model = NULL;
	}

	g_ray_model_state.frame.num_models = 0;

	// TODO N frames in flight
	// HACK: blas caching requires persistent memory
	// proper fix would need some other memory allocation strategy
	// VK_RingBuffer_ClearFrame(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_ClearFrame(&g_ray_model_state.kusochki_alloc);
}
