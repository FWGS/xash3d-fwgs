#include "vk_rtx.h"

#include "vk_core.h"
#include "vk_common.h"
#include "vk_render.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_cvar.h"
#include "vk_textures.h"

#include "eiface.h"
#include "xash3d_mathlib.h"

#include <string.h>

#define MAX_ACCELS 1024
#define MAX_SCRATCH_BUFFER (16*1024*1024)
#define MAX_ACCELS_BUFFER (64*1024*1024)
#define MAX_LIGHT_TEXTURES 256

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
	float sad_padding_[1];
	vec4_t emissive;
} vk_kusok_data_t;

typedef struct {
	uint32_t num_lighttextures;
	uint32_t padding__[3];
	struct {
		// TODO should we move emissive here?
		uint32_t kusok_index;
		uint32_t padding__[3];
	} lighttexture[MAX_LIGHT_TEXTURES];
} vk_lighttexture_data_t;

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

	// TODO this should really be a single uniform buffer for matrices and light data
	vk_buffer_t lighttextures_buffer;

	vk_ray_model_t models[MAX_ACCELS];
	VkAccelerationStructureKHR tlas;

	unsigned frame_number;
	vk_image_t frames[2];

	qboolean reload_pipeline;
} g_rtx;

static struct {
	int num_models;
	int num_lighttextures;
	uint32_t scratch_offset, buffer_offset;
} g_rtx_scene;

static struct {
	vec3_t emissive;
	qboolean set;
} g_rtx_emissive_texture_table_t[MAX_TEXTURES];

static qboolean hack_rad_table_initialized = false;
static struct {
	const char *name;
	int r, g, b, intensity;
} hack_valve_rad_table[] = {
	"+0~GENERIC65", 255, 255, 255, 750,
	"+0~GENERIC85", 110, 140, 235, 20000,
	"+0~GENERIC86", 255, 230, 125, 10000,
	"+0~GENERIC86B", 60, 220, 170, 20000,
	"+0~GENERIC86R", 128, 0, 0, 60000,
	"GENERIC87A", 100, 255, 100, 1000,
	"GENERIC88A", 255, 100, 100, 1000,
	"GENERIC89A", 40, 40, 130, 1000,
	"GENERIC90A", 200, 255, 200, 1000,
	"GENERIC105", 255, 100, 100, 1000,
	"GENERIC106", 120, 120, 100, 1000,
	"GENERIC107", 180, 50, 180, 1000,
	"GEN_VEND1", 50, 180, 50, 1000,
	"EMERGLIGHT", 255, 200, 100, 50000,
	"+0~FIFTS_LGHT01", 160, 170, 220, 4000,
	"+0~FIFTIES_LGT2", 160, 170, 220, 5000,
	"+0~FIFTS_LGHT4", 160, 170, 220, 4000,
	"+0~LIGHT1", 40, 60, 150, 3000,
	"+0~LIGHT3A", 180, 180, 230, 10000,
	"+0~LIGHT4A", 200, 190, 130, 11000,
	"+0~LIGHT5A", 80, 150, 200, 10000,
	"+0~LIGHT6A", 150, 5, 5, 25000,
	"+0~TNNL_LGT1", 240, 230, 100, 10000,
	"+0~TNNL_LGT2", 190, 255, 255, 12000,
	"+0~TNNL_LGT3", 150, 150, 210, 17000,
	"+0~TNNL_LGT4", 170, 90, 40, 10000,
	"+0LAB1_W6D", 165, 230, 255, 4000,
	"+0LAB1_W6", 150, 160, 210, 8800,
	"+0LAB1_W7", 245, 240, 210, 4000,
	"SKKYLITE", 165, 230, 255, 1000,
	"+0~DRKMTLS1", 205, 0, 0, 6000,
	"+0~DRKMTLGT1", 200, 200, 180, 6000,
	"+0~DRKMTLS2", 150, 120, 20, 30000,
	"+0~DRKMTLS2C", 255, 200, 100, 50000,
	"+0DRKMTL_SCRN", 60, 80, 255, 10000,
	"~LAB_CRT9A", 225, 150, 150, 100,
	"~LAB_CRT9B", 100, 100, 255, 100,
	"~LAB_CRT9C", 100, 200, 150, 100,
	"~LIGHT3A", 190, 20, 20, 3000,
	"~LIGHT3B", 155, 155, 235, 2000,
	"~LIGHT3C", 220, 210, 150, 2500,
	"~LIGHT3E", 90, 190, 140, 6000,
	"C1A3C_MAP", 100, 100, 255, 100,
	"FIFTIES_MON1B", 100, 100, 180, 30,
	"+0~LAB_CRT8", 50, 50, 255, 100,
	"ELEV2_CIEL", 255, 200, 100, 800,
	"YELLOW", 255, 200, 100, 2000,
	"RED", 255, 0, 0, 1000,
	"+0~GENERIC65", 255, 255, 255, 14000,
	"+0~LIGHT3A", 255, 255, 255, 25000,
	"~LIGHT3B", 84, 118, 198, 14000,
	"+0~DRKMTLS2C", 255, 200, 100, 10,
	"~LIGHT3A", 190, 20, 20, 14000,
	"~LIGHT3C", 198, 215, 74, 14000,
	"+0~LIGHT4A", 231, 223, 82, 20000,
	"+0~FIFTS_LGHT06", 255, 255, 255, 8000,
	"+0~FIFTIES_LGT2", 255, 255, 255, 20000,
	"~SPOTYELLOW", 189, 231, 253, 20000,
	"~SPOTBLUE", 7, 163, 245, 18000,
	"+0~DRKMTLS1", 255, 10, 10, 14000,
	"CRYS_2TOP", 171, 254, 168, 14000,
	"+0~GENERIC85", 11000, 16000, 22000, 255,
	"DRKMTL_SCRN3", 1, 111, 220, 500,
	"+0~LIGHT3A", 255, 255, 255, 25000,
	"~LIGHT3B", 84, 118, 198, 14000,
	"+0~DRKMTLS2C", 255, 200, 100, 10,
	"~LIGHT3A", 190, 20, 20, 14000,
	"~LIGHT3C", 198, 215, 74, 14000,
	"+0~LIGHT4A", 231, 223, 82, 20000,
	"+0~FIFTS_LGHT06", 255, 255, 255, 8000,
	"+0~FIFTIES_LGT2", 255, 255, 255, 20000,
	"~SPOTYELLOW", 189, 231, 253, 20000,
	"+0~DRKMTLS1", 255, 10, 10, 14000,
	"LITEPANEL1", 190, 170, 120, 2500,
	"+0BUTTONLITE", 255, 255, 255, 25,
	"+ABUTTONLITE", 255, 255, 255, 25,
	"+0~FIFTS_LGHT3", 160, 170, 220, 4000,
	"~LIGHT5F", 200, 190, 140, 2500,
	"+A~FIFTIES_LGT2", 160, 170, 220, 4000,
	"~LIGHT3F", 200, 190, 140, 2500,
	"~SPOTBLUE", 7, 163, 245, 18000,
	"+0~FIFTS_LGHT5", 255, 255, 255, 10000,
	"+0~LAB1_CMP2", 255, 255, 255, 20,
	"LAB1_COMP3D", 255, 255, 255, 20,
	"~LAB1_COMP7", 255, 255, 255, 20,
	"+0~GENERIC65", 255, 255, 255, 750,
	"+0~LAB1_CMP2", 255, 255, 255, 20,
	"LAB1_COMP3D", 255, 255, 255, 20,
	"~LAB1_COMP7", 255, 255, 255, 20,
};

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
	g_rtx_scene.num_lighttextures = 0;
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

	if (!hack_rad_table_initialized) {
		hack_rad_table_initialized = true;
		for (int i = 0; i < ARRAYSIZE(hack_valve_rad_table); ++i) {
			const float scale = hack_valve_rad_table[i].intensity / (255.f * 255.f);
			int tex_id;
			char name[256];
			Q_sprintf(name, "halflife.wad/%s.mip", hack_valve_rad_table[i].name);
			tex_id = VK_FindTexture(name);
			if (!tex_id)
				continue;
			ASSERT(tex_id < MAX_TEXTURES);

			g_rtx_emissive_texture_table_t[tex_id].emissive[0] = hack_valve_rad_table[i].r * scale;
			g_rtx_emissive_texture_table_t[tex_id].emissive[1] = hack_valve_rad_table[i].g * scale;
			g_rtx_emissive_texture_table_t[tex_id].emissive[2] = hack_valve_rad_table[i].b * scale;
			g_rtx_emissive_texture_table_t[tex_id].set = true;
		}
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
			ASSERT(create_info->element_count % 3 == 0);
			kusok->triangles = create_info->element_count / 3;

			ASSERT(create_info->texture_id < MAX_TEXTURES);
			if (create_info->texture_id >= 0 && g_rtx_emissive_texture_table_t[create_info->texture_id].set) {
				VectorCopy(g_rtx_emissive_texture_table_t[create_info->texture_id].emissive, kusok->emissive);
			} else {
				kusok->emissive[0] = create_info->emissive.r;
				kusok->emissive[1] = create_info->emissive.g;
				kusok->emissive[2] = create_info->emissive.b;
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
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	const vk_image_t* frame_src = g_rtx.frames + ((g_rtx.frame_number+1)%2);
	const vk_image_t* frame_dst = g_rtx.frames + (g_rtx.frame_number%2);

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
		VkAccelerationStructureInstanceKHR *inst = g_rtx.tlas_geom_buffer.mapped;
		for (int i = 0; i < g_rtx_scene.num_models; ++i) {
			const vk_ray_model_t * const model = g_rtx.models + i;
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
		const VkDescriptorBufferInfo dbi_lighttextures = {
			.buffer = g_rtx.lighttextures_buffer.buffer,
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
				.pBufferInfo = &dbi_lighttextures,
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
	vkCmdDispatch(cmdbuf, (FRAME_WIDTH+WG_W-1)/WG_W, (FRAME_HEIGHT+WG_H-1)/WG_H, 1);

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
			{.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3},
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

	if (!createBuffer(&g_rtx.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_ACCELS,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
		// FIXME complain, handle
		return false;
	}

	if (!createBuffer(&g_rtx.lighttextures_buffer, sizeof(vk_lighttexture_data_t),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT /* | VK_BUFFER_USAGE_TRANSFER_DST_BIT */,
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

	// TODO dealloc all ASes
	cleanupASFIXME();

	destroyBuffer(&g_rtx.scratch_buffer);
	destroyBuffer(&g_rtx.accels_buffer);
	destroyBuffer(&g_rtx.tlas_geom_buffer);
	destroyBuffer(&g_rtx.kusochki_buffer);
	destroyBuffer(&g_rtx.lighttextures_buffer);
}

