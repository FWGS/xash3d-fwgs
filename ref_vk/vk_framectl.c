#include "vk_framectl.h"

#include "vk_2d.h"
#include "vk_scene.h"

#include "eiface.h"

vk_framectl_t vk_frame = {0};

static struct
{
	VkSemaphore image_available;
	VkSemaphore done;
	VkFence fence;
} g_frame;

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

static VkImage createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage) {
	VkImage image;
	VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.extent.width = width;
	ici.extent.height = height;
	ici.extent.depth = 1;
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.format = format;
	ici.tiling = tiling;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	ici.usage = usage;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	XVK_CHECK(vkCreateImage(vk_core.device, &ici, NULL, &image));

	return image;
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

static void createDepthImage(int w, int h) {
	const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
	const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkMemoryRequirements memreq;

	vk_frame.depth.image = createImage(w, h, vk_frame.depth.format, tiling, usage);

	vkGetImageMemoryRequirements(vk_core.device, vk_frame.depth.image, &memreq);
	vk_frame.depth.device_memory = allocateDeviceMemory(memreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	XVK_CHECK(vkBindImageMemory(vk_core.device, vk_frame.depth.image, vk_frame.depth.device_memory.device_memory, 0));

	{
		VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ivci.format = vk_frame.depth.format;
		ivci.image = vk_frame.depth.image;
		ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		ivci.subresourceRange.levelCount = 1;
		ivci.subresourceRange.layerCount = 1;
		XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &vk_frame.depth.image_view));
	}
}

static void destroyDepthImage( void ) {
	vkDestroyImageView(vk_core.device, vk_frame.depth.image_view, NULL);
	vkDestroyImage(vk_core.device, vk_frame.depth.image, NULL);
	freeDeviceMemory(&vk_frame.depth.device_memory);
}

static qboolean createRenderPass( void ) {
	VkAttachmentDescription attachments[] = {{
		.format = VK_FORMAT_B8G8R8A8_UNORM, //SRGB,// FIXME too early swapchain.create_info.imageFormat;
		.samples = VK_SAMPLE_COUNT_1_BIT,
		//.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	}, {
		// Depth
		.format = vk_frame.depth.format = findSupportedImageFormat(depth_formats, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
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

	XVK_CHECK(vkCreateRenderPass(vk_core.device, &rpci, NULL, &vk_frame.render_pass));

	return true;
}

static void destroySwapchain( VkSwapchainKHR swapchain )
{
	for (uint32_t i = 0; i < vk_frame.num_images; ++i)
	{
		vkDestroyImageView(vk_core.device, vk_frame.image_views[i], NULL);
		vkDestroyFramebuffer(vk_core.device, vk_frame.framebuffers[i], NULL);
	}

	vkDestroySwapchainKHR(vk_core.device, swapchain, NULL);

	destroyDepthImage();
}

static qboolean createSwapchain( void )
{
	VkSwapchainCreateInfoKHR *create_info = &vk_frame.create_info;
	const uint32_t prev_num_images = vk_frame.num_images;

	XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_frame.surface_caps));

	create_info->sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info->pNext = NULL;
	create_info->surface = vk_core.surface.surface;
	create_info->imageFormat = VK_FORMAT_B8G8R8A8_UNORM;//SRGB; // TODO get from surface_formats
	create_info->imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR; // TODO get from surface_formats
	create_info->imageExtent.width = vk_frame.surface_caps.currentExtent.width;
	create_info->imageExtent.height = vk_frame.surface_caps.currentExtent.height;
	create_info->imageArrayLayers = 1;
	create_info->imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create_info->imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info->preTransform = vk_frame.surface_caps.currentTransform;
	create_info->compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info->presentMode = VK_PRESENT_MODE_FIFO_KHR; // TODO caps, MAILBOX is better
	//create_info->presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; // TODO caps, MAILBOX is better
	create_info->clipped = VK_TRUE;
	create_info->oldSwapchain = vk_frame.swapchain;

	create_info->minImageCount = vk_frame.surface_caps.minImageCount + 3;
	if (vk_frame.surface_caps.maxImageCount && create_info->minImageCount > vk_frame.surface_caps.maxImageCount)
		create_info->minImageCount = vk_frame.surface_caps.maxImageCount;

	XVK_CHECK(vkCreateSwapchainKHR(vk_core.device, create_info, NULL, &vk_frame.swapchain));
	if (create_info->oldSwapchain)
	{
		destroySwapchain( create_info->oldSwapchain );
	}

	createDepthImage(vk_frame.surface_caps.currentExtent.width, vk_frame.surface_caps.currentExtent.height);

	vk_frame.num_images = 0;
	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, vk_frame.swapchain, &vk_frame.num_images, NULL));
	if (prev_num_images != vk_frame.num_images)
	{
		if (vk_frame.images)
		{
			Mem_Free(vk_frame.images);
			Mem_Free(vk_frame.image_views);
			Mem_Free(vk_frame.framebuffers);
		}

		vk_frame.images = Mem_Malloc(vk_core.pool, sizeof(*vk_frame.images) * vk_frame.num_images);
		vk_frame.image_views = Mem_Malloc(vk_core.pool, sizeof(*vk_frame.image_views) * vk_frame.num_images);
		vk_frame.framebuffers = Mem_Malloc(vk_core.pool, sizeof(*vk_frame.framebuffers) * vk_frame.num_images);
	}

	XVK_CHECK(vkGetSwapchainImagesKHR(vk_core.device, vk_frame.swapchain, &vk_frame.num_images, vk_frame.images));

	for (uint32_t i = 0; i < vk_frame.num_images; ++i) {
		VkImageViewCreateInfo ivci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = vk_frame.create_info.imageFormat,
			.image = vk_frame.images[i],
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.levelCount = 1,
			.subresourceRange.layerCount = 1,
		};

		XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, vk_frame.image_views + i));

		{
			const VkImageView attachments[] = {
				vk_frame.image_views[i],
				vk_frame.depth.image_view
			};
			VkFramebufferCreateInfo fbci = {
				.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
				.renderPass = vk_frame.render_pass,
				.attachmentCount = ARRAYSIZE(attachments),
				.pAttachments = attachments,
				.width = vk_frame.create_info.imageExtent.width,
				.height = vk_frame.create_info.imageExtent.height,
				.layers = 1,
			};
			XVK_CHECK(vkCreateFramebuffer(vk_core.device, &fbci, NULL, vk_frame.framebuffers + i));
		}
	}

	return true;
}

void R_BeginFrame( qboolean clearScene )
{
	// gEngine.Con_Reportf("%s(clearScene=%d)\n", __FUNCTION__, clearScene);

	// Check that swapchain has the same size
	{
		VkSurfaceCapabilitiesKHR surface_caps;
		XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface.surface, &surface_caps));

		if (surface_caps.currentExtent.width != vk_frame.surface_caps.currentExtent.width
			|| surface_caps.currentExtent.height != vk_frame.surface_caps.currentExtent.height)
		{
			createSwapchain();
		}
	}

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

	vk2dBegin();
}

void GL_RenderFrame( const struct ref_viewpass_s *rvp )
{
	// gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
	FIXME_VK_SceneSetViewPass(rvp);
}

void R_EndFrame( void )
{
	uint32_t swapchain_image_index;
	VkClearValue clear_value[] = {
		{.color = {{1., 0., 0., 0.}}},
		{.depthStencil = {1., 0.}} // TODO reverse-z
	};
	VkPipelineStageFlags stageflags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo subinfo = {
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
	VkPresentInfoKHR presinfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pSwapchains = &vk_frame.swapchain,
		.pImageIndices = &swapchain_image_index,
		.swapchainCount = 1,
		.pWaitSemaphores = &g_frame.done,
		.waitSemaphoreCount = 1,
	};

	// gEngine.Con_Reportf("%s\n", __FUNCTION__);

	for (int i = 0;; ++i)
	{
		const VkResult acquire_result = vkAcquireNextImageKHR(vk_core.device, vk_frame.swapchain, UINT64_MAX, g_frame.image_available,
			VK_NULL_HANDLE, &swapchain_image_index);
		switch (acquire_result)
		{
			case VK_ERROR_OUT_OF_DATE_KHR:
			case VK_ERROR_SURFACE_LOST_KHR:
				if (i == 0) {
					createSwapchain();
					continue;
				}
				gEngine.Con_Printf(S_WARN "vkAcquireNextImageKHR returned %s, frame will be lost\n", resultName(acquire_result));
				return;

			default:
				XVK_CHECK(acquire_result);
		}

		break;
	}

	{
		VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
	}

	{
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = vk_frame.render_pass,
			.renderArea.extent.width = vk_frame.create_info.imageExtent.width,
			.renderArea.extent.height = vk_frame.create_info.imageExtent.height,
			.clearValueCount = ARRAYSIZE(clear_value),
			.pClearValues = clear_value,
			.framebuffer = vk_frame.framebuffers[swapchain_image_index],
		};
		vkCmdBeginRenderPass(vk_core.cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
	}

	{
		const VkViewport viewport[] = {
			{0.f, 0.f, (float)vk_frame.surface_caps.currentExtent.width, (float)vk_frame.surface_caps.currentExtent.height, 0.f, 1.f},
		};
		const VkRect2D scissor[] = {{
			{0, 0},
			vk_frame.surface_caps.currentExtent,
		}};

		vkCmdSetViewport(vk_core.cb, 0, ARRAYSIZE(viewport), viewport);
		vkCmdSetScissor(vk_core.cb, 0, ARRAYSIZE(scissor), scissor);
	}

	VK_SceneRender();

	vk2dEnd();

	vkCmdEndRenderPass(vk_core.cb);
	XVK_CHECK(vkEndCommandBuffer(vk_core.cb));

	XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, g_frame.fence));

	{
		const VkResult present_result = vkQueuePresentKHR(vk_core.queue, &presinfo);
		switch (present_result)
		{
			case VK_ERROR_OUT_OF_DATE_KHR:
			case VK_ERROR_SURFACE_LOST_KHR:
				gEngine.Con_Printf(S_WARN "vkQueuePresentKHR returned %s, frame will be lost\n", resultName(present_result));
				break;
			default:
				XVK_CHECK(present_result);
		}
	}

	// TODO bad sync
	XVK_CHECK(vkWaitForFences(vk_core.device, 1, &g_frame.fence, VK_TRUE, INT64_MAX));
	XVK_CHECK(vkResetFences(vk_core.device, 1, &g_frame.fence));
}

qboolean VK_FrameCtlInit( void )
{
	if (!createRenderPass())
		return false;

	if (!createSwapchain())
		return false;

	g_frame.image_available = createSemaphore();
	g_frame.done = createSemaphore();
	g_frame.fence = createFence();

	return true;
}

void VK_FrameCtlShutdown( void )
{
	destroyFence(g_frame.fence);
	destroySemaphore(g_frame.done);
	destroySemaphore(g_frame.image_available);

	destroySwapchain( vk_frame.swapchain );

	vkDestroyRenderPass(vk_core.device, vk_frame.render_pass, NULL);
}
