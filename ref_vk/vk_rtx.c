#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_render.h"
#include "vk_buffer.h"

#include "eiface.h"

#define MAX_ACCELS 1024
#define MAX_SCRATCH_BUFFER (16*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)

/*
typedef struct {
	//int lightmap, texture;
	//int render_mode;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	VkBuffer buffer;
} vk_ray_model_t;
*/

static struct {
	/* VkPipelineLayout pipeline_layout; */
	/* VkPipeline rtx_compute_pipeline; */
	/* VkDescriptorPool desc_pool; */
	/* VkDescriptorSet desc_set; */

	vk_buffer_t accels_buffer;
	vk_buffer_t scratch_buffer;
	VkDeviceAddress accels_buffer_addr, scratch_buffer_addr;

	VkAccelerationStructureKHR accels[MAX_ACCELS];
} g_rtx;

static struct {
	int num_accels;
	uint32_t scratch_offset, buffer_offset;
} g_rtx_scene;

static VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(vk_core.device, &bdai);
}

static qboolean createAndBuildAccelerationStructure(VkCommandBuffer cmdbuf, const VkAccelerationStructureGeometryKHR *geoms, const uint32_t *max_prim_counts, const VkAccelerationStructureBuildRangeInfoKHR **build_ranges, uint32_t n_geoms, VkAccelerationStructureTypeKHR type) {
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

	VkAccelerationStructureKHR *handle = g_rtx.accels + g_rtx_scene.num_accels;
	if (g_rtx_scene.num_accels > ARRAYSIZE(g_rtx.accels)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return false;
	}

	vkGetAccelerationStructureBuildSizesKHR(
		vk_core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, max_prim_counts, &build_size);

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
		return false;
	}

	if (MAX_ACCELS_BUFFER - g_rtx_scene.buffer_offset < build_size.accelerationStructureSize) {
		gEngine.Con_Printf(S_ERROR "Accels buffer overflow: left %u bytes, but need %u\n",
			MAX_ACCELS_BUFFER - g_rtx_scene.buffer_offset,
			build_size.accelerationStructureSize);
		return false;
	}

	asci.size = build_size.accelerationStructureSize;
	XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, handle));

	// TODO alignment?
	g_rtx_scene.buffer_offset += build_size.accelerationStructureSize;
	g_rtx_scene.buffer_offset = (g_rtx_scene.buffer_offset + 255) & ~255; // Buffer must be aligned to 256 according to spec
	g_rtx_scene.num_accels++;

	build_info.dstAccelerationStructure = *handle;
	build_info.scratchData.deviceAddress = g_rtx.scratch_buffer_addr + g_rtx_scene.scratch_offset;
	g_rtx_scene.scratch_offset += build_size.buildScratchSize;

	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, build_ranges);
	return true;
}

void VK_RaySceneBegin( void )
{
	ASSERT(vk_core.rtx);
	g_rtx_scene.buffer_offset = 0;
	g_rtx_scene.scratch_offset = 0;

	// FIXME we really should not do this; cache ASs per model
	for (int i = 0; i < g_rtx_scene.num_accels; ++i) {
		if (g_rtx.accels[i] != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.accels[i], NULL);
	}
	g_rtx_scene.num_accels = 0;
}

/*
static vk_ray_model_t *getModelByHandle(vk_ray_model_handle_t handle)
{
}
*/

void VK_RayScenePushModel( VkCommandBuffer cmdbuf, const vk_ray_model_create_t *model) // _handle_t model_handle )
{
	ASSERT(vk_core.rtx);

	{
		//vk_ray_model_t *model = getModelByHandle(model_handle);
		const VkDeviceAddress buffer_addr = getBufferDeviceAddress(model->buffer);
		const uint32_t prim_count = model->element_count / 3;
		const VkAccelerationStructureGeometryKHR geom[] = {
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = model->index_offset == UINT32_MAX ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT16,
						.maxVertex = model->max_vertex,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr + model->vertex_offset,
						.indexData.deviceAddress = buffer_addr + model->index_offset,
					},
			} };

		const uint32_t max_prim_counts[ARRAYSIZE(geom)] = { prim_count };
		const VkAccelerationStructureBuildRangeInfoKHR build_range_tri = {
			.primitiveCount = prim_count,
		};
		const VkAccelerationStructureBuildRangeInfoKHR* build_ranges[ARRAYSIZE(geom)] = { &build_range_tri };

		createAndBuildAccelerationStructure(cmdbuf,
			geom, max_prim_counts, build_ranges, ARRAYSIZE(geom), VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
	}
}

void VK_RaySceneEnd( VkCommandBuffer cmdbuf )
{
	ASSERT(vk_core.rtx);
	// 1. Barrier for building all BLASes
	// 2. Create TLAS
	// 3. Update descriptor sets (bind dest image, tlas, projection matrix)
	// 4. dispatch compute
	// 5. blit to swapchain image // TODO is it more efficient to draw to it as a texture?
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

	return true;
}

void VK_RayShutdown( void )
{
	ASSERT(vk_core.rtx);
	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);

	// TODO dealloc all ASes
}

