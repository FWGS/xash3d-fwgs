#include "vk_pipeline.h"

#include "vk_framectl.h" // VkRenderPass

#include "eiface.h"

VkPipeline createPipeline(const vk_pipeline_create_info_t *ci)
{
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
		.cullMode = VK_CULL_MODE_BACK_BIT,
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

	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ci->num_stages,
		.pStages = ci->stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &raster_state,
		.pMultisampleState = &multi_state,
		.pColorBlendState = &color_blend,
		.pDepthStencilState = &depth,
		.layout = ci->layout,
		.renderPass = vk_frame.render_pass,
		.pDynamicState = &dynamic_state_create_info,
		.subpass = 0,
	};

	VkPipeline pipeline;
	XVK_CHECK(vkCreateGraphicsPipelines(vk_core.device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));

	return pipeline;
}
