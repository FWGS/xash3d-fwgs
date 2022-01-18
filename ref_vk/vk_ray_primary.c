#include "vk_ray_primary.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"

#include "eiface.h" // ARRAYSIZE

enum {
	RtPrim_SBT_RayGen,

	RtPrim_SBT_RayMiss,

	RtPrim_SBT_RayHit,
	RtPrim_SBT_RayHit_WithAlphaTest,
	RtPrim_SBT_RayHit_END = RtPrim_SBT_RayHit_WithAlphaTest,

	RtPrim_SBT_COUNT,
};

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

	vk_buffer_t sbt_buffer;

	VkPipeline pipeline;
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

static VkPipeline createPipeline( void ) {
	VkPipeline pipeline;

	enum {
		ShaderStageIndex_RayGen,
		ShaderStageIndex_RayMiss,
		ShaderStageIndex_RayClosestHit,
		ShaderStageIndex_RayAnyHit_AlphaTest,
		ShaderStageIndex_COUNT,
	};

	VkPipelineShaderStageCreateInfo shaders[ShaderStageIndex_COUNT];
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[RtPrim_SBT_COUNT];

	const VkRayTracingPipelineCreateInfoKHR rtpci = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		//TODO .flags = VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR  ....
		.stageCount = ARRAYSIZE(shaders),
		.pStages = shaders,
		.groupCount = ARRAYSIZE(shader_groups),
		.pGroups = shader_groups,
		.maxPipelineRayRecursionDepth = 1,
		.layout = g_ray_primary.desc.riptors.pipeline_layout,
	};

#define DEFINE_SHADER(filename, bit, sbt_index) \
	shaders[sbt_index] = (VkPipelineShaderStageCreateInfo){ \
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
		.stage = VK_SHADER_STAGE_##bit##_BIT_KHR, \
		.module = loadShader(filename ".spv"), \
		.pName = "main", \
		.pSpecializationInfo = NULL, \
	}

	DEFINE_SHADER("ray_primary.rgen", RAYGEN, ShaderStageIndex_RayGen);
	DEFINE_SHADER("ray_primary.rchit", CLOSEST_HIT, ShaderStageIndex_RayClosestHit);
	DEFINE_SHADER("ray_primary_alphatest.rahit", ANY_HIT, ShaderStageIndex_RayAnyHit_AlphaTest);
	DEFINE_SHADER("ray_primary.rmiss", MISS, ShaderStageIndex_RayMiss);

	// TODO static assert
#define ASSERT_SHADER_OFFSET(sbt_kind, sbt_index, offset) \
	ASSERT((offset) == (sbt_index - sbt_kind))

	ASSERT_SHADER_OFFSET(RtPrim_SBT_RayGen, RtPrim_SBT_RayGen, 0);
	ASSERT_SHADER_OFFSET(RtPrim_SBT_RayMiss, RtPrim_SBT_RayMiss, SHADER_OFFSET_MISS_REGULAR);

	ASSERT_SHADER_OFFSET(RtPrim_SBT_RayHit, RtPrim_SBT_RayHit, SHADER_OFFSET_HIT_REGULAR_BASE + SHADER_OFFSET_HIT_REGULAR);
	ASSERT_SHADER_OFFSET(RtPrim_SBT_RayHit, RtPrim_SBT_RayHit_WithAlphaTest, SHADER_OFFSET_HIT_REGULAR_BASE + SHADER_OFFSET_HIT_ALPHA_TEST);

	//ASSERT_SHADER_OFFSET(ShaderBindingTable_Hit_Base, ShaderBindingTable_Hit_Additive, SHADER_OFFSET_HIT_REGULAR_BASE + SHADER_OFFSET_HIT_ADDITIVE);

	shader_groups[RtPrim_SBT_RayGen] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderStageIndex_RayGen,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[RtPrim_SBT_RayHit] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = ShaderStageIndex_RayClosestHit,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[RtPrim_SBT_RayHit_WithAlphaTest] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
		.anyHitShader = ShaderStageIndex_RayAnyHit_AlphaTest,
		.closestHitShader = ShaderStageIndex_RayClosestHit,
		.generalShader = VK_SHADER_UNUSED_KHR,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	shader_groups[RtPrim_SBT_RayMiss] = (VkRayTracingShaderGroupCreateInfoKHR) {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
		.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
		.anyHitShader = VK_SHADER_UNUSED_KHR,
		.closestHitShader = VK_SHADER_UNUSED_KHR,
		.generalShader = ShaderStageIndex_RayMiss,
		.intersectionShader = VK_SHADER_UNUSED_KHR,
	};

	XVK_CHECK(vkCreateRayTracingPipelinesKHR(vk_core.device, VK_NULL_HANDLE, g_pipeline_cache, 1, &rtpci, NULL, &pipeline));

	for (int i = 0; i < ARRAYSIZE(shaders); ++i)
		vkDestroyShaderModule(vk_core.device, shaders[i].module, NULL);

	if (pipeline == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	{
		const uint32_t sbt_handle_size = vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupHandleSize;
		const uint32_t sbt_handles_buffer_size = ARRAYSIZE(shader_groups) * sbt_handle_size;
		uint8_t *sbt_handles = Mem_Malloc(vk_core.pool, sbt_handles_buffer_size);
		XVK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(vk_core.device, pipeline, 0, ARRAYSIZE(shader_groups), sbt_handles_buffer_size, sbt_handles));
		for (int i = 0; i < ARRAYSIZE(shader_groups); ++i)
		{
			uint8_t *sbt_dst = g_ray_primary.sbt_buffer.mapped;
			memcpy(sbt_dst + vk_core.physical_device.sbt_record_size * i, sbt_handles + sbt_handle_size * i, sbt_handle_size);
		}
		Mem_Free(sbt_handles);
	}

	return pipeline;
}

qboolean XVK_RayTracePrimaryInit( void ) {
	if (!VK_BufferCreate("primary ray sbt_buffer", &g_ray_primary.sbt_buffer, RtPrim_SBT_COUNT * vk_core.physical_device.sbt_record_size,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
	{
		return false;
	}

	initDescriptors();

	g_ray_primary.pipeline = createPipeline();
	ASSERT(g_ray_primary.pipeline != VK_NULL_HANDLE);

	return true;
}

void XVK_RayTracePrimaryDestroy( void ) {
	vkDestroyPipeline(vk_core.device, g_ray_primary.pipeline, NULL);
	VK_DescriptorsDestroy(&g_ray_primary.desc.riptors);

	VK_BufferDestroy(&g_ray_primary.sbt_buffer);
}

void XVK_RayTracePrimaryReloadPipeline( void ) {
	VkPipeline pipeline = createPipeline();
	if (pipeline == VK_NULL_HANDLE)
		return;

	vkDestroyPipeline(vk_core.device, g_ray_primary.pipeline, NULL);
	g_ray_primary.pipeline = pipeline;
}

void XVK_RayTracePrimary( VkCommandBuffer cmdbuf, const xvk_ray_trace_primary_t *args ) {
	updateDescriptors( args );

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_primary.pipeline);
	/* { */
	/* 	vk_rtx_push_constants_t push_constants = { */
	/* 		.time = gpGlobals->time, */
	/* 		.random_seed = (uint32_t)gEngine.COM_RandomLong(0, INT32_MAX), */
	/* 		.bounces = vk_rtx_bounces->value, */
	/* 		.pixel_cone_spread_angle = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)FRAME_HEIGHT), */
	/* 		.debug_light_index_begin = (uint32_t)(vk_rtx_light_begin->value), */
	/* 		.debug_light_index_end = (uint32_t)(vk_rtx_light_end->value), */
	/* 		.flags = r_lightmap->value ? PUSH_FLAG_LIGHTMAP_ONLY : 0, */
	/* 	}; */
	/* 	vkCmdPushConstants(cmdbuf, g_ray_primary.descriptors.pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 0, sizeof(push_constants), &push_constants); */
	/* } */

	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, g_ray_primary.desc.riptors.pipeline_layout, 0, 1, g_ray_primary.desc.riptors.desc_sets + 0, 0, NULL);

	{
		const uint32_t sbt_record_size = vk_core.physical_device.sbt_record_size;
#define SBT_INDEX(index, count) { \
.deviceAddress = XVK_BufferGetDeviceAddress(g_ray_primary.sbt_buffer.buffer) + sbt_record_size * index, \
.size = sbt_record_size * (count), \
.stride = sbt_record_size, \
}
		const VkStridedDeviceAddressRegionKHR sbt_raygen = SBT_INDEX(RtPrim_SBT_RayGen, 1);
		const VkStridedDeviceAddressRegionKHR sbt_miss = SBT_INDEX(RtPrim_SBT_RayMiss, 1); //ShaderBindingTable_Miss_Empty - ShaderBindingTable_Miss);
		const VkStridedDeviceAddressRegionKHR sbt_hit = SBT_INDEX(RtPrim_SBT_RayHit, RtPrim_SBT_RayHit_END - RtPrim_SBT_RayHit);
		const VkStridedDeviceAddressRegionKHR sbt_callable = { 0 };

		vkCmdTraceRaysKHR(cmdbuf, &sbt_raygen, &sbt_miss, &sbt_hit, &sbt_callable, args->width, args->height, 1 );
	}
}

