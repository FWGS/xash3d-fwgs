#pragma once

#include "vk_core.h"

#include "vk_rtx.h"

qboolean XVK_RayTracePrimaryInit( void );
void XVK_RayTracePrimaryDestroy( void );

typedef struct {
	uint32_t width, height;

	struct {
		VkAccelerationStructureKHR tlas;
		vk_buffer_region_t ubo;
	} in;

	struct {
		VkImageView base_color_r;
		//VkImageView normals;
	} out;
} xvk_ray_trace_primary_t;

void XVK_RayTracePrimary( VkCommandBuffer cmdbuf, const xvk_ray_trace_primary_t *args );
