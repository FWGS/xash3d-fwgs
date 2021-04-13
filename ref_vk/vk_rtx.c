#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_render.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_cvar.h"
#include "vk_textures.h"
#include "vk_light.h"

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
	uint32_t leaf;
	uint32_t num_dlights;
	uint32_t num_surface_lights;
	uint32_t emissive;
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

typedef struct {
	matrix3x4 transform_row;
	VkAccelerationStructureKHR accel;
	uint32_t kusochki_offset;
} vk_ray_model_t;

typedef struct {
	float t;
	int bounces;
	float prev_frame_blend_factor;
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

	// TODO this should really be a single uniform buffer for matrices and light data
	vk_buffer_t emissive_kusochki_buffer;
	vk_buffer_t light_leaves_buffer;

	VkAccelerationStructureKHR tlas;

	// Data that is alive longer than one frame, usually within one map
	struct {
		uint32_t buffer_offset;
		int num_kusochki;
	} map;

	// Per-frame data that is accumulated between RayFrameBegin and End calls
	struct {
		int num_models;
		int num_lighttextures;
		vk_ray_model_t models[MAX_ACCELS];
		uint32_t scratch_offset; // for building dynamic blases
	} frame;

	unsigned frame_number;
	vk_image_t frames[2];

	qboolean reload_pipeline;

	// HACK: we don't have a way to properly destroy all models and their Vulkan objects on shutdown.
	// This makes validation layers unhappy. Remember created objects here and destroy them manually.
	VkAccelerationStructureKHR blases[MAX_ACCELS];
} g_rtx;

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
	VkAccelerationStructureKHR *accel;
	const VkAccelerationStructureGeometryKHR *geoms;
	const uint32_t *max_prim_counts;
	const VkAccelerationStructureBuildRangeInfoKHR **build_ranges;
	uint32_t n_geoms;
	VkAccelerationStructureTypeKHR type;
	qboolean dynamic;
} as_build_args_t;

static qboolean createOrUpdateAccelerationStructure(VkCommandBuffer cmdbuf, const as_build_args_t *args) {
	const qboolean should_create = *args->accel == VK_NULL_HANDLE;
	const qboolean is_update = false; // TODO: can allow updates only if we know that we only touch vertex positions essentially
	// (no geometry/instance count, flags, etc changes are allowed by the spec)

	VkAccelerationStructureBuildGeometryInfoKHR build_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = args->type,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | ( args->dynamic ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0),
		.mode =  is_update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = args->n_geoms,
		.pGeometries = args->geoms,
		.srcAccelerationStructure = *args->accel,
	};

	VkAccelerationStructureBuildSizesInfoKHR build_size = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};

	uint32_t scratch_buffer_size = 0;

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

	if (MAX_SCRATCH_BUFFER - g_rtx.frame.scratch_offset < scratch_buffer_size) {
		gEngine.Con_Printf(S_ERROR "Scratch buffer overflow: left %u bytes, but need %u\n",
			MAX_SCRATCH_BUFFER - g_rtx.frame.scratch_offset,
			scratch_buffer_size);
		return false;
	}

	if (should_create) {
		VkAccelerationStructureCreateInfoKHR asci = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = g_rtx.accels_buffer.buffer,
			.offset = g_rtx.map.buffer_offset,
			.type = args->type,
			.size = build_size.accelerationStructureSize,
		};

		if (MAX_ACCELS_BUFFER - g_rtx.map.buffer_offset < build_size.accelerationStructureSize) {
			gEngine.Con_Printf(S_ERROR "Accels buffer overflow: left %u bytes, but need %u\n",
				MAX_ACCELS_BUFFER - g_rtx.map.buffer_offset,
				build_size.accelerationStructureSize);
			return false;
		}

		XVK_CHECK(vkCreateAccelerationStructureKHR(vk_core.device, &asci, NULL, args->accel));

		g_rtx.map.buffer_offset += build_size.accelerationStructureSize;
		g_rtx.map.buffer_offset = (g_rtx.map.buffer_offset + 255) & ~255; // Buffer must be aligned to 256 according to spec
	}

	build_info.dstAccelerationStructure = *args->accel;
	build_info.scratchData.deviceAddress = g_rtx.scratch_buffer_addr + g_rtx.frame.scratch_offset;
	g_rtx.frame.scratch_offset += scratch_buffer_size;

	vkCmdBuildAccelerationStructuresKHR(cmdbuf, 1, &build_info, args->build_ranges);
	return true;
}

void VK_RayNewMap( void ) {
	ASSERT(vk_core.rtx);

	g_rtx.map.buffer_offset = 0;
	g_rtx.map.num_kusochki = 0;

	// Upload light leaves
	ASSERT(g_lights.num_leaves <= MAX_LIGHT_LEAVES);
	memcpy(g_rtx.light_leaves_buffer.mapped, g_lights.leaves, g_lights.num_leaves * sizeof(vk_light_leaf_t));

	// Upload emissive kusochki
	{
		vk_emissive_kusochki_t *ek = g_rtx.emissive_kusochki_buffer.mapped;
		ASSERT(g_lights.num_emissive_surfaces <= MAX_EMISSIVE_KUSOCHKI);
		memset(ek, 0, sizeof(*ek));
		ek->num_kusochki = g_lights.num_emissive_surfaces;
		// for (int i = 0; i < g_lights.num_emissive_surfaces; ++i) {
		// 	VectorCopy(g_lights.emissive_surfaces[i].emissive, ek->kusochki[i].emissive_color);
		// 	ek->kusochki[i].kusok_index = g_lights.emissive_surfaces[i].surface_index;
		// }
	}
}

void VK_RayFrameBegin( void )
{
	ASSERT(vk_core.rtx);

	g_rtx.frame.scratch_offset = 0;
	g_rtx.frame.num_models = 0;
	g_rtx.frame.num_lighttextures = 0;
}

void VK_RayFrameAddModelDynamic( VkCommandBuffer cmdbuf, const vk_ray_model_dynamic_t *dynamic)
{
	PRINT_NOT_IMPLEMENTED();

#if 0
	vk_ray_model_t* model = g_rtx.models + g_rtx_scene.num_models;
	ASSERT(g_rtx_scene.num_models <= ARRAYSIZE(g_rtx.models));

	if (g_rtx_scene.num_models == ARRAYSIZE(g_rtx.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	ASSERT(vk_core.rtx);

	{
		const VkDeviceAddress buffer_addr = getBufferDeviceAddress(dynamic->buffer);
		const uint32_t prim_count = dynamic->element_count / 3;
		const VkAccelerationStructureGeometryKHR geom[] = {
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = dynamic->index_offset == UINT32_MAX ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT16,
						.maxVertex = dynamic->max_vertex,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr + dynamic->vertex_offset * sizeof(vk_vertex_t),
						.indexData.deviceAddress = buffer_addr + dynamic->index_offset * sizeof(uint16_t),
					},
			} };

		const uint32_t max_prim_counts[ARRAYSIZE(geom)] = { prim_count };
		const VkAccelerationStructureBuildRangeInfoKHR build_range_tri = {
			.primitiveCount = prim_count,
		};
		const VkAccelerationStructureBuildRangeInfoKHR* build_ranges[ARRAYSIZE(geom)] = { &build_range_tri };

		model->accel = createOrUpdateAccelerationStructure(cmdbuf,
			geom, max_prim_counts, build_ranges, ARRAYSIZE(geom), VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

		if (!model->accel) {
			gEngine.Con_Printf(S_ERROR "Error building BLAS\n");
			return;
		}

		// Store geometry references in kusochki
		{
			vk_kusok_data_t *kusok = (vk_kusok_data_t*)(g_rtx.kusochki_buffer.mapped) + g_rtx_scene.num_models;
			kusok->vertex_offset = dynamic->vertex_offset;
			kusok->index_offset = dynamic->index_offset;
			ASSERT(dynamic->element_count % 3 == 0);
			kusok->triangles = dynamic->element_count / 3;

			ASSERT(dynamic->texture_id < MAX_TEXTURES);
			if (dynamic->texture_id >= 0 && g_emissive_texture_table[dynamic->texture_id].set) {
				VectorCopy(g_emissive_texture_table[dynamic->texture_id].emissive, kusok->emissive);
			} else {
				kusok->emissive[0] = dynamic->emissive.r;
				kusok->emissive[1] = dynamic->emissive.g;
				kusok->emissive[2] = dynamic->emissive.b;
			}

			if (kusok->emissive[0] > 0 || kusok->emissive[1] > 0 || kusok->emissive[2] > 0) {
				if (g_rtx_scene.num_lighttextures < MAX_LIGHT_TEXTURES) {
					vk_lighttexture_data_t *ltd = (vk_lighttexture_data_t*)g_rtx.lighttextures_buffer.mapped;
					ltd->lighttexture[g_rtx_scene.num_lighttextures].kusok_index = g_rtx_scene.num_models;
					g_rtx_scene.num_lighttextures++;
					ltd->num_lighttextures = g_rtx_scene.num_lighttextures;
				} else {
					gEngine.Con_Printf(S_ERROR "Ran out of light textures space");
				}
			}
		}

		memcpy(model->transform_row, *dynamic->transform_row, sizeof(model->transform_row));

		g_rtx_scene.num_models++;
	}
#endif
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

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	const vk_image_t* frame_src = g_rtx.frames + ((g_rtx.frame_number + 1) % 2);
	const vk_image_t* frame_dst = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself
	ASSERT(args->ubo.size == sizeof(float) * 16 * 2);

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
			const vk_ray_model_t* const model = g_rtx.frame.models + i;
			ASSERT(model->accel != VK_NULL_HANDLE);
			inst[i] = (VkAccelerationStructureInstanceKHR){
				.instanceCustomIndex = model->kusochki_offset,
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
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	// 2. Create TLAS
	if (g_rtx.frame.num_models > 0)
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
		const uint32_t tl_max_prim_counts[ARRAYSIZE(tl_geom)] = { MAX_ACCELS };
		const VkAccelerationStructureBuildRangeInfoKHR tl_build_range = {
			.primitiveCount = g_rtx.frame.num_models,
		};
		const VkAccelerationStructureBuildRangeInfoKHR* tl_build_ranges[] = { &tl_build_range };
		const as_build_args_t asrgs = {
			.geoms = tl_geom,
			.max_prim_counts = tl_max_prim_counts,
			.build_ranges = tl_build_ranges,
			.n_geoms = ARRAYSIZE(tl_geom),
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.dynamic = true,
			.accel = &g_rtx.tlas,
		};
		if (!createOrUpdateAccelerationStructure(cmdbuf, &asrgs)) {
			gEngine.Host_Error("Could not create/update TLAS\n");
			return;
		}
	}

	if (g_rtx.tlas != VK_NULL_HANDLE)
	{
		// 3. Update descriptor sets (bind dest image, tlas, projection matrix)
		{
			const VkDescriptorImageInfo dii_dst = {
				.sampler = VK_NULL_HANDLE,
				.imageView = frame_dst->view,
				.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			};
			const VkDescriptorImageInfo dii_src = {
				.sampler = VK_NULL_HANDLE,
				.imageView = frame_src->view,
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
			const VkDescriptorBufferInfo dbi_emissive_kusochki = {
				.buffer = g_rtx.emissive_kusochki_buffer.buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
			};
			const VkDescriptorBufferInfo dbi_light_leaves = {
				.buffer = g_rtx.light_leaves_buffer.buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE,
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
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.dstSet = g_rtx.desc_set,
					.dstBinding = 7,
					.dstArrayElement = 0,
					.pBufferInfo = &dbi_emissive_kusochki,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.dstSet = g_rtx.desc_set,
					.dstBinding = 8,
					.dstArrayElement = 0,
					.pImageInfo = &dii_src,
				},
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					.dstSet = g_rtx.desc_set,
					.dstBinding = 9,
					.dstArrayElement = 0,
					.pBufferInfo = &dbi_light_leaves,
				},
			};

			vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
		}
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
			vkCmdPushConstants(cmdbuf, g_rtx.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);
		}
		vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_rtx.pipeline_layout, 0, 1, &g_rtx.desc_set, 0, NULL);
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
	}, {
		.binding = 7,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 8,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	}, {
		.binding = 9,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	},
	};

	VkDescriptorSetLayoutCreateInfo dslci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = ARRAYSIZE(bindings), .pBindings = bindings, };

	VkPushConstantRange push_const = {0};
	push_const.offset = 0;
	push_const.size = sizeof(vk_rtx_push_constants_t);
	push_const.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &g_rtx.desc_layout));

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
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2},
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4},
			{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 3},
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

	if (!createBuffer(&g_rtx.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}
	if (!createBuffer(&g_rtx.emissive_kusochki_buffer, sizeof(vk_emissive_kusochki_t),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!createBuffer(&g_rtx.light_leaves_buffer, sizeof(vk_light_leaf_t) * MAX_LIGHT_LEAVES,
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

	if (vk_core.debug)
		gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");

	return true;
}

void VK_RayShutdown( void )
{
	ASSERT(vk_core.rtx);

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i)
		VK_ImageDestroy(g_rtx.frames + i);

	vkDestroyPipeline(vk_core.device, g_rtx.pipeline, NULL);
	vkDestroyDescriptorPool(vk_core.device, g_rtx.desc_pool, NULL);
	vkDestroyPipelineLayout(vk_core.device, g_rtx.pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(vk_core.device, g_rtx.desc_layout, NULL);

	if (g_rtx.tlas != VK_NULL_HANDLE)
		vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.tlas, NULL);

	for (int i = 0; i < ARRAYSIZE(g_rtx.blases); ++i) {
		if (g_rtx.blases[i] != VK_NULL_HANDLE)
			vkDestroyAccelerationStructureKHR(vk_core.device, g_rtx.blases[i], NULL);
	}

	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);
	destroyBuffer(&g_rtx.tlas_geom_buffer);
	destroyBuffer(&g_rtx.kusochki_buffer);
	destroyBuffer(&g_rtx.emissive_kusochki_buffer);
	destroyBuffer(&g_rtx.light_leaves_buffer);
}

qboolean VK_RayModelInit( vk_ray_model_init_t args ) {
	VkAccelerationStructureGeometryKHR *geoms;
	uint32_t *geom_max_prim_counts;
	VkAccelerationStructureBuildRangeInfoKHR *geom_build_ranges;
	VkAccelerationStructureBuildRangeInfoKHR **geom_build_ranges_ptr;
	const VkDeviceAddress buffer_addr = getBufferDeviceAddress(args.buffer);
	vk_kusok_data_t *kusochki;
	qboolean result;

	ASSERT(vk_core.rtx);
	ASSERT(g_rtx.map.num_kusochki <= MAX_KUSOCHKI);

	if (g_rtx.map.num_kusochki == MAX_KUSOCHKI) {
		gEngine.Con_Printf(S_ERROR "Maximum number of kusochki exceeded\n");
		return false;
	}

	geoms = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geoms));
	geom_max_prim_counts = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_max_prim_counts));
	geom_build_ranges = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));
	geom_build_ranges_ptr = Mem_Malloc(vk_core.pool, args.model->num_geometries * sizeof(*geom_build_ranges));

	kusochki = (vk_kusok_data_t*)(g_rtx.kusochki_buffer.mapped) + g_rtx.map.num_kusochki;
	args.model->rtx.kusochki_offset = g_rtx.map.num_kusochki;

	for (int i = 0; i < args.model->num_geometries; ++i) {
		const vk_render_geometry_t *mg = args.model->geometries + i;
		const uint32_t prim_count = mg->element_count / 3;
		const uint32_t vertex_offset = args.vertex_offset + mg->vertex_offset;
		const uint32_t index_offset = args.index_offset + mg->index_offset;

		geom_max_prim_counts[i] = prim_count;
		geoms[i] = (VkAccelerationStructureGeometryKHR)
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
				.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
				.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
				.geometry.triangles =
					(VkAccelerationStructureGeometryTrianglesDataKHR){
						.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
						.indexType = args.index_offset == UINT32_MAX ? VK_INDEX_TYPE_NONE_KHR : VK_INDEX_TYPE_UINT16,
						.maxVertex = mg->vertex_count,
						.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
						.vertexStride = sizeof(vk_vertex_t),
						.vertexData.deviceAddress = buffer_addr + vertex_offset * sizeof(vk_vertex_t),
						.indexData.deviceAddress = buffer_addr + index_offset * sizeof(uint16_t),
					},
			};

		geom_build_ranges[i] = (VkAccelerationStructureBuildRangeInfoKHR) {
			.primitiveCount = prim_count,
		};
		geom_build_ranges_ptr[i] = geom_build_ranges + i;

		kusochki[i].vertex_offset = vertex_offset;
		kusochki[i].index_offset = index_offset;
		kusochki[i].triangles = prim_count;
		kusochki[i].leaf = mg->leaf;
		kusochki[i].emissive = ((mg->texture >= 0 && mg->texture < MAX_TEXTURES)
			? g_emissive_texture_table[mg->texture].set
			: 0);

		if (mg->leaf >= 0 && mg->leaf < g_lights.num_leaves)
		{
			const vk_light_leaf_t* leaflight = g_lights.leaves + mg->leaf;
			kusochki[i].num_dlights = leaflight->num_dlights;
			kusochki[i].num_surface_lights = leaflight->num_slights;
		} else {
			kusochki[i].num_dlights = kusochki[i].num_surface_lights = 0;
		}

		if (kusochki[i].emissive && mg->surface_index >= 0) {
			vk_emissive_kusochki_t *ek = g_rtx.emissive_kusochki_buffer.mapped;
			for (int j = 0; j < g_lights.num_emissive_surfaces; ++j) {
				if (mg->surface_index == g_lights.emissive_surfaces[j].surface_index) {
					VectorCopy(g_lights.emissive_surfaces[j].emissive, ek->kusochki[j].emissive_color);
					ek->kusochki[j].kusok_index = i + args.model->rtx.kusochki_offset;
					break;
				}
			}
		}
	}

	{
		const as_build_args_t asrgs = {
			.geoms = geoms,
			.max_prim_counts = geom_max_prim_counts,
			.build_ranges = geom_build_ranges_ptr,
			.n_geoms = args.model->num_geometries,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.dynamic = false, //model->dynamic,
			.accel = &args.model->rtx.blas,
		};

		// TODO batch building multiple blases together
		const VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));

		result = createOrUpdateAccelerationStructure(vk_core.cb, &asrgs);

		XVK_CHECK(vkEndCommandBuffer(vk_core.cb));
	}

	{
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &vk_core.cb,
		};
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
		g_rtx.frame.scratch_offset = 0;
	}

	Mem_Free(geom_build_ranges);
	Mem_Free(geom_max_prim_counts);
	Mem_Free(geoms);

	if (result) {
		int blas_index;
		g_rtx.map.num_kusochki += args.model->num_geometries;

		for (blas_index = 0; blas_index < ARRAYSIZE(g_rtx.blases); ++blas_index) {
			if (g_rtx.blases[blas_index] == VK_NULL_HANDLE) {
				g_rtx.blases[blas_index] = args.model->rtx.blas;
				break;
			}
		}

		if (blas_index == ARRAYSIZE(g_rtx.blases))
			gEngine.Con_Printf(S_WARN "Too many BLASes created :(\n");
	}

	gEngine.Con_Reportf("Model %s (%p) created blas %p\n", args.model->debug_name, args.model, args.model->rtx.blas);

	return result;
}

void VK_RayModelDestroy( struct vk_render_model_s *model ) {
	ASSERT(vk_core.rtx);
	if (model->rtx.blas != VK_NULL_HANDLE) {
		int blas_index;
		for (blas_index = 0; blas_index < ARRAYSIZE(g_rtx.blases); ++blas_index) {
			if (g_rtx.blases[blas_index] == model->rtx.blas) {
				g_rtx.blases[blas_index] = VK_NULL_HANDLE;
				break;
			}
		}
		if (blas_index == ARRAYSIZE(g_rtx.blases))
			gEngine.Con_Printf(S_WARN "Model BLAS was missing\n");

		vkDestroyAccelerationStructureKHR(vk_core.device, model->rtx.blas, NULL);
		model->rtx.blas = VK_NULL_HANDLE;
	}
}

void VK_RayFrameAddModel( const struct vk_render_model_s *model, const matrix3x4 *transform_row ) {
	ASSERT(vk_core.rtx);

	ASSERT(g_rtx.frame.num_models <= ARRAYSIZE(g_rtx.frame.models));

	if (g_rtx.frame.num_models == ARRAYSIZE(g_rtx.frame.models)) {
		gEngine.Con_Printf(S_ERROR "Ran out of AccelerationStructure slots\n");
		return;
	}

	{
		vk_ray_model_t* ray_model = g_rtx.frame.models + g_rtx.frame.num_models;
		ASSERT(model->rtx.blas != VK_NULL_HANDLE);
		ray_model->accel = model->rtx.blas;
		ray_model->kusochki_offset = model->rtx.kusochki_offset;
		memcpy(ray_model->transform_row, *transform_row, sizeof(ray_model->transform_row));
		g_rtx.frame.num_models++;
	}
}
