#pragma once
#include "vk_core.h"

typedef struct vk_buffer_s
{
	// TODO coalesce allocations
	VkDeviceMemory device_memory;
	VkBuffer buffer;

	void *mapped;
	uint32_t size;
} vk_buffer_t;

qboolean createBuffer(vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags);
void destroyBuffer(vk_buffer_t *buf);


