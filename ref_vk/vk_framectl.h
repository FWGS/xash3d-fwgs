#pragma once
#include "vk_core.h"

#include "xash3d_types.h"

typedef struct vk_framectl_s {
	struct {
		VkFormat format;
		device_memory_t device_memory;
		VkImage image;
		VkImageView image_view;
	} depth;

	struct {
		// Used when the entire rendering is traditional triangle rasterization
		// Discards and clears color buffer
		VkRenderPass raster;

		// Used for 2D overlay rendering after ray tracing pass
		// Preserves color buffer contents
		VkRenderPass after_ray_tracing;
	} render_pass;

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
void VK_RenderFrame( const struct ref_viewpass_s *rvp );
void R_EndFrame( void );
