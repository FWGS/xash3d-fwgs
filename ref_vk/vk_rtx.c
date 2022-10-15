#include "vk_rtx.h"

#include "ray_pass.h"
#include "ray_resources.h"
#include "vk_ray_accel.h"

#include "vk_ray_primary.h"
#include "vk_ray_light_direct.h"

#include "vk_buffer.h"
#include "vk_common.h"
#include "vk_core.h"
#include "vk_cvar.h"
#include "vk_denoiser.h"
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

// TODO sync with shaders
// TODO optimal values
#define WG_W 16
#define WG_H 8

typedef struct {
	vec3_t pos;
	float radius;
	vec3_t color;
	float padding_;
} vk_light_t;

typedef struct PushConstants vk_rtx_push_constants_t;

typedef struct {
	xvk_image_t denoised;

#define X(index, name, ...) xvk_image_t name;
RAY_PRIMARY_OUTPUTS(X)
RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
#undef X

	xvk_image_t diffuse_gi;
	xvk_image_t specular;
	xvk_image_t additive;
} xvk_ray_frame_images_t;

static struct {
	// Holds UniformBuffer data
	vk_buffer_t uniform_buffer;
	uint32_t uniform_unit_size;

	// TODO with proper intra-cmdbuf sync we don't really need 2x images
	unsigned frame_number;
	xvk_ray_frame_images_t frames[MAX_FRAMES_IN_FLIGHT];

	struct {
		struct ray_pass_s *primary_ray;
		struct ray_pass_s *light_direct_poly;
		struct ray_pass_s *light_direct_point;
		struct ray_pass_s *denoiser;
	} pass;

	vk_meatpipe_t mainpipe;

	qboolean reload_pipeline;
	qboolean reload_lighting;
} g_rtx = {0};


void VK_RayNewMap( void ) {
	RT_VkAccelNewMap();
	RT_RayModel_Clear();
}

void VK_RayFrameBegin( void )
{
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
	const xvk_ray_frame_images_t *current_frame;
	float fov_angle_y;
	const vk_lights_bindings_t *light_bindings;
} perform_tracing_args_t;

static void performTracing(VkCommandBuffer cmdbuf, const perform_tracing_args_t* args) {
	vk_ray_resources_t res = {
		.width = FRAME_WIDTH,
		.height = FRAME_HEIGHT,
		.resources = {
			[RayResource_tlas] = {
				.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
				.value.accel = (VkWriteDescriptorSetAccelerationStructureKHR){
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
					.accelerationStructureCount = 1,
					.pAccelerationStructures = &g_accel.tlas,
					.pNext = NULL,
				},
			},
#define RES_SET_BUFFER(name, type_, source_, offset_, size_) \
	[RayResource_##name] = { \
		.type = type_, \
		.value.buffer = (VkDescriptorBufferInfo) { \
			.buffer = (source_), \
			.offset = (offset_), \
			.range = (size_), \
		} \
	}
			RES_SET_BUFFER(ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, g_rtx.uniform_buffer.buffer, args->frame_index * g_rtx.uniform_unit_size, sizeof(struct UniformBuffer)),

#define RES_SET_SBUFFER_FULL(name, source_) \
	RES_SET_BUFFER(name, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, source_.buffer, 0, source_.size)
			RES_SET_SBUFFER_FULL(kusochki, g_ray_model_state.kusochki_buffer),
			RES_SET_SBUFFER_FULL(indices, args->render_args->geometry_data),
			RES_SET_SBUFFER_FULL(vertices, args->render_args->geometry_data),
			RES_SET_BUFFER(lights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args->light_bindings->buffer, args->light_bindings->metadata.offset, args->light_bindings->metadata.size),
			RES_SET_BUFFER(light_clusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, args->light_bindings->buffer, args->light_bindings->grid.offset, args->light_bindings->grid.size),
#undef RES_SET_SBUFFER_FULL
#undef RES_SET_BUFFER

			[RayResource_all_textures] = {
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.value.image_array = tglob.dii_all_textures,
			},

			[RayResource_skybox] = {
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.value.image = {
					.sampler = vk_core.default_sampler,
					.imageView = tglob.skybox_cube.vk.image.view ? tglob.skybox_cube.vk.image.view : tglob.cubemap_placeholder.vk.image.view,
					.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				},
			},

#define RES_SET_IMAGE(index, name, ...) \
	[RayResource_##name] = { \
		.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
		.write = {0}, \
		.read = {0}, \
		.image = &args->current_frame->name, \
	},
			RAY_PRIMARY_OUTPUTS(RES_SET_IMAGE)
			RAY_LIGHT_DIRECT_POLY_OUTPUTS(RES_SET_IMAGE)
			RAY_LIGHT_DIRECT_POINT_OUTPUTS(RES_SET_IMAGE)
			RES_SET_IMAGE(-1, denoised)
#undef RES_SET_IMAGE
		},
	};

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

	RayPassPerform( cmdbuf, args->frame_index, g_rtx.pass.primary_ray, &res );

	{
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

	RayPassPerform( cmdbuf, args->frame_index, g_rtx.pass.light_direct_poly, &res );
	RayPassPerform( cmdbuf, args->frame_index, g_rtx.pass.light_direct_point, &res );
	RayPassPerform( cmdbuf, args->frame_index, g_rtx.pass.denoiser, &res );

	{
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.src = {
				.image = args->current_frame->denoised.image,
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

static void reloadPass( struct ray_pass_s **slot, struct ray_pass_s *new_pass ) {
	if (!new_pass)
		return;

	RayPassDestroy( *slot );
	*slot = new_pass;
}

void VK_RayFrameEnd(const vk_ray_frame_render_args_t* args)
{
	const VkCommandBuffer cmdbuf = args->cmdbuf;
	const xvk_ray_frame_images_t* current_frame = g_rtx.frames + (g_rtx.frame_number % 2);

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

		reloadPass( &g_rtx.pass.primary_ray, R_VkRayPrimaryPassCreate());
		reloadPass( &g_rtx.pass.light_direct_poly, R_VkRayLightDirectPolyPassCreate());
		reloadPass( &g_rtx.pass.light_direct_point, R_VkRayLightDirectPointPassCreate());
		reloadPass( &g_rtx.pass.denoiser, R_VkRayDenoiserCreate());

		g_rtx.reload_pipeline = false;
	}

	if (g_ray_model_state.frame.num_models == 0) {
		const r_vkimage_blit_args blit_args = {
			.in_stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.src = {
				.image = current_frame->denoised.image,
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

		R_VkImageClear( cmdbuf, current_frame->denoised.image );
		R_VkImageBlit( cmdbuf, &blit_args );
	} else {
		const perform_tracing_args_t trace_args = {
			.render_args = args,
			.frame_index = (g_rtx.frame_number % 2),
			.current_frame = current_frame,
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

	g_rtx.pass.primary_ray = R_VkRayPrimaryPassCreate();
	ASSERT(g_rtx.pass.primary_ray);

	g_rtx.pass.light_direct_poly = R_VkRayLightDirectPolyPassCreate();
	ASSERT(g_rtx.pass.light_direct_poly);

	g_rtx.pass.light_direct_point = R_VkRayLightDirectPointPassCreate();
	ASSERT(g_rtx.pass.light_direct_point);

	g_rtx.pass.denoiser = R_VkRayDenoiserCreate();
	ASSERT(g_rtx.pass.denoiser);

	ASSERT(R_VkMeatpipeLoad(&g_rtx.mainpipe, "rt.meat"));

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

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i) {
#define CREATE_GBUFFER_IMAGE(name, format_, add_usage_bits) \
		do { \
			char debug_name[64]; \
			const xvk_image_create_t create = { \
				.debug_name = debug_name, \
				.width = FRAME_WIDTH, \
				.height = FRAME_HEIGHT, \
				.mips = 1, \
				.layers = 1, \
				.format = format_, \
				.tiling = VK_IMAGE_TILING_OPTIMAL, \
				.usage = VK_IMAGE_USAGE_STORAGE_BIT | add_usage_bits, \
				.has_alpha = true, \
				.is_cubemap = false, \
			}; \
			Q_snprintf(debug_name, sizeof(debug_name), "rtx frames[%d] " # name, i); \
			g_rtx.frames[i].name = XVK_ImageCreate(&create); \
		} while(0)

		CREATE_GBUFFER_IMAGE(denoised, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

#define rgba8 VK_FORMAT_R8G8B8A8_UNORM
#define rgba32f VK_FORMAT_R32G32B32A32_SFLOAT
#define rgba16f VK_FORMAT_R16G16B16A16_SFLOAT
#define X(index, name, format) CREATE_GBUFFER_IMAGE(name, format, 0);
// TODO better format for normals VK_FORMAT_R16G16B16A16_SNORM
// TODO make sure this format and usage is suppported
		RAY_PRIMARY_OUTPUTS(X)
		RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
		RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
#undef X
#undef rgba8
#undef rgba32f
#undef rgba16f
		CREATE_GBUFFER_IMAGE(diffuse_gi, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
		CREATE_GBUFFER_IMAGE(specular, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
		CREATE_GBUFFER_IMAGE(additive, VK_FORMAT_R16G16B16A16_SFLOAT, 0);
#undef CREATE_GBUFFER_IMAGE
	}

	gEngine.Cmd_AddCommand("vk_rtx_reload", reloadPipeline, "Reload RTX shader");
	gEngine.Cmd_AddCommand("vk_rtx_reload_rad", reloadLighting, "Reload RAD files for static lights");
	gEngine.Cmd_AddCommand("vk_rtx_freeze", freezeModels, "Freeze models, do not update/add/delete models from to-draw list");

	return true;
}

void VK_RayShutdown( void ) {
	ASSERT(vk_core.rtx);

	R_VkMeatpipeDestroy(&g_rtx.mainpipe);

	RayPassDestroy(g_rtx.pass.denoiser);
	RayPassDestroy(g_rtx.pass.light_direct_poly);
	RayPassDestroy(g_rtx.pass.light_direct_point);
	RayPassDestroy(g_rtx.pass.primary_ray);

	for (int i = 0; i < ARRAYSIZE(g_rtx.frames); ++i) {
		XVK_ImageDestroy(&g_rtx.frames[i].denoised);
#define X(index, name, ...) XVK_ImageDestroy(&g_rtx.frames[i].name);
		RAY_PRIMARY_OUTPUTS(X)
		RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
		RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
#undef X
		XVK_ImageDestroy(&g_rtx.frames[i].diffuse_gi);
		XVK_ImageDestroy(&g_rtx.frames[i].specular);
		XVK_ImageDestroy(&g_rtx.frames[i].additive);
	}

	VK_BufferDestroy(&g_ray_model_state.kusochki_buffer);
	VK_BufferDestroy(&g_rtx.uniform_buffer);

	RT_VkAccelShutdown();
}
