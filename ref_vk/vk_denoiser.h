#pragma once

#include "vk_core.h"

qboolean XVK_DenoiserInit( void );
void XVK_DenoiserDestroy( void );

typedef struct {
	VkCommandBuffer cmdbuf;
	uint32_t width, height;
	VkImageView view_src, view_dst;
} xvk_denoiser_args_t;

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args );
