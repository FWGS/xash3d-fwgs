#include "vk_ray_light_direct.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "shaders/ray_interop.h"


enum {
	// TODO set 0 ?
	RtLDir_Desc_TLAS,
	RtLDir_Desc_UBO,
	RtLDir_Desc_Kusochki,
	RtLDir_Desc_Indices,
	RtLDir_Desc_Vertices,
	RtLDir_Desc_Textures,

	// TODO set 1
	RtLDir_Desc_Lights,
	RtLDir_Desc_LightClusters,

	// TODO set 1
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X

	RtLDir_Desc_COUNT
};

#define LIST_SHADER_MODULES(X) \
	X(RayGen, "ray_light_poly_direct.rgen", RAYGEN) \
	X(AlphaTest, "ray_common_alphatest.rahit", ANY_HIT) \
	X(Miss, "ray_shadow.rmiss", MISS) \

static struct {
	struct {
		vk_descriptors_t riptors;
		VkDescriptorSetLayoutBinding bindings[RtLDir_Desc_COUNT];
		vk_descriptor_value_t values[RtLDir_Desc_COUNT];

		// TODO: split into two sets, one common to all rt passes (tlas, kusochki, etc), another one this pass only
		VkDescriptorSet sets[1];
	} desc;

	vk_pipeline_ray_t pipeline;
} g_ray_light_direct;

static void initDescriptors( void ) {
	g_ray_light_direct.desc.riptors = (vk_descriptors_t) {
		.bindings = g_ray_light_direct.desc.bindings,
		.num_bindings = COUNTOF(g_ray_light_direct.desc.bindings),
		.values = g_ray_light_direct.desc.values,
		.num_sets = COUNTOF(g_ray_light_direct.desc.sets),
		.desc_sets = g_ray_light_direct.desc.sets,
		/* .push_constants = (VkPushConstantRange){ */
		/* 	.offset = 0, */
		/* 	.size = sizeof(vk_rtx_push_constants_t), */
		/* 	.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, */
		/* }, */
	};

	// FIXME more conservative shader stages
#define INIT_BINDING(index, name, type, count) \
	g_ray_light_direct.desc.bindings[RtLDir_Desc_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, \
	}

	INIT_BINDING(1, TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
	INIT_BINDING(2, UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(3, Kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(4, Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(5, Vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(6, Textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES);
	INIT_BINDING(7, Lights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(8, LightClusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

//#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X
#undef INIT_BINDING

	VK_DescriptorsCreate(&g_ray_light_direct.desc.riptors);
}

static void updateDescriptors( const vk_ray_resources_t *res ) {
#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X

	g_ray_light_direct.desc.values[RtLDir_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &res->scene.tlas,
	};

#define DESC_SET_BUFFER(index, buffer_) \
	g_ray_light_direct.desc.values[index].buffer = (VkDescriptorBufferInfo){ \
		.buffer = res->scene.buffer_.buffer, \
		.offset = res->scene.buffer_.offset, \
		.range = res->scene.buffer_.size, \
	}

	DESC_SET_BUFFER(RtLDir_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtLDir_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtLDir_Desc_Indices, indices);
	DESC_SET_BUFFER(RtLDir_Desc_Vertices, vertices);
	DESC_SET_BUFFER(RtLDir_Desc_Lights, lights);
	DESC_SET_BUFFER(RtLDir_Desc_LightClusters, light_clusters);

#undef DESC_SET_BUFFER

	g_ray_light_direct.desc.values[RtLDir_Desc_Textures].image_array = res->scene.all_textures;

	VK_DescriptorsWrite(&g_ray_light_direct.desc.riptors);
}

static vk_pipeline_ray_t createPipeline( void ) {
	// FIXME move this into vk_pipeline
	const struct SpecializationData {
		uint32_t sbt_record_size;
	} spec_data = {
		.sbt_record_size = vk_core.physical_device.sbt_record_size,
	};
	const VkSpecializationMapEntry spec_map[] = {
		{.constantID = SPEC_SBT_RECORD_SIZE_INDEX, .offset = offsetof(struct SpecializationData, sbt_record_size), .size = sizeof(uint32_t) },
	};
	VkSpecializationInfo spec = {
		.mapEntryCount = COUNTOF(spec_map),
		.pMapEntries = spec_map,
		.dataSize = sizeof(spec_data),
		.pData = &spec_data,
	};

	enum {
#define X(name, file, type) \
		ShaderStageIndex_##name,
		LIST_SHADER_MODULES(X)
#undef X
	};
const vk_shader_stage_t stages[] = {
#define X(name, file, type) \
		{.filename = file ".spv", .stage = VK_SHADER_STAGE_##type##_BIT_KHR, .specialization_info = &spec},
		LIST_SHADER_MODULES(X)
#undef X
	};

	const int misses[] = {
		ShaderStageIndex_Miss,
	};

	const vk_pipeline_ray_hit_group_t hits[] = {
		// TODO rigidly specify the expected sbt structure w/ offsets and materials
		{ // 0: fully opaque: no need for closest nor any hits
			.closest = -1,
			.any = -1,
		},
		{ // 1: materials w/ alpha mask: need alpha test
			.closest = -1,
			.any = ShaderStageIndex_AlphaTest, // TODO these can directly be a string
		},
	};

	const vk_pipeline_ray_create_info_t prtc = {
		.debug_name = "light direct",
		.stages = stages,
		.stages_count = COUNTOF(stages),
		.groups = {
			.miss = misses,
			.miss_count = COUNTOF(misses),
			.hit = hits,
			.hit_count = COUNTOF(hits),
		},
		.layout = g_ray_light_direct.desc.riptors.pipeline_layout,
	};

	return VK_PipelineRayTracingCreate(&prtc);
}

qboolean XVK_RayTraceLightDirectInit( void ) {
	initDescriptors();

	g_ray_light_direct.pipeline = createPipeline();
	ASSERT(g_ray_light_direct.pipeline.pipeline != VK_NULL_HANDLE);

	return true;
}

void XVK_RayTraceLightDirectDestroy( void ) {
	VK_PipelineRayTracingDestroy(&g_ray_light_direct.pipeline);
	VK_DescriptorsDestroy(&g_ray_light_direct.desc.riptors);
}

void XVK_RayTraceLightDirectReloadPipeline( void ) {
	const vk_pipeline_ray_t new_pipeline = createPipeline();
	if (new_pipeline.pipeline == VK_NULL_HANDLE)
		return;

	VK_PipelineRayTracingDestroy(&g_ray_light_direct.pipeline);

	g_ray_light_direct.pipeline = new_pipeline;
}

void XVK_RayTraceLightDirect( VkCommandBuffer cmdbuf, const vk_ray_resources_t *res ) {
	updateDescriptors( res );

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_light_direct.pipeline.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_light_direct.desc.riptors.pipeline_layout, 0, 1, g_ray_light_direct.desc.riptors.desc_sets + 0, 0, NULL);
	VK_PipelineRayTracingTrace(cmdbuf, &g_ray_light_direct.pipeline, res->width, res->height);
}

