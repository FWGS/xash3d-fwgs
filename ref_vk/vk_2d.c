#include "vk_2d.h"

#include "vk_buffer.h"
#include "vk_core.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_framectl.h"
#include "vk_renderstate.h"
#include "vk_pipeline.h"
#include "vk_descriptor.h"

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

typedef struct vertex_2d_s {
	float x, y;
	float u, v;
	color_rgba8_t color;
} vertex_2d_t;

// TODO should these be dynamic?
#define MAX_PICS 16384
#define MAX_BATCHES 256

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + 1];

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

static vertex_2d_t* allocQuadVerts(int blending_mode, int texnum)
{
	vertex_2d_t* const ptr = ((vertex_2d_t*)(g2d.pics_buffer.mapped)) + g2d.num_pics;
	if (g2d.batch[g2d.current_batch].texture != texnum || g2d.batch[g2d.current_batch].blending_mode != blending_mode)
	{
		if (g2d.batch[g2d.current_batch].vertex_count != 0)
		{
			if (g2d.current_batch == MAX_BATCHES - 1)
			{
				gEngine.Con_Printf(S_ERROR "VK FIXME RAN OUT OF BATCHES");
				return NULL;
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
		gEngine.Con_Printf(S_ERROR "VK FIXME RAN OUT OF BUFFER");
		return NULL;
	}

	g2d.num_pics += 6;
	g2d.batch[g2d.current_batch].vertex_count += 6;
	return ptr;
}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	vertex_2d_t *p = allocQuadVerts(vk_renderstate.blending_mode, texnum);

	if (!p) {
		gEngine.Con_Printf(S_ERROR "VK FIXME %s(%f, %f, %f, %f, %f, %f, %f, %f, %d(%s))\n", __FUNCTION__,
			x, y, w, h, s1, t1, s2, t2, texnum, findTexture(texnum)->name);

		return;
	}

	{
		const float vw = vk_frame.create_info.imageExtent.width;
		const float vh = vk_frame.create_info.imageExtent.height;
		const float x1 = (x / vw)*2.f - 1.f;
		const float y1 = (y / vh)*2.f - 1.f;
		const float x2 = ((x + w) / vw)*2.f - 1.f;
		const float y2 = ((y + h) / vh)*2.f - 1.f;
		const color_rgba8_t color = vk_renderstate.tri_color;

		p[0] = (vertex_2d_t){x1, y1, s1, t1, color};
		p[1] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[2] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[3] = (vertex_2d_t){x2, y1, s2, t1, color};
		p[4] = (vertex_2d_t){x1, y2, s1, t2, color};
		p[5] = (vertex_2d_t){x2, y2, s2, t2, color};
	}
}

static void drawFill( float x, float y, float w, float h, int r, int g, int b, int a, int blending_mode )
{
	const color_rgba8_t prev_color = vk_renderstate.tri_color;
	const int prev_blending = vk_renderstate.blending_mode;
	vk_renderstate.blending_mode = blending_mode;
	vk_renderstate.tri_color = (color_rgba8_t){r, g, b, a};
	R_DrawStretchPic(x, y, w, h, 0, 0, 1, 1, VK_FindTexture(REF_WHITE_TEXTURE));
	vk_renderstate.tri_color = prev_color;
	vk_renderstate.blending_mode = prev_blending;
}

void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransAdd);
}

void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransColor);
}

void vk2dBegin( void )
{
	g2d.num_pics = 0;
	g2d.current_batch = 0;
	g2d.batch[0].vertex_count = 0;
}

void vk2dEnd( VkCommandBuffer cmdbuf )
{
	const VkDeviceSize offset = 0;

	if (!g2d.num_pics)
		return;

	vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g2d.pics_buffer.buffer, &offset);

	if (vk_core.debug)
	{
		VkDebugUtilsLabelEXT label = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = "2d overlay",
		};
		vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label);
	}

	for (int i = 0; i <= g2d.current_batch; ++i)
	{
		vk_texture_t *texture = findTexture(g2d.batch[i].texture);
		const VkPipeline pipeline = g2d.pipelines[g2d.batch[i].blending_mode];
		if (texture->vk.descriptor)
		{
			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g2d.pipeline_layout, 0, 1, &texture->vk.descriptor, 0, NULL);
			vkCmdDraw(cmdbuf, g2d.batch[i].vertex_count, 1, g2d.batch[i].vertex_offset, 0);
		} // FIXME else what?
	}

	if (vk_core.debug)
		vkCmdEndDebugUtilsLabelEXT(cmdbuf);
}

static qboolean createPipelines( void )
{
	{
		/* VkPushConstantRange push_const = { */
		/* 	.offset = 0, */
		/* 	.size = sizeof(AVec3f), */
		/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
		/* }; */

		VkDescriptorSetLayout descriptor_layouts[] = {
			vk_desc.one_texture_layout,
		};

		VkPipelineLayoutCreateInfo plci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = ARRAYSIZE(descriptor_layouts),
			.pSetLayouts = descriptor_layouts,
			/* .pushConstantRangeCount = 1, */
			/* .pPushConstantRanges = &push_const, */
		};

		XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g2d.pipeline_layout));
	}

	{
		const VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, x)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vertex_2d_t, u)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vertex_2d_t, color)},
		};

		const vk_shader_stage_t shader_stages[] = {
		{
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.filename = "2d.vert.spv",
		}, {
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.filename = "2d.frag.spv",
		}};

		vk_pipeline_graphics_create_info_t pci = {
			.layout = g2d.pipeline_layout,
			.attribs = attribs,
			.num_attribs = ARRAYSIZE(attribs),
			.stages = shader_stages,
			.num_stages = ARRAYSIZE(shader_stages),
			.vertex_stride = sizeof(vertex_2d_t),
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_ALWAYS,
			.cullMode = VK_CULL_MODE_NONE,
		};

		for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		{
			switch (i)
			{
				case kRenderNormal:
					pci.blendEnable = VK_FALSE;
					break;

				case kRenderTransColor:
				case kRenderTransTexture:
					pci.blendEnable = VK_TRUE;
					pci.colorBlendOp = VK_BLEND_OP_ADD;
					pci.srcAlphaBlendFactor = pci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					pci.dstAlphaBlendFactor = pci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;

				case kRenderTransAlpha:
					pci.blendEnable = VK_FALSE;
					// FIXME pglEnable( GL_ALPHA_TEST );
					break;

				case kRenderGlow:
				case kRenderTransAdd:
					pci.blendEnable = VK_TRUE;
					pci.colorBlendOp = VK_BLEND_OP_ADD;
					pci.srcAlphaBlendFactor = pci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					pci.dstAlphaBlendFactor = pci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					break;
			}

			g2d.pipelines[i] = VK_PipelineGraphicsCreate(&pci);

			if (!g2d.pipelines[i])
			{
				// TODO complain
				return false;
			}
		}
	}

	return true;
}

qboolean initVk2d( void )
{
	if (!createPipelines())
		return false;

	if (!createBuffer("2d pics_buffer", &g2d.pics_buffer, sizeof(vertex_2d_t) * (MAX_PICS * 6),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		// FIXME cleanup
		return false;

	g2d.max_pics = MAX_PICS * 6;

	return true;
}

void deinitVk2d( void )
{
	destroyBuffer(&g2d.pics_buffer);
	for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g2d.pipelines[i], NULL);

	vkDestroyPipelineLayout(vk_core.device, g2d.pipeline_layout, NULL);
}
