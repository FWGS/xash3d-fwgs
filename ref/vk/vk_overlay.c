#include "vk_overlay.h"

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


typedef struct vertex_2d_s {
	float x, y;
	float u, v;
	color_rgba8_t color;
} vertex_2d_t;

// TODO should these be dynamic?
#define MAX_PICS 16384
#define MAX_VERTICES (MAX_PICS * 6)
#define MAX_BATCHES 256

typedef struct {
	uint32_t vertex_offset, vertex_count;
	int texture;
	int blending_mode;
} batch_t;

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + 1];

	vk_buffer_t pics_buffer;
	r_flipping_buffer_t pics_buffer_alloc;
	qboolean exhausted_this_frame;

	batch_t batch[MAX_BATCHES];
	int batch_count;

	// TODO texture bindings?
} g2d;

static vertex_2d_t* allocQuadVerts(int blending_mode, int texnum) {
	const uint32_t pics_offset = R_FlippingBuffer_Alloc(&g2d.pics_buffer_alloc, 6, 1);
	vertex_2d_t* const ptr = ((vertex_2d_t*)(g2d.pics_buffer.mapped)) + pics_offset;
	batch_t *batch = g2d.batch + (g2d.batch_count-1);

	if (pics_offset == ALO_ALLOC_FAILED) {
		if (!g2d.exhausted_this_frame) {
			gEngine.Con_Printf(S_ERROR "2d: ran out of vertex memory\n");
			g2d.exhausted_this_frame = true;
		}
		return NULL;
	}

	if (batch->texture != texnum
		|| batch->blending_mode != blending_mode
		|| batch->vertex_offset > pics_offset) {
		if (batch->vertex_count != 0) {
			if (g2d.batch_count == MAX_BATCHES) {
				if (!g2d.exhausted_this_frame) {
					gEngine.Con_Printf(S_ERROR "2d: ran out of batch memory\n");
					g2d.exhausted_this_frame = true;
				}
				return NULL;
			}

			++g2d.batch_count;
			batch++;
		}

		batch->vertex_offset = pics_offset;
		batch->vertex_count = 0;
		batch->texture = texnum;
		batch->blending_mode = blending_mode;
	}

	batch->vertex_count += 6;
	ASSERT(batch->vertex_count + batch->vertex_offset <= MAX_VERTICES);
	return ptr;
}

void R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int texnum )
{
	vertex_2d_t *const p = allocQuadVerts(vk_renderstate.blending_mode, texnum);

	if (!p) {
		/* gEngine.Con_Printf(S_ERROR "VK FIXME %s(%f, %f, %f, %f, %f, %f, %f, %f, %d(%s))\n", __FUNCTION__, */
		/* 	x, y, w, h, s1, t1, s2, t2, texnum, findTexture(texnum)->name); */
		return;
	}

	{
		// TODO do this in shader bro
		const float vw = vk_frame.width;
		const float vh = vk_frame.height;
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

static void clearAccumulated( void ) {
	R_FlippingBuffer_Flip(&g2d.pics_buffer_alloc);

	g2d.batch_count = 1;
	g2d.batch[0].texture = -1;
	g2d.batch[0].vertex_offset = 0;
	g2d.batch[0].vertex_count = 0;
	g2d.exhausted_this_frame = false;
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

qboolean R_VkOverlay_Init( void ) {
	if (!createPipelines())
		return false;

	// TODO this doesn't need to be host visible, could use staging too
	if (!VK_BufferCreate("2d pics_buffer", &g2d.pics_buffer, sizeof(vertex_2d_t) * MAX_VERTICES,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ))
		// FIXME cleanup
		return false;

	R_FlippingBuffer_Init(&g2d.pics_buffer_alloc, MAX_VERTICES);

	return true;
}

void R_VkOverlay_Shutdown( void ) {
	VK_BufferDestroy(&g2d.pics_buffer);
	for (int i = 0; i < ARRAYSIZE(g2d.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g2d.pipelines[i], NULL);

	vkDestroyPipelineLayout(vk_core.device, g2d.pipeline_layout, NULL);
}

void R_VkOverlay_DrawAndFlip( VkCommandBuffer cmdbuf ) {
	DEBUG_BEGIN(cmdbuf, "2d overlay");

	{
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g2d.pics_buffer.buffer, &offset);
	}

	for (int i = 0; i < g2d.batch_count && g2d.batch[i].vertex_count > 0; ++i)
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

	DEBUG_END(cmdbuf);

	clearAccumulated();
}

void R_DrawStretchRaw( float x, float y, float w, float h, int cols, int rows, const byte *data, qboolean dirty )
{
	PRINT_NOT_IMPLEMENTED();
}

void R_DrawTileClear( int texnum, int x, int y, int w, int h )
{
	PRINT_NOT_IMPLEMENTED_ARGS("%s", findTexture(texnum)->name );
}

void CL_FillRGBA( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransAdd);
}

void CL_FillRGBABlend( float x, float y, float w, float h, int r, int g, int b, int a )
{
	drawFill(x, y, w, h, r, g, b, a, kRenderTransColor);
}
