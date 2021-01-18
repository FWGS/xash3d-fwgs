#pragma once
#include "vk_core.h"

#include "xash3d_types.h"

typedef struct vk_framectl_s {
	VkRenderPass render_pass;

	VkSurfaceCapabilitiesKHR surface_caps;
	VkSwapchainCreateInfoKHR create_info;
	VkSwapchainKHR swapchain;
	uint32_t num_images;
	VkImage *images;
	VkImageView *image_views;
	VkFramebuffer *framebuffers;
} vk_framectl_t;

extern vk_framectl_t vk_frame;

qboolean VK_FrameCtlInit( void );
void VK_FrameCtlShutdown( void );

void R_BeginFrame( qboolean clearScene );
void R_RenderScene( void );
void R_EndFrame( void );
