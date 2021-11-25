#pragma once
#include "vk_core.h"

typedef struct {
	// FIXME better memory allocation
	// OCHEN PLOHO
	device_memory_t devmem;
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
} xvk_image_create_t;

xvk_image_t XVK_ImageCreate(const xvk_image_create_t *create);
void XVK_ImageDestroy(xvk_image_t *img);
