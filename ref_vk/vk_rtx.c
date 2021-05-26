#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_render.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_cvar.h"
#include "vk_textures.h"
#include "vk_light.h"
#include "vk_descriptor.h"

#include "eiface.h"
#include "xash3d_mathlib.h"

#include <string.h>

#define MAX_ACCELS 1024
#define MAX_KUSOCHKI 8192
#define MAX_SCRATCH_BUFFER (16*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)
#define MAX_EMISSIVE_KUSOCHKI 256
#define MAX_LIGHT_LEAVES 8192

// TODO settings/realtime modifiable/adaptive
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720

// TODO sync with shaders
// TODO optimal values
#define WG_W 16
#define WG_H 8

typedef struct {
	vec3_t pos;
	float radius;
	vec3_t color;
	float padding_;
} vk_light_t;

typedef struct {
	uint32_t index_offset;
	uint32_t vertex_offset;
	uint32_t triangles;
	uint32_t debug_is_emissive;
	uint32_t texture;
	//float sad_padding_[1];
} vk_kusok_data_t;

typedef struct {
	uint32_t num_kusochki;
	uint32_t padding__[3];
	struct {
		// TODO should we move emissive here?
		uint32_t kusok_index;
		uint32_t padding__[3];
		vec3_t emissive_color;
		uint32_t padding___;
	} kusochki[MAX_EMISSIVE_KUSOCHKI];
} vk_emissive_kusochki_t;

typedef struct vk_ray_model_s {
	VkAccelerationStructureKHR as;
	VkAccelerationStructureGeometryKHR *geoms;
	int num_geoms;
	int size;
	uint32_t kusochki_offset;
	qboolean dynamic;
	qboolean taken;
} vk_ray_model_t;

typedef struct {
	matrix3x4 transform_row;
	vk_ray_model_t *model;
} vk_ray_draw_model_t;

typedef struct {
	float t;
	int bounces;
	float prev_frame_blend_factor;
} vk_rtx_push_constants_t;

typedef struct {
	int min_cell[4], size[3]; // 4th element is padding
	vk_light_cluster_t clusters[MAX_LIGHT_CLUSTERS];
} vk_ray_shader_light_grid;

enum {
	RayDescBinding_DestImage = 0,
	RayDescBinding_TLAS = 1,
	RayDescBinding_UBOMatrices = 2,
	RayDescBinding_Kusochki = 3,
	RayDescBinding_Indices = 4,
	RayDescBinding_Vertices = 5,
	RayDescBinding_UBOLights = 6,
	RayDescBinding_EmissiveKusochki = 7,
	RayDescBinding_PrevFrame = 8,
	RayDescBinding_LightClusters = 9,
	RayDescBinding_Textures = 10,
	RayDescBinding_COUNT
};

static struct {
	vk_descriptors_t descriptors;
	VkDescriptorSetLayoutBinding desc_bindings[RayDescBinding_COUNT];
	vk_descriptor_value_t desc_values[RayDescBinding_COUNT];
	VkDescriptorSet desc_sets[1];

	VkPipeline pipeline;

	// Stores AS built data. Lifetime similar to render buffer:
	// - some portion lives for entire map lifetime
	// - some portion lives only for a single frame (may have several frames in flight)
	// TODO: unify this with render buffer
	// Needs: AS_STORAGE_BIT, SHADER_DEVICE_ADDRESS_BIT
	vk_buffer_t accels_buffer;
	vk_ring_buffer_t accels_buffer_alloc;

	// Temp: lives only during a single frame (may have many in flight)
	// Used for building ASes;
	// Needs: AS_STORAGE_BIT, SHADER_DEVICE_ADDRESS_BIT
	vk_buffer_t scratch_buffer;
	VkDeviceAddress accels_buffer_addr, scratch_buffer_addr;

	// Temp-ish: used for making TLAS, contains addressed to all used BLASes
	// Lifetime and nature of usage similar to scratch_buffer
	// TODO: unify them
	// Needs: SHADER_DEVICE_ADDRESS, STORAGE_BUFFER, AS_BUILD_INPUT_READ_ONLY
	vk_buffer_t tlas_geom_buffer;

	// Geometry metadata. Lifetime is similar to geometry lifetime itself.
	// Semantically close to render buffer (describes layout for those objects)
	// TODO unify with render buffer
	// Needs: STORAGE_BUFFER
	vk_buffer_t kusochki_buffer;
	vk_ring_buffer_t kusochki_alloc;

	// TODO this should really be a single uniform buffer for matrices and light data

	// Expected to be small (qualifies for uniform buffer)
	// Two distinct modes: (TODO which?)
	// - static map-only lighting: constant for the entire map lifetime.
	//   Could be joined with render buffer, if not for possible uniform buffer binding optimization.
	//   This is how it operates now.
	// - fully dynamic lights: re-built each frame, so becomes similar to scratch_buffer and could be unified (same about uniform binding opt)
	//   This allows studio and other non-brush model to be emissive.
	// Needs: STORAGE/UNIFORM_BUFFER
	vk_buffer_t emissive_kusochki_buffer;

	// Planned to contain seveal types of data:
	// - grid structure itself
	// - lights data:
	//   - dlights (fully dynamic)
	//   - entity lights (can be dynamic with light styles)
	//   - surface lights (map geometry is static, however brush models can have them too and move around (e.g. wagonchik and elevators))
	// Therefore, this is also dynamic and lifetime is per-frame
	// TODO: unify with scratch buffer
	// Needs: STORAGE_BUFFER
	// Can be potentially crated using compute shader (would need shader write bit)
	vk_buffer_t light_grid_buffer;

	// TODO need several TLASes for N frames in flight
	VkAccelerationStructureKHR tlas;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		int num_models;
		int num_lighttextures;
		vk_ray_draw_model_t models[MAX_ACCELS];
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	unsigned frame_number;
	vk_image_t frames[2];

	qboolean reload_pipeline;
	qboolean freeze_models;

#define MODEL_CACHE_SIZE 1024
	vk_ray_model_t models_cache[MODEL_CACHE_SIZE];
} g_rtx = {0};

static VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(vk_core.device, &bdai);
}

static VkDeviceAddress getASAddress(VkAccelerationStructureKHR as) {
	VkAccelerationStructureDeviceAddressInfoKHR asdai = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = as,
	};
	return vkGetAccelerationStructureDeviceAddressKHR(vk_core.device, &asdai);
}

typedef struct {
	const char *debug_name;
	VkAccelerationStructureKHR *p_accel;
	const VkAccelerationStructureGeometryKHR *geoms;
	const uint32_t *max_prim_counts;
	const VkAccelerationStructureBuildRangeInfoKHR **build_ranges;
	uint32_t n_geoms;
	VkAccelerationStructureTypeKHR type;
	qboolean dynamic;
} as_build_args_t;

static void returnModelToCache(vk_ray_model_t *model) {
	ASSERT(model->taken);
	model->taken = false;
}

static vk_ray_model_t *getModelFromCache(int num_geoms, const VkAccelerationStructureGeometryKHR *geoms) { //}, int size) {
	vk_ray_model_t *model = NULL;
	int i;
	for (i = 0; i < ARRAYSIZE(g_rtx.models_cache); ++i)
	{
	 	model = g_rtx.models_cache + i;
		if (model->taken)
			continue;

		if (!model->as)
			break;

		if (model->num_geoms != num_geoms)
			continue;

		int j;
		for (j = 0; j < num_geoms; ++j) {
			if (model->geoms[j].geometryType != geoms[j].geometryType)
				break;

			if (model->geoms[j].flags != geoms[j].flags)

			if (geoms[j].geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
				// TODO what else should we compare?
				if (model->geoms[j].geometry.triangles.maxVertex != geoms[j].geometry.triangles.maxVertex)
					break;
			} else {
				PRINT_NOT_IMPLEMENTED_ARGS("Non-tri geometries are not implemented");
				break;
			}
		}

		if (j == num_geoms)
			break;
	}

	if (i == ARRAYSIZE(g_rtx.models_cache))
		return NULL;

	// if (model->size > 0)
	// 	ASSERT(model->size >= size);

	if (!model->geoms) {
		const size_t size = sizeof(*geoms) * num_geoms;
		model->geoms = Mem_Malloc(vk_core.pool, size);
		memcpy(model->geoms, geoms, size);
		model->num_geoms = num_geoms;
	}

	model->taken = true;
	return model;
}

static qboolean createOrUpdateAccelerationStructure(VkCommandBuffer cmdbuf, const as_build_args_t *args) {
	qboolean should_create = *args->p_accel == VK_NULL_HANDLE;
	qboolean is_update = false; //!should_create && args->dynamic;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = args->type,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | ( args->dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0),
		.mode =  is_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = args->n_geoms,
		.pGeometries = args->geoms,
		.srcAccelerationStructure = *args->p_accel,
	};

	VkAccelerationStructureBuildSizesInfoKHR build_size = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
	};

	uint32_t scratch_buffer_size = 0;

	ASSERT(args->geoms);
	ASSERT(args->n_geoms > 0);
	ASSERT(args->p_accel);

	vkGetAccelerationStructureBuildSizesKHR(
		vk_core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, args->max_prim_counts, &build_size);

	scratch_buffer_size = is_update ? build_size.updateScratchSize : build_size.buildScratchSize;

	if (0)
	{
		uint32_t max_prims = 0;
		for (int i = 0; i < args->n_geoms; ++i)
			max_prims += args->max_prim_counts[i];
		gEngine.Con_Reportf(
			"AS max_prims=%u, n_geoms=%u, build size: %d, scratch size: %d\n", max_prims, args->n_geoms, build_size.accelerationStructureSize, build_size.buildScratchSize);
	}

	if (MAX_SCRATCH_BUFFER < g_rtx.frame.scratch_offset + scratch_buffer_size) {
		gEngine.Con_Printf(S_ERROR "Scratch buffer overflow: left %u bytes, but need %u\n",
			MAX_SCRATCH_BUFFER - g_rtx.frame.scratch_offset,
			scratch_buffer_size);
		return false;
	}

	if (should_create) {
		const uint32_t buffer_offset = VK_RingBuffer_Alloc(&g_rtx.accels_buffer_alloc, build_size.accelerationStructureSize, 256);
		VkAccelerationStructureCreateInfoKHR asci = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = g_rtx.accels_buffer.buffer,
			.offset = buffer_offset,
			.type = args->type,
			.size = build_size.accelerationStructureSize,
		};

		if (buffer_offset == AllocFailed) {
			gEngine.Con_Printf(S_ERROR "Failed to allocated %u bytes for accel buffer\n",
				build_size.accelerationStructureSize);
			return false;
		}

		XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, args->p_accel));
		SET_DEBUG_NAME(*args->p_accel, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, args->debug_name);

		// gEngine.Con_Reportf("AS=%p, n_geoms=%u, build: %#x %d %#x\n", *args->p_accel, args->n_geoms, buffer_offset, build_size.accelerationStructureSize, buffer_offset + build_size.accelerationStructureSize);
	}

	// If not enough data for building, just create
	if (!cmdbuf || !args->build_ranges)
		return true;

	build_info.dstAccelerationStructure = *args->p_accel;
	build_info.scratchData.deviceAddress = g_rtx.scratch_buffer_addr + g_rtx.frame.scratch_offset;
	uint32_t scratch_offset_initial = g_rtx.frame.scratch_offset;
	g_rtx.frame.scratch_offset += scratch_buffer_size;
	g_rtx.frame.scratch_offset = ALIGN_UP(g_rtx.frame.scratch_offset, vk_core.physical_device.properties_accel.minAccelerationStructureScratchOffsetAlignment);

	//gEngine.Con_Reportf("AS=%p, n_geoms=%u, scratch: %#x %d %#x\n", *args->p_accel, args->n_geoms, scratch_offset_initial, scratch_buffer_size, scratch_offset_initial + scratch_buffer_size);

	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, args->build_ranges);
	return true;
}

static void createTlas( VkCommandBuffer cmdbuf ) {
	const VkAccelerationStructureGeometryKHR tl_geom[] = {
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			//.flags = VK_GEOMETRY_OPAQUE_BIT,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry.instances =
				(VkAccelerationStructureGeometryInstancesDataKHR){
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
					.data.deviceAddress = getBufferDeviceAddress(g_rtx.tlas_geom_buffer.buffer),
					.arrayOfPointers = VK_FALSE,
				},
		},
	};
	const uint32_t tl_max_prim_counts[ARRAYSIZE(tl_geom)] = { cmdbuf == VK_NULL_HANDLE ? MAX_ACCELS : g_rtx.frame.num_models };
	const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
		.primitiveCount = g_rtx.frame.num_models,
	};
	const VkAccelerationStructureBuildRangeInfoKHR* tl_build_ranges[] = { &tl_build_range };
	const as_build_args_t asrgs = {
		.geoms = tl_geom,
		.max_prim_counts = tl_max_prim_counts,
		.build_ranges =  cmdbuf == VK_NULL_HANDLE ? NULL : tl_build_ranges,
		.n_geoms = ARRAYSIZE(tl_geom),
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		// we can't really rebuild TLAS because instance count changes are not allowed .dynamic = true,
		.dynamic = false,
		.p_accel = &g_rtx.tlas,
		.debug_name = "TLAS",
	};
	if (!createOrUpdateAccelerationStructure(cmdbuf, &asrgs)) {
		gEngine.Host_Error("Could not create/update TLAS\n");
		return;
	}
}

void VK_RayNewMap( void ) {
	ASSERT(vk_core.rtx);

	VK_RingBuffer_Clear(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_Clear(&g_rtx.kusochki_alloc);

	// Clear model cache
	for (int i = 0; i < ARRAYSIZE(g_rtx.models_cache); ++i) {
		vk_ray_model_t *model = g_rtx.models_cache + i;
		if (model->as == VK_NULL_HANDLE)
			break;

		vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		memset(model, 0, sizeof(*model));
	}

	// Recreate tlas
	// Why here and not in init: to make sure that its memory is preserved. Map init will clear all memory regions.
	{
		if (g_rtx.tlas != VK_NULL_HANDLE) {
			vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.tlas, NULL);
			g_rtx.tlas = VK_NULL_HANDLE;
		}

		createTlas(VK_NULL_HANDLE);
	}

	// Upload light grid
	{
		vk_ray_shader_light_grid *grid = g_rtx.light_grid_buffer.mapped;
		ASSERT(g_lights.grid.num_cells <= MAX_LIGHT_CLUSTERS);
		VectorCopy(g_lights.grid.min_cell, grid->min_cell);
		VectorCopy(g_lights.grid.size, grid->size);
		memcpy(grid->clusters, g_lights.grid.cells, g_lights.grid.num_cells * sizeof(vk_light_cluster_t));
	}

	// Upload emissive kusochki
	// TODO we build kusochki indices when uploading/processing kusochki
	{
		vk_emissive_kusochki_t *ek = g_rtx.emissive_kusochki_buffer.mapped;
		ASSERT(g_lights.num_emissive_surfaces <= MAX_EMISSIVE_KUSOCHKI);
		memset(ek, 0, sizeof(*ek));
		ek->num_kusochki = g_lights.num_emissive_surfaces;
	}
}

void VK_RayMapLoadEnd( void ) {
	VK_RingBuffer_Fix(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_Fix(&g_rtx.kusochki_alloc);
}

void VK_RayFrameBegin( void )
{
	ASSERT(vk_core.rtx);

	if (g_rtx.freeze_models)
		return;

	// FIXME we depend on the fact that only a single frame can be in flight
	// currently framectl waits for the queue to complete before returning
	// so we can be sure here that previous frame is complete and we're free to
	// destroy/reuse dynamic ASes from previous frame
	for (int i = 0; i < g_rtx.frame.num_models; ++i) {
		vk_ray_draw_model_t *model = g_rtx.frame.models + i;
		ASSERT(model->model);
		if (!model->model->dynamic)
			continue;

		returnModelToCache(model->model);
		model->model = NULL;
	}

	g_rtx.frame.scratch_offset = 0;
	g_rtx.frame.num_models = 0;
	g_rtx.frame.num_lighttextures = 0;

	// TODO N frames in flight
	// HACK: blas caching requires persistent memory
	// proper fix would need some other memory allocation strategy
	// VK_RingBuffer_ClearFrame(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_ClearFrame(&g_rtx.kusochki_alloc);
}

static void createPipeline( void )
{
	const vk_pipeline_compute_create_info_t ci = {
		.layout = g_rtx.descriptors.pipeline_layout,
		.shader_filename = "rtx.comp.spv",
	};

	g_rtx.pipeline = VK_PipelineComputeCreate(&ci);
	ASSERT(g_rtx.pipeline);
}

static void validateModelData( void ) {
	const vk_kusok_data_t* kusochki = g_rtx.kusochki_buffer.mapped;
	ASSERT(g_rtx.frame.num_models <= ARRAYSIZE(g_rtx.frame.models));
	for (int i = 0; i < g_rtx.frame.num_models; ++i) {
		const vk_ray_draw_model_t *draw_model = g_rtx.frame.models + i;
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
			// uint32_t debug_is_emissive;
		}
	}
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	const vk_image_t* frame_src = g_rtx.frames + ((g_rtx.frame_number + 1) % 2);
	const vk_image_t* frame_dst = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself
	ASSERT(args->ubo.size == sizeof(float) * 16 * 2);

	if (vk_core.debug)
		validateModelData();

	g_rtx.frame_number++;

	if (g_rtx.reload_pipeline) {
		gEngine.Con_Printf(S_WARN "Reloading RTX shaders/pipelines\n");
		// TODO gracefully handle reload errors: need to change createPipeline, loadShader, VK_PipelineCreate...
		vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
		createPipeline();
		g_rtx.reload_pipeline = false;
	}

	// Upload all blas instances references to GPU mem
	{
		VkAccelerationStructureInstanceKHR* inst = g_rtx.tlas_geom_buffer.mapped;
		for (int i = 0; i < g_rtx.frame.num_models; ++i) {
			const vk_ray_draw_model_t* const model = g_rtx.frame.models + i;
			ASSERT(model->model);
			ASSERT(model->model->as != VK_NULL_HANDLE);
			inst[i] = (VkAccelerationStructureInstanceKHR){
				.instanceCustomIndex = model->model->kusochki_offset,
				.mask = 0xff,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = 0,
				.accelerationStructureReference = getASAddress(model->model->as), // TODO cache this addr
			};
			memcpy(&inst[i].transform, model->transform_row, sizeof(VkTransformMatrixKHR));
		}
	}

	// Barrier for building all BLASes
	// BLAS building is now in cmdbuf, need to synchronize with results
	{
		VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, // | VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.buffer = g_rtx.accels_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	// 2. Build TLAS
	if (g_rtx.frame.num_models > 0)
	{
		createTlas(cmdbuf);
	}

	if (g_rtx.tlas != VK_NULL_HANDLE)
	{
		// 3. Update descriptor sets (bind dest image, tlas, projection matrix)
		VkDescriptorImageInfo dii_all_textures[MAX_TEXTURES];

		g_rtx.desc_values[RayDescBinding_DestImage].image = (VkDescriptorImageInfo){
			.sampler = VK_NULL_HANDLE,
			.imageView = frame_dst->view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		g_rtx.desc_values[RayDescBinding_PrevFrame].image = (VkDescriptorImageInfo){
			.sampler = VK_NULL_HANDLE,
			.imageView = frame_src->view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};

		g_rtx.desc_values[RayDescBinding_UBOMatrices].buffer = (VkDescriptorBufferInfo){
			.buffer = args->ubo.buffer,
			.offset = args->ubo.offset,
			.range = args->ubo.size,
		};

		g_rtx.desc_values[RayDescBinding_Kusochki].buffer = (VkDescriptorBufferInfo){
			.buffer = g_rtx.kusochki_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty g_rtx_scene.num_models * sizeof(vk_kusok_data_t),
		};

		g_rtx.desc_values[RayDescBinding_Indices].buffer = (VkDescriptorBufferInfo){
			.buffer = args->geometry_data.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty args->geometry_data.size,
		};

		g_rtx.desc_values[RayDescBinding_Vertices].buffer = (VkDescriptorBufferInfo){
			.buffer = args->geometry_data.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty args->geometry_data.size,
		};

		g_rtx.desc_values[RayDescBinding_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &g_rtx.tlas,
		};

		g_rtx.desc_values[RayDescBinding_UBOLights].buffer = (VkDescriptorBufferInfo){
			.buffer = args->dlights.buffer,
			.offset = args->dlights.offset,
			.range = args->dlights.size,
		};

		g_rtx.desc_values[RayDescBinding_EmissiveKusochki].buffer = (VkDescriptorBufferInfo){
			.buffer = g_rtx.emissive_kusochki_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		g_rtx.desc_values[RayDescBinding_LightClusters].buffer = (VkDescriptorBufferInfo){
			.buffer = g_rtx.light_grid_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		g_rtx.desc_values[RayDescBinding_Textures].image_array = dii_all_textures;

		// TODO: move this to vk_texture.c
		for (int i = 0; i < MAX_TEXTURES; ++i) {
			const vk_texture_t *texture = findTexture(i);
			const qboolean exists = texture->vk.image_view != VK_NULL_HANDLE;
			dii_all_textures[i].sampler = VK_NULL_HANDLE;
			dii_all_textures[i].imageView = exists ? texture->vk.image_view : findTexture(tglob.defaultTexture)->vk.image_view;
			ASSERT(dii_all_textures[i].imageView != VK_NULL_HANDLE);
			dii_all_textures[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VK_DescriptorsWrite(&g_rtx.descriptors);
	}

		// 4. Barrier for TLAS build and dest image layout transfer
		{
			VkBufferMemoryBarrier bmb[] = { {
				.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.buffer = g_rtx.accels_buffer.buffer,
				.offset = 0,
				.size = VK_WHOLE_SIZE,
			} };
			VkImageMemoryBarrier image_barrier[] = { {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.image = frame_dst->image,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
				.subresourceRange = (VkImageSubresourceRange) {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
			}} };
			vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
				0, NULL, ARRAYSIZE(bmb), bmb, ARRAYSIZE(image_barrier), image_barrier);
		}

	if (g_rtx.tlas) {
		// 4. dispatch compute
		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_rtx.pipeline);
		{
			vk_rtx_push_constants_t push_constants = {
				.t = gpGlobals->realtime,
				.bounces = vk_rtx_bounces->value,
				.prev_frame_blend_factor = vk_rtx_prev_frame_blend_factor->value,
			};
			vkCmdPushConstants(cmdbuf, g_rtx.descriptors.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
		}
		vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_rtx.descriptors.pipeline_layout, 0, 1, g_rtx.descriptors.desc_sets + 0, 0, NULL);
		vkCmdDispatch(cmdbuf, (FRAME_WIDTH + WG_W - 1) / WG_W, (FRAME_HEIGHT + WG_H - 1) / WG_H, 1);
	}

	// Blit RTX frame onto swapchain image
	{
		VkImageMemoryBarrier image_barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = frame_dst->image,
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
			},
			{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = args->dst.image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}};
		vkCmdPipelineBarrier(args->cmdbuf,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
	}

	{
		VkImageBlit region = {0};
		region.srcOffsets[1].x = FRAME_WIDTH;
		region.srcOffsets[1].y = FRAME_HEIGHT;
		region.srcOffsets[1].z = 1;
		region.dstOffsets[1].x = args->dst.width;
		region.dstOffsets[1].y = args->dst.height;
		region.dstOffsets[1].z = 1;
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		vkCmdBlitImage(args->cmdbuf, frame_dst->image, VK_IMAGE_LAYOUT_GENERAL,
			args->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
			VK_FILTER_NEAREST);
	}
}

static void createLayouts( void ) {
	VkSampler samplers[MAX_TEXTURES];

	g_rtx.descriptors.bindings = g_rtx.desc_bindings;
	g_rtx.descriptors.num_bindings = ARRAYSIZE(g_rtx.desc_bindings);
	g_rtx.descriptors.values = g_rtx.desc_values;
	g_rtx.descriptors.num_sets = 1;
	g_rtx.descriptors.desc_sets = g_rtx.desc_sets;
	g_rtx.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = sizeof(vk_rtx_push_constants_t),
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_DestImage] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_DestImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_TLAS] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_TLAS,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_UBOMatrices] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_UBOMatrices,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_Kusochki] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Kusochki,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_Indices] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Indices,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_Vertices] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Vertices,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_UBOLights] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_UBOLights,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_EmissiveKusochki] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_EmissiveKusochki,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_PrevFrame] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_PrevFrame,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_LightClusters] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_LightClusters,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_rtx.desc_bindings[RayDescBinding_Textures] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Textures,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_TEXTURES,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.pImmutableSamplers = samplers,
	};

	for (int i = 0; i < ARRAYSIZE(samplers); ++i)
		samplers[i] = vk_core.default_sampler;

	VK_DescriptorsCreate(&g_rtx.descriptors);
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

static void freezeModels( void ) {
	g_rtx.freeze_models = !g_rtx.freeze_models;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure
	if (!createBuffer("ray accels_buffer", &g_rtx.accels_buffer, MAX_ACCELS_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		))
	{
		return false;
	}
	g_rtx.accels_buffer_addr = getBufferDeviceAddress(g_rtx.accels_buffer.buffer);
	g_rtx.accels_buffer_alloc.size = g_rtx.accels_buffer.size;

	if (!createBuffer("ray scratch_buffer", &g_rtx.scratch_buffer, MAX_SCRATCH_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		)) {
		return false;
	}
	g_rtx.scratch_buffer_addr = getBufferDeviceAddress(g_rtx.scratch_buffer.buffer);

	if (!createBuffer("ray tlas_geom_buffer", &g_rtx.tlas_geom_buffer, sizeof(VkAccelerationStructureInstanceKHR) * MAX_ACCELS,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!createBuffer("ray kusochki_buffer", &g_rtx.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}
	g_rtx.kusochki_alloc.size = MAX_KUSOCHKI;

	if (!createBuffer("ray emissive_kusochki_buffer", &g_rtx.emissive_kusochki_buffer, sizeof(vk_emissive_kusochki_t),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!createBuffer("ray light_grid_buffer", &g_rtx.light_grid_buffer, sizeof(vk_ray_shader_light_grid),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	createLayouts();
	createPipeline();

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i) {
		g_rtx.frames[i] = VK_ImageCreate(FRAME_WIDTH, FRAME_HEIGHT, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	}

	// Start with black previous frame
	{
		const vk_image_t *frame_src = g_rtx.frames + 1;
		const VkImageMemoryBarrier image_barriers[] = { {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = frame_src->image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
		}} };

		const VkClearColorValue clear_value = {0};

		const VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
		vkCmdPipelineBarrier(vk_core.cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
			0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
		vkCmdClearColorImage(vk_core.cb, frame_src->image, VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &image_barriers->subresourceRange);
		XVK_CHECK(vkEndCommandBuffer(vk_core.cb));

		{
			const VkSubmitInfo subinfo = {
				.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				.commandBufferCount = 1,
				.pCommandBuffers = &vk_core.cb,
			};
			XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
			XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
		}
	}

	if (vk_core.debug) {
		gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");
		gEngine.Cmd_AddCommand("vk_rtx_freeze", freezeModels, "Freeze models, do not update/add/delete models from to-draw list");
	}

	return true;
}

void VK_RayShutdown( void )
{
	ASSERT(vk_core.rtx);

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i)
		VK_ImageDestroy(g_rtx.frames + i);

	vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
	VK_DescriptorsDestroy(&g_rtx.descriptors);

	if (g_rtx.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.tlas, NULL);

	for (int i = 0; i < ARRAYSIZE(g_rtx.models_cache); ++i) {
		vk_ray_model_t *model = g_rtx.models_cache + i;
		if (model->as != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		model->as = VK_NULL_HANDLE;
	}

	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);
	destroyBuffer(&g_rtx.tlas_geom_buffer);
	destroyBuffer(&g_rtx.kusochki_buffer);
	destroyBuffer(&g_rtx.emissive_kusochki_buffer);
	destroyBuffer(&g_rtx.light_grid_buffer);
}

vk_ray_model_t* VK_RayModelCreate( vk_ray_model_init_t args ) {
	VkAccelerationStructureGeometryKHR *geoms;
	uint32_t *geom_max_prim_counts;
	VkAccelerationStructureBuildRangeInfoKHR *geom_build_ranges;
	VkAccelerationStructureBuildRangeInfoKHR **geom_build_ranges_ptr;
	const VkDeviceAddress buffer_addr = getBufferDeviceAddress(args.buffer);
	vk_kusok_data_t *kusochki;
	const uint32_t kusochki_count_offset = VK_RingBuffer_Alloc(&g_rtx.kusochki_alloc, args.model->num_geometries, 1);
	vk_ray_model_t *ray_model;

	ASSERT(vk_core.rtx);

	if (g_rtx.freeze_models)
		return args.model->ray_model;

	if (kusochki_count_offset == AllocFailed) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded on model %s\n", args.model->debug_name);
		return false;
	}

	// FIXME don't touch allocator each frame many times pls
	geoms = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geoms));
	geom_max_prim_counts = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_max_prim_counts));
	geom_build_ranges = Mem_Calloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));
	geom_build_ranges_ptr = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));

	kusochki = (vk_kusok_data_t*)(g_rtx.kusochki_buffer.mapped) + kusochki_count_offset;

	for (int i = 0; i < args.model->num_geometries; ++i) {
		const vk_render_geometry_t *mg = args.model->geometries + i;
		const uint32_t prim_count = mg->element_count / 3;
		const uint32_t vertex_offset = mg->vertex_offset + VK_RenderBufferGetOffsetInUnits(mg->vertex_buffer);
		const uint32_t index_offset = mg->index_buffer == InvalidHandle ? UINT32_MAX : (mg->index_offset + VK_RenderBufferGetOffsetInUnits(mg->index_buffer));
		const qboolean is_emissive = ((mg->texture >= 0 && mg->texture < MAX_TEXTURES)
			? g_emissive_texture_table[mg->texture].set
			: false);

		geom_max_prim_counts[i] = prim_count;
		geoms[i] = (VkAccelerationStructureGeometryKHR)
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = mg->index_buffer == InvalidHandle ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT16,
						.maxVertex = mg->vertex_count,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr + vertex_offset * sizeof(vk_vertex_t),
						.indexData.deviceAddress = buffer_addr + index_offset * sizeof(uint16_t),
					},
			};

		// gEngine.Con_Printf("  g%d: v(%#x %d %#x) V%d i(%#x %d %#x) I%d\n", i,
		// 	vertex_offset*sizeof(vk_vertex_t), mg->vertex_count * sizeof(vk_vertex_t), (vertex_offset + mg->vertex_count) * sizeof(vk_vertex_t), mg->vertex_count,
		// 	index_offset*sizeof(uint16_t), mg->element_count * sizeof(uint16_t), (index_offset + mg->element_count) * sizeof(uint16_t), mg->element_count);

		geom_build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR) {
			.primitiveCount = prim_count,
		};
		geom_build_ranges_ptr[i] = geom_build_ranges + i;

		kusochki[i].vertex_offset = vertex_offset;
		kusochki[i].index_offset = index_offset;
		kusochki[i].triangles = prim_count;
		kusochki[i].debug_is_emissive = is_emissive;

		// TODO animated textures
		kusochki[i].texture = mg->texture;

		// TODO this is bad. there should be another way to tie kusochki index to emissive surface index
		if (is_emissive && mg->surface_index >= 0) {
			vk_emissive_kusochki_t *ek = g_rtx.emissive_kusochki_buffer.mapped;
			for (int j = 0; j < g_lights.num_emissive_surfaces; ++j) {
				if (mg->surface_index == g_lights.emissive_surfaces[j].surface_index) {
					VectorCopy(g_lights.emissive_surfaces[j].emissive, ek->kusochki[j].emissive_color);
					ek->kusochki[j].kusok_index = i + kusochki_count_offset;
					break;
				}
			}
		}
	}

	{
		as_build_args_t asrgs = {
			.geoms = geoms,
			.max_prim_counts = geom_max_prim_counts,
			.build_ranges = geom_build_ranges_ptr,
			.n_geoms = args.model->num_geometries,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.dynamic = args.model->dynamic,
			.debug_name = args.model->debug_name,
		};
		ray_model = getModelFromCache(args.model->num_geometries, geoms); //, build_size.accelerationStructureSize);
		if (!ray_model) {
			gEngine.Con_Printf(S_ERROR "Ran out of model cache slots\n");
		} else {
			qboolean result;
			asrgs.p_accel = &ray_model->as;

			// TODO batch building multiple blases together
			const VkCommandBufferBeginInfo beginfo = {
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			};
			XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
			result = createOrUpdateAccelerationStructure(vk_core.cb, &asrgs);
			XVK_CHECK(vkEndCommandBuffer(vk_core.cb));

			if (!result)
			{
				gEngine.Con_Printf(S_ERROR "Could not build BLAS for %s\n", args.model->debug_name);
				returnModelToCache(ray_model);
				ray_model = NULL;
			} else {
				const VkSubmitInfo subinfo = {
					.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					.commandBufferCount = 1,
					.pCommandBuffers = &vk_core.cb,
				};
				XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
				XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
				g_rtx.frame.scratch_offset = 0;

				ray_model->kusochki_offset = kusochki_count_offset;
				ray_model->dynamic = args.model->dynamic;
			}
		}
	}

	Mem_Free(geom_build_ranges_ptr);
	Mem_Free(geom_build_ranges);
	Mem_Free(geom_max_prim_counts);
	Mem_Free(geoms); // TODO this can be cached within models_cache ??

	//gEngine.Con_Reportf("Model %s (%p) created blas %p\n", args.model->debug_name, args.model, args.model->rtx.blas);

	return ray_model;
}

void VK_RayModelDestroy( struct vk_ray_model_s *model ) {
	ASSERT(!g_rtx.freeze_models);

	ASSERT(vk_core.rtx);
	if (model->as != VK_NULL_HANDLE) {
		//gEngine.Con_Reportf("Model %s destroying AS=%p blas_index=%d\n", model->debug_name, model->rtx.blas, blas_index);

		vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		memset(model, 0, sizeof(*model));
	}
}

void VK_RayFrameAddModel( vk_ray_model_t *model, const matrix3x4 *transform_row ) {
	ASSERT(vk_core.rtx);

	ASSERT(g_rtx.frame.num_models <= ARRAYSIZE(g_rtx.frame.models));

	if (g_rtx.freeze_models)
		return;

	if (g_rtx.frame.num_models == ARRAYSIZE(g_rtx.frame.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	{
		vk_ray_draw_model_t* draw_model = g_rtx.frame.models + g_rtx.frame.num_models;
		ASSERT(model->as != VK_NULL_HANDLE);
		draw_model->model = model;
		memcpy(draw_model->transform_row, *transform_row, sizeof(draw_model->transform_row));
		g_rtx.frame.num_models++;
	}
}
