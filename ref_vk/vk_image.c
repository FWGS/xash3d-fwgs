#include "vk_image.h"

xvk_image_t XVK_ImageCreate(const xvk_image_create_t *create) {
	const qboolean is_depth = !!(create->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
	xvk_image_t image;
	VkMemoryRequirements memreq;
	VkImageViewCreateInfo ivci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent.width = create->width,
		.extent.height = create->height,
		.extent.depth = 1,
		.mipLevels = create->mips,
		.arrayLayers = create->layers,
		.format = create->format,
		.tiling = create->tiling,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.usage = create->usage,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.flags = create->is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
	};

	XVK_CHECK(vkCreateImage(vk_core.device, &ici, NULL, &image.image));

	if (create->debug_name)
		SET_DEBUG_NAME(image.image, VK_OBJECT_TYPE_IMAGE, create->debug_name);

	vkGetImageMemoryRequirements(vk_core.device, image.image, &memreq);
	image.devmem = VK_DevMemAllocate(create->debug_name, memreq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	XVK_CHECK(vkBindImageMemory(vk_core.device, image.image, image.devmem.device_memory, image.devmem.offset));

	ivci.viewType = create->is_cubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
	ivci.format = ici.format;
	ivci.image = image.image;
	ivci.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	ivci.subresourceRange.baseMipLevel = 0;
	ivci.subresourceRange.levelCount = ici.mipLevels;
	ivci.subresourceRange.baseArrayLayer = 0;
	ivci.subresourceRange.layerCount = ici.arrayLayers;
	ivci.components = (VkComponentMapping){0, 0, 0, (is_depth || create->has_alpha) ? 0 : VK_COMPONENT_SWIZZLE_ONE};
	XVK_CHECK(vkCreateImageView(vk_core.device, &ivci, NULL, &image.view));

	if (create->debug_name)
		SET_DEBUG_NAME(image.view, VK_OBJECT_TYPE_IMAGE_VIEW, create->debug_name);

	image.width = create->width;
	image.height = create->height;
	image.mips = create->mips;

	return image;
}

void XVK_ImageDestroy(xvk_image_t *img) {
	vkDestroyImageView(vk_core.device, img->view, NULL);
	vkDestroyImage(vk_core.device, img->image, NULL);
	VK_DevMemFree(&img->devmem);
	*img = (xvk_image_t){0};
}
