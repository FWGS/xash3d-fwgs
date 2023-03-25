#include "vk_render.h"

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_geometry.h"
#include "vk_staging.h"
#include "vk_const.h"
#include "vk_common.h"
#include "vk_pipeline.h"
#include "vk_textures.h"
#include "vk_math.h"
#include "vk_rtx.h"
#include "vk_descriptor.h"
#include "vk_framectl.h" // FIXME needed for dynamic models cmdbuf
#include "vk_previous_frame.h"
#include "alolcator.h"
#include "profiler.h"
#include "r_speeds.h"

#include "eiface.h"
#include "xash3d_mathlib.h"
#include "protocol.h" // MAX_DLIGHTS

#include <memory.h>

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

#define PROFILER_SCOPES(X) \
	X(renderbegin, "VK_RenderBegin"); \

#define SCOPE_DECLARE(scope, name) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kVkRenderType_COUNT];

	vk_buffer_t uniform_buffer;
	uint32_t ubo_align;

	float fov_angle_y;

	struct {
		int dynamic_model_count;
	} stats;
} g_render;

static qboolean createPipelines( void )
{
	/* VkPushConstantRange push_const = { */
	/* 	.offset = 0, */
	/* 	.size = sizeof(AVec3f), */
	/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
	/* }; */

	VkDescriptorSetLayout descriptor_layouts[] = {
		vk_desc.one_uniform_buffer_layout,
		vk_desc.one_texture_layout,
		vk_desc.one_texture_layout,
		vk_desc.one_uniform_buffer_layout,
	};

	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = ARRAYSIZE(descriptor_layouts),
		.pSetLayouts = descriptor_layouts,
		/* .pushConstantRangeCount = 1, */
		/* .pPushConstantRanges = &push_const, */
	};

	// FIXME store layout separately
	XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g_render.pipeline_layout));

	{
		struct ShaderSpec {
			float alpha_test_threshold;
			uint32_t max_dlights;
		} spec_data = { .25f, MAX_DLIGHTS };
		const VkSpecializationMapEntry spec_map[] = {
			{.constantID = 0, .offset = offsetof(struct ShaderSpec, alpha_test_threshold), .size = sizeof(float) },
			{.constantID = 1, .offset = offsetof(struct ShaderSpec, max_dlights), .size = sizeof(uint32_t) },
		};

		VkSpecializationInfo shader_spec = {
			.mapEntryCount = ARRAYSIZE(spec_map),
			.pMapEntries = spec_map,
			.dataSize = sizeof(struct ShaderSpec),
			.pData = &spec_data
		};

		const VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, pos)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, normal)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, gl_tc)},
			{.binding = 0, .location = 3, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, lm_tc)},
			{.binding = 0, .location = 4, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = offsetof(vk_vertex_t, color)},
			{.binding = 0, .location = 6, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, prev_pos)},
		};

		const vk_shader_stage_t shader_stages[] = {
		{
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.filename = "brush.vert.spv",
			.specialization_info = NULL,
		}, {
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.filename = "brush.frag.spv",
			.specialization_info = &shader_spec,
		}};

		vk_pipeline_graphics_create_info_t ci = {
			.layout = g_render.pipeline_layout,
			.attribs = attribs,
			.num_attribs = ARRAYSIZE(attribs),

			.stages = shader_stages,
			.num_stages = ARRAYSIZE(shader_stages),

			.vertex_stride = sizeof(vk_vertex_t),

			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,

			.blendEnable = VK_FALSE,

			.cullMode = VK_CULL_MODE_FRONT_BIT,
		};

		for (int i = 0; i < kVkRenderType_COUNT; ++i)
		{
			const char *name = "UNDEFINED";
			switch (i)
			{
				case kVkRenderTypeSolid:
					spec_data.alpha_test_threshold = 0.f;
					ci.blendEnable = VK_FALSE;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					name = "kVkRenderTypeSolid";
					break;

				case kVkRenderType_A_1mA_RW:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD;
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "kVkRenderType_A_1mA_RW";
					break;

				case kVkRenderType_A_1mA_R:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD;
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "kVkRenderType_A_1mA_R";
					break;

				case kVkRenderType_A_1:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_FALSE; // Fake bloom, should be over geometry too
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD;
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					name = "kVkRenderType_A_1";
					break;

				case kVkRenderType_A_1_R:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD;
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					name = "kVkRenderType_A_1_R";
					break;

				case kVkRenderType_AT:
					spec_data.alpha_test_threshold = .25f;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_FALSE;
					name = "kVkRenderType_AT";
					break;

				case kVkRenderType_1_1_R:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD;
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					name = "kVkRenderType_1_1_R";
					break;

				default:
					ASSERT(!"Unreachable");
			}

			g_render.pipelines[i] = VK_PipelineGraphicsCreate(&ci);

			if (!g_render.pipelines[i])
			{
				// TODO complain
				return false;
			}

			if (vk_core.debug)
			{
				VkDebugUtilsObjectNameInfoEXT debug_name = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
					.objectHandle = (uint64_t)g_render.pipelines[i],
					.objectType = VK_OBJECT_TYPE_PIPELINE,
					.pObjectName = name,
				};
				XVK_CHECK(vkSetDebugUtilsObjectNameEXT(vk_core.device, &debug_name));
			}
		}
	}

	return true;
}

typedef struct {
	uint32_t num_lights, pad[3];
	struct {
		vec4_t pos_r;
		vec4_t color;
	} light[MAX_DLIGHTS];
} vk_ubo_lights_t;

#define MAX_DRAW_COMMANDS 8192 // TODO estimate
#define MAX_DEBUG_NAME_LENGTH 32

typedef struct render_draw_s {
	int lightmap, texture;
	int pipeline_index;
	uint32_t element_count;
	uint32_t index_offset, vertex_offset;
	/* TODO this should be a separate thing? */ struct { float r, g, b; } emissive;
} render_draw_t;

enum draw_command_type_e {
	DrawLabelBegin,
	DrawLabelEnd,
	DrawDraw
};

typedef struct {
	enum draw_command_type_e type;
	union {
		char debug_label[MAX_DEBUG_NAME_LENGTH];
		struct {
			render_draw_t draw;
			uint32_t ubo_offset;
			matrix3x4 transform;
		} draw;
	};
} draw_command_t;

static struct {
	int uniform_data_set_mask;
	uniform_data_t current_uniform_data;
	uniform_data_t dirty_uniform_data;

	r_flipping_buffer_t uniform_alloc;
	uint32_t current_ubo_offset_FIXME;

	draw_command_t draw_commands[MAX_DRAW_COMMANDS];
	int num_draw_commands;

	matrix4x4 model, view, projection;

	qboolean current_frame_is_ray_traced;
} g_render_state;

qboolean VK_RenderInit( void ) {
	PROFILER_SCOPES(APROF_SCOPE_INIT);

	g_render.ubo_align = Q_max(4, vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);

	const uint32_t uniform_unit_size = ((sizeof(uniform_data_t) + g_render.ubo_align - 1) / g_render.ubo_align) * g_render.ubo_align;
	const uint32_t uniform_buffer_size = uniform_unit_size * MAX_UNIFORM_SLOTS;
	R_FlippingBuffer_Init(&g_render_state.uniform_alloc, uniform_buffer_size);

	if (!VK_BufferCreate("render uniform_buffer", &g_render.uniform_buffer, uniform_buffer_size,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | (vk_core.rtx ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0)))
		return false;

	{
		VkDescriptorBufferInfo dbi_uniform_data = {
			.buffer = g_render.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(uniform_data_t),
		};
		VkDescriptorBufferInfo dbi_uniform_lights = {
			.buffer = g_render.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(vk_ubo_lights_t),
		};
		VkWriteDescriptorSet wds[] = {{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &dbi_uniform_data,
				.dstSet = vk_desc.ubo_sets[0], // FIXME
			}, {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstBinding = 0,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
				.pBufferInfo = &dbi_uniform_lights,
				.dstSet = vk_desc.ubo_sets[1], // FIXME
			}};
		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
	}

	if (!createPipelines())
		return false;

	R_SpeedsRegisterMetric(&g_render.stats.dynamic_model_count, "models_dynamic", "");
	return true;
}

void VK_RenderShutdown( void )
{
	for (int i = 0; i < ARRAYSIZE(g_render.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g_render.pipelines[i], NULL);
	vkDestroyPipelineLayout( vk_core.device, g_render.pipeline_layout, NULL );

	VK_BufferDestroy( &g_render.uniform_buffer );
}

enum {
	UNIFORM_UNSET = 0,
	UNIFORM_SET_MATRIX_MODEL = 2,
	UNIFORM_SET_MATRIX_VIEW = 4,
	UNIFORM_SET_MATRIX_PROJECTION = 8,
	UNIFORM_SET_ALL = UNIFORM_SET_MATRIX_MODEL | UNIFORM_SET_MATRIX_VIEW | UNIFORM_SET_MATRIX_PROJECTION,
	UNIFORM_UPLOADED = 16,
};

void VK_RenderBegin( qboolean ray_tracing ) {
	APROF_SCOPE_BEGIN(renderbegin);

	g_render_state.uniform_data_set_mask = UNIFORM_UNSET;
	g_render_state.current_ubo_offset_FIXME = UINT32_MAX;
	memset(&g_render_state.current_uniform_data, 0, sizeof(g_render_state.current_uniform_data));
	memset(&g_render_state.dirty_uniform_data, 0, sizeof(g_render_state.dirty_uniform_data));
	R_FlippingBuffer_Flip(&g_render_state.uniform_alloc);

	g_render_state.num_draw_commands = 0;
	g_render_state.current_frame_is_ray_traced = ray_tracing;

	R_GeometryBuffer_Flip();

	if (ray_tracing)
		VK_RayFrameBegin();

	APROF_SCOPE_END(renderbegin);
}

// Vulkan has Y pointing down, and z should end up in (0, 1)
// NOTE this matrix is row-major
static const matrix4x4 vk_proj_fixup = {
	{1, 0, 0, 0},
	{0, -1, 0, 0},
	{0, 0, .5, .5},
	{0, 0, 0, 1}
};

void VK_RenderStateSetMatrixProjection(const matrix4x4 projection, float fov_angle_y)
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_MATRIX_PROJECTION;
	Matrix4x4_Concat( g_render_state.projection, vk_proj_fixup, projection );
	g_render.fov_angle_y = fov_angle_y;
}

void VK_RenderStateSetMatrixView(const matrix4x4 view)
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_MATRIX_VIEW;
	Matrix4x4_Copy(g_render_state.view, view);
}

void VK_RenderStateSetMatrixModel( const matrix4x4 model )
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_MATRIX_MODEL;
	Matrix4x4_Copy(g_render_state.model, model);

	// Assume that projection and view matrices are already properly set
	ASSERT(g_render_state.uniform_data_set_mask & UNIFORM_SET_MATRIX_VIEW);
	ASSERT(g_render_state.uniform_data_set_mask & UNIFORM_SET_MATRIX_PROJECTION);

	{
		matrix4x4 mv, mvp;
		// TODO this can be cached (on a really slow device?)
		Matrix4x4_Concat(mv, g_render_state.view, g_render_state.model);
		Matrix4x4_Concat(mvp, g_render_state.projection, mv);
		Matrix4x4_ToArrayFloatGL(mvp, (float*)g_render_state.dirty_uniform_data.mvp);
	}
}

static uint32_t allocUniform( uint32_t size, uint32_t alignment ) {
	// FIXME Q_max is not correct, we need NAIMENSCHEEE OBSCHEEE KRATNOE
	const uint32_t align = Q_max(alignment, g_render.ubo_align);
	const uint32_t offset = R_FlippingBuffer_Alloc(&g_render_state.uniform_alloc, size, align);
	return offset;
}

static draw_command_t *drawCmdAlloc( void ) {
	ASSERT(g_render_state.num_draw_commands < ARRAYSIZE(g_render_state.draw_commands));
	return g_render_state.draw_commands + (g_render_state.num_draw_commands++);
}

static void drawCmdPushDebugLabelBegin( const char *debug_label ) {
	if (vk_core.debug) {
		draw_command_t *draw_command = drawCmdAlloc();
		draw_command->type = DrawLabelBegin;
		Q_strncpy(draw_command->debug_label, debug_label, sizeof draw_command->debug_label);
	}
}

static void drawCmdPushDebugLabelEnd( void ) {
	if (vk_core.debug) {
		draw_command_t *draw_command = drawCmdAlloc();
		draw_command->type = DrawLabelEnd;
	}
}

// FIXME get rid of this garbage
static uint32_t getUboOffset_FIXME( void ) {
	// Figure out whether we need to update UBO data, and upload new data if we do
	// TODO generally it's not safe to do memcmp for structures comparison
	if (g_render_state.current_ubo_offset_FIXME == UINT32_MAX
		|| ((g_render_state.uniform_data_set_mask & UNIFORM_UPLOADED) == 0)
		|| memcmp(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.current_uniform_data)) != 0) {
		g_render_state.current_ubo_offset_FIXME = allocUniform(sizeof(uniform_data_t), 16 /* why 16? vec4? */);

		if (g_render_state.current_ubo_offset_FIXME == ALO_ALLOC_FAILED)
			return UINT32_MAX;

		uniform_data_t *const ubo = (uniform_data_t*)((byte*)g_render.uniform_buffer.mapped + g_render_state.current_ubo_offset_FIXME);
		memcpy(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.dirty_uniform_data));
		memcpy(ubo, &g_render_state.current_uniform_data, sizeof(*ubo));
		g_render_state.uniform_data_set_mask |= UNIFORM_UPLOADED;
	}

	return g_render_state.current_ubo_offset_FIXME;
}

static void drawCmdPushDraw( const render_draw_t *draw )
{
	draw_command_t *draw_command;

	ASSERT(draw->pipeline_index >= 0);
	ASSERT(draw->pipeline_index < ARRAYSIZE(g_render.pipelines));
	ASSERT(draw->lightmap >= 0);
	ASSERT(draw->texture >= 0);

	if ((g_render_state.uniform_data_set_mask & UNIFORM_SET_ALL) != UNIFORM_SET_ALL) {
		gEngine.Con_Printf( S_ERROR "Not all uniform state was initialized prior to rendering\n" );
		return;
	}

	if (g_render_state.num_draw_commands >= ARRAYSIZE(g_render_state.draw_commands)) {
		gEngine.Con_Printf( S_ERROR "Maximum number of draw commands reached\n" );
		return;
	}

	const uint32_t ubo_offset = getUboOffset_FIXME();
	if (ubo_offset == ALO_ALLOC_FAILED) {
		// TODO stagger this
		gEngine.Con_Printf( S_ERROR "Ran out of uniform slots\n" );
		return;
	}

	draw_command = drawCmdAlloc();
	draw_command->draw.draw = *draw;
	draw_command->draw.ubo_offset = ubo_offset;
	draw_command->type = DrawDraw;
	Matrix3x4_Copy(draw_command->draw.transform, g_render_state.model);
}

// Return offset of dlights data into UBO buffer
static uint32_t writeDlightsToUBO( void )
{
	vk_ubo_lights_t* ubo_lights;
	int num_lights = 0;
	const uint32_t ubo_lights_offset = allocUniform(sizeof(*ubo_lights), 4);
	if (ubo_lights_offset == UINT32_MAX) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate UBO for DLights\n");
		return UINT32_MAX;
	}
	ubo_lights = (vk_ubo_lights_t*)((byte*)(g_render.uniform_buffer.mapped) + ubo_lights_offset);

	// TODO this should not be here (where? vk_scene?)
	for (int i = 0; i < MAX_DLIGHTS && num_lights < ARRAYSIZE(ubo_lights->light); ++i) {
		const dlight_t *l = gEngine.GetDynamicLight(i);
		if( !l || l->die < gpGlobals->time || !l->radius )
			continue;
		Vector4Set(
			ubo_lights->light[num_lights].color,
			l->color.r / 255.f,
			l->color.g / 255.f,
			l->color.b / 255.f,
			1.f);
		Vector4Set(
			ubo_lights->light[num_lights].pos_r,
			l->origin[0],
			l->origin[1],
			l->origin[2],
			l->radius);

		num_lights++;
	}

	ubo_lights->num_lights = num_lights;
	return ubo_lights_offset;
}

void VK_Render_FIXME_Barrier( VkCommandBuffer cmdbuf ) {
	const VkBuffer geom_buffer = R_GeometryBuffer_Get();
	// FIXME
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			//.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, // FIXME
			.dstAccessMask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT , // FIXME
			.buffer = geom_buffer,
			.offset = 0, // FIXME
			.size = VK_WHOLE_SIZE, // FIXME
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			//VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			//VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}
}

void VK_RenderEnd( VkCommandBuffer cmdbuf )
{
	// TODO we can sort collected draw commands for more efficient and correct rendering
	// that requires adding info about distance to camera for correct order-dependent blending

	int pipeline = -1;
	int texture = -1;
	int lightmap = -1;
	uint32_t ubo_offset = -1;

	const uint32_t dlights_ubo_offset = writeDlightsToUBO();
	if (dlights_ubo_offset == UINT32_MAX)
		return;

	ASSERT(!g_render_state.current_frame_is_ray_traced);


	{
		const VkBuffer geom_buffer = R_GeometryBuffer_Get();
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmdbuf, 0, 1, &geom_buffer, &offset);
		vkCmdBindIndexBuffer(cmdbuf, geom_buffer, 0, VK_INDEX_TYPE_UINT16);
	}

	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 3, 1, vk_desc.ubo_sets + 1, 1, &dlights_ubo_offset);

	for (int i = 0; i < g_render_state.num_draw_commands; ++i) {
		const draw_command_t *const draw = g_render_state.draw_commands + i;

		switch (draw->type) {
			case DrawLabelBegin:
				{
					VkDebugUtilsLabelEXT label = {
						.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
						.pLabelName = draw->debug_label,
					};
					vkCmdBeginDebugUtilsLabelEXT(cmdbuf, &label);
				}
				continue;
			case DrawLabelEnd:
				vkCmdEndDebugUtilsLabelEXT(cmdbuf);
				continue;
		}

		if (ubo_offset != draw->draw.ubo_offset)
		{
			ubo_offset = draw->draw.ubo_offset;
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 0, 1, vk_desc.ubo_sets, 1, &ubo_offset);
		}

		if (pipeline != draw->draw.draw.pipeline_index) {
			pipeline = draw->draw.draw.pipeline_index;
			vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipelines[pipeline]);
		}

		if (lightmap != draw->draw.draw.lightmap) {
			lightmap = draw->draw.draw.lightmap;
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 2, 1, &findTexture(lightmap)->vk.descriptor, 0, NULL);
		}

		if (texture != draw->draw.draw.texture)
		{
			texture = draw->draw.draw.texture;
			// TODO names/enums for binding points
			vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 1, 1, &findTexture(texture)->vk.descriptor, 0, NULL);
		}

		// Only indexed mode is supported
		ASSERT(draw->draw.draw.index_offset >= 0);
		vkCmdDrawIndexed(cmdbuf, draw->draw.draw.element_count, 1, draw->draw.draw.index_offset, draw->draw.draw.vertex_offset, 0);
	}
}

void VK_RenderDebugLabelBegin( const char *name )
{
	drawCmdPushDebugLabelBegin(name);
}

void VK_RenderDebugLabelEnd( void )
{
	drawCmdPushDebugLabelEnd();
}

void VK_RenderEndRTX( VkCommandBuffer cmdbuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h )
{
	const VkBuffer geom_buffer = R_GeometryBuffer_Get();
	ASSERT(vk_core.rtx);

	{
		const vk_ray_frame_render_args_t args = {
			.cmdbuf = cmdbuf,
			.dst = {
				.image_view = img_dst_view,
				.image = img_dst,
				.width = w,
				.height = h,
			},

			.projection = &g_render_state.projection,
			.view = &g_render_state.view,

			.geometry_data = {
				.buffer = geom_buffer,
				.size = VK_WHOLE_SIZE,
			},

			.fov_angle_y = g_render.fov_angle_y,
		};

		VK_RayFrameEnd(&args);
	}
}

qboolean VK_RenderModelInit( vk_render_model_t *model ) {
	if (vk_core.rtx && (g_render_state.current_frame_is_ray_traced || !model->dynamic)) {
		const VkBuffer geom_buffer = R_GeometryBuffer_Get();
		// TODO runtime rtx switch: ???
		const vk_ray_model_init_t args = {
			.buffer = geom_buffer,
			.model = model,
		};
		model->ray_model = VK_RayModelCreate(args);
		model->dynamic_polylights = NULL;
		model->dynamic_polylights_count = 0;
		return !!model->ray_model;
	}

	// TODO pre-bake optimal draws
	return true;
}

void VK_RenderModelDestroy( vk_render_model_t* model ) {
	// FIXME why the condition? we should do the cleanup anyway
	if (vk_core.rtx && (g_render_state.current_frame_is_ray_traced || !model->dynamic)) {
		VK_RayModelDestroy(model->ray_model);
		if (model->dynamic_polylights)
			Mem_Free(model->dynamic_polylights);
	}
}

void VK_RenderModelDraw( const cl_entity_t *ent, vk_render_model_t* model ) {
	int current_texture = -1;
	int element_count = 0;
	int index_offset = -1;
	int vertex_offset = 0;

	// TODO get rid of this dirty ubo thing
	Vector4Copy(model->color, g_render_state.dirty_uniform_data.color);
	ASSERT(model->lightmap <= MAX_LIGHTMAPS);
	const int lightmap = model->lightmap > 0 ? tglob.lightmapTextures[model->lightmap - 1] : tglob.whiteTexture;

	if (g_render_state.current_frame_is_ray_traced) {
		if (ent != NULL && model != NULL) {
			R_PrevFrame_SaveCurrentState( ent->index, g_render_state.model );
			R_PrevFrame_ModelTransform( ent->index, model->prev_transform );
		}
		else {
			Matrix4x4_Copy( model->prev_transform, g_render_state.model );
		}

		VK_RayFrameAddModel(model->ray_model, model, (const matrix3x4*)g_render_state.model);

		return;
	}

	drawCmdPushDebugLabelBegin( model->debug_name );

	for (int i = 0; i < model->num_geometries; ++i) {
		const vk_render_geometry_t *geom = model->geometries + i;
		const qboolean split = current_texture != geom->texture
			|| vertex_offset != geom->vertex_offset
			|| (index_offset + element_count) != geom->index_offset;

		// We only support indexed geometry
		ASSERT(geom->index_offset >= 0);

		if (geom->texture < 0)
			continue;

		if (split) {
			if (element_count) {
				render_draw_t draw = {
					.lightmap = lightmap,
					.texture = current_texture,
					.pipeline_index = model->render_type,
					.element_count = element_count,
					.vertex_offset = vertex_offset,
					.index_offset = index_offset,
				};

				drawCmdPushDraw( &draw );
			}

			current_texture = geom->texture;
			index_offset = geom->index_offset;
			vertex_offset = geom->vertex_offset;
			element_count = 0;
		}

		// Make sure that all surfaces are concatenated in buffers
		ASSERT(index_offset + element_count == geom->index_offset);
		element_count += geom->element_count;
	}

	if (element_count) {
		const render_draw_t draw = {
			.lightmap = lightmap,
			.texture = current_texture,
			.pipeline_index = model->render_type,
			.element_count = element_count,
			.vertex_offset = vertex_offset,
			.index_offset = index_offset,
		};

		drawCmdPushDraw( &draw );
	}

	drawCmdPushDebugLabelEnd();
}

#define MAX_DYNAMIC_GEOMETRY 256

static struct {
	vk_render_model_t model;
	vk_render_geometry_t geometries[MAX_DYNAMIC_GEOMETRY];
} g_dynamic_model = {0};

void VK_RenderModelDynamicBegin( vk_render_type_e render_type, const vec4_t color, const char *debug_name_fmt, ... ) {
	va_list argptr;
	va_start( argptr, debug_name_fmt );
	vsnprintf(g_dynamic_model.model.debug_name, sizeof(g_dynamic_model.model.debug_name), debug_name_fmt, argptr );
	va_end( argptr );

	ASSERT(!g_dynamic_model.model.geometries);
	g_dynamic_model.model.geometries = g_dynamic_model.geometries;
	g_dynamic_model.model.num_geometries = 0;
	g_dynamic_model.model.render_type = render_type;
	g_dynamic_model.model.lightmap = 0;
	Vector4Copy(color, g_dynamic_model.model.color);
}
void VK_RenderModelDynamicAddGeometry( const vk_render_geometry_t *geom ) {
	ASSERT(g_dynamic_model.model.geometries);
	if (g_dynamic_model.model.num_geometries == MAX_DYNAMIC_GEOMETRY) {
		ERROR_THROTTLED(10, "Ran out of dynamic model geometry slots for model %s", g_dynamic_model.model.debug_name);
		return;
	}

	g_dynamic_model.geometries[g_dynamic_model.model.num_geometries++] = *geom;
}
void VK_RenderModelDynamicCommit( void ) {
	ASSERT(g_dynamic_model.model.geometries);

	if (g_dynamic_model.model.num_geometries > 0) {
		g_render.stats.dynamic_model_count++;
		g_dynamic_model.model.dynamic = true;
		VK_RenderModelInit( &g_dynamic_model.model );
		VK_RenderModelDraw( NULL, &g_dynamic_model.model );
	}

	g_dynamic_model.model.debug_name[0] = '\0';
	g_dynamic_model.model.geometries = NULL;
}
