#include "vk_map.h"

#include "vk_core.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_framectl.h"
#include "vk_math.h"

#include "ref_params.h"
#include "eiface.h"

#include <math.h>
#include <memory.h>

typedef struct map_vertex_s
{
	vec3_t pos;
	/* vec2_t gl_tc; */
	/* vec2_t lm_tc; */
} map_vertex_t;

static struct {
	// TODO merge these into a single buffer
	vk_buffer_t vertex_buffer;
	uint32_t num_vertices;

	vk_buffer_t index_buffer;
	uint32_t num_indices;

	vk_buffer_t uniform_buffer;

	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
} gmap;

typedef struct {
	matrix4x4 worldview;
	matrix4x4 projection;
	matrix4x4 vkfixup;
	matrix4x4 mvp;
} uniform_data_t;

static qboolean createPipelines( void )
{
	/* VkPushConstantRange push_const = { */
	/* 	.offset = 0, */
	/* 	.size = sizeof(AVec3f), */
	/* 	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, */
	/* }; */

	VkDescriptorSetLayout descriptor_layouts[] = {
		vk_core.descriptor_pool.one_uniform_buffer_layout,
	};

	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = ARRAYSIZE(descriptor_layouts),
		.pSetLayouts = descriptor_layouts,
		/* .pushConstantRangeCount = 1, */
		/* .pPushConstantRanges = &push_const, */
	};

	// FIXME store layout separately
	XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &gmap.pipeline_layout));

	{
		VkVertexInputAttributeDescription attribs[] = {
			{.binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(map_vertex_t, pos)},
		};

		VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = loadShader("map.vert.spv"),
			.pName = "main",
		}, {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = loadShader("map.frag.spv"),
			.pName = "main",
		}};

		vk_pipeline_create_info_t ci = {
			.layout = gmap.pipeline_layout,
			.attribs = attribs,
			.num_attribs = ARRAYSIZE(attribs),

			.stages = shader_stages,
			.num_stages = ARRAYSIZE(shader_stages),

			.vertex_stride = sizeof(map_vertex_t),

			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,

			.blendEnable = VK_FALSE,
		};

		gmap.pipeline = createPipeline(&ci);
		if (!gmap.pipeline)
			return false;

		for (int i = 0; i < (int)ARRAYSIZE(shader_stages); ++i)
			vkDestroyShaderModule(vk_core.device, shader_stages[i].module, NULL);
	}

	return true;
}

qboolean VK_MapInit( void )
{
	const uint32_t vertex_buffer_size = MAX_MAP_VERTS * sizeof(map_vertex_t);
	const uint32_t index_buffer_size = MAX_MAP_VERTS * sizeof(uint16_t) * 3; // TODO count this properly

	// TODO device memory and friends (e.g. handle mobile memory ...)

	if (!createBuffer(&gmap.vertex_buffer, vertex_buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&gmap.index_buffer, index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createBuffer(&gmap.uniform_buffer, sizeof(uniform_data_t), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	if (!createPipelines())
		return false;

	{
		VkDescriptorBufferInfo dbi = {
			.buffer = gmap.uniform_buffer.buffer,
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};
		VkWriteDescriptorSet wds[] = { {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &dbi,
			.dstSet = vk_core.descriptor_pool.ubo_sets[0], // FIXME
		}};
		vkUpdateDescriptorSets(vk_core.device, ARRAYSIZE(wds), wds, 0, NULL);
	}

	return true;
}

void VK_MapShutdown( void )
{
	vkDestroyPipeline( vk_core.device, gmap.pipeline, NULL );
	vkDestroyPipelineLayout( vk_core.device, gmap.pipeline_layout, NULL );

	destroyBuffer( &gmap.vertex_buffer );
	destroyBuffer( &gmap.index_buffer );
	destroyBuffer( &gmap.uniform_buffer );
}

// tell the renderer what new map is started
void R_NewMap( void )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);

	const model_t *world = gEngine.pfnGetModelByIndex(1);
	map_vertex_t *bvert = gmap.vertex_buffer.mapped;
	uint16_t *bind = gmap.index_buffer.mapped;

	// Free previous map data
	gmap.num_vertices = 0;
	gmap.num_indices = 0;

	for( int i = 0; i < world->numsurfaces; ++i)
	{
		const uint16_t first_vertex_index = gmap.num_vertices;
		const msurface_t *surf = world->surfaces + i;

		if( surf->flags & ( SURF_DRAWSKY | SURF_DRAWTURB | SURF_CONVEYOR | SURF_DRAWTURB_QUADS ) )
			continue;

		//gEngine.Con_Reportf( "surface %d: numverts=%d numedges=%d\n", i, surf->polys ? surf->polys->numverts : -1, surf->numedges );

		if (surf->numedges + gmap.num_vertices > MAX_MAP_VERTS)
		{
			gEngine.Con_Printf(S_ERROR "Ran out of buffer vertex space\n");
			break;
		}

		for( int k = 0; k < surf->numedges; k++ )
		{
			const int iedge = world->surfedges[surf->firstedge + k];
			const medge_t *edge = world->edges + (iedge >= 0 ? iedge : -iedge);
			const mvertex_t *vertex = world->vertexes + (iedge >= 0 ? edge->v[0] : edge->v[1]);

			//gEngine.Con_Printf("VERT %u %f %f %f\n", gmap.num_vertices, vertex->position[0], vertex->position[1], vertex->position[2]);

			bvert[gmap.num_vertices++] = (map_vertex_t){
				{vertex->position[0], vertex->position[1], vertex->position[2]}
			};

			// TODO contemplate triangle_strip (or fan?) + primitive restart
			if (k > 1) {
				ASSERT(gmap.num_indices < (MAX_MAP_VERTS * 3 - 3));
				ASSERT(first_vertex_index + k < UINT16_MAX);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + 0);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + k - 1);
				bind[gmap.num_indices++] = (uint16_t)(first_vertex_index + k);
				//gEngine.Con_Printf("INDX %u %d %d %d\n", gmap.num_indices, (int)bind[gmap.num_indices-3], (int)bind[gmap.num_indices-2], (int)bind[gmap.num_indices-1]);
			}
		}
	}

	gEngine.Con_Reportf("Loaded surfaces: %d, vertices: %u\n, indices: %u", world->numsurfaces, gmap.num_vertices, gmap.num_indices);
}

// FIXME this is a total garbage. pls avoid adding even more weird local static state
static ref_viewpass_t fixme_rvp;

void FIXME_VK_MapSetViewPass( const struct ref_viewpass_s *rvp )
{
	fixme_rvp = *rvp;
}

static float R_GetFarClip( void )
{
	/* FIXME
	if( WORLDMODEL && RI.drawWorld )
		return MOVEVARS->zmax * 1.73f;
	*/
	return 2048.0f;
}

#define max( a, b )                 (((a) > (b)) ? (a) : (b))
#define min( a, b )                 (((a) < (b)) ? (a) : (b))

static void R_SetupProjectionMatrix( matrix4x4 m )
{
	float xMin, xMax, yMin, yMax, zNear, zFar;

	/*
	if( RI.drawOrtho )
	{
		const ref_overview_t *ov = gEngfuncs.GetOverviewParms();
		Matrix4x4_CreateOrtho( m, ov->xLeft, ov->xRight, ov->yTop, ov->yBottom, ov->zNear, ov->zFar );
		return;
	}
	*/

	const float farClip = R_GetFarClip();

	zNear = 4.0f;
	zFar = max( 256.0f, farClip );

	yMax = zNear * tan( fixme_rvp.fov_y * M_PI_F / 360.0f );
	yMin = -yMax;

	xMax = zNear * tan( fixme_rvp.fov_x * M_PI_F / 360.0f );
	xMin = -xMax;

	Matrix4x4_CreateProjection( m, xMax, xMin, yMax, yMin, zNear, zFar );
}

static void R_SetupModelviewMatrix( matrix4x4 m )
{
	Matrix4x4_CreateModelview( m );
	Matrix4x4_ConcatRotate( m, -fixme_rvp.viewangles[2], 1, 0, 0 );
	Matrix4x4_ConcatRotate( m, -fixme_rvp.viewangles[0], 0, 1, 0 );
	Matrix4x4_ConcatRotate( m, -fixme_rvp.viewangles[1], 0, 0, 1 );
	Matrix4x4_ConcatTranslate( m, -fixme_rvp.vieworigin[0], -fixme_rvp.vieworigin[1], -fixme_rvp.vieworigin[2] );
}

void VK_MapRender( void )
{
	{
		uniform_data_t *ubo = gmap.uniform_buffer.mapped;
		matrix4x4 worldview={0}, projection={0}, mvp={0}, tmp={0};
		//uniform_data_t uniform_data = {0};

		// Vulkan has Y pointing down, and z should end up in (0, 1)
		const matrix4x4 vk_proj_fixup = {
			{1, 0, 0, 0},
			{0, -1, 0, 0},
			{0, 0, .5, 0},
			{0, 0, .5, 1}
		};

		R_SetupModelviewMatrix( worldview );
		R_SetupProjectionMatrix( projection );
		memcpy(&ubo->worldview, worldview, sizeof(matrix4x4));
		memcpy(&ubo->projection, projection, sizeof(matrix4x4));
		Matrix4x4_Concat( mvp, projection, worldview);
		memcpy(&ubo->mvp, mvp, sizeof(matrix4x4));
		memcpy(&ubo->vkfixup, vk_proj_fixup, sizeof(matrix4x4));

		//Matrix4x4_Concat( mvp, projection, worldview);
		//Matrix4x4_Concat( mvp, tmp, worldview );
		//Matrix4x4_Concat( tmp, vk_proj_fixup, mvp);
		//memcpy(gmap.uniform_buffer.mapped, tmp, sizeof(tmp));
		//memcpy(gmap.uniform_buffer.mapped, mvp, sizeof(tmp));
		//memcpy(gmap.uniform_buffer.mapped, tmp, sizeof(tmp));

		/*
		vkCmdUpdateBuffer(vk_core.cb, gmap.uniform_buffer.buffer, 0, sizeof(uniform_data), &uniform_data);

		VkMemoryBarrier mem_barrier = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		};
		vkCmdPipelineBarrier(vk_core.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
				VK_DEPENDENCY_DEVICE_GROUP_BIT, 1, &mem_barrier, 0, NULL, 0, NULL);
		*/
	}

	/* TODO
	if( RP_NORMALPASS( ))
	{
		int	x, x2, y, y2;

		// set up viewport (main, playersetup)
		x = floor( RI.viewport[0] * gpGlobals->width / gpGlobals->width );
		x2 = ceil(( RI.viewport[0] + RI.viewport[2] ) * gpGlobals->width / gpGlobals->width );
		y = floor( gpGlobals->height - RI.viewport[1] * gpGlobals->height / gpGlobals->height );
		y2 = ceil( gpGlobals->height - ( RI.viewport[1] + RI.viewport[3] ) * gpGlobals->height / gpGlobals->height );

		pglViewport( x, y2, x2 - x, y - y2 );
	}
	else
	{
		// envpass, mirrorpass
		pglViewport( RI.viewport[0], RI.viewport[1], RI.viewport[2], RI.viewport[3] );
	}
	*/

	// ...

	{
		const VkDeviceSize offset = 0;
		vkCmdBindPipeline(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gmap.pipeline);
		vkCmdBindVertexBuffers(vk_core.cb, 0, 1, &gmap.vertex_buffer.buffer, &offset);
		vkCmdBindIndexBuffer(vk_core.cb, gmap.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdBindDescriptorSets(vk_core.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gmap.pipeline_layout, 0, 1, vk_core.descriptor_pool.ubo_sets, 0, NULL);
		vkCmdDrawIndexed(vk_core.cb, gmap.num_indices, 1, 0, 0, 0);
	}
}

void R_RenderScene( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}
