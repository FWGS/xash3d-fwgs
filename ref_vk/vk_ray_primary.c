#include "vk_ray_primary.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"

#include "eiface.h" // ARRAYSIZE

enum {
	// TODO set 0
	RtPrim_Desc_TLAS,
	RtPrim_Desc_UBO,
	RtPrim_Desc_Kusochki,
	RtPrim_Desc_Indices,
	RtPrim_Desc_Vertices,
	RtPrim_Desc_Textures,

	// TODO set 1
#define X(index, name, ...) RtPrim_Desc_Out_##name,
RAY_PRIMARY_OUTPUTS(X)
#undef X

	RtPrim_Desc_COUNT
};

static struct {
	struct {
		vk_descriptors_t riptors;
		VkDescriptorSetLayoutBinding bindings[RtPrim_Desc_COUNT];
		vk_descriptor_value_t values[RtPrim_Desc_COUNT];

		// TODO: split into two sets, one common to all rt passes (tlas, kusochki, etc), another one this pass only
		VkDescriptorSet sets[1];
	} desc;

	vk_pipeline_ray_t pipeline;
} g_ray_primary;

static void initDescriptors( void ) {
	g_ray_primary.desc.riptors = (vk_descriptors_t) {
		.bindings = g_ray_primary.desc.bindings,
		.num_bindings = ARRAYSIZE(g_ray_primary.desc.bindings),
		.values = g_ray_primary.desc.values,
		.num_sets = ARRAYSIZE(g_ray_primary.desc.sets),
		.desc_sets = g_ray_primary.desc.sets,
		/* .push_constants = (VkPushConstantRange){ */
		/* 	.offset = 0, */
		/* 	.size = sizeof(vk_rtx_push_constants_t), */
		/* 	.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, */
		/* }, */
	};

#define INIT_BINDING(index, name, type, count, stages) \
	g_ray_primary.desc.bindings[RtPrim_Desc_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = stages, \
	}

	INIT_BINDING(1, TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	INIT_BINDING(2, UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(3, Kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(4, Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(5, Vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(6, Textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

#define X(index, name, ...) \
	INIT_BINDING(index, Out_##name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
RAY_PRIMARY_OUTPUTS(X)
#undef X
#undef INIT_BINDING

	VK_DescriptorsCreate(&g_ray_primary.desc.riptors);
}

static void updateDescriptors( const xvk_ray_trace_primary_t* args ) {
#define X(index, name, ...) \
	g_ray_primary.desc.values[RtPrim_Desc_Out_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = args->out.name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
RAY_PRIMARY_OUTPUTS(X)

	g_ray_primary.desc.values[RtPrim_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &args->in.tlas,
	};

#define DESC_SET_BUFFER(index, buffer_) \
	g_ray_primary.desc.values[index].buffer = (VkDescriptorBufferInfo){ \
		.buffer = args->in.buffer_.buffer, \
		.offset = args->in.buffer_.offset, \
		.range = args->in.buffer_.size, \
	}

	DESC_SET_BUFFER(RtPrim_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtPrim_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtPrim_Desc_Indices, indices);
	DESC_SET_BUFFER(RtPrim_Desc_Vertices, vertices);

#undef DESC_SET_BUFFER

	g_ray_primary.desc.values[RtPrim_Desc_Textures].image_array = args->in.all_textures;

	VK_DescriptorsWrite(&g_ray_primary.desc.riptors);
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

#define LIST_SHADER_MODULES(X) \
	X(RayGen, "ray_primary.rgen", RAYGEN) \
	X(Miss, "ray_primary.rmiss", MISS) \
	X(HitClosest, "ray_primary.rchit", CLOSEST_HIT) \
	X(HitAnyAlphaTest, "ray_common_alphatest.rahit", ANY_HIT) \

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
			.closest = ShaderStageIndex_HitClosest,
			.any = -1,
		},
		{ // 1: materials w/ alpha mask: need alpha test
			.closest = ShaderStageIndex_HitClosest,
			.any = ShaderStageIndex_HitAnyAlphaTest, // TODO these can directly be a string
		},
	};

	const vk_pipeline_ray_create_info_t prtc = {
		.debug_name = "primary ray",
		.stages = stages,
		.stages_count = COUNTOF(stages),
		.groups = {
			.miss = misses,
			.miss_count = COUNTOF(misses),
			.hit = hits,
			.hit_count = COUNTOF(hits),
		},
		.layout = g_ray_primary.desc.riptors.pipeline_layout,
	};

	return VK_PipelineRayTracingCreate(&prtc);
}

qboolean XVK_RayTracePrimaryInit( void ) {
	initDescriptors();

	g_ray_primary.pipeline = createPipeline();
	ASSERT(g_ray_primary.pipeline.pipeline != VK_NULL_HANDLE);

	return true;
}

void XVK_RayTracePrimaryDestroy( void ) {
	VK_PipelineRayTracingDestroy(&g_ray_primary.pipeline);
	VK_DescriptorsDestroy(&g_ray_primary.desc.riptors);
}

void XVK_RayTracePrimaryReloadPipeline( void ) {
	const vk_pipeline_ray_t new_pipeline = createPipeline();
	if (new_pipeline.pipeline == VK_NULL_HANDLE)
		return;

	VK_PipelineRayTracingDestroy(&g_ray_primary.pipeline);

	g_ray_primary.pipeline = new_pipeline;
}

void XVK_RayTracePrimary( VkCommandBuffer cmdbuf, const xvk_ray_trace_primary_t *args ) {
	updateDescriptors( args );

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_primary.pipeline.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_primary.desc.riptors.pipeline_layout, 0, 1, g_ray_primary.desc.riptors.desc_sets + 0, 0, NULL);
	VK_PipelineRayTracingTrace(cmdbuf, &g_ray_primary.pipeline, args->width, args->height);
}

