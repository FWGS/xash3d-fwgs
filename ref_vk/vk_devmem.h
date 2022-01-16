#pragma once
#include "vk_core.h"

qboolean VK_DevMemInit( void );
void VK_DevMemDestroy( void );

typedef struct vk_devmem_s {
	VkDeviceMemory device_memory;
	uint32_t offset;
	void *mapped;

	struct { int devmem, block; } priv_;
} vk_devmem_t;

vk_devmem_t VK_DevMemAllocate(VkMemoryRequirements req, VkMemoryPropertyFlags props, VkMemoryAllocateFlags flags);
void VK_DevMemFree(const vk_devmem_t *mem);
