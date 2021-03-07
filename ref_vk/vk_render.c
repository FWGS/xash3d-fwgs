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

#include <memory.h>

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

typedef struct vk_buffer_alloc_s {
	// TODO uint32_t sequence
	uint32_t unit_size; // if 0 then this alloc slot is free
	uint32_t buffer_offset_in_units;
	uint32_t count;
	qboolean locked;
	vk_lifetime_t lifetime;

	vk_ray_model_handle_t rtx_model;
} vk_buffer_alloc_t;

// TODO estimate
#define MAX_ALLOCS 1024

static struct {
	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + 1];

	vk_buffer_t buffer;
	uint32_t buffer_free_offset;
	uint32_t buffer_frame_begin_offset;

	vk_buffer_t uniform_buffer;
	uint32_t ubo_align;

	struct {
		int align_holes_size;
	} stat;

	vk_buffer_alloc_t allocs[MAX_ALLOCS];
	int allocs_free[MAX_ALLOCS];
	int num_free_allocs;
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

		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, pos)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(vk_vertex_t, normal)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, gl_tc)},
			{.binding = 0, .location = 3, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(vk_vertex_t, lm_tc)},
		};

		VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = loadShader("brush.vert.spv"),
			.pName = "main",
		}, {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = loadShader("brush.frag.spv"),
			.pName = "main",
			.pSpecializationInfo = &shader_spec,
		}};

		vk_pipeline_create_info_t ci = {
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
					ci.depthTestEnable = VK_FALSE;
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

			g_render.pipelines[i] = createPipeline(&ci);

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

		for (int i = 0; i < (int)ARRAYSIZE(shader_stages); ++i)
			vkDestroyShaderModule(vk_core.device, shader_stages[i].module, NULL);
	}

	return true;
}

static void resetAllocFreeList( void ) {
	g_render.num_free_allocs = MAX_ALLOCS;
	for (int i = 0; i < MAX_ALLOCS; ++i) {
		g_render.allocs_free[i] = MAX_ALLOCS - i - 1;
		g_render.allocs[i].unit_size = 0;
	}
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
	// TODO Better estimates
	const uint32_t vertex_buffer_size = MAX_BUFFER_VERTICES * sizeof(float) * (3 + 3 + 2 + 2);
	const uint32_t index_buffer_size = MAX_BUFFER_INDICES * sizeof(uint16_t);
	uint32_t uniform_unit_size;

	g_render.ubo_align = Q_max(4, vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);
	uniform_unit_size = ((sizeof(uniform_data_t) + g_render.ubo_align - 1) / g_render.ubo_align) * g_render.ubo_align;

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer(&g_render.buffer, vertex_buffer_size + index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (vk_core.rtx ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&g_render.uniform_buffer, uniform_unit_size * MAX_UNIFORM_SLOTS, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
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

	resetAllocFreeList();

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

vk_buffer_handle_t VK_RenderBufferAlloc( uint32_t unit_size, uint32_t count, vk_lifetime_t lifetime )
{
	const uint32_t offset = ALIGN_UP(g_render.buffer_free_offset, unit_size);
	const uint32_t alloc_size = unit_size * count;
	vk_buffer_alloc_t *alloc;
	vk_buffer_handle_t handle = InvalidHandle;

	// FIXME long lifetimes are not supported yet
	ASSERT(lifetime != LifetimeLong);
	ASSERT(unit_size > 0);

	if (offset + alloc_size > g_render.buffer.size) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate %u bytes aligned at %u from buffer; only %u are left",
				alloc_size, unit_size, g_render.buffer.size - offset);
		return InvalidHandle;
	}

	if (!g_render.num_free_allocs) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate buffer, allocs count exhausted\n" );
		return InvalidHandle;
	}

	// TODO bake sequence number into handle (to detect buffer lifetime misuse)
	handle = g_render.allocs_free[--g_render.num_free_allocs];
	alloc = g_render.allocs + handle;
	ASSERT(alloc->unit_size == 0);

	alloc->buffer_offset_in_units = offset / unit_size;
	alloc->unit_size = unit_size;
	alloc->lifetime = lifetime;
	alloc->count = count;

	g_render.stat.align_holes_size += offset - g_render.buffer_free_offset;
	g_render.buffer_free_offset = offset + alloc_size;

	if (lifetime < LifetimeSingleFrame)
		g_render.buffer_frame_begin_offset = g_render.buffer_free_offset;

	return handle;
}

static vk_buffer_alloc_t *getBufferFromHandle( vk_buffer_handle_t handle )
{
	vk_buffer_alloc_t *alloc;

	ASSERT(handle >= 0);
	ASSERT(handle < MAX_ALLOCS);

	// TODO check sequence number
	alloc = g_render.allocs + handle;
	ASSERT(alloc->unit_size != 0);

	return alloc;
}

vk_buffer_lock_t VK_RenderBufferLock( vk_buffer_handle_t handle )
{
	vk_buffer_lock_t ret = {0};
	vk_buffer_alloc_t *alloc = getBufferFromHandle( handle );
	ASSERT(!alloc->locked);
	alloc->locked = true;

	ret.unit_size = alloc->unit_size;
	ret.count = alloc->count;
	ret.ptr = ((byte*)g_render.buffer.mapped) + alloc->unit_size * alloc->buffer_offset_in_units;

	return ret;
}

void VK_RenderBufferUnlock( vk_buffer_handle_t handle )
{
	vk_buffer_alloc_t *alloc = getBufferFromHandle( handle );
	ASSERT(alloc->locked);
	alloc->locked = false;

	// TODO upload from staging to gpumem
}

// Free all LifetimeSingleFrame resources
void VK_RenderBufferClearFrame( void )
{
	g_render.buffer_free_offset = g_render.buffer_frame_begin_offset;

	for (int i = 0; i < MAX_ALLOCS; ++i) {
		vk_buffer_alloc_t *alloc = g_render.allocs + i;

		if (!alloc->unit_size)
			continue;

		if (alloc->lifetime != LifetimeSingleFrame)
			continue;
alloc->unit_size = 0;
		g_render.allocs_free[g_render.num_free_allocs++] = i;
		ASSERT(g_render.num_free_allocs <= MAX_ALLOCS);
	}
}

// Free all LifetimeMap resources
void VK_RenderBufferClearMap( void )
{
	g_render.buffer_free_offset = g_render.buffer_frame_begin_offset = 0;
	g_render.stat.align_holes_size = 0;
	resetAllocFreeList();
}

void VK_RenderBufferPrintStats( void )
{
	gEngine.Con_Reportf("Buffer usage: %uKiB of (%uKiB); holes: %u bytes\n",
		g_render.buffer_free_offset / 1024,
		g_render.buffer.size / 1024,
		g_render.stat.align_holes_size);
}

#define MAX_DRAW_COMMANDS 8192 // TODO estimate
#define MAX_DEBUG_NAME_LENGTH 32

typedef struct {
	render_draw_t draw;
	uint32_t ubo_offset;
	//char debug_name[MAX_DEBUG_NAME_LENGTH];
} draw_command_t;

static struct {
	int uniform_data_set_mask;
	uniform_data_t current_uniform_data;
	uniform_data_t dirty_uniform_data;

	uint32_t current_ubo_offset;
	uint32_t uniform_free_offset;

	draw_command_t draw_commands[MAX_DRAW_COMMANDS];
	int num_draw_commands;

	// FIXME vk_rtx-specific
	struct {
		matrix4x4 proj_inv, view_inv;
	} rtx;
} g_render_state;

enum {
	UNIFORM_UNSET = 0,
	UNIFORM_SET_COLOR = 1,
	UNIFORM_SET_MATRIX = 2,
	UNIFORM_SET_ALL = UNIFORM_SET_COLOR | UNIFORM_SET_MATRIX,
	UNIFORM_UPLOADED = 4,
};

void VK_RenderBegin( void ) {
	g_render_state.uniform_free_offset = 0;
	g_render_state.uniform_data_set_mask = UNIFORM_UNSET;
	g_render_state.current_ubo_offset = UINT32_MAX;

	memset(&g_render_state.current_uniform_data, 0, sizeof(g_render_state.current_uniform_data));
	memset(&g_render_state.dirty_uniform_data, 0, sizeof(g_render_state.dirty_uniform_data));

	g_render_state.num_draw_commands = 0;
}

void VK_RenderStateSetColor( float r, float g, float b, float a )
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_COLOR;
	g_render_state.dirty_uniform_data.color[0] = r;
	g_render_state.dirty_uniform_data.color[1] = g;
	g_render_state.dirty_uniform_data.color[2] = b;
	g_render_state.dirty_uniform_data.color[3] = a;
}

void VK_RenderStateSetMatrix( const matrix4x4 mvp )
{
	g_render_state.uniform_data_set_mask |= UNIFORM_SET_MATRIX;
	Matrix4x4_ToArrayFloatGL( mvp, (float*)g_render_state.dirty_uniform_data.mvp );
}

void VK_RenderStateSetProjectionMatrix(const matrix4x4 proj_vk)
{
	matrix4x4 proj_inv_row;
	Matrix4x4_Invert_Full(proj_inv_row, proj_vk);
	Matrix4x4_ToArrayFloatGL(proj_inv_row, (float*)g_render_state.rtx.proj_inv);
}

void VK_RenderStateSetViewMatrix(const matrix4x4 view)
{
	// TODO there's a more efficient way to construct an inverse view matrix
	// from vforward/right/up vectors and origin in g_camera
	matrix4x4 tmp;
	Matrix4x4_Invert_Full(tmp, view);
	Matrix4x4_ToArrayFloatGL( tmp, (float*)g_render_state.rtx.view_inv);
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

void VK_RenderScheduleDraw( const render_draw_t *draw )
{
	const vk_buffer_alloc_t *vertex_buffer = NULL, *index_buffer = NULL;
	draw_command_t *draw_command;

	ASSERT(draw->render_mode >= 0);
	ASSERT(draw->render_mode < ARRAYSIZE(g_render.pipelines));
	ASSERT(draw->lightmap >= 0);
	ASSERT(draw->texture >= 0);

	{
		vertex_buffer = getBufferFromHandle(draw->vertex_buffer);
		ASSERT(vertex_buffer);
		ASSERT(!vertex_buffer->locked);
	}

	// Index buffer is optional
	if (draw->index_buffer != InvalidHandle)
	{
		index_buffer = getBufferFromHandle(draw->index_buffer);
		ASSERT(index_buffer);
		ASSERT(!index_buffer->locked);
	}

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
	if (g_render_state.current_ubo_offset == UINT32_MAX || ((g_render_state.uniform_data_set_mask & UNIFORM_UPLOADED) == 0) || memcmp(&g_render_state.current_uniform_data, &g_render_state.dirty_uniform_data, sizeof(g_render_state.current_uniform_data)) != 0) {
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

	draw_command = g_render_state.draw_commands + (g_render_state.num_draw_commands++);
	draw_command->draw = *draw;
	draw_command->ubo_offset = g_render_state.current_ubo_offset;
}

void VK_RenderEnd( VkCommandBuffer cmdbuf )
{
	// TODO we can sort collected draw commands for more efficient and correct rendering
	// that requires adding info about distance to camera for correct order-dependent blending

	int pipeline = -1;
	int texture = -1;
	int lightmap = -1;
	uint32_t ubo_offset = -1;

	ASSERT(!vk_core.rtx);

	{
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmdbuf, 0, 1, &g_render.buffer.buffer, &offset);
		vkCmdBindIndexBuffer(cmdbuf, g_render.buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	}

	{
		vk_ubo_lights_t* ubo_lights;
		const uint32_t ubo_lights_offset = allocUniform(sizeof(*ubo_lights), 4);
		if (ubo_lights_offset == UINT32_MAX) {
			gEngine.Con_Printf(S_ERROR "Cannot allocate UBO for DLights\n");
			return;
		}
		ubo_lights = (vk_ubo_lights_t*)((byte*)(g_render.uniform_buffer.mapped) + ubo_lights_offset);

		// TODO this should not be here (where? vk_scene?)
		ubo_lights->num_lights = 0;
		for (int i = 0; i < MAX_DLIGHTS; ++i) {
			const dlight_t *l = gEngine.GetDynamicLight(i);
			if( !l || l->die < gpGlobals->time || !l->radius )
				continue;
			Vector4Set(
				ubo_lights->light[ubo_lights->num_lights].color,
				l->color.r / 255.f,
				l->color.g / 255.f,
				l->color.b / 255.f,
				1.f);
			Vector4Set(
				ubo_lights->light[ubo_lights->num_lights].pos_r,
				l->origin[0],
				l->origin[1],
				l->origin[2],
				l->radius);
			ubo_lights->num_lights++;
		}

		vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 3, 1, vk_desc.ubo_sets + 1, 1, &ubo_lights_offset);
	}

	for (int i = 0; i < g_render_state.num_draw_commands; ++i) {
		const draw_command_t *const draw = g_render_state.draw_commands + i;
		const vk_buffer_alloc_t *vertex_buffer = getBufferFromHandle( draw->draw.vertex_buffer );
		const vk_buffer_alloc_t *index_buffer = draw->draw.index_buffer != InvalidHandle ? getBufferFromHandle( draw->draw.index_buffer ) : NULL;
		const uint32_t vertex_offset = vertex_buffer->buffer_offset_in_units + draw->draw.vertex_offset;

		if (ubo_offset != draw->ubo_offset)
		{
			ubo_offset = draw->ubo_offset;
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 0, 1, vk_desc.ubo_sets, 1, &ubo_offset);
		}

		if (pipeline != draw->draw.render_mode) {
			pipeline = draw->draw.render_mode;
			vkCmdBindPipeline(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipelines[pipeline]);
		}

		if (lightmap != draw->draw.lightmap) {
			lightmap = draw->draw.lightmap;
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 2, 1, &findTexture(lightmap)->vk.descriptor, 0, NULL);
		}

		if (texture != draw->draw.texture)
		{
			texture = draw->draw.texture;
			// TODO names/enums for binding points
			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render.pipeline_layout, 1, 1, &findTexture(texture)->vk.descriptor, 0, NULL);
		}

		if (draw->draw.index_buffer) {
			const uint32_t index_offset = index_buffer->buffer_offset_in_units + draw->draw.index_offset;
			vkCmdDrawIndexed(vk_core.cb, draw->draw.element_count, 1, index_offset, vertex_offset, 0);
		} else {
			vkCmdDraw(vk_core.cb, draw->draw.element_count, 1, vertex_offset, 0);
		}
	}
}

void VK_RenderDebugLabelBegin( const char *name )
{
	// TODO fix this
	/* if (vk_core.debug) { */
	/* 	VkDebugUtilsLabelEXT label = { */
	/* 		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, */
	/* 		.pLabelName = name, */
	/* 	}; */
	/* 	vkCmdBeginDebugUtilsLabelEXT(vk_core.cb, &label); */
	/* } */
}

void VK_RenderDebugLabelEnd( void )
{
	/* if (vk_core.debug) */
	/* 	vkCmdEndDebugUtilsLabelEXT(vk_core.cb); */
}

void VK_RenderEndRTX( VkCommandBuffer cmdbuf, VkImageView img_dst_view, VkImage img_dst, uint32_t w, uint32_t h )
{
	ASSERT(vk_core.rtx);
	VK_RaySceneBegin();
	for (int i = 0; i < g_render_state.num_draw_commands; ++i) {
		const draw_command_t *const draw = g_render_state.draw_commands + i;
		const vk_buffer_alloc_t *vertex_buffer = getBufferFromHandle( draw->draw.vertex_buffer );
		const vk_buffer_alloc_t *index_buffer = draw->draw.index_buffer != InvalidHandle ? getBufferFromHandle( draw->draw.index_buffer ) : NULL;
		const uint32_t vertex_offset = vertex_buffer->buffer_offset_in_units + draw->draw.vertex_offset;

		// TODO there's a more complex story with lifetimes and rebuilds && vertex_buffer->lifetime < LifetimeSingleFrame)
		// TODO it would make sense to join logical models into a single ray model
		// but here we've completely lost this info, as models are now just a stream
		// of independent draws

		const vk_ray_model_create_t ray_model_args = {
			.element_count = draw->draw.element_count,
			.max_vertex = vertex_buffer->count, // TODO this is an upper bound for brushes at least, it can be lowered
			.index_offset = index_buffer ? index_buffer->unit_size * (draw->draw.index_offset + index_buffer->buffer_offset_in_units) : UINT32_MAX,
			.vertex_offset = (draw->draw.vertex_offset + vertex_buffer->buffer_offset_in_units) * vertex_buffer->unit_size,
			.buffer = g_render.buffer.buffer,
		};

		VK_RayScenePushModel(cmdbuf, &ray_model_args);
	}

	{
		float *matrices = NULL;
		const vk_ray_scene_render_args_t args = {
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
				.offset = allocUniform(sizeof(float) * 16 * 2, 16 * sizeof(float)),
				.size = sizeof(float) * 16 * 2,
			},
		};

		if (args.ubo.offset == UINT32_MAX) {
			gEngine.Con_Printf(S_ERROR "Cannot allocate UBO for RTX\n");
			return;
		}

		matrices = (float*)((byte*)g_render.uniform_buffer.mapped + args.ubo.offset);
		memcpy(matrices, &g_render_state.rtx, sizeof(g_render_state.rtx));

		VK_RaySceneEnd(&args);
	}
}
