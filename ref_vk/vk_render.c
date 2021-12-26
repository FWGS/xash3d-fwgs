#include "vk_render.h"

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_const.h"
#include "vk_common.h"
#include "vk_pipeline.h"
#include "vk_textures.h"
#include "vk_math.h"
#include "vk_rtx.h"
#include "vk_descriptor.h"

#include "eiface.h"
#include "xash3d_mathlib.h"
#include "protocol.h" // MAX_DLIGHTS

#include "camera.h"
#include "pm_defs.h"
#include "pmtrace.h"

#include <memory.h>

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

// TODO estimate
#define MAX_ALLOCS 1024

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + 1];

	vk_buffer_t buffer;
	vk_ring_buffer_t buffer_alloc_ring;

	vk_buffer_t uniform_buffer;
	uint32_t ubo_align;

	float fov_angle_y;
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
			{.binding = 0, .location = 5, .format = VK_FORMAT_R32_UINT, .offset = offsetof(vk_vertex_t, flags)},
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

		for (int i = 0; i < ARRAYSIZE(g_render.pipelines); ++i)
		{
			const char *name = "UNDEFINED";
			switch (i)
			{
				case kRenderNormal:
					spec_data.alpha_test_threshold = 0.f;
					ci.blendEnable = VK_FALSE;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					name = "brush kRenderNormal";
					break;

				case kRenderTransColor:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "brush kRenderTransColor";
					break;

				case kRenderTransAdd:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check

					// sprites do SRC_ALPHA
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;// TODO ? FACTOR_ONE;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					name = "brush kRenderTransAdd";
					break;

				case kRenderTransAlpha:
					spec_data.alpha_test_threshold = .25f;
					ci.depthWriteEnable = VK_TRUE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_FALSE;
					name = "brush kRenderTransAlpha(test)";
					break;

				case kRenderGlow:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					break;

				case kRenderTransTexture:
					spec_data.alpha_test_threshold = 0.f;
					ci.depthWriteEnable = VK_FALSE;
					ci.depthTestEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "brush kRenderTransTexture/Glow";
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

qboolean VK_RenderInit( void )
{
	const uint32_t vertex_buffer_size = MAX_BUFFER_VERTICES * sizeof(vk_vertex_t);
	const uint32_t index_buffer_size = MAX_BUFFER_INDICES * sizeof(uint16_t);
	uint32_t uniform_unit_size;

	g_render.ubo_align = Q_max(4, vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);
	uniform_unit_size = ((sizeof(uniform_data_t) + g_render.ubo_align - 1) / g_render.ubo_align) * g_render.ubo_align;

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer("render buffer", &g_render.buffer, vertex_buffer_size + index_buffer_size,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (vk_core.rtx ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0),
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | (vk_core.rtx ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0))) // TODO staging buffer?
		return false;

	if (!createBuffer("render uniform_buffer", &g_render.uniform_buffer, uniform_unit_size * MAX_UNIFORM_SLOTS,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | (vk_core.rtx ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0))) // TODO staging buffer?
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

	g_render.buffer_alloc_ring.size = g_render.buffer.size;

	return true;
}

void VK_RenderShutdown( void )
{
	for (int i = 0; i < ARRAYSIZE(g_render.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g_render.pipelines[i], NULL);
	vkDestroyPipelineLayout( vk_core.device, g_render.pipeline_layout, NULL );

	destroyBuffer( &g_render.buffer );
	destroyBuffer( &g_render.uniform_buffer );
}

xvk_render_buffer_allocation_t XVK_RenderBufferAllocAndLock( uint32_t unit_size, uint32_t count ) {
	const uint32_t alloc_size = unit_size * count;
	uint32_t offset;
	xvk_render_buffer_allocation_t retval = {0};

	ASSERT(unit_size > 0);

	offset = VK_RingBuffer_Alloc(&g_render.buffer_alloc_ring, alloc_size, unit_size);

	if (offset == AllocFailed) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate %u bytes aligned at %u from buffer; only %u are left",
				alloc_size, unit_size, g_render.buffer_alloc_ring.free);
		return retval;
	}

	// TODO bake sequence number into handle (to detect buffer lifetime misuse)
	retval.buffer.unit.size = unit_size;
	retval.buffer.unit.count = count;
	retval.buffer.unit.offset = offset / unit_size;
	retval.ptr = ((byte*)g_render.buffer.mapped) + offset;

	return retval;
}

void XVK_RenderBufferUnlock( xvk_render_buffer_t handle ) {
	// TODO check whether we need to upload something from staging, etc
}

void XVK_RenderBufferMapFreeze( void ) {
	VK_RingBuffer_Fix(&g_render.buffer_alloc_ring);
}

void XVK_RenderBufferMapClear( void ) {
	VK_RingBuffer_Clear(&g_render.buffer_alloc_ring);
}

void XVK_RenderBufferFrameClear( /*int frame_id*/void ) {
	VK_RingBuffer_ClearFrame(&g_render.buffer_alloc_ring);
}

void XVK_RenderBufferPrintStats( void ) {
	// TODO get alignment holes size
	gEngine.Con_Reportf("Buffer usage: %uKiB of (%uKiB)\n",
		g_render.buffer_alloc_ring.permanent_size / 1024,
		g_render.buffer.size / 1024);
}

#define MAX_DRAW_COMMANDS 8192 // TODO estimate
#define MAX_DEBUG_NAME_LENGTH 32

typedef struct render_draw_s {
	int lightmap, texture;
	int render_mode;
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

	uint32_t current_ubo_offset;
	uint32_t uniform_free_offset;

	draw_command_t draw_commands[MAX_DRAW_COMMANDS];
	int num_draw_commands;

	matrix4x4 model, view, projection;

	qboolean current_frame_is_ray_traced;
} g_render_state;

enum {
	UNIFORM_UNSET = 0,
	UNIFORM_SET_COLOR = 1,
	UNIFORM_SET_MATRIX_MODEL = 2,
	UNIFORM_SET_MATRIX_VIEW = 4,
	UNIFORM_SET_MATRIX_PROJECTION = 8,
	UNIFORM_SET_ALL = UNIFORM_SET_COLOR | UNIFORM_SET_MATRIX_MODEL | UNIFORM_SET_MATRIX_VIEW | UNIFORM_SET_MATRIX_PROJECTION,
	UNIFORM_UPLOADED = 16,
};

void VK_RenderBegin( qboolean ray_tracing ) {
	g_render_state.uniform_free_offset = 0;
	g_render_state.uniform_data_set_mask = UNIFORM_UNSET;
	g_render_state.current_ubo_offset = UINT32_MAX;

	memset(&g_render_state.current_uniform_data, 0, sizeof(g_render_state.current_uniform_data));
	memset(&g_render_state.dirty_uniform_data, 0, sizeof(g_render_state.dirty_uniform_data));

	g_render_state.num_draw_commands = 0;
	g_render_state.current_frame_is_ray_traced = ray_tracing;

	if (ray_tracing)
		VK_RayFrameBegin();
}

void VK_RenderStateSetColor( float r, float g, float b, float a )
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_COLOR;
	g_render_state.dirty_uniform_data.color[0] = r;
	g_render_state.dirty_uniform_data.color[1] = g;
	g_render_state.dirty_uniform_data.color[2] = b;
	g_render_state.dirty_uniform_data.color[3] = a;
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
	const uint32_t offset = (((g_render_state.uniform_free_offset + align - 1) / align) * align);
	if (offset + size > g_render.uniform_buffer.size)
		return UINT32_MAX;

	g_render_state.uniform_free_offset = offset + size;
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

static void drawCmdPushDraw( const render_draw_t *draw )
{
	draw_command_t *draw_command;

	ASSERT(draw->render_mode >= 0);
	ASSERT(draw->render_mode < ARRAYSIZE(g_render.pipelines));
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

	// Figure out whether we need to update UBO data, and upload new data if we do
	// TODO generally it's not safe to do memcmp for structures comparison
	if (g_render_state.current_ubo_offset == UINT32_MAX || ((g_render_state.uniform_data_set_mask & UNIFORM_UPLOADED) == 0)
		|| memcmp(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.current_uniform_data)) != 0) {
		uniform_data_t *ubo;
		g_render_state.current_ubo_offset = allocUniform( sizeof(uniform_data_t), 16 );
		if (g_render_state.current_ubo_offset == UINT32_MAX) {
			gEngine.Con_Printf( S_ERROR "Ran out of uniform slots\n" );
			return;
		}

		ubo = (uniform_data_t*)((byte*)g_render.uniform_buffer.mapped + g_render_state.current_ubo_offset);
		memcpy(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.dirty_uniform_data));
		memcpy(ubo, &g_render_state.current_uniform_data, sizeof(*ubo));
		g_render_state.uniform_data_set_mask |= UNIFORM_UPLOADED;
	}

	draw_command = drawCmdAlloc();
	draw_command->draw.draw = *draw;
	draw_command->draw.ubo_offset = g_render_state.current_ubo_offset;
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
		dlight_t *l = gEngine.GetDynamicLight(i);
		if( !l || l->die < gpGlobals->time || !l->radius )
			continue;

		// Draw flashlight (workaround)
		cl_entity_t	*entPlayer;
		entPlayer = gEngine.GetLocalPlayer();
		if( FBitSet( entPlayer->curstate.effects, EF_DIMLIGHT )) {
			#define FLASHLIGHT_DISTANCE		2000	// in units
			pmtrace_t		*trace;
			vec3_t		forward, view_ofs;
			vec3_t		vecSrc, vecEnd;
			float		falloff;
			trace = gEngine.EV_VisTraceLine( vecSrc, vecEnd, PM_NORMAL );
			// compute falloff
			falloff = trace->fraction * FLASHLIGHT_DISTANCE;
			if( falloff < 500.0f ) falloff = 1.0f;
			else falloff = 500.0f / falloff;
			falloff *= falloff;

			AngleVectors( g_camera.viewangles, forward, NULL, NULL );
			view_ofs[0] = view_ofs[1] = 0.0f;
			if( entPlayer->curstate.usehull == 1 ) {
				view_ofs[2] = 12.0f; // VEC_DUCK_VIEW;
			} else {
				view_ofs[2] = 28.0f; // DEFAULT_VIEWHEIGHT
			}
			VectorAdd( entPlayer->origin, view_ofs, vecSrc );
			VectorMA( vecSrc, FLASHLIGHT_DISTANCE, forward, vecEnd );
			trace = gEngine.EV_VisTraceLine( vecSrc, vecEnd, PM_NORMAL );
			VectorMA( trace->endpos, -10, forward, l->origin );

			// apply brigthness to dlight
			l->color.r = bound( 0, falloff * 255, 255 );
			l->color.g = bound( 0, falloff * 255, 255 );
			l->color.b = bound( 0, falloff * 255, 255 );
			l->radius = 75;

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
		} else {
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
		}
		num_lights++;
	}

	ubo_lights->num_lights = num_lights;
	return ubo_lights_offset;
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
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g_render.buffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmdbuf, g_render.buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	}

	vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 3, 1, vk_desc.ubo_sets + 1, 1, &dlights_ubo_offset);

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
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 0, 1, vk_desc.ubo_sets, 1, &ubo_offset);
		}

		if (pipeline != draw->draw.draw.render_mode) {
			pipeline = draw->draw.draw.render_mode;
			vkCmdBindPipeline(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipelines[pipeline]);
		}

		if (lightmap != draw->draw.draw.lightmap) {
			lightmap = draw->draw.draw.lightmap;
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 2, 1, &findTexture(lightmap)->vk.descriptor, 0, NULL);
		}

		if (texture != draw->draw.draw.texture)
		{
			texture = draw->draw.draw.texture;
			// TODO names/enums for binding points
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 1, 1, &findTexture(texture)->vk.descriptor, 0, NULL);
		}

		// Only indexed mode is supported
		ASSERT(draw->draw.draw.index_offset >= 0);
		vkCmdDrawIndexed(vk_core.cb, draw->draw.draw.element_count, 1, draw->draw.draw.index_offset, draw->draw.draw.vertex_offset, 0);
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
			// FIXME this should really be in vk_rtx, calling vk_render(or what?) to alloc slot for it
			.ubo = {
				.buffer = g_render.uniform_buffer.buffer,
				.offset = allocUniform(sizeof(matrix4x4) * 2, sizeof(matrix4x4)),
				.size = sizeof(matrix4x4) * 2,
			},

			.geometry_data = {
				.buffer = g_render.buffer.buffer,
				.size = VK_WHOLE_SIZE,
			},

			.fov_angle_y = g_render.fov_angle_y,
		};

		if (args.ubo.offset == UINT32_MAX) {
			gEngine.Con_Printf(S_ERROR "Cannot allocate UBO for RTX\n");
			return;
		}

		{
			matrix4x4 *ubo_matrices = (matrix4x4*)((byte*)g_render.uniform_buffer.mapped + args.ubo.offset);
			matrix4x4 proj_inv, view_inv;
			Matrix4x4_Invert_Full(proj_inv, g_render_state.projection);
			Matrix4x4_ToArrayFloatGL(proj_inv, (float*)ubo_matrices[0]);

			// TODO there's a more efficient way to construct an inverse view matrix
			// from vforward/right/up vectors and origin in g_camera
			Matrix4x4_Invert_Full(view_inv, g_render_state.view);
			Matrix4x4_ToArrayFloatGL(view_inv, (float*)ubo_matrices[1]);
		}

		VK_RayFrameEnd(&args);
	}
}

qboolean VK_RenderModelInit( vk_render_model_t *model ) {
	if (vk_core.rtx && (g_render_state.current_frame_is_ray_traced || !model->dynamic)) {
		// TODO runtime rtx switch: ???
		const vk_ray_model_init_t args = {
			.buffer = g_render.buffer.buffer,
			.model = model,
		};
		model->ray_model = VK_RayModelCreate(args);
		return !!model->ray_model;
	}

	// TODO pre-bake optimal draws
	return true;
}

void VK_RenderModelDestroy( vk_render_model_t* model ) {
	if (vk_core.rtx && (g_render_state.current_frame_is_ray_traced || !model->dynamic)) {
		VK_RayModelDestroy(model->ray_model);
	}
}

void VK_RenderModelDraw( const cl_entity_t *ent, vk_render_model_t* model ) {
	int current_texture = -1;
	int element_count = 0;
	int index_offset = -1;
	int vertex_offset = 0;

	if (g_render_state.current_frame_is_ray_traced) {
		VK_RayFrameAddModel(model->ray_model, model, (const matrix3x4*)g_render_state.model, g_render_state.dirty_uniform_data.color, ent ? ent->curstate.rendercolor : (color24){255,255,255});
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
					.lightmap = tglob.lightmapTextures[0], // FIXME there can be more than one lightmap textures
					.texture = current_texture,
					.render_mode = model->render_mode,
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
			.lightmap = tglob.lightmapTextures[0],
			.texture = current_texture,
			.render_mode = model->render_mode,
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

void VK_RenderModelDynamicBegin( int render_mode, const char *debug_name_fmt, ... ) {
	va_list argptr;
	va_start( argptr, debug_name_fmt );
	vsnprintf(g_dynamic_model.model.debug_name, sizeof(g_dynamic_model.model.debug_name), debug_name_fmt, argptr );
	va_end( argptr );

	ASSERT(!g_dynamic_model.model.geometries);
	g_dynamic_model.model.geometries = g_dynamic_model.geometries;
	g_dynamic_model.model.num_geometries = 0;
	g_dynamic_model.model.render_mode = render_mode;
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
		g_dynamic_model.model.dynamic = true;
		VK_RenderModelInit( &g_dynamic_model.model );
		VK_RenderModelDraw( NULL, &g_dynamic_model.model );
	}

	g_dynamic_model.model.debug_name[0] = '\0';
	g_dynamic_model.model.geometries = NULL;
}
