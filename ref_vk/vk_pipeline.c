#include "vk_pipeline.h"

#include "vk_framectl.h" // VkRenderPass

#include "eiface.h"

#define MAX_STAGES 2

static struct {
	VkPipelineCache cache;
} g_pipeline;

qboolean VK_PipelineInit( void )
{
	VkPipelineCacheCreateInfo pcci = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    .initialDataSize = 0,
    .pInitialData = NULL,
	};

	XVK_CHECK(vkCreatePipelineCache(vk_core.device, &pcci, NULL, &g_pipeline.cache));
	return true;
}

void VK_PipelineShutdown( void )
{
	vkDestroyPipelineCache(vk_core.device, g_pipeline.cache, NULL);
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

	for (int i = 0; i < ci->num_stages; ++i) {
		stage_create_infos[i] = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = ci->stages[i].stage,
			.module = loadShader(ci->stages[i].filename),
			.pSpecializationInfo = ci->stages[i].specialization_info,
			.pName = "main",
		};
	}

	XVK_CHECK(vkCreateGraphicsPipelines(vk_core.device, g_pipeline.cache, 1, &gpci, NULL, &pipeline));

	for (int i = 0; i < ci->num_stages; ++i) {
		vkDestroyShaderModule(vk_core.device, stage_create_infos[i].module, NULL);
	}

	return pipeline;
}

VkPipeline VK_PipelineComputeCreate(const vk_pipeline_compute_create_info_t *ci) {
	const VkComputePipelineCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.layout = ci->layout,
		.stage = (VkPipelineShaderStageCreateInfo){
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = loadShader(ci->shader_filename),
			.pName = "main",
			.pSpecializationInfo = ci->specialization_info,
		},
	};

	VkPipeline pipeline;
	XVK_CHECK(vkCreateComputePipelines(vk_core.device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
	vkDestroyShaderModule(vk_core.device, cpci.stage.module, NULL);

	return pipeline;
}
