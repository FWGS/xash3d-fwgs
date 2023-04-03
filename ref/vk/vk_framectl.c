#include "vk_framectl.h"

#include "vk_overlay.h"
#include "vk_scene.h"
#include "vk_render.h"
#include "vk_rtx.h"
#include "vk_cvar.h"
#include "vk_devmem.h"
#include "vk_swapchain.h"
#include "vk_image.h"
#include "vk_staging.h"
#include "vk_commandpool.h"
#include "vk_combuf.h"

#include "profiler.h"
#include "r_speeds.h"

#include "eiface.h" // ARRAYSIZE

#include <string.h>

extern ref_globals_t *gpGlobals;

vk_framectl_t vk_frame = {0};

// Phase tracking is needed for getting screenshots. Basically, getting a screenshot does the same things as R_EndFrame, and they need to be congruent.
typedef enum {
	Phase_Idle,
	Phase_FrameBegan,
	Phase_RenderingEnqueued,
	Phase_Submitted,
} frame_phase_t;

typedef struct {
	vk_combuf_t *combuf;
	VkFence fence_done;
	VkSemaphore sem_framebuffer_ready;
	VkSemaphore sem_done;

	// This extra semaphore is required because we need to synchronize 2 things on GPU:
	// 1. swapchain
	// 2. next frame command buffer
	// Unfortunately waiting on semaphore also means resetting it when it is signaled
	// so we can't reuse the same one for two purposes and need to mnozhit sunchnosti
	VkSemaphore sem_done2;
} vk_framectl_frame_t;

static struct {
	vk_framectl_frame_t frames[MAX_CONCURRENT_FRAMES];

	struct {
		int index;
		r_vk_swapchain_framebuffer_t framebuffer;
		frame_phase_t phase;
	} current;
} g_frame;

#define PROFILER_SCOPES(X) \
	X(frame, "Frame", APROF_SCOPE_FLAG_DECOR); \
	X(begin_frame, "R_BeginFrame", 0); \
	X(render_frame, "VK_RenderFrame", 0); \
	X(end_frame, "R_EndFrame", 0); \
	X(frame_gpu_wait, "Wait for GPU", APROF_SCOPE_FLAG_WAIT); \
	X(wait_for_frame_fence, "waitForFrameFence", APROF_SCOPE_FLAG_WAIT); \

#define SCOPE_DECLARE(scope, name, flags) APROF_SCOPE_DECLARE(scope)
PROFILER_SCOPES(SCOPE_DECLARE)
#undef SCOPE_DECLARE

// TODO move into vk_image
static VkFormat findSupportedImageFormat(const VkFormat *candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
	for (int i = 0; candidates[i] != VK_FORMAT_UNDEFINED; ++i) {
		VkFormatProperties props;
		VkFormatFeatureFlags props_format;
		vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, candidates[i], &props);
		switch (tiling) {
			case VK_IMAGE_TILING_OPTIMAL:
				props_format = props.optimalTilingFeatures; break;
			case VK_IMAGE_TILING_LINEAR:
				props_format = props.linearTilingFeatures; break;
			default:
				return VK_FORMAT_UNDEFINED;
		}
		if ((props_format & features) == features)
			return candidates[i];
	}

	return VK_FORMAT_UNDEFINED;
}

// TODO sort these based on ???
static const VkFormat depth_formats[] = {
	VK_FORMAT_D32_SFLOAT,
	VK_FORMAT_D24_UNORM_S8_UINT,
	VK_FORMAT_X8_D24_UNORM_PACK32,
	VK_FORMAT_D16_UNORM,
	VK_FORMAT_D32_SFLOAT_S8_UINT,
	VK_FORMAT_D16_UNORM_S8_UINT,
	VK_FORMAT_UNDEFINED
};

static VkRenderPass createRenderPass( VkFormat depth_format, qboolean ray_tracing ) {
	VkRenderPass render_pass;

	const VkAttachmentDescription attachments[] = {{
		.format = SWAPCHAIN_FORMAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = ray_tracing ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR /* TODO: prod renderer should not care VK_ATTACHMENT_LOAD_OP_DONT_CARE */,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = ray_tracing ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	}, {
		// Depth
		.format = depth_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	}};

	const VkAttachmentReference color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	const VkAttachmentReference depth_attachment = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	const VkSubpassDescription subdesc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		.pDepthStencilAttachment = &depth_attachment,
	};

	const VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = ARRAYSIZE(attachments),
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subdesc,
	};

	XVK_CHECK(vkCreateRenderPass(vk_core.device, &rpci, NULL, &render_pass));
	return render_pass;
}

static void waitForFrameFence( void ) {
	APROF_SCOPE_BEGIN(wait_for_frame_fence);
	const VkFence fence_done[1] = {g_frame.frames[g_frame.current.index].fence_done};
	for(qboolean loop = true; loop; ) {
#define MAX_WAIT (10ull * 1000*1000*1000)
		const VkResult fence_result = vkWaitForFences(vk_core.device, COUNTOF(fence_done), fence_done, VK_TRUE, MAX_WAIT);
#undef MAX_WAIT
		switch (fence_result) {
			case VK_SUCCESS:
				loop = false;
				break;
			case VK_TIMEOUT:
				gEngine.Con_Printf(S_ERROR "Waiting for frame fence to be signaled timed out after 10 seconds. Wat\n");
				break;
			default:
				XVK_CHECK(fence_result);
		}
	}

	XVK_CHECK(vkResetFences(vk_core.device, COUNTOF(fence_done), fence_done));
	APROF_SCOPE_END(wait_for_frame_fence);
}

static void updateGamma( void ) {
	// FIXME when
	{
		cvar_t* vid_gamma = gEngine.pfnGetCvarPointer( "gamma", 0 );
		cvar_t* vid_brightness = gEngine.pfnGetCvarPointer( "brightness", 0 );
		if( gEngine.R_DoResetGamma( ))
		{
			// paranoia cubemaps uses this
			gEngine.BuildGammaTable( 1.8f, 0.0f );

			// paranoia cubemap rendering
			if( gEngine.drawFuncs->GL_BuildLightmaps )
				gEngine.drawFuncs->GL_BuildLightmaps( );
		}
		else if( FBitSet( vid_gamma->flags, FCVAR_CHANGED ) || FBitSet( vid_brightness->flags, FCVAR_CHANGED ))
		{
			gEngine.BuildGammaTable( vid_gamma->value, vid_brightness->value );
			// FIXME rebuild lightmaps
		}
	}
}

void R_BeginFrame( qboolean clearScene ) {
	APROF_SCOPE_DECLARE_BEGIN(begin_frame_tail, "R_BeginFrame_tail");
	ASSERT(g_frame.current.phase == Phase_Submitted || g_frame.current.phase == Phase_Idle);
	g_frame.current.index = (g_frame.current.index + 1) % MAX_CONCURRENT_FRAMES;

	vk_framectl_frame_t *const frame = g_frame.frames + g_frame.current.index;

	{
		waitForFrameFence();
		// Current command buffer is done and available
		// Previous might still be in flight

		// TODO R_VkQueryPoolGetFrameResults(g_frame.qpools + g_frame.current.index);
	}

	APROF_SCOPE_END(begin_frame_tail);

	const uint32_t prev_frame_event_index = aprof_scope_frame();

	APROF_SCOPE_BEGIN(frame);
	APROF_SCOPE_BEGIN(begin_frame);

	{
		const vk_combuf_scopes_t gpurofl = R_VkCombufScopesGet(frame->combuf);
		R_SpeedsDisplayMore(prev_frame_event_index, &gpurofl);
	}

	if (vk_core.rtx && FBitSet( vk_rtx->flags, FCVAR_CHANGED )) {
		vk_frame.rtx_enabled = CVAR_TO_BOOL( vk_rtx );
	}
	ClearBits( vk_rtx->flags, FCVAR_CHANGED );

	updateGamma();

	ASSERT(!g_frame.current.framebuffer.framebuffer);

	R_VkStagingFrameBegin();

	g_frame.current.framebuffer = R_VkSwapchainAcquire( frame->sem_framebuffer_ready );
	vk_frame.width = g_frame.current.framebuffer.width;
	vk_frame.height = g_frame.current.framebuffer.height;

	VK_RenderBegin( vk_frame.rtx_enabled );

	R_VkCombufBegin( frame->combuf );

	g_frame.current.phase = Phase_FrameBegan;
	APROF_SCOPE_END(begin_frame);
}

void VK_RenderFrame( const struct ref_viewpass_s *rvp )
{
	APROF_SCOPE_BEGIN(render_frame);
	VK_SceneRender( rvp );
	APROF_SCOPE_END(render_frame);
}

static void enqueueRendering( vk_combuf_t* combuf ) {
	const VkClearValue clear_value[] = {
		{.color = {{1., 0., 0., 0.}}},
		{.depthStencil = {1., 0.}} // TODO reverse-z
	};

	ASSERT(g_frame.current.phase == Phase_FrameBegan);

	const VkCommandBuffer cmdbuf = combuf->cmdbuf;
	VK_Render_FIXME_Barrier(cmdbuf);

	if (vk_frame.rtx_enabled)
		VK_RenderEndRTX( combuf, g_frame.current.framebuffer.view, g_frame.current.framebuffer.image, g_frame.current.framebuffer.width, g_frame.current.framebuffer.height );

	{
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_frame.rtx_enabled ? vk_frame.render_pass.after_ray_tracing : vk_frame.render_pass.raster,
			.renderArea.extent.width = g_frame.current.framebuffer.width,
			.renderArea.extent.height = g_frame.current.framebuffer.height,
			.clearValueCount = ARRAYSIZE(clear_value),
			.pClearValues = clear_value,
			.framebuffer = g_frame.current.framebuffer.framebuffer,
		};
		vkCmdBeginRenderPass(cmdbuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	}

	{
		const VkViewport viewport[] = {
			{0.f, 0.f, (float)g_frame.current.framebuffer.width, (float)g_frame.current.framebuffer.height, 0.f, 1.f},
		};
		const VkRect2D scissor[] = {{
			{0, 0},
			{g_frame.current.framebuffer.width, g_frame.current.framebuffer.height},
		}};

		vkCmdSetViewport(cmdbuf, 0, ARRAYSIZE(viewport), viewport);
		vkCmdSetScissor(cmdbuf, 0, ARRAYSIZE(scissor), scissor);
	}

	if (!vk_frame.rtx_enabled)
		VK_RenderEnd( cmdbuf );

	R_VkOverlay_DrawAndFlip( cmdbuf );

	vkCmdEndRenderPass(cmdbuf);

	g_frame.current.phase = Phase_RenderingEnqueued;
}

static void submit( vk_combuf_t* combuf, qboolean wait ) {
	ASSERT(g_frame.current.phase == Phase_RenderingEnqueued);

	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	vk_framectl_frame_t *const frame = g_frame.frames + g_frame.current.index;
	vk_framectl_frame_t *const prev_frame = g_frame.frames + (g_frame.current.index + 1) % MAX_CONCURRENT_FRAMES;

	R_VkCombufEnd(combuf);

	const VkCommandBuffer cmdbufs[] = {
		R_VkStagingFrameEnd(),
		cmdbuf,
	};

	{
		const VkPipelineStageFlags stageflags[] = {
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		};

		// TODO for RT renderer we only touch framebuffer at the very end of rendering/cmdbuf.
		// Can we postpone waitinf for framebuffer semaphore until we actually need it.
		const VkSemaphore waitophores[] = {
			frame->sem_framebuffer_ready,
			prev_frame->sem_done2,
		};
		const VkSemaphore signalphores[] = {
			frame->sem_done,
			frame->sem_done2,
		};
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = NULL,
			.commandBufferCount = cmdbufs[0] ? 2 : 1,
			.pCommandBuffers = cmdbufs[0] ? cmdbufs : cmdbufs + 1,
			.waitSemaphoreCount = COUNTOF(waitophores),
			.pWaitSemaphores = waitophores,
			.pWaitDstStageMask = stageflags,
			.signalSemaphoreCount = COUNTOF(signalphores),
			.pSignalSemaphores = signalphores,
		};
		//gEngine.Con_Printf("SYNC: wait for semaphore %d, signal semaphore %d\n", (g_frame.current.index + 1) % MAX_CONCURRENT_FRAMES, g_frame.current.index);
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, frame->fence_done));
		g_frame.current.phase = Phase_Submitted;
	}

	R_VkSwapchainPresent(g_frame.current.framebuffer.index, frame->sem_done);
	g_frame.current.framebuffer = (r_vk_swapchain_framebuffer_t){0};

	if (wait) {
		APROF_SCOPE_BEGIN(frame_gpu_wait);
		XVK_CHECK(vkWaitForFences(vk_core.device, 1, &frame->fence_done, VK_TRUE, INT64_MAX));
		APROF_SCOPE_END(frame_gpu_wait);

		/* if (vk_core.debug) { */
		/* 	// FIXME more scopes */
		/* 	XVK_CHECK(vkQueueWaitIdle(vk_core.queue)); */
		/* } */
		g_frame.current.phase = Phase_Idle;
	}
}

inline static VkCommandBuffer currentCommandBuffer( void ) {
	return g_frame.frames[g_frame.current.index].combuf->cmdbuf;
}

void R_EndFrame( void )
{
	APROF_SCOPE_BEGIN_EARLY(end_frame);

	if (g_frame.current.phase == Phase_FrameBegan) {
		vk_combuf_t *const combuf = g_frame.frames[g_frame.current.index].combuf;
		enqueueRendering( combuf );
		submit( combuf, false );
		//submit( cmdbuf, true );
	}

	APROF_SCOPE_END(end_frame);
	APROF_SCOPE_END(frame);
}

static void toggleRaytracing( void ) {
	ASSERT(vk_core.rtx);
	vk_frame.rtx_enabled = !vk_frame.rtx_enabled;
	gEngine.Cvar_Set("vk_rtx", vk_frame.rtx_enabled ? "1" : "0");
	gEngine.Con_Printf(S_WARN "Switching ray tracing to %d\n", vk_frame.rtx_enabled);
}

qboolean VK_FrameCtlInit( void )
{
	PROFILER_SCOPES(APROF_SCOPE_INIT_EX);

	const VkFormat depth_format = findSupportedImageFormat(depth_formats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// FIXME move this out to renderers
	vk_frame.render_pass.raster = createRenderPass(depth_format, false);
	if (vk_core.rtx)
		vk_frame.render_pass.after_ray_tracing = createRenderPass(depth_format, true);

	if (!R_VkSwapchainInit(vk_frame.render_pass.raster, depth_format))
		return false;

	for (int i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
		vk_framectl_frame_t *const frame = g_frame.frames + i;
		frame->combuf = R_VkCombufOpen();

		frame->sem_framebuffer_ready = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_framebuffer_ready, VK_OBJECT_TYPE_SEMAPHORE, "framebuffer_ready[%d]", i);
		frame->sem_done = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_done, VK_OBJECT_TYPE_SEMAPHORE, "done[%d]", i);
		frame->sem_done2 = R_VkSemaphoreCreate();
		SET_DEBUG_NAMEF(frame->sem_done2, VK_OBJECT_TYPE_SEMAPHORE, "done2[%d]", i);
		frame->fence_done = R_VkFenceCreate(true);
		SET_DEBUG_NAMEF(frame->fence_done, VK_OBJECT_TYPE_FENCE, "done[%d]", i);
	}

	// Signal first frame semaphore as done
	{
		const VkPipelineStageFlags stageflags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = NULL,
			.commandBufferCount = 0,
			.pCommandBuffers = NULL,
			.waitSemaphoreCount = 0,
			.pWaitSemaphores = NULL,
			.pWaitDstStageMask = &stageflags,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &g_frame.frames[0].sem_done2,
		};
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
	}

	vk_frame.rtx_enabled = vk_core.rtx;

	if (vk_core.rtx) {
		gEngine.Cmd_AddCommand("vk_rtx_toggle", toggleRaytracing, "Toggle between rasterization and ray tracing");
	}

	return true;
}

void VK_FrameCtlShutdown( void ) {
	for (int i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
		vk_framectl_frame_t *const frame = g_frame.frames + i;
		R_VkCombufClose(frame->combuf);
		R_VkSemaphoreDestroy(frame->sem_framebuffer_ready);
		R_VkSemaphoreDestroy(frame->sem_done);
		R_VkSemaphoreDestroy(frame->sem_done2);
		R_VkFenceDestroy(frame->fence_done);
	}

	R_VkSwapchainShutdown();

	vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.raster, NULL);
	if (vk_core.rtx)
		vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.after_ray_tracing, NULL);
}

static qboolean canBlitFromSwapchainToFormat( VkFormat dest_format ) {
	VkFormatProperties props;

	vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, SWAPCHAIN_FORMAT, &props);
	if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
		gEngine.Con_Reportf(S_WARN "Swapchain source format doesn't support blit\n");
		return false;
	}

	vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, dest_format, &props);
	if (!(props.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
		gEngine.Con_Reportf(S_WARN "Destination format doesn't support blit\n");
		return false;
	}

	return true;
}

static rgbdata_t *XVK_ReadPixels( void ) {
	const VkFormat dest_format = VK_FORMAT_R8G8B8A8_UNORM;
	xvk_image_t dest_image;
	const VkImage frame_image = g_frame.current.framebuffer.image;
	rgbdata_t *r_shot = NULL;
	qboolean blit = canBlitFromSwapchainToFormat( dest_format );

	vk_combuf_t *const combuf = g_frame.frames[g_frame.current.index].combuf;
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	if (frame_image == VK_NULL_HANDLE) {
		gEngine.Con_Printf(S_ERROR "no current image, can't take screenshot\n");
		return NULL;
	}

	// Create destination image to blit/copy framebuffer pixels to
	{
		const xvk_image_create_t xic = {
			.debug_name = "screenshot",
			.width = vk_frame.width,
			.height = vk_frame.height,
			.mips = 1,
			.layers = 1,
			.format = dest_format,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.has_alpha = false,
			.is_cubemap = false,
			.memory_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		};
		dest_image = XVK_ImageCreate(&xic);
	}

	// Make sure that all rendering ops are enqueued
	enqueueRendering( combuf );

	{
		// Barrier 1: dest image
		const VkImageMemoryBarrier image_barrier[2] = {{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = dest_image.image,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}}, { // Barrier 2: source swapchain image
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = frame_image,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
		}}};

		vkCmdPipelineBarrier(cmdbuf,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, ARRAYSIZE(image_barrier), image_barrier);
	}

	// Blit/transfer
	if (blit) {
		const VkImageBlit blit = {
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
			.dstSubresource.layerCount = 1,
			.srcOffsets = {{0}, {vk_frame.width, vk_frame.height, 1}},
			.dstOffsets = {{0}, {vk_frame.width, vk_frame.height, 1}}
		};
		vkCmdBlitImage(cmdbuf,
			frame_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dest_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
	} else {
		const VkImageCopy copy = {
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
			.dstSubresource.layerCount = 1,
			.extent.width = vk_frame.width,
			.extent.height = vk_frame.height,
			.extent.depth = 1,
		};

		vkCmdCopyImage(cmdbuf,
			frame_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dest_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		gEngine.Con_Printf(S_WARN "Blit is not supported, screenshot will likely have mixed components; TODO: swizzle in software\n");
	}

	{
		// Barrier 1: dest image
		VkImageMemoryBarrier image_barrier[2] = {{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = dest_image.image,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}}, { // Barrier 2: source swapchain image
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = frame_image,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.subresourceRange = (VkImageSubresourceRange) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
		}}};

		vkCmdPipelineBarrier(cmdbuf,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				0, 0, NULL, 0, NULL, ARRAYSIZE(image_barrier), image_barrier);
	}

	submit( combuf, true );

	// copy bytes to buffer
	{
		const VkImageSubresource subres = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		};
		VkSubresourceLayout layout;
		const char *mapped = dest_image.devmem.mapped;
		vkGetImageSubresourceLayout(vk_core.device, dest_image.image, &subres, &layout);

		mapped += layout.offset;

		{
			const int row_size = 4 * vk_frame.width;
			poolhandle_t r_temppool = vk_core.pool; // TODO

			r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
			r_shot->width = vk_frame.width;
			r_shot->height = vk_frame.height;
			r_shot->flags = IMAGE_HAS_COLOR;
			r_shot->type = PF_RGBA_32;
			r_shot->size = r_shot->width * r_shot->height * gEngine.Image_GetPFDesc( r_shot->type )->bpp;
			r_shot->palette = NULL;
			r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

			if (!blit) {
				if (dest_format != VK_FORMAT_R8G8B8A8_UNORM || SWAPCHAIN_FORMAT != VK_FORMAT_B8G8R8A8_UNORM) {
					gEngine.Con_Printf(S_WARN "Don't have a blit function for this format pair, will save as-is without conversion; expect image to look wrong\n");
					blit = true;
				} else {
					byte *dst = r_shot->buffer;
					for (int y = 0; y < vk_frame.height; ++y, mapped += layout.rowPitch) {
						const byte *src = (const byte*)mapped;
						for (int x = 0; x < vk_frame.width; ++x, dst += 4, src += 4) {
							dst[0] = src[2];
							dst[1] = src[1];
							dst[2] = src[0];
							dst[3] = src[3];
						}
					}
				}
			}

			if (blit) {
				for (int y = 0; y < vk_frame.height; ++y, mapped += layout.rowPitch) {
					memcpy(r_shot->buffer + row_size * y, mapped, row_size);
				}
			}
		}
	}

	XVK_ImageDestroy( &dest_image );

	return r_shot;
}

qboolean VID_ScreenShot( const char *filename, int shot_type )
{
	uint flags = 0;
	int	width = 0, height = 0;
	qboolean	result;

	// get screen frame
	rgbdata_t *r_shot = XVK_ReadPixels();
	if (!r_shot)
		return false;

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngine.fsapi->AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
		flags |= IMAGE_RESAMPLE;
		if( gpGlobals->wideScreen )
		{
			height = 480;
			width = 800;
		}
		else
		{
			height = 480;
			width = 640;
		}
		break;
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = 200;
		width = 320;
		break;
	case VID_MAPSHOT:
		flags |= IMAGE_RESAMPLE|IMAGE_QUANTIZE;	// GoldSrc request overviews in 8-bit format
		height = 768;
		width = 1024;
		break;
	}

	gEngine.Image_Process( &r_shot, width, height, flags, 0.0f );

	// write image
	result = gEngine.FS_SaveImage( filename, r_shot );
	gEngine.fsapi->AllowDirectPaths( false );			// always reset after store screenshot
	gEngine.FS_FreeImage( r_shot );

	gEngine.Con_Printf("Wrote screenshot %s\n", filename);
	return result;
}
