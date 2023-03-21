#pragma once
#include "vk_core.h"

#include "xash3d_types.h"

#define MAX_CONCURRENT_FRAMES 2

typedef struct vk_framectl_s {
	// TODO only used from 2d, remove
	uint32_t width, height;

	// FIXME
	VkCommandBuffer cmdbuf;

	// TODO move these into renderer and 2d
	struct {
		// Used when the entire rendering is traditional triangle rasterization
		// Discards and clears color buffer
		VkRenderPass raster;

		// Used for 2D overlay rendering after ray tracing pass
		// Preserves color buffer contents
		VkRenderPass after_ray_tracing;
	} render_pass;

	qboolean rtx_enabled;
} vk_framectl_t;

extern vk_framectl_t vk_frame;

qboolean VK_FrameCtlInit( void );
void VK_FrameCtlShutdown( void );

void R_BeginFrame( qboolean clearScene );
void VK_RenderFrame( const struct ref_viewpass_s *rvp );
void R_EndFrame( void );

qboolean VID_ScreenShot( const char *filename, int shot_type );
