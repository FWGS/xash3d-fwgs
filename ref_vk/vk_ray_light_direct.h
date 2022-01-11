#pragma once
#include "vk_core.h"
#include "vk_rtx.h"
#include "shaders/ray_light_direct_iface.h"

qboolean XVK_RayTraceLightDirectInit( void );
void XVK_RayTraceLightDirectDestroy( void );
void XVK_RayTraceLightDirectReloadPipeline( void );

typedef struct {
	uint32_t width, height;

	struct {
		// TODO separate desc set
		VkAccelerationStructureKHR tlas;

		// needed for alpha testing :(
		vk_buffer_region_t ubo;
		vk_buffer_region_t kusochki, indices, vertices;
		VkDescriptorImageInfo *all_textures; // [MAX_TEXTURES]

		vk_buffer_region_t lights;
		vk_buffer_region_t light_clusters;

#define X(index, name, ...) VkImageView name;
		RAY_LIGHT_DIRECT_INPUTS(X)
	} in;

	struct {
		RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X
	} out;
} xvk_ray_trace_light_direct_t;

void XVK_RayTraceLightDirect( VkCommandBuffer cmdbuf, const xvk_ray_trace_light_direct_t *args );
