#pragma once

#include "vk_core.h"

qboolean XVK_DenoiserInit( void );
void XVK_DenoiserDestroy( void );

void XVK_DenoiserReloadPipeline( void );

typedef struct {
	VkCommandBuffer cmdbuf;
	uint32_t width, height;

	struct {
		VkImageView base_color_view;
		VkImageView diffuse_gi_view;
	} src;

	VkImageView dst_view;
} xvk_denoiser_args_t;

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args );
