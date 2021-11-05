#pragma once

#include "vk_core.h"

qboolean XVK_DenoiserInit( void );
void XVK_DenoiserDestroy( void );

typedef struct {
	VkCommandBuffer cmdbuf;

	struct {
		VkImage image;
		uint32_t width, height;
	} in;

	struct {
		VkImage image;
		uint32_t width, height;
	} out;
} xvk_denoiser_args_t;

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args );
