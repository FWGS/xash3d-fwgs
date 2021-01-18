#include "vk_framectl.h"

#include "vk_2d.h"

#include "eiface.h"

vk_framectl_t vk_frame = {0};

static struct
{
	VkSemaphore image_available;
	VkSemaphore done;
	VkFence fence;
} g_frame;

static qboolean createRenderPass( void ) {
	VkAttachmentDescription attachments[] = {{
		.format = VK_FORMAT_B8G8R8A8_SRGB,// FIXME too early swapchain.create_info.imageFormat;
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		//.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		//attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	/*}, {
		// Depth
		.format = g.depth_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		//attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		//attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		*/
	}};

	VkAttachmentReference color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	/*
	VkAttachmentReference depth_attachment = {0};
	depth_attachment.attachment = 1;
	depth_attachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	*/

	VkSubpassDescription subdesc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		//.pDepthStencilAttachment = &depth_attachment,
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

static qboolean createSwapchain( void )
{
	VkSwapchainCreateInfoKHR *create_info = &vk_frame.create_info;
	const uint32_t prev_num_images = vk_frame.num_images;

	XVK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_core.physical_device.device, vk_core.surface.surface, &vk_frame.surface_caps));

	create_info->sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info->pNext = NULL;
	create_info->surface = vk_core.surface.surface;
	create_info->imageFormat = VK_FORMAT_B8G8R8A8_SRGB; // TODO get from surface_formats
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
				//g.depth_image_view
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
	gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d)\n", __FUNCTION__, clearScene);
	vk2dBegin();
}

void R_RenderScene( void )
{
	gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);
}

void R_EndFrame( void )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s\n", __FUNCTION__);

	uint32_t swapchain_image_index;
	VkClearValue clear_value[] = {
		{.color = {{1., 0., 0., 0.}}},
		//{.depthStencil = {1., 0.}}
	};
	VkRenderPassBeginInfo rpbi = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = vk_frame.render_pass,
		.renderArea.extent.width = vk_frame.create_info.imageExtent.width,
		.renderArea.extent.height = vk_frame.create_info.imageExtent.height,
		.clearValueCount = ARRAYSIZE(clear_value),
		.pClearValues = clear_value,
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

	XVK_CHECK(vkAcquireNextImageKHR(vk_core.device, vk_frame.swapchain, UINT64_MAX, g_frame.image_available,
		VK_NULL_HANDLE, &swapchain_image_index));
	rpbi.framebuffer = vk_frame.framebuffers[swapchain_image_index];


	{
		VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		XVK_CHECK(vkBeginCommandBuffer(vk_core.cb, &beginfo));
	}

	vkCmdBeginRenderPass(vk_core.cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

	vk2dEnd();

	vkCmdEndRenderPass(vk_core.cb);
	XVK_CHECK(vkEndCommandBuffer(vk_core.cb));

	XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, g_frame.fence));
	XVK_CHECK(vkQueuePresentKHR(vk_core.queue, &presinfo));

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

	for (uint32_t i = 0; i < vk_frame.num_images; ++i)
	{
		vkDestroyImageView(vk_core.device, vk_frame.image_views[i], NULL);
		vkDestroyFramebuffer(vk_core.device, vk_frame.framebuffers[i], NULL);
	}

	vkDestroySwapchainKHR(vk_core.device, vk_frame.swapchain, NULL);
	vkDestroyRenderPass(vk_core.device, vk_frame.render_pass, NULL);
}
