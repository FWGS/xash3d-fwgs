#include "vk_ray_internal.h"

#include "vk_rtx.h"
#include "vk_textures.h"
#include "vk_materials.h"
#include "vk_geometry.h"
#include "vk_render.h"
#include "vk_staging.h"
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
			const vk_texture_t *tex = findTexture(kusok->tex_base_color);
			ASSERT(tex);
			ASSERT(tex->vk.image.view != VK_NULL_HANDLE);

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

static void applyMaterialToKusok(vk_kusok_data_t* kusok, const vk_render_geometry_t *geom, const vec3_t color, qboolean HACK_reflective) {
	const xvk_material_t *const mat = XVK_GetMaterialForTextureIndex( geom->texture );
	ASSERT(mat);

	// TODO split kusochki into static geometry data and potentially dynamic material data
	// This data is static, should never change
	kusok->vertex_offset = geom->vertex_offset;
	kusok->index_offset = geom->index_offset;
	kusok->triangles = geom->element_count / 3;

	/* if (!render_model->static_map) */
	/* 	VK_LightsAddEmissiveSurface( geom, transform_row, false ); */

	kusok->tex_base_color = mat->tex_base_color;
	kusok->tex_roughness = mat->tex_roughness;
	kusok->tex_metalness = mat->tex_metalness;
	kusok->tex_normalmap = mat->tex_normalmap;

	kusok->roughness = mat->roughness;
	kusok->metalness = mat->metalness;

	// HACK until there is a proper mechanism for patching materials, see https://github.com/w23/xash3d-fwgs/issues/213
	// FIXME also this erases previous roughness unconditionally
	if (HACK_reflective) {
		kusok->tex_roughness = tglob.blackTexture;
	} else if (!mat->set && geom->material == kXVkMaterialChrome) {
		kusok->tex_roughness = tglob.grayTexture;
	}

	if (geom->material == kXVkMaterialSky)
		kusok->tex_base_color |= KUSOK_MATERIAL_FLAG_SKYBOX;

	{
		vec4_t gcolor;
		gcolor[0] = color[0] * mat->base_color[0];
		gcolor[1] = color[1] * mat->base_color[1];
		gcolor[2] = color[2] * mat->base_color[2];
		gcolor[3] = color[3];
		Vector4Copy(gcolor, kusok->color);
	}

	if (geom->material == kXVkMaterialEmissive) {
		VectorCopy( geom->emissive, kusok->emissive );
	} else {
		RT_GetEmissiveForTexture( kusok->emissive, geom->texture );
	}

/* FIXME these should be done in a different way
	if (geom->material == kXVkMaterialConveyor) {
		computeConveyorSpeed( entcolor, geom->texture, kusok->uv_speed );
	} else */ {
		kusok->uv_speed[0] = kusok->uv_speed[1] = 0.f;
	}
}

vk_ray_model_t* VK_RayModelCreate( vk_ray_model_init_t args ) {
	VkAccelerationStructureGeometryKHR *geoms;
	uint32_t *geom_max_prim_counts;
	VkAccelerationStructureBuildRangeInfoKHR *geom_build_ranges;
	const VkDeviceAddress buffer_addr = R_VkBufferGetDeviceAddress(args.buffer); // TODO pass in args/have in buffer itself
	const uint32_t kusochki_count_offset = R_DEBuffer_Alloc(&g_ray_model_state.kusochki_alloc, args.model->dynamic ? LifetimeDynamic : LifetimeStatic, args.model->num_geometries, 1);
	vk_ray_model_t *ray_model;
	int max_prims = 0;

	ASSERT(vk_core.rtx);

	if (g_ray_model_state.freeze_models)
		return args.model->ray_model;

	if (kusochki_count_offset == ALO_ALLOC_FAILED) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded on model %s\n", args.model->debug_name);
		return NULL;
	}

	const vk_staging_buffer_args_t staging_args = {
		.buffer = g_ray_model_state.kusochki_buffer.buffer,
		.offset = kusochki_count_offset * sizeof(vk_kusok_data_t),
		.size = args.model->num_geometries * sizeof(vk_kusok_data_t),
		.alignment = 16,
	};
	const vk_staging_region_t kusok_staging = R_VkStagingLockForBuffer(staging_args);

	if (!kusok_staging.ptr) {
		gEngine.Con_Printf(S_ERROR "Couldn't allocate staging for %d kusochkov for model %s\n", args.model->num_geometries, args.model->debug_name);
		return NULL;
	}

	vk_kusok_data_t *const kusochki = kusok_staging.ptr;

	// FIXME don't touch allocator each frame many times pls
	geoms = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geoms));
	geom_max_prim_counts = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_max_prim_counts));
	geom_build_ranges = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));

	/* gEngine.Con_Reportf("Loading model %s, geoms: %d\n", args.model->debug_name, args.model->num_geometries); */

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

		/* { */
		/* 	const uint32_t index_offset = mg->index_offset * sizeof(uint16_t); */
		/* 	gEngine.Con_Reportf("  g%d: vertices:[%08x, %08x) indices:[%08x, %08x)\n", */
		/* 		i, */
		/* 		mg->vertex_offset * sizeof(vk_vertex_t), (mg->vertex_offset + mg->max_vertex) * sizeof(vk_vertex_t), */
		/* 		index_offset, index_offset + mg->element_count * sizeof(uint16_t) */
		/* 	); */
		/* } */

		if (mg->material == kXVkMaterialSky) {
			kusochki[i].tex_base_color |= KUSOK_MATERIAL_FLAG_SKYBOX;
		} else {
			kusochki[i].tex_base_color &= (~KUSOK_MATERIAL_FLAG_SKYBOX);
		}

		const vec3_t color = {1, 1, 1};
		applyMaterialToKusok(kusochki + i, mg, color, false);
	}

	R_VkStagingUnlock(kusok_staging.handle);

	 // FIXME this is definitely not the right place. We should upload everything in bulk, and only then build blases in bulk too
	const VkCommandBuffer cmdbuf = R_VkStagingCommit();
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			//.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, // FIXME
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT, // FIXME
			.buffer = args.buffer,
			.offset = 0, // FIXME
			.size = VK_WHOLE_SIZE, // FIXME
		}, {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			//.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, // FIXME
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT, // FIXME
			.buffer = staging_args.buffer,
			.offset = staging_args.offset,
			.size = staging_args.size,
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			//VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
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

			DEBUG_BEGINF(cmdbuf, "build blas for %s", args.model->debug_name);
			result = createOrUpdateAccelerationStructure(cmdbuf, &asrgs, ray_model);
			DEBUG_END(cmdbuf);

			if (!result)
			{
				gEngine.Con_Printf(S_ERROR "Could not build BLAS for %s\n", args.model->debug_name);
				returnModelToCache(ray_model);
				ray_model = NULL;
			} else {
				ray_model->kusochki_offset = kusochki_count_offset;
				ray_model->dynamic = args.model->dynamic;
				ray_model->kusochki_updated_this_frame = true;

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

// TODO move this to some common place with traditional renderer
static void computeConveyorSpeed(const color24 rendercolor, int tex_index, vec2_t speed) {
	float sy, cy;
	float flConveyorSpeed = 0.0f;
	float flRate, flAngle;
	vk_texture_t *texture = findTexture( tex_index );
	//gl_texture_t	*texture;

	// FIXME
	/* if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) && RI.currententity == gEngfuncs.GetEntityByIndex( 0 ) ) */
	/* { */
	/* 	// same as doom speed */
	/* 	flConveyorSpeed = -35.0f; */
	/* } */
	/* else */
	{
		flConveyorSpeed = (rendercolor.g<<8|rendercolor.b) / 16.0f;
		if( rendercolor.r ) flConveyorSpeed = -flConveyorSpeed;
	}
	//texture = R_GetTexture( glState.currentTextures[glState.activeTMU] );

	flRate = fabs( flConveyorSpeed ) / (float)texture->width;
	flAngle = ( flConveyorSpeed >= 0 ) ? 180 : 0;

	SinCos( flAngle * ( M_PI_F / 180.0f ), &sy, &cy );
	speed[0] = cy * flRate;
	speed[1] = sy * flRate;
}

void VK_RayFrameAddModel( vk_ray_model_t *model, const vk_render_model_t *render_model, const matrix3x4 *transform_row, const vec4_t color, color24 entcolor) {
	qboolean HACK_reflective = false;
	vk_ray_draw_model_t* draw_model = g_ray_model_state.frame.models + g_ray_model_state.frame.num_models;

	ASSERT(vk_core.rtx);
	ASSERT(g_ray_model_state.frame.num_models <= ARRAYSIZE(g_ray_model_state.frame.models));
	ASSERT(model->num_geoms == render_model->num_geometries);

	if (g_ray_model_state.freeze_models)
		return;

	if (g_ray_model_state.frame.num_models == ARRAYSIZE(g_ray_model_state.frame.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	{
		ASSERT(model->as != VK_NULL_HANDLE);
		draw_model->model = model;
		memcpy(draw_model->transform_row, *transform_row, sizeof(draw_model->transform_row));
	}

	switch (render_model->render_mode) {
		case kRenderNormal:
			draw_model->material_mode = MaterialMode_Opaque;
			break;

		// C = (1 - alpha) * DST + alpha * SRC (TODO is this right?)
		case kRenderTransColor:
		case kRenderTransTexture:
			HACK_reflective = true;
			draw_model->material_mode = MaterialMode_Refractive;
			break;

		// Additive blending: C = SRC * alpha + DST
		case kRenderGlow:
		case kRenderTransAdd:
			draw_model->material_mode = MaterialMode_Additive;
			break;

		// Alpha test (TODO additive? mixing?)
		case kRenderTransAlpha:
			draw_model->material_mode = MaterialMode_Opaque_AlphaTest;
			break;

		default:
			gEngine.Host_Error("Unexpected render mode %d\n", render_model->render_mode);
	}

// TODO optimize:
// - collect list of geoms for which we could update anything (animated textues, uvs, etc)
// - update only those through staging
// - also consider tracking whether the main model color has changed (that'd need to update everything yay)
	if (!model->kusochki_updated_this_frame) // FIXME enabling this makes dynamic models crash the gpu (?!)
	{
		const vk_staging_buffer_args_t staging_args = {
			.buffer = g_ray_model_state.kusochki_buffer.buffer,
			.offset = model->kusochki_offset * sizeof(vk_kusok_data_t),
			.size = render_model->num_geometries * sizeof(vk_kusok_data_t),
			.alignment = 16,
		};
		const vk_staging_region_t kusok_staging = R_VkStagingLockForBuffer(staging_args);

		if (!kusok_staging.ptr) {
			gEngine.Con_Printf(S_ERROR "Couldn't allocate staging for %d kusochkov for model %s\n", model->num_geoms, render_model->debug_name);
			return;
		}

		vk_kusok_data_t *const kusochki = kusok_staging.ptr;

		for (int i = 0; i < render_model->num_geometries; ++i) {
			const vk_render_geometry_t *geom = render_model->geometries + i;
			applyMaterialToKusok(kusochki + i, geom, color, HACK_reflective);
		}

		/* gEngine.Con_Reportf("model %s: geom=%d kuoffs=%d kustoff=%d kustsz=%d sthndl=%d\n", */
		/* 		render_model->debug_name, */
		/* 		render_model->num_geometries, */
		/* 		model->kusochki_offset, */
		/* 		staging_args.offset, staging_args.size, */
		/* 		kusok_staging.handle */
		/* 		); */

		R_VkStagingUnlock(kusok_staging.handle);
		model->kusochki_updated_this_frame = true;
	}

	for (int i = 0; i < render_model->polylights_count; ++i) {
		rt_light_add_polygon_t *const polylight = render_model->polylights + i;
		polylight->transform_row = (const matrix3x4*)transform_row;
		polylight->dynamic = true;
		RT_LightAddPolygon(polylight);
	}

	g_ray_model_state.frame.num_models++;
}

void RT_RayModel_Clear(void) {
	R_DEBuffer_Init(&g_ray_model_state.kusochki_alloc, MAX_KUSOCHKI / 2, MAX_KUSOCHKI / 2);
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
		model->model->kusochki_updated_this_frame = false;

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
	//VK_RingBuffer_ClearFrame(&g_ray_model_state.kusochki_alloc);
	R_DEBuffer_Flip(&g_ray_model_state.kusochki_alloc);
}
