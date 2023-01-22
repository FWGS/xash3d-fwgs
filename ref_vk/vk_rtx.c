#include "vk_rtx.h"

#include "ray_resources.h"
#include "vk_ray_accel.h"

#include "vk_buffer.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_descriptor.h"
#include "vk_light.h"
#include "vk_math.h"
#include "vk_meatpipe.h"
#include "vk_pipeline.h"
#include "vk_ray_internal.h"
#include "vk_staging.h"
#include "vk_textures.h"

#include "alolcator.h"


#include "eiface.h"
#include "xash3d_mathlib.h"

#include <string.h>

#define MAX_FRAMES_IN_FLIGHT 2

// TODO settings/realtime modifiable/adaptive
#if 1
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#elif 0
#define FRAME_WIDTH 2560
#define FRAME_HEIGHT 1440
#else
#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#endif

	// TODO each of these should be registered by the provider of the resource:
#define EXTERNAL_RESOUCES(X) \
		X(TLAS, tlas) \
		X(Buffer, ubo) \
		X(Buffer, kusochki) \
		X(Buffer, indices) \
		X(Buffer, vertices) \
		X(Buffer, lights) \
		X(Buffer, light_clusters) \
		X(Texture, textures) \
		X(Texture, skybox)

enum {
#define RES_ENUM(type, name) ExternalResource_##name,
	EXTERNAL_RESOUCES(RES_ENUM)
#undef RES_ENUM
	ExternalResource_COUNT,
};

#define MAX_RESOURCES 32

typedef struct {
		char name[64];
		vk_resource_t resource;
		xvk_image_t image;
		int refcount;
} rt_resource_t;

static struct {
	// Holds UniformBuffer data
	vk_buffer_t uniform_buffer;
	uint32_t uniform_unit_size;

	// TODO with proper intra-cmdbuf sync we don't really need 2x images
	unsigned frame_number;
	vk_meatpipe_t *mainpipe;
	vk_resource_p *mainpipe_resources;
	rt_resource_t *mainpipe_out;

	rt_resource_t res[MAX_RESOURCES];

	qboolean reload_pipeline;
	qboolean reload_lighting;
} g_rtx = {0};

static int findResource(const char *name) {
	// Find the exact match if exists
	// There might be gaps, so we need to check everything
	for (int i = 0; i < MAX_RESOURCES; ++i) {
		if (strcmp(g_rtx.res[i].name, name) == 0)
			return i;
	}

	return -1;
}

static int getResourceSlotForName(const char *name) {
	const int index = findResource(name);
	if (index >= 0)
		return index;

	// Find first free slot
	for (int i = ExternalResource_COUNT; i < MAX_RESOURCES; ++i) {
		if (!g_rtx.res[i].name[0])
			return i;
	}

	return -1;
}

void VK_RayNewMap( void ) {
	RT_VkAccelNewMap();
	RT_RayModel_Clear();

	g_rtx.res[ExternalResource_skybox].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.value = (vk_descriptor_value_t){
			.image = {
				.sampler = vk_core.default_sampler,
				.imageView = tglob.skybox_cube.vk.image.view ? tglob.skybox_cube.vk.image.view : tglob.cubemap_placeholder.vk.image.view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			},
		},
	};
}

void VK_RayFrameBegin( void ) {
	ASSERT(vk_core.rtx);

	RT_VkAccelFrameBegin();

	if (g_ray_model_state.freeze_models)
		return;

	XVK_RayModel_ClearForNextFrame();

	// TODO: move all lighting update to scene?
	if (g_rtx.reload_lighting) {
		g_rtx.reload_lighting = false;
		// FIXME temporarily not supported VK_LightsLoadMapStaticLights();
	}

	// TODO shouldn't we do this in freeze models mode anyway?
	RT_LightsFrameBegin();
}

static void prepareUniformBuffer( const vk_ray_frame_render_args_t *args, int frame_index, float fov_angle_y ) {
	struct UniformBuffer *ubo = (struct UniformBuffer*)((char*)g_rtx.uniform_buffer.mapped + frame_index * g_rtx.uniform_unit_size);

	matrix4x4 proj_inv, view_inv;
	Matrix4x4_Invert_Full(proj_inv, *args->projection);
	Matrix4x4_ToArrayFloatGL(proj_inv, (float*)ubo->inv_proj);

	// TODO there's a more efficient way to construct an inverse view matrix
	// from vforward/right/up vectors and origin in g_camera
	Matrix4x4_Invert_Full(view_inv, *args->view);
	Matrix4x4_ToArrayFloatGL(view_inv, (float*)ubo->inv_view);

	ubo->ray_cone_width = atanf((2.0f*tanf(DEG2RAD(fov_angle_y) * 0.5f)) / (float)FRAME_HEIGHT);
	ubo->random_seed = (uint32_t)gEngine.COM_RandomLong(0, INT32_MAX);
}

typedef struct {
	const vk_ray_frame_render_args_t* render_args;
	int frame_index;
	float fov_angle_y;
	const vk_lights_bindings_t *light_bindings;
} perform_tracing_args_t;

static void performTracing(VkCommandBuffer cmdbuf, const perform_tracing_args_t* args) {
	// TODO move this to "TLAS producer"
	g_rtx.res[ExternalResource_tlas].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		.value = (vk_descriptor_value_t){
			.accel = (VkWriteDescriptorSetAccelerationStructureKHR) {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
				.accelerationStructureCount = 1,
				.pAccelerationStructures = &g_accel.tlas,
				.pNext = NULL,
			},
		},
	};

#define RES_SET_BUFFER(name, type_, source_, offset_, size_) \
	g_rtx.res[ExternalResource_##name].resource = (vk_resource_t){ \
		.type = type_, \
		.value = (vk_descriptor_value_t) { \
			.buffer = (VkDescriptorBufferInfo) { \
				.buffer = (source_), \
				.offset = (offset_), \
				.range = (size_), \
			} \
		} \
	}

	RES_SET_BUFFER(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, g_rtx.uniform_buffer.buffer, args->frame_index * g_rtx.uniform_unit_size, sizeof(struct UniformBuffer));

#define RES_SET_SBUFFER_FULL(name, source_) \
	RES_SET_BUFFER(name, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, source_.buffer, 0, source_.size)

	// TODO move this to ray model producer
	RES_SET_SBUFFER_FULL(kusochki, g_ray_model_state.kusochki_buffer);

	// TODO move these to vk_geometry
	RES_SET_SBUFFER_FULL(indices, args->render_args->geometry_data);
	RES_SET_SBUFFER_FULL(vertices, args->render_args->geometry_data);

	// TODO move this to lights
	RES_SET_BUFFER(lights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args->light_bindings->buffer, args->light_bindings->metadata.offset, args->light_bindings->metadata.size);
	RES_SET_BUFFER(light_clusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args->light_bindings->buffer, args->light_bindings->grid.offset, args->light_bindings->grid.size);
#undef RES_SET_SBUFFER_FULL
#undef RES_SET_BUFFER

	// Upload kusochki updates
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
			.buffer = g_ray_model_state.kusochki_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		} };

		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	// Clear intra-frame resources
	for (int i = ExternalResource_COUNT; i < MAX_RESOURCES; ++i) {
		rt_resource_t* const res = g_rtx.res + i;
		if (!res->name[0] || !res->image.image)
			continue;

		res->resource.read = res->resource.write = (ray_resource_state_t){0};
	}

	DEBUG_BEGIN(cmdbuf, "yay tracing");
	RT_VkAccelPrepareTlas(cmdbuf);
	prepareUniformBuffer(args->render_args, args->frame_index, args->fov_angle_y);

	// 4. Barrier for TLAS build
	{
		const VkBufferMemoryBarrier bmb[] = { {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.buffer = g_accel.accels_buffer.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		} };
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	{ // FIXME this should be done automatically inside meatpipe, TODO
		//const uint32_t size = sizeof(struct Lights);
		//const uint32_t size = sizeof(struct LightsMetadata); // + 8 * sizeof(uint32_t);
		const VkBufferMemoryBarrier bmb[] = {{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.buffer = args->light_bindings->buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
			0, 0, NULL, ARRAYSIZE(bmb), bmb, 0, NULL);
	}

	R_VkMeatpipePerform(g_rtx.mainpipe, cmdbuf, (vk_meatpipe_perfrom_args_t) {
		.frame_set_slot = args->frame_index,
		.width = FRAME_WIDTH,
		.height = FRAME_HEIGHT,
		.resources = g_rtx.mainpipe_resources,
	});

	{
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.src = {
				.image = g_rtx.mainpipe_out->image.image,
				.width = FRAME_WIDTH,
				.height = FRAME_HEIGHT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			},
			.dst = {
				.image = args->render_args->dst.image,
				.width = args->render_args->dst.width,
				.height = args->render_args->dst.height,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.srcAccessMask = 0,
			},
		};

		R_VkImageBlit( cmdbuf, &blit_args );
	}
	DEBUG_END(cmdbuf);
}

static void cleanupResources(void) {
	for (int i = 0; i < MAX_RESOURCES; ++i) {
		rt_resource_t *const res = g_rtx.res + i;
		if (!res->name[0] || res->refcount || !res->image.image)
			continue;

		XVK_ImageDestroy(&res->image);
		res->name[0] = '\0';
	}
}

static void destroyMainpipe(void) {
	if (!g_rtx.mainpipe)
		return;

	ASSERT(g_rtx.mainpipe_resources);

	for (int i = 0; i < g_rtx.mainpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = g_rtx.mainpipe->resources + i;
		const int index = findResource(mr->name);
		ASSERT(index >= 0);
		ASSERT(index < MAX_RESOURCES);
		rt_resource_t *const res = g_rtx.res + index;
		ASSERT(res->refcount > 0);
		res->refcount--;
	}

	cleanupResources();
	R_VkMeatpipeDestroy(g_rtx.mainpipe);
	g_rtx.mainpipe = NULL;

	Mem_Free(g_rtx.mainpipe_resources);
	g_rtx.mainpipe_resources = NULL;
	g_rtx.mainpipe_out = NULL;
}

static void reloadMainpipe(void) {
	vk_meatpipe_t *const newpipe = R_VkMeatpipeCreateFromFile("rt.meat");
	if (!newpipe)
		return;

	const size_t newpipe_resources_size = sizeof(vk_resource_p) * newpipe->resources_count;
	vk_resource_p *newpipe_resources = Mem_Calloc(vk_core.pool, newpipe_resources_size);
	rt_resource_t *newpipe_out = NULL;

	for (int i = 0; i < newpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = newpipe->resources + i;
		gEngine.Con_Reportf("res %d/%d: %s descriptor=%u count=%d flags=[%c%c] image_format=%u\n",
			i, newpipe->resources_count, mr->name, mr->descriptor_type, mr->count,
			(mr->flags & MEATPIPE_RES_WRITE) ? 'W' : ' ',
			(mr->flags & MEATPIPE_RES_CREATE) ? 'C' : ' ',
			mr->image_format);

		const qboolean create = !!(mr->flags & MEATPIPE_RES_CREATE);

		// FIXME no assert, just complain
		if (create && mr->descriptor_type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			gEngine.Con_Printf(S_ERROR "Only storage image creation is supported for meatpipes\n");
			goto fail;
		}

		// TODO this should be specified as a flag, from rt.json
		const qboolean output = Q_strcmp("dest", mr->name) == 0;

		const int index = create ? getResourceSlotForName(mr->name) : findResource(mr->name);
		if (index < 0) {
			gEngine.Con_Printf(S_ERROR "Couldn't find resource/slot for %s\n", mr->name);
			goto fail;
		}

		rt_resource_t *const res = g_rtx.res + index;

		if (output)
			newpipe_out = res;

		if (create) {
			if (res->image.image == VK_NULL_HANDLE) {
				const xvk_image_create_t create = {
					.debug_name = mr->name,
					.width = FRAME_WIDTH,
					.height = FRAME_HEIGHT,
					.mips = 1,
					.layers = 1,
					.format = mr->image_format,
					.tiling = VK_IMAGE_TILING_OPTIMAL,
					.usage = VK_IMAGE_USAGE_STORAGE_BIT | (output ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
					.has_alpha = true,
					.is_cubemap = false,
				};
				res->image = XVK_ImageCreate(&create);
				Q_strncpy(res->name, mr->name, sizeof(res->name));
			} else {
				// TODO if (mr->image_format != res->image.format) { S_ERROR and goto fail }
			}
		}

		newpipe_resources[i] = &res->resource;

		if (create) {
			if (mr->descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
				newpipe_resources[i]->value.image_object = &res->image;

			res->resource.type = mr->descriptor_type;
		} else {
			// TODO no assert, complain and exit
			// can't do before all resources are properly registered by their producers and not all this temp crap we have right now
			// ASSERT(res->resource.type == mr->descriptor_type);
		}
	}

	if (!newpipe_out) {
		gEngine.Con_Printf(S_ERROR "New rt.json doesn't define an 'dest' output texture\n");
		goto fail;
	}

	// Loading successful
	// Update refcounts
	for (int i = 0; i < newpipe->resources_count; ++i) {
		const vk_meatpipe_resource_t *mr = newpipe->resources + i;
		const int index = findResource(mr->name);
		ASSERT(index >= 0);
		ASSERT(index < MAX_RESOURCES);
		rt_resource_t *const res = g_rtx.res + index;
		res->refcount++;
	}

	destroyMainpipe();

	g_rtx.mainpipe = newpipe;
	g_rtx.mainpipe_resources = newpipe_resources;
	g_rtx.mainpipe_out = newpipe_out;

	return;

fail:
	cleanupResources();

	if (newpipe_resources)
		Mem_Free(newpipe_resources);

	R_VkMeatpipeDestroy(newpipe);
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	// const xvk_ray_frame_images_t* current_frame = g_rtx.frames + (g_rtx.frame_number % 2);

	ASSERT(vk_core.rtx);
	// ubo should contain two matrices
	// FIXME pass these matrices explicitly to let RTX module handle ubo itself

	RT_LightsFrameEnd();
	const vk_lights_bindings_t light_bindings = VK_LightsUpload(cmdbuf);

	g_rtx.frame_number++;

	// if (vk_core.debug)
	// 	XVK_RayModel_Validate();

	if (g_rtx.reload_pipeline) {
		gEngine.Con_Printf(S_WARN "Reloading RTX shaders/pipelines\n");
		XVK_CHECK(vkDeviceWaitIdle(vk_core.device));

		reloadMainpipe();

		g_rtx.reload_pipeline = false;
	}

	ASSERT(g_rtx.mainpipe_out);

	if (g_ray_model_state.frame.num_models == 0) {
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.src = {
				.image = g_rtx.mainpipe_out->image.image,
				.width = FRAME_WIDTH,
				.height = FRAME_HEIGHT,
				.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			},
			.dst = {
				.image = args->dst.image,
				.width = args->dst.width,
				.height = args->dst.height,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.srcAccessMask = 0,
			},
		};

		R_VkImageClear( cmdbuf, g_rtx.mainpipe_out->image.image );
		R_VkImageBlit( cmdbuf, &blit_args );
	} else {
		const perform_tracing_args_t trace_args = {
			.render_args = args,
			.frame_index = (g_rtx.frame_number % 2),
			.fov_angle_y = args->fov_angle_y,
			.light_bindings = &light_bindings,
		};
		performTracing( cmdbuf, &trace_args );
	}
}

static void reloadPipeline( void ) {
	g_rtx.reload_pipeline = true;
}

static void reloadLighting( void ) {
	g_rtx.reload_lighting = true;
}

static void freezeModels( void ) {
	g_ray_model_state.freeze_models = !g_ray_model_state.freeze_models;
}

qboolean VK_RayInit( void )
{
	ASSERT(vk_core.rtx);
	// TODO complain and cleanup on failure

	if (!RT_VkAccelInit())
		return false;

#define REGISTER_EXTERNAL(type, name_) \
	Q_strncpy(g_rtx.res[ExternalResource_##name_].name, #name_, sizeof(g_rtx.res[0].name)); \
	g_rtx.res[ExternalResource_##name_].refcount = 1;
	EXTERNAL_RESOUCES(REGISTER_EXTERNAL)
#undef REGISTER_EXTERNAL

	g_rtx.res[ExternalResource_textures].resource = (vk_resource_t){
		.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.value = (vk_descriptor_value_t){
			.image_array = tglob.dii_all_textures,
		}
	};
	g_rtx.res[ExternalResource_textures].refcount = 1;

	reloadMainpipe();
	if (!g_rtx.mainpipe)
		return false;

	g_rtx.uniform_unit_size = ALIGN_UP(sizeof(struct UniformBuffer), vk_core.physical_device.properties.limits.minUniformBufferOffsetAlignment);

	if (!VK_BufferCreate("ray uniform_buffer", &g_rtx.uniform_buffer, g_rtx.uniform_unit_size * MAX_FRAMES_IN_FLIGHT,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
	{
		return false;
	}

	if (!VK_BufferCreate("ray kusochki_buffer", &g_ray_model_state.kusochki_buffer, sizeof(vk_kusok_data_t) * MAX_KUSOCHKI,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
		// FIXME complain, handle
		return false;
	}
	RT_RayModel_Clear();

	gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");
	gEngine.Cmd_AddCommand("vk_rtx_reload_rad", reloadLighting, "Reload RAD files for static lights");
	gEngine.Cmd_AddCommand("vk_rtx_freeze", freezeModels, "Freeze models, do not update/add/delete models from to-draw list");

	return true;
}

void VK_RayShutdown( void ) {
	ASSERT(vk_core.rtx);

	destroyMainpipe();

	VK_BufferDestroy(&g_ray_model_state.kusochki_buffer);
	VK_BufferDestroy(&g_rtx.uniform_buffer);

	RT_VkAccelShutdown();
}
