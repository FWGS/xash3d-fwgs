#include "vk_framectl.h"

#include "vk_2d.h"
#include "vk_scene.h"
#include "vk_render.h"
#include "vk_rtx.h"
#include "vk_cvar.h"
#include "vk_devmem.h"
#include "vk_swapchain.h"

#include "profiler.h"

#include "eiface.h" // ARRAYSIZE

#include <string.h>

extern ref_globals_t *gpGlobals;

vk_framectl_t vk_frame = {0};

static struct {
	// TODO N frames in flight
	VkSemaphore image_available;
	VkSemaphore done;
	VkFence fence;

	qboolean rtx_enabled;

	r_vk_swapchain_framebuffer_t current_framebuffer;
} g_frame;

#define PROFILER_SCOPES(X) \
	X(end_frame , "R_EndFrame"); \
	X(frame_gpu_wait, "Wait for GPU"); \

#define SCOPE_DECLARE(scope, name) APROF_SCOPE_DECLARE(scope)
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

	VkAttachmentDescription attachments[] = {{
		.format = VK_FORMAT_B8G8R8A8_UNORM, //SRGB,// FIXME too early swapchain.create_info.imageFormat;
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

	VkAttachmentReference color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkAttachmentReference depth_attachment = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subdesc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		.pDepthStencilAttachment = &depth_attachment,
	};

	VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = ARRAYSIZE(attachments),
		.pAttachments = attachments,
		.subpassCount = 1,
		.pSubpasses = &subdesc,
	};

	XVK_CHECK(vkCreateRenderPass(vk_core.device, &rpci, NULL, &render_pass));
	return render_pass;
}

void R_BeginFrame( qboolean clearScene )
{
	if (vk_core.rtx && FBitSet( vk_rtx->flags, FCVAR_CHANGED )) {
		g_frame.rtx_enabled = CVAR_TO_BOOL( vk_rtx );
	}
	ClearBits( vk_rtx->flags, FCVAR_CHANGED );

	{
		gEngine.Con_NPrintf(5, "Perf scopes:");
		for (int i = 0; i < g_aprof.num_scopes; ++i) {
			const aprof_scope_t *const scope = g_aprof.scopes + i;
			gEngine.Con_NPrintf(6 + i, "%s: c%d t%.03f(%.03f)ms s%.03f(%.03f)ms", scope->name,
				scope->frame.count,
				scope->frame.duration / 1e6,
				(scope->frame.duration / 1e6) / scope->frame.count,
				(scope->frame.duration - scope->frame.duration_children) / 1e6,
				(scope->frame.duration - scope->frame.duration_children) / 1e6 / scope->frame.count);
		}

		aprof_scope_frame();
	}

	ASSERT(!g_frame.current_framebuffer.framebuffer);

	g_frame.current_framebuffer = R_VkSwapchainAcquire( g_frame.image_available, g_frame.fence );
	vk_frame.width = g_frame.current_framebuffer.width;
	vk_frame.height = g_frame.current_framebuffer.height;

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

	VK_RenderBegin( g_frame.rtx_enabled );

	{
		VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
	}
}

void VK_RenderFrame( const struct ref_viewpass_s *rvp )
{
	VK_SceneRender( rvp );
}

void R_EndFrame( void )
{
	APROF_SCOPE_BEGIN_EARLY(end_frame);
	VkClearValue clear_value[] = {
		{.color = {{1., 0., 0., 0.}}},
		{.depthStencil = {1., 0.}} // TODO reverse-z
	};
	VkPipelineStageFlags stageflags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	if (g_frame.rtx_enabled)
		VK_RenderEndRTX( vk_core.cb, g_frame.current_framebuffer.view, g_frame.current_framebuffer.image, g_frame.current_framebuffer.width, g_frame.current_framebuffer.height );

	{
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = g_frame.rtx_enabled ? vk_frame.render_pass.after_ray_tracing : vk_frame.render_pass.raster,
			.renderArea.extent.width = g_frame.current_framebuffer.width,
			.renderArea.extent.height = g_frame.current_framebuffer.height,
			.clearValueCount = ARRAYSIZE(clear_value),
			.pClearValues = clear_value,
			.framebuffer = g_frame.current_framebuffer.framebuffer,
		};
		vkCmdBeginRenderPass(vk_core.cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	}

	{
		const VkViewport viewport[] = {
			{0.f, 0.f, (float)g_frame.current_framebuffer.width, (float)g_frame.current_framebuffer.height, 0.f, 1.f},
		};
		const VkRect2D scissor[] = {{
			{0, 0},
			{g_frame.current_framebuffer.width, g_frame.current_framebuffer.height},
		}};

		vkCmdSetViewport(vk_core.cb, 0, ARRAYSIZE(viewport), viewport);
		vkCmdSetScissor(vk_core.cb, 0, ARRAYSIZE(scissor), scissor);
	}

	if (!g_frame.rtx_enabled)
		VK_RenderEnd( vk_core.cb );

	vk2dEnd( vk_core.cb );

	vkCmdEndRenderPass(vk_core.cb);
	XVK_CHECK(vkEndCommandBuffer(vk_core.cb));

	{
		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = NULL,
			.commandBufferCount = 1,
			.pCommandBuffers = &vk_core.cb,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &g_frame.image_available,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &g_frame.done,
			.pWaitDstStageMask = &stageflags,
		};
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, g_frame.fence));
	}

	R_VkSwapchainPresent(g_frame.current_framebuffer.index, g_frame.done);
	g_frame.current_framebuffer = (r_vk_swapchain_framebuffer_t){0};

	APROF_SCOPE_BEGIN(frame_gpu_wait);
	// TODO bad sync
	XVK_CHECK(vkWaitForFences(vk_core.device, 1, &g_frame.fence, VK_TRUE, INT64_MAX));
	XVK_CHECK(vkResetFences(vk_core.device, 1, &g_frame.fence));
	APROF_SCOPE_END(frame_gpu_wait);

	if (vk_core.debug)
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));

	// TODO better sync implies multiple frames in flight, which means that we must
	// retain temporary (SingleFrame) buffer contents for longer, until all users are done.
	// (this probably means that we should really have some kind of refcount going on...)
	// For now we can just erase these buffers now because of sync with fence
	XVK_RenderBufferFrameClear();

	APROF_SCOPE_END(end_frame);
}

static void toggleRaytracing( void ) {
	ASSERT(vk_core.rtx);
	g_frame.rtx_enabled = !g_frame.rtx_enabled;
	gEngine.Cvar_Set("vk_rtx", g_frame.rtx_enabled ? "1" : "0");
	gEngine.Con_Printf(S_WARN "Switching ray tracing to %d\n", g_frame.rtx_enabled);
}

qboolean VK_FrameCtlInit( void )
{
	PROFILER_SCOPES(APROF_SCOPE_INIT);

	const VkFormat depth_format = findSupportedImageFormat(depth_formats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// FIXME move this out to renderers
	vk_frame.render_pass.raster = createRenderPass(depth_format, false);
	if (vk_core.rtx)
		vk_frame.render_pass.after_ray_tracing = createRenderPass(depth_format, true);

	if (!R_VkSwapchainInit(vk_frame.render_pass.raster, depth_format))
		return false;

	g_frame.image_available = createSemaphore();
	g_frame.done = createSemaphore();
	g_frame.fence = createFence();

	g_frame.rtx_enabled = vk_core.rtx;

	if (vk_core.rtx) {
		gEngine.Cmd_AddCommand("vk_rtx_toggle", toggleRaytracing, "Toggle between rasterization and ray tracing");
	}

	return true;
}

void VK_FrameCtlShutdown( void )
{
	destroyFence(g_frame.fence);
	destroySemaphore(g_frame.done);
	destroySemaphore(g_frame.image_available);

	R_VkSwapchainShutdown();

	vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.raster, NULL);
	if (vk_core.rtx)
		vkDestroyRenderPass(vk_core.device, vk_frame.render_pass.after_ray_tracing, NULL);
}

#if 1
static rgbdata_t *XVK_ReadPixels( void ) {
	// FIXME
	return NULL;
}
#else
#error TODO
static qboolean canBlitFromSwapchainToFormat( VkFormat dest_format ) {
	VkFormatProperties props;

	vkGetPhysicalDeviceFormatProperties(vk_core.physical_device.device, g_frame.create_info.imageFormat, &props);
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
	VkImage dest_image;
	VkImage frame_image;
	vk_devmem_t dest_devmem;
	rgbdata_t *r_shot = NULL;
	const int
		width = vk_frame.width,
		height = vk_frame.height;
	qboolean blit = canBlitFromSwapchainToFormat( dest_format );

	if (g_frame.last_frame_index < 0)
		return NULL;

	frame_image = g_frame.images[g_frame.last_frame_index];

	// Create destination image to blit/copy framebuffer pixels to
	{
		VkImageCreateInfo image_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.extent.width = width,
			.extent.height = height,
			.extent.depth = 1,
			.format = dest_format,
			.mipLevels = 1,
			.arrayLayers = 1,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		XVK_CHECK(vkCreateImage(vk_core.device, &image_create_info, NULL, &dest_image));
	}

	{
		VkMemoryRequirements memreq;
		vkGetImageMemoryRequirements(vk_core.device, dest_image, &memreq);
		dest_devmem = VK_DevMemAllocate("screenshot", memreq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 0);
		XVK_CHECK(vkBindImageMemory(vk_core.device, dest_image, dest_devmem.device_memory, dest_devmem.offset));
	}

	{
		VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb_tex, &beginfo));
	}

	{
		// Barrier 1: dest image
		VkImageMemoryBarrier image_barrier[2] = {{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = dest_image,
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
			.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT, // ?????
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

		vkCmdPipelineBarrier(vk_core.cb_tex,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
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
			.srcOffsets = {{0}, {width, height, 1}},
			.dstOffsets = {{0}, {width, height, 1}}
		};
		vkCmdBlitImage(vk_core.cb_tex,
			frame_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dest_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
	} else {
		const VkImageCopy copy = {
			.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.srcSubresource.layerCount = 1,
			.dstSubresource.layerCount = 1,
			.extent.width = width,
			.extent.height = height,
			.extent.depth = 1,
		};

		vkCmdCopyImage(vk_core.cb_tex,
			frame_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dest_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		gEngine.Con_Printf(S_WARN "Blit is not supported, screenshot will likely have mixed components; TODO: swizzle in software\n");
	}

	{
		// Barrier 1: dest image
		VkImageMemoryBarrier image_barrier[2] = {{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = dest_image,
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

		vkCmdPipelineBarrier(vk_core.cb_tex,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, NULL, 0, NULL, ARRAYSIZE(image_barrier), image_barrier);
	}

	// submit command buffer to queue
	XVK_CHECK(vkEndCommandBuffer(vk_core.cb_tex));
	{
		VkSubmitInfo subinfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
		subinfo.commandBufferCount = 1;
		subinfo.pCommandBuffers = &vk_core.cb_tex;
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
	}

	// wait for queue
	XVK_CHECK(vkQueueWaitIdle(vk_core.queue));

	// copy bytes to buffer
	{
		const VkImageSubresource subres = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		};
		VkSubresourceLayout layout;
		const char *mapped = dest_devmem.mapped;
		vkGetImageSubresourceLayout(vk_core.device, dest_image, &subres, &layout);

		mapped += layout.offset;

		{
			const int row_size = 4 * width;
			poolhandle_t r_temppool = vk_core.pool; // TODO

			r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
			r_shot->width = width;
			r_shot->height = height;
			r_shot->flags = IMAGE_HAS_COLOR;
			r_shot->type = PF_RGBA_32;
			r_shot->size = r_shot->width * r_shot->height * gEngine.Image_GetPFDesc( r_shot->type )->bpp;
			r_shot->palette = NULL;
			r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

			if (!blit) {
				if (dest_format != VK_FORMAT_R8G8B8A8_UNORM || g_frame.create_info.imageFormat != VK_FORMAT_B8G8R8A8_UNORM) {
					gEngine.Con_Printf(S_WARN "Don't have a blit function for this format pair, will save as-is w/o conversion; expect image to look wrong\n");
					blit = true;
				} else {
					char *dst = r_shot->buffer;
					for (int y = 0; y < height; ++y, mapped += layout.rowPitch) {
						const char *src = mapped;
						for (int x = 0; x < width; ++x, dst += 4, src += 4) {
							dst[0] = src[2];
							dst[1] = src[1];
							dst[2] = src[0];
							dst[3] = src[3];
						}
					}
				}
			}

			if (blit) {
				for (int y = 0; y < height; ++y, mapped += layout.rowPitch) {
					memcpy(r_shot->buffer + row_size * y, mapped, row_size);
				}
			}
		}
	}

	vkDestroyImage(vk_core.device, dest_image, NULL);
	VK_DevMemFree(&dest_devmem);

	return r_shot;
}
#endif

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
		gEngine.FS_AllowDirectPaths( true );
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
	gEngine.FS_AllowDirectPaths( false );			// always reset after store screenshot
	gEngine.FS_FreeImage( r_shot );

	gEngine.Con_Printf("Wrote screenshot %s\n", filename);
	return result;
}
