#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_render.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_cvar.h"

#include "eiface.h"

#define MAX_ACCELS 1024
#define MAX_SCRATCH_BUFFER (16*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)

// TODO sync with shaders
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
	float sad_padding_[2];
	vec4_t emissive;
} vk_kusok_data_t;

typedef struct {
	//int lightmap, texture;
	//int render_mode;
	//uint32_t element_count;
	//uint32_t index_offset, vertex_offset;
	//VkBuffer buffer;
	matrix3x4 transform_row;
	VkAccelerationStructureKHR accel;
} vk_ray_model_t;

typedef struct {
	float t;
	int bounces;
} vk_rtx_push_constants_t;

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkDescriptorSetLayout desc_layout;
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_set;

	vk_buffer_t accels_buffer;
	vk_buffer_t scratch_buffer;
	VkDeviceAddress accels_buffer_addr, scratch_buffer_addr;

	vk_buffer_t tlas_geom_buffer;

	vk_buffer_t kusochki_buffer;

	vk_ray_model_t models[MAX_ACCELS];
	VkAccelerationStructureKHR tlas;

	qboolean reload_pipeline;
} g_rtx;

static struct {
	int num_models;
	uint32_t scratch_offset, buffer_offset;
} g_rtx_scene;

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

static VkAccelerationStructureKHR createAndBuildAccelerationStructure(VkCommandBuffer cmdbuf, const VkAccelerationStructureGeometryKHR *geoms, const uint32_t *max_prim_counts, const VkAccelerationStructureBuildRangeInfoKHR **build_ranges, uint32_t n_geoms, VkAccelerationStructureTypeKHR type) {
	VkAccelerationStructureKHR accel;

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = type,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = n_geoms,
		.pGeometries = geoms,
	};

	VkAccelerationStructureBuildSizesInfoKHR build_size = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

	VkAccelerationStructureCreateInfoKHR asci = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = g_rtx.accels_buffer.buffer,
		.offset = g_rtx_scene.buffer_offset,
		.type = type,
	};

	vkGetAccelerationStructureBuildSizesKHR(
		vk_core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, max_prim_counts, &build_size);

	if (0)
	{
		uint32_t max_prims = 0;
		for (int i = 0; i < n_geoms; ++i)
			max_prims += max_prim_counts[i];
		gEngine.Con_Reportf(
			"AS max_prims=%u, n_geoms=%u, build size: %d, scratch size: %d\n", max_prims, n_geoms, build_size.accelerationStructureSize, build_size.buildScratchSize);
	}

	if (MAX_SCRATCH_BUFFER - g_rtx_scene.scratch_offset < build_size.buildScratchSize) {
		gEngine.Con_Printf(S_ERROR "Scratch buffer overflow: left %u bytes, but need %u\n",
			MAX_SCRATCH_BUFFER - g_rtx_scene.scratch_offset,
			build_size.buildScratchSize);
		return VK_NULL_HANDLE;
	}

	if (MAX_ACCELS_BUFFER - g_rtx_scene.buffer_offset < build_size.accelerationStructureSize) {
		gEngine.Con_Printf(S_ERROR "Accels buffer overflow: left %u bytes, but need %u\n",
			MAX_ACCELS_BUFFER - g_rtx_scene.buffer_offset,
			build_size.accelerationStructureSize);
		return VK_NULL_HANDLE;
	}

	asci.size = build_size.accelerationStructureSize;
	XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, &accel));

	// TODO this function has weird semantics: it allocates data in buffers, but doesn't allocate the AS itself
	g_rtx_scene.buffer_offset += build_size.accelerationStructureSize;
	g_rtx_scene.buffer_offset = (g_rtx_scene.buffer_offset + 255) & ~255; // Buffer must be aligned to 256 according to spec

	build_info.dstAccelerationStructure = accel;
	build_info.scratchData.deviceAddress = g_rtx.scratch_buffer_addr + g_rtx_scene.scratch_offset;
	g_rtx_scene.scratch_offset += build_size.buildScratchSize;

	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, build_ranges);
	return accel;
}

static void cleanupASFIXME(void)
{
	// FIXME we really should not do this; cache ASs per model
	for (int i = 0; i < g_rtx_scene.num_models; ++i) {
		if (g_rtx.models[i].accel != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.models[i].accel, NULL);
	}
	if (g_rtx.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.tlas, NULL);

	g_rtx_scene.num_models = 0;
}

void VK_RaySceneBegin( void )
{
	ASSERT(vk_core.rtx);

	// FIXME this buffer might have objects that live longer
	g_rtx_scene.buffer_offset = 0;
	g_rtx_scene.scratch_offset = 0;

	cleanupASFIXME();
}

/*
static vk_ray_model_t *getModelByHandle(vk_ray_model_handle_t handle)
{
}
*/

void VK_RayScenePushModel( VkCommandBuffer cmdbuf, const vk_ray_model_create_t *create_info) // _handle_t model_handle )
{
	vk_ray_model_t* model = g_rtx.models + g_rtx_scene.num_models;
	ASSERT(g_rtx_scene.num_models <= ARRAYSIZE(g_rtx.models));

	if (g_rtx_scene.num_models == ARRAYSIZE(g_rtx.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	ASSERT(vk_core.rtx);

	{
		const VkDeviceAddress buffer_addr = getBufferDeviceAddress(create_info->buffer);
		const uint32_t prim_count = create_info->element_count / 3;
		const VkAccelerationStructureGeometryKHR geom[] = {
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = create_info->index_offset == UINT32_MAX ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT16,
						.maxVertex = create_info->max_vertex,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr + create_info->vertex_offset * sizeof(vk_vertex_t),
						.indexData.deviceAddress = buffer_addr + create_info->index_offset * sizeof(uint16_t),
					},
			} };

		const uint32_t max_prim_counts[ARRAYSIZE(geom)] = { prim_count };
		const VkAccelerationStructureBuildRangeInfoKHR build_range_tri = {
			.primitiveCount = prim_count,
		};
		const VkAccelerationStructureBuildRangeInfoKHR* build_ranges[ARRAYSIZE(geom)] = { &build_range_tri };

		model->accel = createAndBuildAccelerationStructure(cmdbuf,
			geom, max_prim_counts, build_ranges, ARRAYSIZE(geom), VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

		if (!model->accel) {
			gEngine.Con_Printf(S_ERROR "Error building BLAS\n");
			return;
		}

		// Store geometry references in kusochki
		{
			vk_kusok_data_t *kusok = (vk_kusok_data_t*)(g_rtx.kusochki_buffer.mapped) + g_rtx_scene.num_models;
			kusok->vertex_offset = create_info->vertex_offset;
			kusok->index_offset = create_info->index_offset;
			kusok->emissive[0] = create_info->emissive.r;
			kusok->emissive[1] = create_info->emissive.g;
			kusok->emissive[2] = create_info->emissive.b;
		}

		memcpy(model->transform_row, *create_info->transform_row, sizeof(model->transform_row));

		g_rtx_scene.num_models++;
	}
}

static void createPipeline( void )
{
	const vk_pipeline_compute_create_info_t ci = {
		.layout = g_rtx.pipeline_layout,
		.shader_filename = "rtx.comp.spv",
	};

	g_rtx.pipeline = VK_PipelineComputeCreate(&ci);
	ASSERT(g_rtx.pipeline);
}

void VK_RaySceneEnd(const vk_ray_scene_render_args_t* args)
{
	ASSERT(vk_core.rtx);
	ASSERT(args->ubo.size == sizeof(float) * 16 * 2); // ubo should contain two matrices
	const VkCommandBuffer cmdbuf = args->cmdbuf;

	if (g_rtx.reload_pipeline) {
		gEngine.Con_Printf(S_WARN "Reloading RTX shaders/pipelines\n");
		// TODO gracefully handle reload errors: need to change createPipeline, loadShader, VK_PipelineCreate...
		vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
		createPipeline();
		g_rtx.reload_pipeline = false;
	}

	// Upload all blas instances references to GPU mem
	{
		VkAccelerationStructureInstanceKHR *inst = g_rtx.tlas_geom_buffer.mapped;
		for (int i = 0; i < g_rtx_scene.num_models; ++i) {
			const vk_ray_model_t * const model = g_rtx.models + i;
			const matrix3x4 * const m = model->transform_row;
			ASSERT(model->accel != VK_NULL_HANDLE);
			inst[i] = (VkAccelerationStructureInstanceKHR){
				.instanceCustomIndex = i,
				.mask = 0xff,
				.instanceShaderBindingTableRecordOffset = 0,
				.flags = 0,
				.accelerationStructureReference = getASAddress(model->accel), // TODO cache this addr
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
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	// 2. Create TLAS
	{
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

		const uint32_t tl_max_prim_counts[ARRAYSIZE(tl_geom)] = {g_rtx_scene.num_models};
		const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
			.primitiveCount = g_rtx_scene.num_models,
		};
		const VkAccelerationStructureBuildRangeInfoKHR *tl_build_ranges[] = {&tl_build_range};
		g_rtx.tlas = createAndBuildAccelerationStructure(cmdbuf,
			tl_geom, tl_max_prim_counts, tl_build_ranges, ARRAYSIZE(tl_geom), VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
	}

	// 3. Update descriptor sets (bind dest image, tlas, projection matrix)
	{
		const VkDescriptorImageInfo dii_dst = {
			.sampler = VK_NULL_HANDLE,
			.imageView = args->dst.image_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		};
		const VkDescriptorBufferInfo dbi_ubo = {
			.buffer = args->ubo.buffer,
			.offset = args->ubo.offset,
			.range = args->ubo.size,
		};
		const VkDescriptorBufferInfo dbi_kusochki = {
			.buffer = g_rtx.kusochki_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty g_rtx_scene.num_models * sizeof(vk_kusok_data_t),
		};
		const VkDescriptorBufferInfo dbi_indices = {
			.buffer = args->geometry_data.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty args->geometry_data.size,
		};
		const VkDescriptorBufferInfo dbi_vertices = {
			.buffer = args->geometry_data.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE, // TODO fails validation when empty args->geometry_data.size,
		};
		const VkWriteDescriptorSetAccelerationStructureKHR wdsas = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &g_rtx.tlas,
		};
		const VkDescriptorBufferInfo dbi_dlights = {
			.buffer = args->dlights.buffer,
			.offset = args->dlights.offset,
			.range = args->dlights.size,
		};
		const VkWriteDescriptorSet wds[] = {
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.pImageInfo = &dii_dst,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 1,
				.dstArrayElement = 0,
				.pNext = &wdsas,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 2,
				.dstArrayElement = 0,
				.pBufferInfo = &dbi_ubo,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 3,
				.dstArrayElement = 0,
				.pBufferInfo = &dbi_kusochki,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 4,
				.dstArrayElement = 0,
				.pBufferInfo = &dbi_indices,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 5,
				.dstArrayElement = 0,
				.pBufferInfo = &dbi_vertices,
			},
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.dstSet = g_rtx.desc_set,
				.dstBinding = 6,
				.dstArrayElement = 0,
				.pBufferInfo = &dbi_dlights,
			},
		};

		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
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
		}};
		VkImageMemoryBarrier image_barrier[] = { {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = args->dst.image,
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

	// 4. dispatch compute
	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_rtx.pipeline);
	{
		vk_rtx_push_constants_t push_constants = {
			.t = gpGlobals->realtime,
			.bounces = vk_rtx_bounces->value,
		};
		vkCmdPushConstants(cmdbuf, g_rtx.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
	}
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_rtx.pipeline_layout, 0, 1, &g_rtx.desc_set, 0, NULL);
	vkCmdDispatch(cmdbuf, (args->dst.width+WG_W-1)/WG_W, (args->dst.height+WG_H-1)/WG_H, 1);
}

static void createLayouts( void ) {
  VkDescriptorSetLayoutBinding bindings[] = {{
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 2,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 3,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 4,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 5,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 6,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	},
	};

	VkDescriptorSetLayoutCreateInfo dslci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = ARRAYSIZE(bindings), .pBindings = bindings, };

	XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &g_rtx.desc_layout));

	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(vk_rtx_push_constants_t);
	push_const.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	{
		VkPipelineLayoutCreateInfo plci = {0};
		plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plci.setLayoutCount = 1;
		plci.pSetLayouts = &g_rtx.desc_layout;
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &push_const;
		XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g_rtx.pipeline_layout));
	}

	{
		VkDescriptorPoolSize pools[] = {
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1},
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3},
			{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2},
			{.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1},
		};

		VkDescriptorPoolCreateInfo dpci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.maxSets = 1, .poolSizeCount = ARRAYSIZE(pools), .pPoolSizes = pools,
		};
		XVK_CHECK(vkCreateDescriptorPool(vk_core.device, &dpci, NULL, &g_rtx.desc_pool));
	}

	{
		VkDescriptorSetAllocateInfo dsai = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = g_rtx.desc_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &g_rtx.desc_layout,
		};
		XVK_CHECK(vkAllocateDescriptorSets(vk_core.device, &dsai, &g_rtx.desc_set));
	}
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure
	if (!createBuffer(&g_rtx.accels_buffer, MAX_ACCELS_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		))
	{
		return false;
	}
	g_rtx.accels_buffer_addr = getBufferDeviceAddress(g_rtx.accels_buffer.buffer);

	if (!createBuffer(&g_rtx.scratch_buffer, MAX_SCRATCH_BUFFER,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		)) {
		return false;
	}
	g_rtx.scratch_buffer_addr = getBufferDeviceAddress(g_rtx.scratch_buffer.buffer);

	if (!createBuffer(&g_rtx.tlas_geom_buffer, sizeof(VkAccelerationStructureInstanceKHR) * MAX_ACCELS,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!createBuffer(&g_rtx.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_ACCELS,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	createLayouts();
	createPipeline();

	if (vk_core.debug)
		gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");

	return true;
}

void VK_RayShutdown( void )
{
	ASSERT(vk_core.rtx);

	// TODO dealloc all ASes

	vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
	vkDestroyDescriptorPool(vk_core.device, g_rtx.desc_pool, NULL);
	vkDestroyPipelineLayout(vk_core.device, g_rtx.pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, g_rtx.desc_layout, NULL);

	cleanupASFIXME();

	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);
	destroyBuffer(&g_rtx.tlas_geom_buffer);
	destroyBuffer(&g_rtx.kusochki_buffer);
}

