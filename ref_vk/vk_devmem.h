#pragma once
#include "vk_core.h"

// FIXME arena allocation, ...
typedef struct vk_devmem_s {
	VkDeviceMemory device_memory;
	uint32_t offset;
} vk_devmem_t;

vk_devmem_t VK_DevMemAllocate(VkMemoryRequirements req, VkMemoryPropertyFlags props, VkMemoryAllocateFlags flags);
void VK_DevMemFree(vk_devmem_t *mem);
