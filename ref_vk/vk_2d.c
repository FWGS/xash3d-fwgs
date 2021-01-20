#include "vk_2d.h"

#include "vk_buffer.h"
#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_framectl.h"
#include "vk_renderstate.h"

#include "com_strings.h"
#include "eiface.h"

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
void R_DrawTileClear( int texnum, int x, int y, int w, int h )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

typedef struct vertex_2d_s {
	float x, y;
	float u, v;
	color_rgba8_t color;
} vertex_2d_t;

// TODO should these be dynamic?
#define MAX_PICS 4096
#define MAX_BATCHES 128

typedef struct vk_2d_pipeline_s {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
} vk_2d_pipeline_t;

static struct {
	vk_2d_pipeline_t pipelines[kRenderTransAdd + 1];

	uint32_t max_pics, num_pics;
	vk_buffer_t pics_buffer;

	struct {
		uint32_t vertex_offset, vertex_count;
		int texture;
		int blending_mode;
	} batch[MAX_BATCHES];
	uint32_t current_batch;

	// TODO texture bindings?
} g2d;

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	if (g2d.batch[g2d.current_batch].texture != texnum || g2d.batch[g2d.current_batch].blending_mode != vk_renderstate.blending_mode)
	{
		if (g2d.batch[g2d.current_batch].vertex_count != 0)
		{
			if (g2d.current_batch == MAX_BATCHES - 1)
			{
				gEngine.Con_Printf(S_ERROR "VK FIXME RAN OUT OF BATCHES: %s(%f, %f, %f, %f, %f, %f, %f, %f, %d(%s))\n", __FUNCTION__,
					x, y, w, h, s1, t1, s2, t2, texnum, findTexture(texnum)->name);
				return;
			}

			++g2d.current_batch;
		}

		g2d.batch[g2d.current_batch].texture = texnum;
		g2d.batch[g2d.current_batch].blending_mode = vk_renderstate.blending_mode;
		g2d.batch[g2d.current_batch].vertex_offset = g2d.num_pics;
		g2d.batch[g2d.current_batch].vertex_count = 0;
	}

	if (g2d.num_pics + 6 > g2d.max_pics)
	{
		gEngine.Con_Printf(S_ERROR "VK FIXME RAN OUT OF BUFFER: %s(%f, %f, %f, %f, %f, %f, %f, %f, %d(%s))\n", __FUNCTION__,
			x, y, w, h, s1, t1, s2, t2, texnum, findTexture(texnum)->name);
		return;
	}

	{
		vertex_2d_t *p = ((vertex_2d_t*)(g2d.pics_buffer.mapped)) + g2d.num_pics;

		const float vw = vk_frame.create_info.imageExtent.width;
		const float vh = vk_frame.create_info.imageExtent.height;
		const float x1 = (x / vw)*2.f - 1.f;
		const float y1 = (y / vh)*2.f - 1.f;
		const float x2 = ((x + w) / vw)*2.f - 1.f;
		const float y2 = ((y + h) / vh)*2.f - 1.f;
		const color_rgba8_t color = vk_renderstate.tri_color;

		g2d.num_pics += 6;
		g2d.batch[g2d.current_batch].vertex_count += 6; // ....
		p[0] = (vertex_2d_t){x1, y1, s1, t1, color};
		p[1] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[2] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[3] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[4] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[5] = (vertex_2d_t){x2, y2, s2, t2, color};
	}
}

static qboolean createPipeline(vk_2d_pipeline_t *pipeline, int blending_mode)
{
	VkVertexInputAttributeDescription attribs[] = {
		{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, x)},
		{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, u)},
		{.binding = 0, .location = 2, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vertex_2d_t, color)},
	};

	VkPipelineShaderStageCreateInfo shader_stages[] = {
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = loadShader("2d.vert.spv"),
		.pName = "main",
	}, {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = loadShader("2d.frag.spv"),
		.pName = "main",
	}};

	VkVertexInputBindingDescription vibd = {
		.binding = 0,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		.stride = sizeof(vertex_2d_t),
	};

	VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vibd,
		.vertexAttributeDescriptionCount = ARRAYSIZE(attribs),
		.pVertexAttributeDescriptions = attribs,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	VkViewport viewport = {
		.x = 0, .y = 0,
		.width = (float)vk_frame.create_info.imageExtent.width,
		.height = (float)vk_frame.create_info.imageExtent.height,
		.minDepth = 0.f, .maxDepth = 1.f,
	};
	VkRect2D scissor = {
		.offset = {0},
		.extent = vk_frame.create_info.imageExtent,
	};
	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
		.pViewports = &viewport,
		.pScissors = &scissor,
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
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	VkPipelineDepthStencilStateCreateInfo depth = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
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
		.stageCount = ARRAYSIZE(shader_stages),
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport_state,
		.pRasterizationState = &raster_state,
		.pMultisampleState = &multi_state,
		.pColorBlendState = &color_blend,
		.pDepthStencilState = &depth,
		//.layout = material->pipeline_layout,
		.renderPass = vk_frame.render_pass,
		.pDynamicState = &dynamic_state_create_info,
		.subpass = 0,
	};

	/* VkPushConstantRange push_const = { */
	/* 	.offset = 0, */
	/* 	.size = sizeof(AVec3f), */
	/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
	/* }; */

	VkDescriptorSetLayout descriptor_layouts[] = {
		vk_core.descriptor_pool.one_texture_layout,
	/* 		g.descriptors[Descriptors_Global]->layout, */
	/* 		g.descriptors[Descriptors_Lightmaps]->layout, */
	/* 		g.descriptors[Descriptors_Textures]->layout, */
	};

	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = ARRAYSIZE(descriptor_layouts),
		.pSetLayouts = descriptor_layouts,
		/* .pushConstantRangeCount = 1, */
		/* .pPushConstantRanges = &push_const, */
	};

	switch (blending_mode)
	{
		case kRenderNormal:
			blend_attachment.blendEnable = VK_FALSE;
			break;

		case kRenderTransColor:
		case kRenderTransTexture:
			blend_attachment.blendEnable = VK_TRUE;
			blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			blend_attachment.srcAlphaBlendFactor = blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dstAlphaBlendFactor = blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;

		case kRenderTransAlpha:
			blend_attachment.blendEnable = VK_FALSE;
			// FIXME pglEnable( GL_ALPHA_TEST );
			break;

		case kRenderGlow:
		case kRenderTransAdd:
			blend_attachment.blendEnable = VK_TRUE;
			blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			blend_attachment.srcAlphaBlendFactor = blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend_attachment.dstAlphaBlendFactor = blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
			break;
	}

	// FIXME store layout separately
	XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &pipeline->pipeline_layout));
	gpci.layout = pipeline->pipeline_layout;

	XVK_CHECK(vkCreateGraphicsPipelines(vk_core.device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline->pipeline));

	for (int i = 0; i < (int)ARRAYSIZE(shader_stages); ++i)
		vkDestroyShaderModule(vk_core.device, shader_stages[i].module, NULL);

	return true;
}

void vk2dBegin( void )
{
	g2d.num_pics = 0;
	g2d.current_batch = 0;
	g2d.batch[0].vertex_count = 0;
}

void vk2dEnd( void )
{
	const VkDeviceSize offset = 0;
	const VkViewport viewport[] = {
		{0.f, 0.f, (float)vk_frame.surface_caps.currentExtent.width, (float)vk_frame.surface_caps.currentExtent.height, 0.f, 1.f},
	};
	const VkRect2D scissor[] = {{
		{0, 0},
		vk_frame.surface_caps.currentExtent,
	}};

	if (!g2d.num_pics)
		return;

	vkCmdSetViewport(vk_core.cb, 0, ARRAYSIZE(viewport), viewport);
	vkCmdSetScissor(vk_core.cb, 0, ARRAYSIZE(scissor), scissor);
	vkCmdBindVertexBuffers(vk_core.cb, 0, 1, &g2d.pics_buffer.buffer, &offset);

	for (int i = 0; i <= g2d.current_batch; ++i)
	{
		vk_texture_t *texture = findTexture(g2d.batch[i].texture);
		const vk_2d_pipeline_t *pipeline = g2d.pipelines + g2d.batch[i].blending_mode;
		if (texture->vk.descriptor)
		{
			vkCmdBindPipeline(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline_layout, 0, 1, &texture->vk.descriptor, 0, NULL);
			vkCmdDraw(vk_core.cb, g2d.batch[i].vertex_count, 1, g2d.batch[i].vertex_offset, 0);
		} // FIXME else what?
	}
}

qboolean initVk2d( void )
{
	for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		if (!createPipeline(g2d.pipelines + i, i))
		{
			// TODO complain
			return false;
		}

	if (!createBuffer(&g2d.pics_buffer, sizeof(vertex_2d_t) * (MAX_PICS * 6),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		// FIXME cleanup
		return false;

	g2d.max_pics = MAX_PICS * 6;

	return true;
}

void deinitVk2d( void )
{
	destroyBuffer(&g2d.pics_buffer);
	for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i) {
		vkDestroyPipeline(vk_core.device, g2d.pipelines[i].pipeline, NULL);
		vkDestroyPipelineLayout(vk_core.device, g2d.pipelines[i].pipeline_layout, NULL);
	}
}
