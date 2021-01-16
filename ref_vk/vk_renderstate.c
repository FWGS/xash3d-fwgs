#include "vk_renderstate.h"

#include "vk_2d.h"
#include "vk_core.h"

#include "cvardef.h"
#include "const.h"
#include "ref_api.h"
#include "com_strings.h"
#include "eiface.h" // ARRAYSIZE

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;

typedef struct { uint8_t r, g, b, a; } color_rgba8_t;

typedef struct render_state_s {
	color_rgba8_t tri_color;
	qboolean fog_allowed;
	qboolean mode_2d;
	int blending_mode; // kRenderNormal, ...
} render_state_t;

static struct
{
	VkSemaphore image_available;
	VkSemaphore done;
	VkFence fence;
} grs;

render_state_t render_state = {0};

static const char *renderModeName(int mode)
{
	switch(mode)
	{
		case kRenderNormal: return "kRenderNormal";
		case kRenderTransColor: return "kRenderTransColor";
		case kRenderTransTexture: return "kRenderTransTexture";
		case kRenderGlow: return "kRenderGlow";
		case kRenderTransAlpha: return "kRenderTransAlpha";
		case kRenderTransAdd: return "kRenderTransAdd";
		default: return "INVALID";
	}
}

void GL_SetRenderMode( int renderMode )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%s(%d))\n", __FUNCTION__, renderModeName(renderMode), renderMode);

	render_state.blending_mode = renderMode;
}

void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d, %d, %d, %d)\n", __FUNCTION__, (int)r, (int)g, (int)b, (int)a);

	render_state.tri_color = (color_rgba8_t){r, g, b, a};
}

void R_AllowFog( qboolean allow )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d)\n", __FUNCTION__, allow);
	render_state.fog_allowed = allow;
}

void R_Set2DMode( qboolean enable )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d)\n", __FUNCTION__, enable);
	render_state.mode_2d = enable;
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
		.renderPass = vk_core.render_pass,
		.renderArea.extent.width = vk_core.swapchain.create_info.imageExtent.width,
		.renderArea.extent.height = vk_core.swapchain.create_info.imageExtent.height,
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
		.pWaitSemaphores = &grs.image_available,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &grs.done,
		.pWaitDstStageMask = &stageflags,
	};
	VkPresentInfoKHR presinfo = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pSwapchains = &vk_core.swapchain.swapchain,
		.pImageIndices = &swapchain_image_index,
		.swapchainCount = 1,
		.pWaitSemaphores = &grs.done,
		.waitSemaphoreCount = 1,
	};

	XVK_CHECK(vkAcquireNextImageKHR(vk_core.device, vk_core.swapchain.swapchain, UINT64_MAX, grs.image_available,
		VK_NULL_HANDLE, &swapchain_image_index));
	rpbi.framebuffer = vk_core.swapchain.framebuffers[swapchain_image_index];


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

	XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, grs.fence));
	XVK_CHECK(vkQueuePresentKHR(vk_core.queue, &presinfo));

	// TODO bad sync
	XVK_CHECK(vkWaitForFences(vk_core.device, 1, &grs.fence, VK_TRUE, INT64_MAX));
	XVK_CHECK(vkResetFences(vk_core.device, 1, &grs.fence));
}

qboolean renderstateInit( void )
{
	grs.image_available = createSemaphore();
	grs.done = createSemaphore();
	grs.fence = createFence();

	return true;
}

void renderstateDestroy( void )
{
}
