#include "vk_brush.h"

#include "vk_core.h"
#include "vk_const.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_framectl.h"
#include "vk_math.h"
#include "vk_textures.h"
#include "vk_lightmap.h"
#include "vk_scene.h"
#include "vk_render.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

// TODO count these properly
#define MAX_BUFFER_VERTICES (1 * 1024 * 1024)
#define MAX_BUFFER_INDICES (MAX_BUFFER_VERTICES * 3)

typedef struct brush_vertex_s
{
	vec3_t pos;
	vec2_t gl_tc;
	vec2_t lm_tc;
} brush_vertex_t;

typedef struct vk_brush_model_surface_s {
	int texture_num;

	// Offset into g_brush.index_buffer in vertices
	uint32_t index_offset;
	uint16_t index_count;
} vk_brush_model_surface_t;

typedef struct vk_brush_model_s {
	//model_t *model;

	// Offset into g_brush.vertex_buffer in vertices
	uint32_t vertex_offset;

	int num_surfaces;
	vk_brush_model_surface_t surfaces[];
} vk_brush_model_t;

static struct {
	// TODO merge these into a single buffer
	vk_buffer_t vertex_buffer;
	uint32_t num_vertices;

	vk_buffer_t index_buffer;
	uint32_t num_indices;

	vk_buffer_t uniform_buffer;
	uint32_t uniform_unit_size;

	VkPipelineLayout pipeline_layout;
	VkPipeline pipelines[kRenderTransAdd + 1];
} g_brush;

/* static brush_vertex_t *allocVertices(int num_vertices) { */
/* 		if (num_vertices + g_brush.num_vertices > MAX_BUFFER_VERTICES) */
/* 		{ */
/* 			gEngine.Con_Printf(S_ERROR "Ran out of buffer vertex space\n"); */
/* 			return NULL; */
/* 		} */
/* } */

uniform_data_t *getUniformSlot(int index)
{
	ASSERT(index >= 0);
	ASSERT(index < MAX_UNIFORM_SLOTS);
	return (uniform_data_t*)(((uint8_t*)g_brush.uniform_buffer.mapped) + (g_brush.uniform_unit_size * index));
}

static qboolean createPipelines( void )
{
	/* VkPushConstantRange push_const = { */
	/* 	.offset = 0, */
	/* 	.size = sizeof(AVec3f), */
	/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
	/* }; */

	VkDescriptorSetLayout descriptor_layouts[] = {
		vk_core.descriptor_pool.one_uniform_buffer_layout,
		vk_core.descriptor_pool.one_texture_layout,
		vk_core.descriptor_pool.one_texture_layout,
	};

	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = ARRAYSIZE(descriptor_layouts),
		.pSetLayouts = descriptor_layouts,
		/* .pushConstantRangeCount = 1, */
		/* .pPushConstantRanges = &push_const, */
	};

	// FIXME store layout separately
	XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &g_brush.pipeline_layout));

	{
		struct ShaderSpec {
			float alpha_test_threshold;
		} spec_data = { .25f };
		const VkSpecializationMapEntry spec_map[] = {
			{.constantID = 0, .offset = offsetof(struct ShaderSpec, alpha_test_threshold), .size = sizeof(float) },
		};

		VkSpecializationInfo alpha_test_spec = {
			.mapEntryCount = ARRAYSIZE(spec_map),
			.pMapEntries = spec_map,
			.dataSize = sizeof(struct ShaderSpec),
			.pData = &spec_data
		};

		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(brush_vertex_t, pos)},
			{.binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(brush_vertex_t, gl_tc)},
			{.binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(brush_vertex_t, lm_tc)},
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
		}};

		vk_pipeline_create_info_t ci = {
			.layout = g_brush.pipeline_layout,
			.attribs = attribs,
			.num_attribs = ARRAYSIZE(attribs),

			.stages = shader_stages,
			.num_stages = ARRAYSIZE(shader_stages),

			.vertex_stride = sizeof(brush_vertex_t),

			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,

			.blendEnable = VK_FALSE,

			.cullMode = VK_CULL_MODE_FRONT_BIT,
		};

		for (int i = 0; i < ARRAYSIZE(g_brush.pipelines); ++i)
		{
			const char *name = "UNDEFINED";
			switch (i)
			{
				case kRenderNormal:
					ci.stages[1].pSpecializationInfo = NULL;
					ci.blendEnable = VK_FALSE;
					ci.depthWriteEnable = VK_TRUE;
					name = "brush kRenderNormal";
					break;

				case kRenderTransColor:
					ci.stages[1].pSpecializationInfo = NULL;
					ci.depthWriteEnable = VK_TRUE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "brush kRenderTransColor";
					break;

				case kRenderTransAdd:
					ci.stages[1].pSpecializationInfo = NULL;
					ci.depthWriteEnable = VK_FALSE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					name = "brush kRenderTransAdd";
					break;

				case kRenderTransAlpha:
					ci.stages[1].pSpecializationInfo = &alpha_test_spec;
					ci.depthWriteEnable = VK_TRUE;
					ci.blendEnable = VK_FALSE;
					name = "brush kRenderTransAlpha(test)";
					break;

				case kRenderGlow:
				case kRenderTransTexture:
					ci.stages[1].pSpecializationInfo = NULL;
					ci.depthWriteEnable = VK_FALSE;
					ci.blendEnable = VK_TRUE;
					ci.colorBlendOp = VK_BLEND_OP_ADD; // TODO check
					ci.srcAlphaBlendFactor = ci.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					ci.dstAlphaBlendFactor = ci.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					name = "brush kRenderTransTexture/Glow";
					break;

				default:
					ASSERT(!"Unreachable");
			}

			g_brush.pipelines[i] = createPipeline(&ci);

			if (!g_brush.pipelines[i])
			{
				// TODO complain
				return false;
			}

			if (vk_core.debug)
			{
				VkDebugUtilsObjectNameInfoEXT debug_name = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
					.objectHandle = (uint64_t)g_brush.pipelines[i],
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

qboolean VK_BrushInit( void )
{
	const uint32_t vertex_buffer_size = MAX_BUFFER_VERTICES * sizeof(brush_vertex_t);
	const uint32_t index_buffer_size = MAX_BUFFER_INDICES * sizeof(uint16_t);
	const uint32_t ubo_align = Q_max(4, vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);

	g_brush.uniform_unit_size = ((sizeof(uniform_data_t) + ubo_align - 1) / ubo_align) * ubo_align;

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer(&g_brush.vertex_buffer, vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&g_brush.index_buffer, index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&g_brush.uniform_buffer, g_brush.uniform_unit_size * MAX_UNIFORM_SLOTS, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createPipelines())
		return false;

	{
		VkDescriptorBufferInfo dbi = {
			.buffer = g_brush.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(uniform_data_t),
		};
		VkWriteDescriptorSet wds[] = { {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.pBufferInfo = &dbi,
			.dstSet = vk_core.descriptor_pool.ubo_sets[0], // FIXME
		}};
		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
	}

	return true;
}

static int fixme_current_pipeline_index = -1;

void VK_BrushShutdown( void )
{
	for (int i = 0; i < ARRAYSIZE(g_brush.pipelines); ++i)
		vkDestroyPipeline(vk_core.device, g_brush.pipelines[i], NULL);
	vkDestroyPipelineLayout( vk_core.device, g_brush.pipeline_layout, NULL );

	destroyBuffer( &g_brush.vertex_buffer );
	destroyBuffer( &g_brush.index_buffer );
	destroyBuffer( &g_brush.uniform_buffer );
}

void VK_BrushDrawModel( const model_t *mod, int render_mode, int ubo_index )
{
	// Expect all buffers to be bound
	const vk_brush_model_t *bmodel = mod->cache.data;
	const uint32_t dynamic_offset[] = { g_brush.uniform_unit_size * ubo_index };
	int current_texture = -1;
	int index_count = 0;
	int index_offset = -1;

	if (!bmodel) {
		gEngine.Con_Printf( S_ERROR "Model %s wasn't loaded\n", mod->name);
		return;
	}

	if (vk_core.debug) {
		VkDebugUtilsLabelEXT label = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = mod->name,
		};
		vkCmdBeginDebugUtilsLabelEXT(vk_core.cb, &label);
	}

	ASSERT(render_mode >= 0);
	ASSERT(render_mode < ARRAYSIZE(g_brush.pipelines));

	if (render_mode != fixme_current_pipeline_index)
	{
		fixme_current_pipeline_index = render_mode;
		vkCmdBindPipeline(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush.pipelines[fixme_current_pipeline_index]);
	}

	vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush.pipeline_layout, 0, 1, vk_core.descriptor_pool.ubo_sets, ARRAYSIZE(dynamic_offset), dynamic_offset);

	for (int i = 0; i < bmodel->num_surfaces; ++i) {
		const vk_brush_model_surface_t *bsurf = bmodel->surfaces + i;
		if (bsurf->texture_num < 0)
			continue;

		if (current_texture != bsurf->texture_num)
		{
			vk_texture_t *texture = findTexture(bsurf->texture_num);
			if (index_count)
				vkCmdDrawIndexed(vk_core.cb, index_count, 1, index_offset, bmodel->vertex_offset, 0);

			vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush.pipeline_layout, 1, 1, &texture->vk.descriptor, 0, NULL);

			current_texture = bsurf->texture_num;
			index_count = 0;
			index_offset = -1;
		}

		if (index_offset < 0)
			index_offset = bsurf->index_offset;
		// Make sure that all surfaces are concatenated in buffers
		ASSERT(index_offset + index_count == bsurf->index_offset);
		index_count += bsurf->index_count;
	}

	if (index_count)
		vkCmdDrawIndexed(vk_core.cb, index_count, 1, index_offset, bmodel->vertex_offset, 0);

	if (vk_core.debug)
		vkCmdEndDebugUtilsLabelEXT(vk_core.cb);
}

qboolean VK_BrushRenderBegin( void )
{
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(vk_core.cb, 0, 1, &g_brush.vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(vk_core.cb, g_brush.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

	if (!tglob.lightmapTextures[0])
	{
		gEngine.Con_Printf( S_ERROR "Don't have a lightmap texture\n");
		return false;
	}

	vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush.pipeline_layout, 2, 1, &findTexture(tglob.lightmapTextures[0])->vk.descriptor, 0, NULL);

	fixme_current_pipeline_index = -1;

	return true;
}

static int loadBrushSurfaces( const model_t *mod, vk_brush_model_surface_t *out_surfaces) {
	brush_vertex_t *bvert = g_brush.vertex_buffer.mapped;
	uint16_t *bind = g_brush.index_buffer.mapped;
	uint32_t vertex_offset = 0;
	int num_surfaces = 0;

	int num_indices = 0, num_vertices = 0, max_texture_id = 0;
	for( int i = 0; i < mod->nummodelsurfaces; ++i)
	{
		const msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;
		vk_brush_model_surface_t *bsurf = out_surfaces + num_surfaces;

		if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) ) {
			gEngine.Con_Reportf("Skipping surface %d because of flags %08x\n", i, surf->flags);
			continue;
		}

		if( FBitSet( surf->flags, SURF_DRAWTILED )) {
			gEngine.Con_Reportf("Skipping surface %d because of tiled flag\n", i);
			continue;
		}

		num_vertices += surf->numedges;
		num_indices += 3 * (surf->numedges - 1);
		if (surf->texinfo->texture->gl_texturenum > max_texture_id)
			max_texture_id = surf->texinfo->texture->gl_texturenum;
	}

	// Load sorted by gl_texturenum
	for (int t = 0; t <= max_texture_id; ++t)
	{
		for( int i = 0; i < mod->nummodelsurfaces; ++i)
		{
			msurface_t *surf = mod->surfaces + mod->firstmodelsurface + i;
			vk_brush_model_surface_t *bsurf = out_surfaces + num_surfaces;
			mextrasurf_t	*info = surf->info;
			const float sample_size = gEngine.Mod_SampleSizeForFace( surf );

			if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) )
				continue;

			if( FBitSet( surf->flags, SURF_DRAWTILED ))
				continue;

			if (t != surf->texinfo->texture->gl_texturenum)
				continue;

			++num_surfaces;

			//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

			if (surf->numedges + g_brush.num_vertices > MAX_BUFFER_VERTICES)
			{
				gEngine.Con_Printf(S_ERROR "Ran out of buffer vertex space\n");
				return -1;
			}

			if ((surf->numedges-1) * 3 + g_brush.num_indices > MAX_BUFFER_INDICES)
			{
				gEngine.Con_Printf(S_ERROR "Ran out of buffer index space\n");
				return -1;
			}

			if (vertex_offset + surf->numedges >= UINT16_MAX)
			{
				gEngine.Con_Printf(S_ERROR "Model %s indices don't fit into 16 bits\n", mod->name);
				return -1;
			}

			bsurf->texture_num = surf->texinfo->texture->gl_texturenum;
			bsurf->index_offset = g_brush.num_indices;
			bsurf->index_count = 0;

			VK_CreateSurfaceLightmap( surf, mod );

			for( int k = 0; k < surf->numedges; k++ )
			{
				const int iedge = mod->surfedges[surf->firstedge + k];
				const medge_t *edge = mod->edges + (iedge >= 0 ? iedge : -iedge);
				const mvertex_t *in_vertex = mod->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);
				brush_vertex_t vertex = {
					{in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]},
				};

				float s = DotProduct( in_vertex->position, surf->texinfo->vecs[0] ) + surf->texinfo->vecs[0][3];
				float t = DotProduct( in_vertex->position, surf->texinfo->vecs[1] ) + surf->texinfo->vecs[1][3];

				s /= surf->texinfo->texture->width;
				t /= surf->texinfo->texture->height;

				vertex.gl_tc[0] = s;
				vertex.gl_tc[1] = t;

				// lightmap texture coordinates
				s = DotProduct( in_vertex->position, info->lmvecs[0] ) + info->lmvecs[0][3];
				s -= info->lightmapmins[0];
				s += surf->light_s * sample_size;
				s += sample_size * 0.5f;
				s /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->width;

				t = DotProduct( in_vertex->position, info->lmvecs[1] ) + info->lmvecs[1][3];
				t -= info->lightmapmins[1];
				t += surf->light_t * sample_size;
				t += sample_size * 0.5f;
				t /= BLOCK_SIZE * sample_size; //fa->texinfo->texture->height;

				vertex.lm_tc[0] = s;
				vertex.lm_tc[1] = t;

				//gEngine.Con_Printf("VERT %u %f %f %f\n", g_brush.num_vertices, in_vertex->position[0], in_vertex->position[1], in_vertex->position[2]);

				bvert[g_brush.num_vertices++] = vertex;

				// TODO contemplate triangle_strip (or fan?) + primitive restart
				if (k > 1) {
					bind[g_brush.num_indices++] = (uint16_t)(vertex_offset + 0);
					bind[g_brush.num_indices++] = (uint16_t)(vertex_offset + k - 1);
					bind[g_brush.num_indices++] = (uint16_t)(vertex_offset + k);
					bsurf->index_count += 3;
				}
			}

			vertex_offset += surf->numedges;
		}
	}

	return num_surfaces;
}

qboolean VK_LoadBrushModel( model_t *mod, const byte *buffer )
{
	vk_brush_model_t *bmodel;

	if (mod->cache.data)
	{
		gEngine.Con_Reportf( S_WARN "Model %s was already loaded\n", mod->name );
		return true;
	}

	gEngine.Con_Reportf("%s: %s flags=%08x\n", __FUNCTION__, mod->name, mod->flags);

	bmodel = Mem_Malloc(vk_core.pool, sizeof(vk_brush_model_t) + sizeof(vk_brush_model_surface_t) * mod->nummodelsurfaces);
	mod->cache.data = bmodel;

	bmodel->vertex_offset = g_brush.num_vertices;
	bmodel->num_surfaces = loadBrushSurfaces( mod, bmodel->surfaces );
	if (bmodel->num_surfaces < 0) {
		gEngine.Con_Reportf( S_ERROR "Model %s was not loaded\n", mod->name );
		return false;
	}

	/* gEngine.Con_Reportf("Model %s, vertex_offset=%d, first surface index_offset=%d\n", */
	/* 		mod->name, */
	/* 		bmodel->vertex_offset, bmodel->num_surfaces ? bmodel->surfaces[0].index_offset : -1); */
	/* for (int i = 0; i < bmodel->num_surfaces; ++i) */
	/* 	gEngine.Con_Reportf("\t%d: tex=%d, off=%d, cnt=%d\n", i, */
	/* 			bmodel->surfaces[i].texture_num, */
	/* 			bmodel->surfaces[i].index_offset, */
	/* 			bmodel->surfaces[i].index_count); */

	gEngine.Con_Reportf("Model %s loaded surfaces: %d (of %d); total vertices: %u, total indices: %u\n", mod->name, bmodel->num_surfaces, mod->nummodelsurfaces, g_brush.num_vertices, g_brush.num_indices);
	return true;
}

void VK_BrushClear( void )
{
	// Free previous map data
	g_brush.num_vertices = 0;
	g_brush.num_indices = 0;
}
