#include "vk_ray_accel.h"

#include "vk_core.h"
#include "vk_rtx.h"
#include "vk_ray_internal.h"
#include "r_speeds.h"

#define MAX_SCRATCH_BUFFER (32*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)

struct rt_vk_ray_accel_s g_accel = {0};

static struct {
	struct {
		int blas_count;
		int accels_built;
	} stats;
} g_accel_;

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

	if (MAX_SCRATCH_BUFFER < g_accel.frame.scratch_offset + scratch_buffer_size) {
		gEngine.Con_Printf(S_ERROR "Scratch buffer overflow: left %u bytes, but need %u\n",
			MAX_SCRATCH_BUFFER - g_accel.frame.scratch_offset,
			scratch_buffer_size);
		return false;
	}

	if (should_create) {
		const uint32_t as_size = build_size.accelerationStructureSize;
		const alo_block_t block = aloPoolAllocate(g_accel.accels_buffer_alloc, as_size, /*TODO why? align=*/256);
		const uint32_t buffer_offset = block.offset;
		const VkAccelerationStructureCreateInfoKHR asci = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = g_accel.accels_buffer.buffer,
			.offset = buffer_offset,
			.type = args->type,
			.size = as_size,
		};

		if (buffer_offset == ALO_ALLOC_FAILED) {
			gEngine.Con_Printf(S_ERROR "Failed to allocated %u bytes for accel buffer\n", (uint32_t)asci.size);
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
	build_info.scratchData.deviceAddress = g_accel.scratch_buffer_addr + g_accel.frame.scratch_offset;
	//uint32_t scratch_offset_initial = g_accel.frame.scratch_offset;
	g_accel.frame.scratch_offset += scratch_buffer_size;
	g_accel.frame.scratch_offset = ALIGN_UP(g_accel.frame.scratch_offset, vk_core.physical_device.properties_accel.minAccelerationStructureScratchOffsetAlignment);

	//gEngine.Con_Reportf("AS=%p, n_geoms=%u, scratch: %#x %d %#x\n", *args->p_accel, args->n_geoms, scratch_offset_initial, scratch_buffer_size, scratch_offset_initial + scratch_buffer_size);

	g_accel_.stats.accels_built++;
	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, &args->build_ranges);
	return true;
}

static void createTlas( VkCommandBuffer cmdbuf, VkDeviceAddress instances_addr ) {
	const VkAccelerationStructureGeometryKHR tl_geom[] = {
		{
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			//.flags = VK_GEOMETRY_OPAQUE_BIT,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry.instances =
				(VkAccelerationStructureGeometryInstancesDataKHR){
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
					.data.deviceAddress = instances_addr,
					.arrayOfPointers = VK_FALSE,
				},
		},
	};
	const uint32_t tl_max_prim_counts[COUNTOF(tl_geom)] = { MAX_ACCELS }; //cmdbuf == VK_NULL_HANDLE ? MAX_ACCELS : g_ray_model_state.frame.num_models };
	const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
		.primitiveCount = g_ray_model_state.frame.num_models,
	};
	const as_build_args_t asrgs = {
		.geoms = tl_geom,
		.max_prim_counts = tl_max_prim_counts,
		.build_ranges =  cmdbuf == VK_NULL_HANDLE ? NULL : &tl_build_range,
		.n_geoms = COUNTOF(tl_geom),
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		// we can't really rebuild TLAS because instance count changes are not allowed .dynamic = true,
		.dynamic = false,
		.p_accel = &g_accel.tlas,
		.debug_name = "TLAS",
	};
	if (!createOrUpdateAccelerationStructure(cmdbuf, &asrgs, NULL)) {
		gEngine.Host_Error("Could not create/update TLAS\n");
		return;
	}
}

void RT_VkAccelPrepareTlas(VkCommandBuffer cmdbuf) {
	ASSERT(g_ray_model_state.frame.num_models > 0);
	DEBUG_BEGIN(cmdbuf, "prepare tlas");

	R_FlippingBuffer_Flip( &g_accel.tlas_geom_buffer_alloc );

	const uint32_t instance_offset = R_FlippingBuffer_Alloc(&g_accel.tlas_geom_buffer_alloc, g_ray_model_state.frame.num_models, 1);
	ASSERT(instance_offset != ALO_ALLOC_FAILED);

	// Upload all blas instances references to GPU mem
	{
		VkAccelerationStructureInstanceKHR* inst = ((VkAccelerationStructureInstanceKHR*)g_accel.tlas_geom_buffer.mapped) + instance_offset;
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
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_REGULAR,
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
					break;
				case MaterialMode_Opaque_AlphaTest:
					inst[i].mask = GEOMETRY_BIT_ALPHA_TEST;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_ALPHA_TEST,
					inst[i].flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
					break;
				case MaterialMode_Refractive:
					inst[i].mask = GEOMETRY_BIT_REFRACTIVE;
					inst[i].instanceShaderBindingTableRecordOffset = SHADER_OFFSET_HIT_REGULAR,
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

	g_accel_.stats.blas_count = g_ray_model_state.frame.num_models;

	// Barrier for building all BLASes
	// BLAS building is now in cmdbuf, need to synchronize with results
	{
		VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, // | VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.buffer = g_accel.accels_buffer.buffer,
			.offset = instance_offset * sizeof(VkAccelerationStructureInstanceKHR),
			.size = g_ray_model_state.frame.num_models * sizeof(VkAccelerationStructureInstanceKHR),
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, COUNTOF(bmb), bmb, 0, NULL);
	}

	// 2. Build TLAS
	createTlas(cmdbuf, g_accel.tlas_geom_buffer_addr + instance_offset * sizeof(VkAccelerationStructureInstanceKHR));
	DEBUG_END(cmdbuf);
}

qboolean RT_VkAccelInit(void) {
	if (!VK_BufferCreate("ray accels_buffer", &g_accel.accels_buffer, MAX_ACCELS_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		))
	{
		return false;
	}
	g_accel.accels_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.accels_buffer.buffer);

	if (!VK_BufferCreate("ray scratch_buffer", &g_accel.scratch_buffer, MAX_SCRATCH_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		)) {
		return false;
	}
	g_accel.scratch_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.scratch_buffer.buffer);

	// TODO this doesn't really need to be host visible, use staging
	if (!VK_BufferCreate("ray tlas_geom_buffer", &g_accel.tlas_geom_buffer, sizeof(VkAccelerationStructureInstanceKHR) * MAX_ACCELS * 2,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}
	g_accel.tlas_geom_buffer_addr = R_VkBufferGetDeviceAddress(g_accel.tlas_geom_buffer.buffer);
	R_FlippingBuffer_Init(&g_accel.tlas_geom_buffer_alloc, MAX_ACCELS * 2);

	R_SpeedsRegisterMetric(&g_accel_.stats.blas_count, "blas_count", "");
	R_SpeedsRegisterMetric(&g_accel_.stats.accels_built, "accels_built", "");

	return true;
}

void RT_VkAccelShutdown(void) {
	if (g_accel.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_accel.tlas, NULL);

	for (int i = 0; i < COUNTOF(g_ray_model_state.models_cache); ++i) {
		vk_ray_model_t *model = g_ray_model_state.models_cache + i;
		if (model->as != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, model->as, NULL);
		model->as = VK_NULL_HANDLE;
	}

	VK_BufferDestroy(&g_accel.scratch_buffer);
	VK_BufferDestroy(&g_accel.accels_buffer);
	VK_BufferDestroy(&g_accel.tlas_geom_buffer);
	if (g_accel.accels_buffer_alloc)
		aloPoolDestroy(g_accel.accels_buffer_alloc);
}

void RT_VkAccelNewMap(void) {
	const int expected_accels = 512; // TODO actually get this from playing the game
	const int accels_alignment = 256; // TODO where does this come from?
	ASSERT(vk_core.rtx);

	if (g_accel.accels_buffer_alloc)
		aloPoolDestroy(g_accel.accels_buffer_alloc);
	g_accel.accels_buffer_alloc = aloPoolCreate(MAX_ACCELS_BUFFER, expected_accels, accels_alignment);

	// Clear model cache
	for (int i = 0; i < COUNTOF(g_ray_model_state.models_cache); ++i) {
		vk_ray_model_t *model = g_ray_model_state.models_cache + i;
		VK_RayModelDestroy(model);
	}

	// Recreate tlas
	// Why here and not in init: to make sure that its memory is preserved. Map init will clear all memory regions.
	{
		if (g_accel.tlas != VK_NULL_HANDLE) {
			vkDestroyAccelerationStructureKHR(vk_core.device, g_accel.tlas, NULL);
			g_accel.tlas = VK_NULL_HANDLE;
		}

		createTlas(VK_NULL_HANDLE, g_accel.tlas_geom_buffer_addr);
	}
}

void RT_VkAccelFrameBegin(void) {
	g_accel.frame.scratch_offset = 0;
}
