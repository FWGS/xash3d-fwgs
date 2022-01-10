#pragma once
#include "vk_core.h"
#include "vk_rtx.h"
#include "shaders/ray_primary_iface.h"

qboolean XVK_RayTracePrimaryInit( void );
void XVK_RayTracePrimaryDestroy( void );
void XVK_RayTracePrimaryReloadPipeline( void );

typedef struct {
	uint32_t width, height;

	struct {
		// TODO separate desc set
		VkAccelerationStructureKHR tlas;
		vk_buffer_region_t ubo;
		vk_buffer_region_t kusochki, indices, vertices;
		VkDescriptorImageInfo *all_textures; // [MAX_TEXTURES]
	} in;

	struct {
#define X(index, name, ...) VkImageView name;
RAY_PRIMARY_OUTPUTS(X)
#undef X
	} out;
} xvk_ray_trace_primary_t;

void XVK_RayTracePrimary( VkCommandBuffer cmdbuf, const xvk_ray_trace_primary_t *args );
