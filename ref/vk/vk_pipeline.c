#include "vk_pipeline.h"

#include "vk_framectl.h" // VkRenderPass
#include "vk_combuf.h"

#include "eiface.h"

#define MAX_STAGES 2

VkPipelineCache g_pipeline_cache;

qboolean VK_PipelineInit( void )
{
	VkPipelineCacheCreateInfo pcci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
		.initialDataSize = 0,
		.pInitialData = NULL,
	};

	XVK_CHECK(vkCreatePipelineCache(vk_core.device, &pcci, NULL, &g_pipeline_cache));
	return true;
}

void VK_PipelineShutdown( void )
{
	vkDestroyPipelineCache(vk_core.device, g_pipeline_cache, NULL);
}

VkShaderModule R_VkShaderLoadFromMem(const void *ptr, uint32_t size, const char *name) {
	if ((size % 4 != 0) || (((uintptr_t)ptr & 3) != 0)) {
		gEngine.Con_Printf(S_ERROR "Couldn't load shader %s: size %u or buf %p is not aligned to 4 bytes as required by SPIR-V/Vulkan spec\n", name, size, ptr);
		return VK_NULL_HANDLE;
	}

	const VkShaderModuleCreateInfo smci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = (const uint32_t*)(void*)ptr,
	};

	VkShaderModule module = VK_NULL_HANDLE;
	const VkResult result = vkCreateShaderModule(vk_core.device, &smci, NULL, &module);
	if (result != VK_SUCCESS) {
		gEngine.Con_Printf(S_ERROR "Couldn't load shader %s: error (%d): %s\n", name, result, R_VkResultName(result));
		return VK_NULL_HANDLE;
	}

	SET_DEBUG_NAME(module, VK_OBJECT_TYPE_SHADER_MODULE, name);
	return module;
}

static VkShaderModule R_VkShaderLoadFromFile(const char *filename) {
	fs_offset_t size = 0;
	byte* const buf = gEngine.fsapi->LoadFile(filename, &size, false);

	if (!buf) {
		gEngine.Con_Printf( S_ERROR "Cannot open shader file \"%s\"\n", filename);
		return VK_NULL_HANDLE;
	}

	const VkShaderModule module = R_VkShaderLoadFromMem(buf, size, filename);

finalize:
	Mem_Free(buf);
	return module;
}

void R_VkShaderDestroy(VkShaderModule module) {
	vkDestroyShaderModule(vk_core.device, module, NULL);
}

VkPipeline VK_PipelineGraphicsCreate(const vk_pipeline_graphics_create_info_t *ci)
{
	VkPipeline pipeline;
	VkVertexInputBindingDescription vibd = {
		.binding = 0,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		.stride = ci->vertex_stride,
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vibd,
		.vertexAttributeDescriptionCount = ci->num_attribs,
		.pVertexAttributeDescriptions = ci->attribs,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineRasterizationStateCreateInfo raster_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = ci->cullMode,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.f,
	};

	VkPipelineMultisampleStateCreateInfo multi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = ci->blendEnable,
		.srcColorBlendFactor = ci->srcColorBlendFactor,
		.dstColorBlendFactor = ci->dstColorBlendFactor,
		.colorBlendOp = ci->colorBlendOp,
		.srcAlphaBlendFactor = ci->srcAlphaBlendFactor,
		.dstAlphaBlendFactor = ci->dstAlphaBlendFactor,
		.alphaBlendOp = ci->alphaBlendOp,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = ci->depthTestEnable,
		.depthWriteEnable = ci->depthWriteEnable,
		.depthCompareOp = ci->depthCompareOp,
	};

	VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ARRAYSIZE(dynamic_states),
		.pDynamicStates = dynamic_states,
	};

	VkPipelineShaderStageCreateInfo stage_create_infos[MAX_STAGES];

	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ci->num_stages,
		.pStages = stage_create_infos,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &raster_state,
		.pMultisampleState = &multi_state,
		.pColorBlendState = &color_blend,
		.pDepthStencilState = &depth,
		.layout = ci->layout,
		.renderPass = vk_frame.render_pass.raster,
		.pDynamicState = &dynamic_state_create_info,
		.subpass = 0,
	};

	if (ci->num_stages > MAX_STAGES)
		return VK_NULL_HANDLE;

	VkShaderModule shaders[MAX_STAGES] = {VK_NULL_HANDLE};

	for (int i = 0; i < ci->num_stages; ++i) {
		if (VK_NULL_HANDLE == (shaders[i] = R_VkShaderLoadFromFile(ci->stages[i].filename)))
			goto finalize;

		stage_create_infos[i] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = ci->stages[i].stage,
			.module = shaders[i],
			.pSpecializationInfo = ci->stages[i].specialization_info,
			.pName = "main",
		};
	}

	XVK_CHECK(vkCreateGraphicsPipelines(vk_core.device, g_pipeline_cache, 1, &gpci, NULL, &pipeline));
finalize:
	for (int i = 0; i < ci->num_stages; ++i)
		R_VkShaderDestroy(shaders[i]);

	return pipeline;
}

VkPipeline VK_PipelineComputeCreate(const vk_pipeline_compute_create_info_t *ci) {
	const VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.layout = ci->layout,
		.stage = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = ci->shader_module,
			.pName = "main",
			.pSpecializationInfo = ci->specialization_info,
		},
	};

	VkPipeline pipeline;
	XVK_CHECK(vkCreateComputePipelines(vk_core.device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));

	return pipeline;
}

vk_pipeline_ray_t VK_PipelineRayTracingCreate(const vk_pipeline_ray_create_info_t *create) {
#define MAX_SHADER_STAGES 16
#define MAX_SHADER_GROUPS 16
	vk_pipeline_ray_t ret = {0};
	VkPipelineShaderStageCreateInfo stages[MAX_SHADER_STAGES];
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[MAX_SHADER_GROUPS];
	const int shader_groups_count = create->groups.hit_count + create->groups.miss_count + 1;
	int raygen_index = -1;
	int group_index = 0;

	const VkRayTracingPipelineCreateInfoKHR rtpci = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		//TODO .flags = VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR  ....
		.stageCount = create->stages_count,
		.pStages = stages,
		.groupCount = shader_groups_count,
		.pGroups = shader_groups,
		.maxPipelineRayRecursionDepth = 1,
		.layout = create->layout,
	};

	ASSERT(shader_groups_count <= MAX_SHADER_GROUPS);

	if (create->stages_count > MAX_SHADER_STAGES) {
		gEngine.Con_Printf(S_ERROR "Too many shader stages %d, max=%d\n", create->stages_count, MAX_SHADER_STAGES);
		return ret;
	}

	for (int i = 0; i < create->stages_count; ++i) {
		const vk_shader_stage_t *const stage = create->stages + i;

		// FIXME going away from loading shaders directly
		ASSERT(!stage->filename);

		if (stage->stage == VK_SHADER_STAGE_RAYGEN_BIT_KHR) {
			ASSERT(raygen_index == -1);
			raygen_index = i;
		}

		stages[i] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = stage->stage,
			.module = stage->module,
			.pName = "main",
			.pSpecializationInfo = stage->specialization_info,
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

	for (int i = 0; i < create->groups.miss_count; ++i) {
		const int miss_index = create->groups.miss[i];

		ASSERT(miss_index >= 0);
		ASSERT(miss_index < create->stages_count);
		ASSERT(create->stages[miss_index].stage == VK_SHADER_STAGE_MISS_BIT_KHR);

		shader_groups[group_index++] = (VkRayTracingShaderGroupCreateInfoKHR) {
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.generalShader = miss_index,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		};
	}

	for (int i = 0; i < create->groups.hit_count; ++i) {
		const vk_pipeline_ray_hit_group_t *const groups = create->groups.hit + i;
		const int closest_index = groups->closest >= 0 ? groups->closest : VK_SHADER_UNUSED_KHR;
		const int any_index = groups->any >= 0 ? groups->any : VK_SHADER_UNUSED_KHR;

		if (closest_index != VK_SHADER_UNUSED_KHR) {
			ASSERT(closest_index < create->stages_count);
			ASSERT(create->stages[closest_index].stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
		}

		if (any_index != VK_SHADER_UNUSED_KHR) {
			ASSERT(any_index < create->stages_count);
			ASSERT(create->stages[any_index].stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
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
		const VkDeviceAddress sbt_addr = R_VkBufferGetDeviceAddress(ret.sbt_buffer.buffer);
		const uint32_t sbt_record_size = vk_core.physical_device.sbt_record_size;
		uint32_t index = 0;

#define SBT_INDEX(count) (VkStridedDeviceAddressRegionKHR){ \
		.deviceAddress = sbt_addr + sbt_record_size * index, \
		.size = sbt_record_size * (count), \
		.stride = sbt_record_size, \
	}; index += count
		ret.sbt.raygen = SBT_INDEX(1);
		ret.sbt.miss = SBT_INDEX(create->groups.miss_count);
		ret.sbt.hit = SBT_INDEX(create->groups.hit_count);
		ret.sbt.callable = (VkStridedDeviceAddressRegionKHR){ 0 };
	}

	Q_strncpy(ret.debug_name, create->debug_name, sizeof(ret.debug_name));

	return ret;
}

void VK_PipelineRayTracingDestroy(vk_pipeline_ray_t* pipeline) {
	vkDestroyPipeline(vk_core.device, pipeline->pipeline, NULL);
	VK_BufferDestroy(&pipeline->sbt_buffer);
	pipeline->pipeline = VK_NULL_HANDLE;
}

void VK_PipelineRayTracingTrace(vk_combuf_t *combuf, const vk_pipeline_ray_t *pipeline, uint32_t width, uint32_t height, int scope_id) {
		// TODO bind this and accepts descriptors as args? vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->pipeline);
		const int begin_id = R_VkCombufScopeBegin(combuf, scope_id);
		vkCmdTraceRaysKHR(combuf->cmdbuf, &pipeline->sbt.raygen, &pipeline->sbt.miss, &pipeline->sbt.hit, &pipeline->sbt.callable, width, height, 1 );
		R_VkCombufScopeEnd(combuf, begin_id, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
}
