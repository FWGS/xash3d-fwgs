#include "vk_ray_light_direct.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"

typedef struct {
	const char *filename;
	VkShaderStageFlagBits stage;
} vk_pipeline_shader_stage_t;

typedef struct {
	int closest;
	int any;
} vk_pipeline_ray_shader_group_t;

typedef struct {
	const char *debug_name;
	VkPipelineLayout layout;

	const vk_pipeline_shader_stage_t *shaders;
	int shaders_count;

	struct {
		const int *miss;
		int miss_count;

		const vk_pipeline_ray_shader_group_t *hit;
		int hit_count;
	} group;
} vk_pipeline_ray_tracing_create_t;

typedef struct {
	VkPipeline pipeline;
	vk_buffer_t sbt_buffer; // TODO suballocate this from a single central buffer or something
	struct {
		VkStridedDeviceAddressRegionKHR raygen, miss, hit, callable;
	} sbt;
	char debug_name[32];
} vk_pipeline_ray_tracing_t;

static vk_pipeline_ray_tracing_t VK_PipelineRayTracingCreate(const vk_pipeline_ray_tracing_create_t *create) {
#define MAX_SHADER_STAGES 16
#define MAX_SHADER_GROUPS 16
	vk_pipeline_ray_tracing_t ret = {0};
	VkPipelineShaderStageCreateInfo shaders[MAX_SHADER_STAGES];
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[MAX_SHADER_GROUPS];
	const int shader_groups_count = create->group.hit_count + create->group.miss_count + 1;
	int raygen_index = -1;
	int group_index = 0;

	const VkRayTracingPipelineCreateInfoKHR rtpci = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		//TODO .flags = VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR  ....
		.stageCount = create->shaders_count,
		.pStages = shaders,
		.groupCount = shader_groups_count,
		.pGroups = shader_groups,
		.maxPipelineRayRecursionDepth = 1,
		.layout = create->layout,
	};

	ASSERT(create->shaders_count <= MAX_SHADER_STAGES);
	ASSERT(shader_groups_count <= MAX_SHADER_GROUPS);

	for (int i = 0; i < create->shaders_count; ++i) {
		const vk_pipeline_shader_stage_t *const stage = create->shaders + i;

		if (stage->stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) {
			ASSERT(raygen_index == -1);
			raygen_index = i;
		}

		shaders[i] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = stage->stage,
			.module = loadShader(stage->filename),
			.pName = "main",
			.pSpecializationInfo = NULL,
		};
	}

	ASSERT(raygen_index >= 0);

	shader_groups[group_index++] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = raygen_index,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	for (int i = 0; i < create->group.miss_count; ++i) {
		const int miss_index = create->group.miss[i];

		ASSERT(miss_index >= 0);
		ASSERT(miss_index < create->shaders_count);
		ASSERT(create->shaders[miss_index].stage == VK_SHADER_STAGE_MISS_BIT_KHR);

		shader_groups[group_index++] = (VkRayTracingShaderGroupCreateInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.generalShader = miss_index,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
	}

	for (int i = 0; i < create->group.hit_count; ++i) {
		const vk_pipeline_ray_shader_group_t *const group = create->group.hit + i;
		const int closest_index = group->closest >= 0 ? group->closest : VK_SHADER_UNUSED_KHR;
		const int any_index = group->any >= 0 ? group->any : VK_SHADER_UNUSED_KHR;

		if (closest_index != VK_SHADER_UNUSED_KHR) {
			ASSERT(closest_index < create->shaders_count);
			ASSERT(create->shaders[closest_index].stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
		}

		if (any_index != VK_SHADER_UNUSED_KHR) {
			ASSERT(any_index < create->shaders_count);
			ASSERT(create->shaders[any_index].stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
		}

		shader_groups[group_index++] = (VkRayTracingShaderGroupCreateInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.anyHitShader = any_index,
			.closestHitShader = closest_index,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
	}

	XVK_CHECK(vkCreateRayTracingPipelinesKHR(vk_core.device, VK_NULL_HANDLE, g_pipeline_cache, 1, &rtpci, NULL, &ret.pipeline));

	for (int i = 0; i < create->shaders_count; ++i)
		vkDestroyShaderModule(vk_core.device, shaders[i].module, NULL);

	if (ret.pipeline == VK_NULL_HANDLE)
		return ret;

	// TODO: do not allocate sbt buffer per pipeline. make a central buffer and use that
	// TODO: does it really need to be host-visible?
	{
		char buf[64];
		Q_snprintf(buf, sizeof(buf), "%s sbt", create->debug_name);
		if (!VK_BufferCreate(buf, &ret.sbt_buffer, shader_groups_count * vk_core.physical_device.sbt_record_size,
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		{
			vkDestroyPipeline(vk_core.device, ret.pipeline, NULL);
			ret.pipeline = VK_NULL_HANDLE;
			return ret;
		}
	}

	{
		const uint32_t sbt_handle_size = vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize;
		const uint32_t sbt_handles_buffer_size = shader_groups_count * sbt_handle_size;
		uint8_t *sbt_handles = Mem_Malloc(vk_core.pool, sbt_handles_buffer_size);
		XVK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(vk_core.device, ret.pipeline, 0, shader_groups_count, sbt_handles_buffer_size, sbt_handles));
		for (int i = 0; i < shader_groups_count; ++i)
		{
			uint8_t *sbt_dst = ret.sbt_buffer.mapped;
			memcpy(sbt_dst + vk_core.physical_device.sbt_record_size * i, sbt_handles + sbt_handle_size * i, sbt_handle_size);
		}
		Mem_Free(sbt_handles);
	}

	{
		const VkDeviceAddress sbt_addr = XVK_BufferGetDeviceAddress(ret.sbt_buffer.buffer);
		const uint32_t sbt_record_size = vk_core.physical_device.sbt_record_size;
		uint32_t index = 0;

		ASSERT(sbt_record_size == 64); // FIXME in shader constant specialization
#define SBT_INDEX(count) (VkStridedDeviceAddressRegionKHR){ \
		.deviceAddress = sbt_addr + sbt_record_size * index, \
		.size = sbt_record_size * (count), \
		.stride = sbt_record_size, \
	}; index += count
		ret.sbt.raygen = SBT_INDEX(1);
		ret.sbt.miss = SBT_INDEX(create->group.miss_count);
		ret.sbt.hit = SBT_INDEX(create->group.hit_count);
		ret.sbt.callable = (VkStridedDeviceAddressRegionKHR){ 0 };
	}

	Q_strncpy(ret.debug_name, create->debug_name, sizeof(ret.debug_name));

	return ret;
}

static void VK_PipelineRayTracingDestroy(vk_pipeline_ray_tracing_t* pipeline) {
	vkDestroyPipeline(vk_core.device, pipeline->pipeline, NULL);
	VK_BufferDestroy(&pipeline->sbt_buffer);
	pipeline->pipeline = VK_NULL_HANDLE;
}

static void VK_PipelineRayTracingTrace(VkCommandBuffer cmdbuf, const vk_pipeline_ray_tracing_t *pipeline, uint32_t width, uint32_t height) {
	DEBUG_BEGIN(cmdbuf, pipeline->debug_name);
		// TODO bind this and accepts descriptors as args? vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->pipeline);
		vkCmdTraceRaysKHR(cmdbuf, &pipeline->sbt.raygen, &pipeline->sbt.miss, &pipeline->sbt.hit, &pipeline->sbt.callable, width, height, 1 );
	DEBUG_END(cmdbuf);
}

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

#define LIST_SBT

static struct {
	struct {
		vk_descriptors_t riptors;
		VkDescriptorSetLayoutBinding bindings[RtLDir_Desc_COUNT];
		vk_descriptor_value_t values[RtLDir_Desc_COUNT];

		// TODO: split into two sets, one common to all rt passes (tlas, kusochki, etc), another one this pass only
		VkDescriptorSet sets[1];
	} desc;

	vk_pipeline_ray_tracing_t pipeline;
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

static void updateDescriptors( const xvk_ray_trace_light_direct_t* args ) {
#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = args->in.name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = args->out.name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X

	g_ray_light_direct.desc.values[RtLDir_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &args->in.tlas,
	};

#define DESC_SET_BUFFER(index, buffer_) \
	g_ray_light_direct.desc.values[index].buffer = (VkDescriptorBufferInfo){ \
		.buffer = args->in.buffer_.buffer, \
		.offset = args->in.buffer_.offset, \
		.range = args->in.buffer_.size, \
	}

	DESC_SET_BUFFER(RtLDir_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtLDir_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtLDir_Desc_Indices, indices);
	DESC_SET_BUFFER(RtLDir_Desc_Vertices, vertices);
	DESC_SET_BUFFER(RtLDir_Desc_Lights, lights);
	DESC_SET_BUFFER(RtLDir_Desc_LightClusters, light_clusters);

#undef DESC_SET_BUFFER

	g_ray_light_direct.desc.values[RtLDir_Desc_Textures].image_array = args->in.all_textures;

	VK_DescriptorsWrite(&g_ray_light_direct.desc.riptors);
}

static vk_pipeline_ray_tracing_t createPipeline( void ) {
	enum {
#define X(name, file, type) \
		ShaderStageIndex_##name,
		LIST_SHADER_MODULES(X)
#undef X
	};

	const vk_pipeline_shader_stage_t stages[] = {
#define X(name, file, type) \
		{.filename = file ".spv", .stage = VK_SHADER_STAGE_##type##_BIT_KHR},
		LIST_SHADER_MODULES(X)
#undef X
	};

	const int misses[] = {
		ShaderStageIndex_Miss,
	};

	const vk_pipeline_ray_shader_group_t hits[] = {
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

	const vk_pipeline_ray_tracing_create_t prtc = {
		.debug_name = "light direct",
		.shaders = stages,
		.shaders_count = COUNTOF(stages),
		.group = {
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
	vk_pipeline_ray_tracing_t new_pipeline = createPipeline();
	if (new_pipeline.pipeline == VK_NULL_HANDLE)
		return;

	VK_PipelineRayTracingDestroy(&g_ray_light_direct.pipeline);

	g_ray_light_direct.pipeline = new_pipeline;
}

void XVK_RayTraceLightDirect( VkCommandBuffer cmdbuf, const xvk_ray_trace_light_direct_t *args ) {
	updateDescriptors( args );

	DEBUG_BEGIN(cmdbuf, "lights direct");
		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_light_direct.pipeline.pipeline);
		vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_light_direct.desc.riptors.pipeline_layout, 0, 1, g_ray_light_direct.desc.riptors.desc_sets + 0, 0, NULL);
		VK_PipelineRayTracingTrace(cmdbuf, &g_ray_light_direct.pipeline, args->width, args->height);
	DEBUG_END(cmdbuf);
}

