#pragma once
#include "vk_core.h"
#include "vk_devmem.h"

typedef struct xvk_image_s {
	vk_devmem_t devmem;
	VkImage image;
	VkImageView view;

	uint32_t width, height;
	int mips;
} xvk_image_t;

typedef struct {
	const char *debug_name;
	uint32_t width, height;
	int mips, layers;
	VkFormat format;
	VkImageTiling tiling;
	VkImageUsageFlags usage;
	qboolean has_alpha;
	qboolean is_cubemap;
	VkMemoryPropertyFlags memory_props;
} xvk_image_create_t;

xvk_image_t XVK_ImageCreate(const xvk_image_create_t *create);
void XVK_ImageDestroy(xvk_image_t *img);

void R_VkImageClear(VkCommandBuffer cmdbuf, VkImage image);

typedef struct {
	VkPipelineStageFlags in_stage;
	struct {
		VkImage image;
		int width, height;
		VkImageLayout oldLayout;
		VkAccessFlags srcAccessMask;
	} src, dst;
} r_vkimage_blit_args;

void R_VkImageBlit( VkCommandBuffer cmdbuf, const r_vkimage_blit_args *blit_args );

