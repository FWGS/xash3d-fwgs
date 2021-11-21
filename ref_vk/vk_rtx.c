#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_cvar.h"
#include "vk_textures.h"
#include "vk_light.h"
#include "vk_descriptor.h"
#include "vk_ray_internal.h"
#include "vk_denoiser.h"

#include "eiface.h"
#include "xash3d_mathlib.h"

#include <string.h>

#define MAX_SCRATCH_BUFFER (32*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)

#define MAX_LIGHT_LEAVES 8192

enum {
	ShaderBindingTable_RayGen,

	ShaderBindingTable_Miss,
	ShaderBindingTable_Miss_Shadow,
	ShaderBindingTable_Miss_Empty,

	ShaderBindingTable_Hit,
	ShaderBindingTable_Hit_WithAlphaTest,
	ShaderBindingTable_Hit_Additive,

	ShaderBindingTable_COUNT
};

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

typedef struct PushConstants vk_rtx_push_constants_t;

typedef struct {
	int min_cell[4], size[3]; // 4th element is padding
	struct LightCluster cells[MAX_LIGHT_CLUSTERS];
} vk_ray_shader_light_grid;

enum {
	RayDescBinding_Dest_ImageBaseColor = 0,
	RayDescBinding_TLAS = 1,
	RayDescBinding_UBOMatrices = 2,

	RayDescBinding_Kusochki = 3,
	RayDescBinding_Indices = 4,
	RayDescBinding_Vertices = 5,
	RayDescBinding_Textures = 6,

	RayDescBinding_Lights = 7,
	RayDescBinding_LightClusters = 8,

	RayDescBinding_Dest_ImageDiffuseGI = 9,
	RayDescBinding_Dest_ImageSpecular = 10,
	RayDescBinding_Dest_ImageAdditive = 11,
	RayDescBinding_Dest_ImageNormals = 12,

	RayDescBinding_COUNT
};

typedef struct {
	xvk_image_t denoised;
	xvk_image_t base_color;
	xvk_image_t diffuse_gi;
	xvk_image_t specular;
	xvk_image_t additive;
	xvk_image_t normals;
} xvk_ray_frame_images_t;

static struct {
	vk_descriptors_t descriptors;
	VkDescriptorSetLayoutBinding desc_bindings[RayDescBinding_COUNT];
	vk_descriptor_value_t desc_values[RayDescBinding_COUNT];
	VkDescriptorSet desc_sets[1];

	VkPipeline pipeline;

	// Shader binding table buffer
	vk_buffer_t sbt_buffer;
	uint32_t sbt_record_size;

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
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	unsigned frame_number;
	xvk_ray_frame_images_t frames[2];

	qboolean reload_pipeline;
	qboolean reload_lighting;
} g_rtx = {0};

VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
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

// TODO split this into smaller building blocks in a separate module
qboolean createOrUpdateAccelerationStructure(VkCommandBuffer cmdbuf, const as_build_args_t *args, vk_ray_model_t *model) {
	qboolean should_create = *args->p_accel == VK_NULL_HANDLE;
#if 1 // update does not work at all on AMD gpus
	qboolean is_update = false; // FIXME this crashes for some reason !should_create && args->dynamic;
#else
	qboolean is_update = !should_create && args->dynamic;
#endif

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = args->type,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | ( args->dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0),
		.mode =  is_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = args->n_geoms,
		.pGeometries = args->geoms,
		.srcAccelerationStructure = is_update ? *args->p_accel : VK_NULL_HANDLE,
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

#if 0
	{
		uint32_t max_prims = 0;
		for (int i = 0; i < args->n_geoms; ++i)
			max_prims += args->max_prim_counts[i];
		gEngine.Con_Reportf(
			"AS max_prims=%u, n_geoms=%u, build size: %d, scratch size: %d\n", max_prims, args->n_geoms, build_size.accelerationStructureSize, build_size.buildScratchSize);
	}
#endif

	if (MAX_SCRATCH_BUFFER < g_rtx.frame.scratch_offset + scratch_buffer_size) {
		gEngine.Con_Printf(S_ERROR "Scratch buffer overflow: left %u bytes, but need %u\n",
			MAX_SCRATCH_BUFFER - g_rtx.frame.scratch_offset,
			scratch_buffer_size);
		return false;
	}

	if (should_create) {
		const uint32_t as_size = build_size.accelerationStructureSize;
		const uint32_t buffer_offset = VK_RingBuffer_Alloc(&g_rtx.accels_buffer_alloc, as_size, 256);
		const VkAccelerationStructureCreateInfoKHR asci = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = g_rtx.accels_buffer.buffer,
			.offset = buffer_offset,
			.type = args->type,
			.size = as_size,
		};

		if (buffer_offset == AllocFailed) {
			gEngine.Con_Printf(S_ERROR "Failed to allocated %u bytes for accel buffer\n", asci.size);
			return false;
		}

		XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, args->p_accel));
		SET_DEBUG_NAME(*args->p_accel, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, args->debug_name);

		if (model) {
			model->size = asci.size;
			model->debug.as_offset = buffer_offset;
		}

		// gEngine.Con_Reportf("AS=%p, n_geoms=%u, build: %#x %d %#x\n", *args->p_accel, args->n_geoms, buffer_offset, asci.size, buffer_offset + asci.size);
	}

	// If not enough data for building, just create
	if (!cmdbuf || !args->build_ranges)
		return true;

	if (model) {
		ASSERT(model->size >= build_size.accelerationStructureSize);
	}

	build_info.dstAccelerationStructure = *args->p_accel;
	build_info.scratchData.deviceAddress = g_rtx.scratch_buffer_addr + g_rtx.frame.scratch_offset;
	//uint32_t scratch_offset_initial = g_rtx.frame.scratch_offset;
	g_rtx.frame.scratch_offset += scratch_buffer_size;
	g_rtx.frame.scratch_offset = ALIGN_UP(g_rtx.frame.scratch_offset, vk_core.physical_device.properties_accel.minAccelerationStructureScratchOffsetAlignment);

	//gEngine.Con_Reportf("AS=%p, n_geoms=%u, scratch: %#x %d %#x\n", *args->p_accel, args->n_geoms, scratch_offset_initial, scratch_buffer_size, scratch_offset_initial + scratch_buffer_size);

	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, &args->build_ranges);
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
	const uint32_t tl_max_prim_counts[ARRAYSIZE(tl_geom)] = { MAX_ACCELS }; //cmdbuf == VK_NULL_HANDLE ? MAX_ACCELS : g_ray_model_state.frame.num_models };
	const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
		.primitiveCount = g_ray_model_state.frame.num_models,
	};
	const as_build_args_t asrgs = {
		.geoms = tl_geom,
		.max_prim_counts = tl_max_prim_counts,
		.build_ranges =  cmdbuf == VK_NULL_HANDLE ? NULL : &tl_build_range,
		.n_geoms = ARRAYSIZE(tl_geom),
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		// we can't really rebuild TLAS because instance count changes are not allowed .dynamic = true,
		.dynamic = false,
		.p_accel = &g_rtx.tlas,
		.debug_name = "TLAS",
	};
	if (!createOrUpdateAccelerationStructure(cmdbuf, &asrgs, NULL)) {
		gEngine.Host_Error("Could not create/update TLAS\n");
		return;
	}
}

void VK_RayNewMap( void ) {
	ASSERT(vk_core.rtx);

	VK_RingBuffer_Clear(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_Clear(&g_ray_model_state.kusochki_alloc);

	// Clear model cache
	for (int i = 0; i < ARRAYSIZE(g_ray_model_state.models_cache); ++i) {
		vk_ray_model_t *model = g_ray_model_state.models_cache + i;
		VK_RayModelDestroy(model);
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
}

void VK_RayMapLoadEnd( void ) {
	VK_RingBuffer_Fix(&g_rtx.accels_buffer_alloc);
	VK_RingBuffer_Fix(&g_ray_model_state.kusochki_alloc);
}

void VK_RayFrameBegin( void )
{
	ASSERT(vk_core.rtx);

	g_rtx.frame.scratch_offset = 0;

	if (g_ray_model_state.freeze_models)
		return;

	XVK_RayModel_ClearForNextFrame();

	if (g_rtx.reload_lighting) {
		g_rtx.reload_lighting = false;
		VK_LightsLoadMapStaticLights();
	}

	// TODO shouldn't we do this in freeze models mode anyway?
	VK_LightsFrameInit();
}

static void createPipeline( void )
{
	struct RayShaderSpec {
		int max_point_lights;
		int max_emissive_kusochki;
		uint32_t max_visible_point_lights;
		uint32_t max_visible_surface_lights;
		float light_grid_cell_size;
		int max_light_clusters;
	} spec_data = {
		.max_point_lights = MAX_POINT_LIGHTS,
		.max_emissive_kusochki = MAX_EMISSIVE_KUSOCHKI,
		.max_visible_point_lights = MAX_VISIBLE_POINT_LIGHTS,
		.max_visible_surface_lights = MAX_VISIBLE_SURFACE_LIGHTS,
		.light_grid_cell_size = LIGHT_GRID_CELL_SIZE,
		.max_light_clusters = MAX_LIGHT_CLUSTERS,
	};
	const VkSpecializationMapEntry spec_map[] = {
		{.constantID = 0, .offset = offsetof(struct RayShaderSpec, max_point_lights), .size = sizeof(int) },
		{.constantID = 1, .offset = offsetof(struct RayShaderSpec, max_emissive_kusochki), .size = sizeof(int) },
		{.constantID = 2, .offset = offsetof(struct RayShaderSpec, max_visible_point_lights), .size = sizeof(uint32_t) },
		{.constantID = 3, .offset = offsetof(struct RayShaderSpec, max_visible_surface_lights), .size = sizeof(uint32_t) },
		{.constantID = 4, .offset = offsetof(struct RayShaderSpec, light_grid_cell_size), .size = sizeof(float) },
		{.constantID = 5, .offset = offsetof(struct RayShaderSpec, max_light_clusters), .size = sizeof(int) },
	};

	VkSpecializationInfo spec = {
		.mapEntryCount = ARRAYSIZE(spec_map),
		.pMapEntries = spec_map,
		.dataSize = sizeof(spec_data),
		.pData = &spec_data,
	};

	enum {
		ShaderStageIndex_RayGen,
		ShaderStageIndex_Miss,
		ShaderStageIndex_Miss_Shadow,
		ShaderStageIndex_Miss_Empty,
		ShaderStageIndex_ClosestHit,
		ShaderStageIndex_AnyHit_AlphaTest,
		ShaderStageIndex_AnyHit_Additive,
		ShaderStageIndex_COUNT,
	};

#define DEFINE_SHADER(filename, bit, sbt_index) \
	shaders[sbt_index] = (VkPipelineShaderStageCreateInfo){ \
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
		.stage = VK_SHADER_STAGE_##bit##_BIT_KHR, \
		.module = loadShader(filename), \
		.pName = "main", \
		.pSpecializationInfo = &spec, \
	}

	VkPipelineShaderStageCreateInfo shaders[ShaderStageIndex_COUNT];
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[ShaderBindingTable_COUNT];

	const VkRayTracingPipelineCreateInfoKHR rtpci = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		//TODO .flags = VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR  ....
		.stageCount = ARRAYSIZE(shaders),
		.pStages = shaders,
		.groupCount = ARRAYSIZE(shader_groups),
		.pGroups = shader_groups,
		.maxPipelineRayRecursionDepth = 1,
		.layout = g_rtx.descriptors.pipeline_layout,
	};

	DEFINE_SHADER("ray.rgen.spv", RAYGEN, ShaderStageIndex_RayGen);
	DEFINE_SHADER("ray.rmiss.spv", MISS, ShaderStageIndex_Miss);
	DEFINE_SHADER("shadow.rmiss.spv", MISS, ShaderStageIndex_Miss_Shadow);
	DEFINE_SHADER("empty.rmiss.spv", MISS, ShaderStageIndex_Miss_Empty);
	DEFINE_SHADER("ray.rchit.spv", CLOSEST_HIT, ShaderStageIndex_ClosestHit);
	DEFINE_SHADER("alphamask.rahit.spv", ANY_HIT, ShaderStageIndex_AnyHit_AlphaTest);
	DEFINE_SHADER("additive.rahit.spv", ANY_HIT, ShaderStageIndex_AnyHit_Additive);

	// TODO static assert
#define ASSERT_SHADER_OFFSET(sbt_kind, sbt_index, offset) \
	ASSERT(offset == (sbt_index - sbt_kind))

	ASSERT_SHADER_OFFSET(ShaderBindingTable_RayGen, ShaderBindingTable_RayGen, 0);
	ASSERT_SHADER_OFFSET(ShaderBindingTable_Miss, ShaderBindingTable_Miss, SHADER_OFFSET_MISS_REGULAR);
	ASSERT_SHADER_OFFSET(ShaderBindingTable_Miss, ShaderBindingTable_Miss_Shadow, SHADER_OFFSET_MISS_SHADOW);
	ASSERT_SHADER_OFFSET(ShaderBindingTable_Hit, ShaderBindingTable_Hit, SHADER_OFFSET_HIT_REGULAR);
	ASSERT_SHADER_OFFSET(ShaderBindingTable_Hit, ShaderBindingTable_Hit_WithAlphaTest, SHADER_OFFSET_HIT_ALPHA_TEST);
	ASSERT_SHADER_OFFSET(ShaderBindingTable_Hit, ShaderBindingTable_Hit_Additive, SHADER_OFFSET_HIT_ADDITIVE);

	shader_groups[ShaderBindingTable_RayGen] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderStageIndex_RayGen,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Miss] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderStageIndex_Miss,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Miss_Shadow] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderStageIndex_Miss_Shadow,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Miss_Empty] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderBindingTable_Miss_Empty,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Hit] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = ShaderStageIndex_ClosestHit,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Hit_WithAlphaTest] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.anyHitShader = ShaderStageIndex_AnyHit_AlphaTest,
		.closestHitShader = ShaderStageIndex_ClosestHit,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[ShaderBindingTable_Hit_Additive] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.anyHitShader = ShaderStageIndex_AnyHit_Additive,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	XVK_CHECK(vkCreateRayTracingPipelinesKHR(vk_core.device, VK_NULL_HANDLE, g_pipeline_cache, 1, &rtpci, NULL, &g_rtx.pipeline));
	ASSERT(g_rtx.pipeline != VK_NULL_HANDLE);

	{
		const uint32_t sbt_handle_size = vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize;
		const uint32_t sbt_handles_buffer_size = ARRAYSIZE(shader_groups) * sbt_handle_size;
		uint8_t *sbt_handles = Mem_Malloc(vk_core.pool, sbt_handles_buffer_size);
		XVK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(vk_core.device, g_rtx.pipeline, 0, ARRAYSIZE(shader_groups), sbt_handles_buffer_size, sbt_handles));
		for (int i = 0; i < ARRAYSIZE(shader_groups); ++i)
		{
			uint8_t *sbt_dst = g_rtx.sbt_buffer.mapped;
			memcpy(sbt_dst + g_rtx.sbt_record_size * i, sbt_handles + sbt_handle_size * i, sbt_handle_size);
		}
		Mem_Free(sbt_handles);
	}

	for (int i = 0; i < ARRAYSIZE(shaders); ++i)
		vkDestroyShaderModule(vk_core.device, shaders[i].module, NULL);
}

static void prepareTlas( VkCommandBuffer cmdbuf ) {
	ASSERT(g_ray_model_state.frame.num_models > 0);

	// Upload all blas instances references to GPU mem
	{
		VkAccelerationStructureInstanceKHR* inst = g_rtx.tlas_geom_buffer.mapped;
		for (int i = 0; i < g_ray_model_state.frame.num_models; ++i) {
			const vk_ray_draw_model_t* const model = g_ray_model_state.frame.models + i;
			ASSERT(model->model);
			ASSERT(model->model->as != VK_NULL_HANDLE);
			inst[i] = (VkAccelerationStructureInstanceKHR){
				.instanceCustomIndex = model->model->kusochki_offset,
				.instanceShaderBindingTableRecordOffset = 0,
				.accelerationStructureReference = getASAddress(model->model->as), // TODO cache this addr
			};
			switch (model->material_mode) {
				case MaterialMode_Opaque:
					inst[i].mask = GEOMETRY_BIT_OPAQUE;
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
					break;
				case MaterialMode_Opaque_AlphaTest:
					inst[i].mask = GEOMETRY_BIT_OPAQUE;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_ALPHA_TEST,
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
					break;
				case MaterialMode_Refractive:
					inst[i].mask = GEOMETRY_BIT_REFRACTIVE;
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
					break;
				case MaterialMode_Additive:
					inst[i].mask = GEOMETRY_BIT_ADDITIVE;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_ADDITIVE,
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
					break;
			}
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
	createTlas(cmdbuf);
}

static void updateDescriptors( VkCommandBuffer cmdbuf, const vk_ray_frame_render_args_t *args, const xvk_ray_frame_images_t *frame_dst ) {
	// 3. Update descriptor sets (bind dest image, tlas, projection matrix)
	VkDescriptorImageInfo dii_all_textures[MAX_TEXTURES];

	g_rtx.desc_values[RayDescBinding_Dest_ImageBaseColor].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = frame_dst->base_color.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_rtx.desc_values[RayDescBinding_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &g_rtx.tlas,
	};

	g_rtx.desc_values[RayDescBinding_UBOMatrices].buffer = (VkDescriptorBufferInfo){
		.buffer = args->ubo.buffer,
		.offset = args->ubo.offset,
		.range = args->ubo.size,
	};

	g_rtx.desc_values[RayDescBinding_Kusochki].buffer = (VkDescriptorBufferInfo){
		.buffer = g_ray_model_state.kusochki_buffer.buffer,
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

	g_rtx.desc_values[RayDescBinding_Textures].image_array = dii_all_textures;

	// TODO: move this to vk_texture.c
	for (int i = 0; i < MAX_TEXTURES; ++i) {
		const vk_texture_t *texture = findTexture(i);
		const qboolean exists = texture->vk.image.view != VK_NULL_HANDLE;
		dii_all_textures[i].sampler = vk_core.default_sampler; // FIXME on AMD using pImmutableSamplers leads to NEAREST filtering ??. VK_NULL_HANDLE;
		dii_all_textures[i].imageView = exists ? texture->vk.image.view : findTexture(tglob.defaultTexture)->vk.image.view;
		ASSERT(dii_all_textures[i].imageView != VK_NULL_HANDLE);
		dii_all_textures[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	g_rtx.desc_values[RayDescBinding_Lights].buffer = (VkDescriptorBufferInfo){
		.buffer = g_ray_model_state.lights_buffer.buffer,
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};

	g_rtx.desc_values[RayDescBinding_LightClusters].buffer = (VkDescriptorBufferInfo){
		.buffer = g_rtx.light_grid_buffer.buffer,
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};

	g_rtx.desc_values[RayDescBinding_Dest_ImageDiffuseGI].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = frame_dst->diffuse_gi.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_rtx.desc_values[RayDescBinding_Dest_ImageSpecular].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = frame_dst->specular.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_rtx.desc_values[RayDescBinding_Dest_ImageAdditive].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = frame_dst->additive.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_rtx.desc_values[RayDescBinding_Dest_ImageNormals].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = frame_dst->normals.view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_rtx.descriptors);
}

static qboolean rayTrace( VkCommandBuffer cmdbuf, const xvk_ray_frame_images_t *current_frame, float fov_angle_y ) {
#define LIST_GBUFFER_IMAGES(X) \
	X(base_color) \
	X(diffuse_gi) \
	X(specular) \
	X(additive) \
	X(normals) \

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
		VkImageMemoryBarrier image_barrier[] = {
#define GBUFFER_WRITE_BARRIER(img) { \
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, \
			.image = current_frame->img.image, \
			.srcAccessMask = 0, \
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT, \
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, \
			.newLayout = VK_IMAGE_LAYOUT_GENERAL, \
			.subresourceRange = (VkImageSubresourceRange) { \
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, \
				.baseMipLevel = 0, \
				.levelCount = 1, \
				.baseArrayLayer = 0, \
				.layerCount = 1, \
			}, \
		},
LIST_GBUFFER_IMAGES(GBUFFER_WRITE_BARRIER)
		};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, ARRAYSIZE(image_barrier), image_barrier);
	}

	// 4. dispatch ray tracing
	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_rtx.pipeline);
	{
		vk_rtx_push_constants_t push_constants = {
			.time = gpGlobals->time,
			.random_seed = (uint32_t)gEngine.COM_RandomLong(0, INT32_MAX),
			.bounces = vk_rtx_bounces->value,
			.prev_frame_blend_factor = vk_rtx_prev_frame_blend_factor->value,
			.pixel_cone_spread_angle = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)FRAME_HEIGHT),
			.debug_light_index_begin = (uint32_t)(vk_rtx_light_begin->value),
			.debug_light_index_end = (uint32_t)(vk_rtx_light_end->value),
			.flags = r_lightmap->value ? PUSH_FLAG_LIGHTMAP_ONLY : 0,
		};
		vkCmdPushConstants(cmdbuf, g_rtx.descriptors.pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 0, sizeof(push_constants), &push_constants);
	}
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_rtx.descriptors.pipeline_layout, 0, 1, g_rtx.descriptors.desc_sets + 0, 0, NULL);

	{
		const uint32_t sbt_record_size = g_rtx.sbt_record_size;
		//const uint32_t sbt_record_size = vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize;
#define SBT_INDEX(index, count) { \
.deviceAddress = getBufferDeviceAddress(g_rtx.sbt_buffer.buffer) + g_rtx.sbt_record_size * index, \
.size = sbt_record_size * (count), \
.stride = sbt_record_size, \
}
		const VkStridedDeviceAddressRegionKHR sbt_raygen = SBT_INDEX(ShaderBindingTable_RayGen, 1);
		const VkStridedDeviceAddressRegionKHR sbt_miss = SBT_INDEX(ShaderBindingTable_Miss, ShaderBindingTable_Miss_Empty - ShaderBindingTable_Miss);
		const VkStridedDeviceAddressRegionKHR sbt_hit = SBT_INDEX(ShaderBindingTable_Hit, ShaderBindingTable_Hit_Additive - ShaderBindingTable_Hit);
		const VkStridedDeviceAddressRegionKHR sbt_callable = { 0 };

		vkCmdTraceRaysKHR(cmdbuf, &sbt_raygen, &sbt_miss, &sbt_hit, &sbt_callable, FRAME_WIDTH, FRAME_HEIGHT, 1 );
	}

	return true;
}

// Finalize and update dynamic lights
static void updateLights( void )
{
	VK_LightsFrameFinalize();

	// Upload light grid
	{
		vk_ray_shader_light_grid *grid = g_rtx.light_grid_buffer.mapped;
		ASSERT(g_lights.map.grid_cells <= MAX_LIGHT_CLUSTERS);
		VectorCopy(g_lights.map.grid_min_cell, grid->min_cell);
		VectorCopy(g_lights.map.grid_size, grid->size);

		for (int i = 0; i < g_lights.map.grid_cells; ++i) {
			const vk_lights_cell_t *const src = g_lights.cells + i;
			struct LightCluster *const dst = grid->cells + i;

			dst->num_point_lights = src->num_point_lights;
			dst->num_emissive_surfaces = src->num_emissive_surfaces;
			memcpy(dst->point_lights, src->point_lights, sizeof(uint8_t) * src->num_point_lights);
			memcpy(dst->emissive_surfaces, src->emissive_surfaces, sizeof(uint8_t) * src->num_emissive_surfaces);
		}
	}

	// Upload dynamic emissive kusochki
	{
		struct Lights *lights = g_ray_model_state.lights_buffer.mapped;
		ASSERT(g_lights.num_emissive_surfaces <= MAX_EMISSIVE_KUSOCHKI);
		lights->num_kusochki = g_lights.num_emissive_surfaces;
		for (int i = 0; i < g_lights.num_emissive_surfaces; ++i) {
			const vk_emissive_surface_t *const src_esurf = g_lights.emissive_surfaces + i;
			struct EmissiveKusok *const dst_ekusok = lights->kusochki + i;

			dst_ekusok->kusok_index = src_esurf->kusok_index;
			Matrix3x4_Copy(dst_ekusok->tx_row_x, src_esurf->transform);
			VectorCopy(src_esurf->emissive, dst_ekusok->emissive);
		}

		lights->num_point_lights = g_lights.num_point_lights;
		for (int i = 0; i < g_lights.num_point_lights; ++i) {
			vk_point_light_t *const src = g_lights.point_lights + i;
			struct PointLight *const dst = lights->point_lights + i;

			VectorCopy(src->origin, dst->origin_r);
			dst->origin_r[3] = src->radius;

			VectorCopy(src->color, dst->color_stopdot);
			dst->color_stopdot[3] = src->stopdot;

			VectorCopy(src->dir, dst->dir_stopdot2);
			dst->dir_stopdot2[3] = src->stopdot2;

			dst->environment = !!(src->flags & LightFlag_Environment);
		}

		lights->skybox_rt = tglob.skyboxTextures[0];
		lights->skybox_bk = tglob.skyboxTextures[1];
		lights->skybox_lf = tglob.skyboxTextures[2];
		lights->skybox_ft = tglob.skyboxTextures[3];
		lights->skybox_up = tglob.skyboxTextures[4];
		lights->skybox_dn = tglob.skyboxTextures[5];
	}
}

static void clearVkImage( VkCommandBuffer cmdbuf, VkImage image ) {
	const VkImageMemoryBarrier image_barriers[] = { {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.image = image,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
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

	vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);

	vkCmdClearColorImage(cmdbuf, image, VK_IMAGE_LAYOUT_GENERAL, &clear_value, 1, &image_barriers->subresourceRange);
}

typedef struct {
	VkCommandBuffer cmdbuf;

	VkPipelineStageFlags in_stage;
	struct {
		VkImage image;
		int width, height;
		VkImageLayout oldLayout;
		VkAccessFlags srcAccessMask;
	} src, dst;
} xvk_blit_args;

static void blitImage( const xvk_blit_args *blit_args ) {
	{
		const VkImageMemoryBarrier image_barriers[] = { {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->src.image,
			.srcAccessMask = blit_args->src.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = blit_args->src.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}, {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = blit_args->dst.srcAccessMask,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = blit_args->dst.oldLayout,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		} };

		vkCmdPipelineBarrier(blit_args->cmdbuf,
			blit_args->in_stage,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
	}

	{
		VkImageBlit region = {0};
		region.srcOffsets[1].x = blit_args->src.width;
		region.srcOffsets[1].y = blit_args->src.height;
		region.srcOffsets[1].z = 1;
		region.dstOffsets[1].x = blit_args->dst.width;
		region.dstOffsets[1].y = blit_args->dst.height;
		region.dstOffsets[1].z = 1;
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		vkCmdBlitImage(blit_args->cmdbuf,
			blit_args->src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			blit_args->dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region,
			VK_FILTER_NEAREST);
	}

	{
		VkImageMemoryBarrier image_barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = blit_args->dst.image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}};
		vkCmdPipelineBarrier(blit_args->cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
	}
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	const xvk_ray_frame_images_t* current_frame = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself
	ASSERT(args->ubo.size == sizeof(float) * 16 * 2);

	g_rtx.frame_number++;

	// if (vk_core.debug)
	// 	XVK_RayModel_Validate();

	if (g_rtx.reload_pipeline) {
		gEngine.Con_Printf(S_WARN "Reloading RTX shaders/pipelines\n");
		// TODO gracefully handle reload errors: need to change createPipeline, loadShader, VK_PipelineCreate...
		vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
		createPipeline();

		XVK_DenoiserReloadPipeline();
		g_rtx.reload_pipeline = false;
	}

	updateLights();

	if (g_ray_model_state.frame.num_models == 0) {
		const xvk_blit_args blit_args = {
			.cmdbuf = args->cmdbuf,
			.in_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.src = {
				.image = current_frame->denoised.image,
				.width = FRAME_WIDTH,
				.height = FRAME_HEIGHT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			},
			.dst = {
				.image = args->dst.image,
				.width = args->dst.width,
				.height = args->dst.height,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.srcAccessMask = 0,
			},
		};

		clearVkImage( cmdbuf, current_frame->denoised.image );
		blitImage( &blit_args );
	} else {
		prepareTlas(cmdbuf);
		updateDescriptors(cmdbuf, args, current_frame);
		rayTrace(cmdbuf, current_frame, args->fov_angle_y);

		{
			const VkImageMemoryBarrier image_barriers[] = {
#define GBUFFER_READ_BARRIER(img) { \
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, \
			.image = current_frame->img.image, \
			.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, \
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT, \
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL, \
			.newLayout = VK_IMAGE_LAYOUT_GENERAL, \
			.subresourceRange = (VkImageSubresourceRange) { \
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, \
				.baseMipLevel = 0, \
				.levelCount = 1, \
				.baseArrayLayer = 0, \
				.layerCount = 1, \
			}, \
		},
LIST_GBUFFER_IMAGES(GBUFFER_READ_BARRIER)
				{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.image = current_frame->denoised.image,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,
					.subresourceRange =
						(VkImageSubresourceRange){
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1,
						},
			} };
			vkCmdPipelineBarrier(args->cmdbuf,
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
		}

		{
			const xvk_denoiser_args_t denoiser_args = {
				.cmdbuf = cmdbuf,
				.width = FRAME_WIDTH,
				.height = FRAME_HEIGHT,
				.src = {
					.base_color_view = current_frame->base_color.view,
					.diffuse_gi_view = current_frame->diffuse_gi.view,
					.specular_view = current_frame->specular.view,
					.additive_view = current_frame->additive.view,
					.normals_view = current_frame->normals.view,
				},
				.dst_view = current_frame->denoised.view,
			};

			XVK_DenoiserDenoise( &denoiser_args );
		}

		{
			const xvk_blit_args blit_args = {
				.cmdbuf = args->cmdbuf,
				.in_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				.src = {
					.image = current_frame->denoised.image,
					.width = FRAME_WIDTH,
					.height = FRAME_HEIGHT,
					.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
					.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				},
				.dst = {
					.image = args->dst.image,
					.width = args->dst.width,
					.height = args->dst.height,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.srcAccessMask = 0,
				},
			};

			blitImage( &blit_args );
		}
	}
}

static void createLayouts( void ) {
	//VkSampler samplers[MAX_TEXTURES];

	g_rtx.descriptors.bindings = g_rtx.desc_bindings;
	g_rtx.descriptors.num_bindings = ARRAYSIZE(g_rtx.desc_bindings);
	g_rtx.descriptors.values = g_rtx.desc_values;
	g_rtx.descriptors.num_sets = 1;
	g_rtx.descriptors.desc_sets = g_rtx.desc_sets;
	g_rtx.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = sizeof(vk_rtx_push_constants_t),
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Dest_ImageDiffuseGI] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Dest_ImageDiffuseGI,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};
	g_rtx.desc_bindings[RayDescBinding_Dest_ImageAdditive] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Dest_ImageAdditive,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};
	g_rtx.desc_bindings[RayDescBinding_Dest_ImageSpecular] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Dest_ImageSpecular,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};
	g_rtx.desc_bindings[RayDescBinding_Dest_ImageNormals] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Dest_ImageNormals,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Dest_ImageBaseColor] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Dest_ImageBaseColor,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_TLAS] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_TLAS,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_UBOMatrices] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_UBOMatrices,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Kusochki] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Kusochki,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Indices] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Indices,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Vertices] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Vertices,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_Textures] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Textures,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_TEXTURES,
		.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
		// FIXME on AMD using immutable samplers leads to nearest filtering ???!
		.pImmutableSamplers = NULL, //samplers,
	};

	// for (int i = 0; i < ARRAYSIZE(samplers); ++i)
	// 	samplers[i] = vk_core.default_sampler;

	g_rtx.desc_bindings[RayDescBinding_Lights] = (VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_Lights,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
	};

	g_rtx.desc_bindings[RayDescBinding_LightClusters] =	(VkDescriptorSetLayoutBinding){
		.binding = RayDescBinding_LightClusters,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
	};

	VK_DescriptorsCreate(&g_rtx.descriptors);
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

static void reloadLighting( void ) {
	g_rtx.reload_lighting = true;
}


static void freezeModels( void ) {
	g_ray_model_state.freeze_models = !g_ray_model_state.freeze_models;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure

	//g_rtx.sbt_record_size = ALIGN_UP(vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleAlignment);
	g_rtx.sbt_record_size = ALIGN_UP(vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupBaseAlignment);

	if (!createBuffer("ray sbt_buffer", &g_rtx.sbt_buffer, ShaderBindingTable_COUNT * g_rtx.sbt_record_size,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		return false;
	}

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
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
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

	if (!createBuffer("ray kusochki_buffer", &g_ray_model_state.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}
	g_ray_model_state.kusochki_alloc.size = MAX_KUSOCHKI;

	if (!createBuffer("ray lights_buffer", &g_ray_model_state.lights_buffer, sizeof(struct Lights),
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
#define CREATE_GBUFFER_IMAGE(name, format_, add_usage_bits) \
		do { \
			char debug_name[64]; \
			const xvk_image_create_t create = { \
				.debug_name = debug_name, \
				.width = FRAME_WIDTH, \
				.height = FRAME_HEIGHT, \
				.mips = 1, \
				.format = format_, \
				.tiling = VK_IMAGE_TILING_OPTIMAL, \
				.usage = VK_IMAGE_USAGE_STORAGE_BIT | add_usage_bits, \
				.has_alpha = true, \
			}; \
			Q_snprintf(debug_name, sizeof(debug_name), "rtx frames[%d] " # name, i); \
			g_rtx.frames[i].name = XVK_ImageCreate(&create); \
		} while(0)

		CREATE_GBUFFER_IMAGE(denoised, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		CREATE_GBUFFER_IMAGE(base_color, VK_FORMAT_R8G8B8A8_UNORM, 0);
		CREATE_GBUFFER_IMAGE(diffuse_gi, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
		CREATE_GBUFFER_IMAGE(specular, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
		CREATE_GBUFFER_IMAGE(additive, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
		// TODO make sure this format and usage is suppported
		CREATE_GBUFFER_IMAGE(normals, VK_FORMAT_R16G16B16A16_SNORM, 0);
#undef CREATE_GBUFFER_IMAGE
	}

	if (vk_core.debug) {
		gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");
		gEngine.Cmd_AddCommand("vk_rtx_reload_rad", reloadLighting, "Reload RAD files for static lights");
		gEngine.Cmd_AddCommand("vk_rtx_freeze", freezeModels, "Freeze models, do not update/add/delete models from to-draw list");
	}

	return true;
}

void VK_RayShutdown( void ) {
	ASSERT(vk_core.rtx);

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i) {
		XVK_ImageDestroy(&g_rtx.frames[i].denoised);
		XVK_ImageDestroy(&g_rtx.frames[i].base_color);
		XVK_ImageDestroy(&g_rtx.frames[i].diffuse_gi);
		XVK_ImageDestroy(&g_rtx.frames[i].specular);
		XVK_ImageDestroy(&g_rtx.frames[i].additive);
		XVK_ImageDestroy(&g_rtx.frames[i].normals);
	}

	vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
	VK_DescriptorsDestroy(&g_rtx.descriptors);

	if (g_rtx.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.tlas, NULL);

	for (int i = 0; i < ARRAYSIZE(g_ray_model_state.models_cache); ++i) {
		vk_ray_model_t *model = g_ray_model_state.models_cache + i;
		if (model->as != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		model->as = VK_NULL_HANDLE;
	}

	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);
	destroyBuffer(&g_rtx.tlas_geom_buffer);
	destroyBuffer(&g_ray_model_state.kusochki_buffer);
	destroyBuffer(&g_ray_model_state.lights_buffer);
	destroyBuffer(&g_rtx.light_grid_buffer);
	destroyBuffer(&g_rtx.sbt_buffer);
}
