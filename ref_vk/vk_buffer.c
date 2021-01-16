#include "vk_buffer.h"

#include <memory.h>

uint32_t findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	for (uint32_t i = 0; i < vk_core.physical_device.memory_properties.memoryTypeCount; ++i) {
		if (!(type_index_bits & (1 << i)))
			continue;

		if ((vk_core.physical_device.memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}

	return UINT32_MAX;
}

qboolean createBuffer(vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkMemoryAllocateInfo mai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
	};
	VkMemoryRequirements memreq;
	XVK_CHECK(vkCreateBuffer(vk_core.device, &bci, NULL, &buf->buffer));

	vkGetBufferMemoryRequirements(vk_core.device, buf->buffer, &memreq);
	gEngine.Con_Reportf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	mai.allocationSize = memreq.size;
	mai.memoryTypeIndex = findMemoryWithType(memreq.memoryTypeBits, flags);
	XVK_CHECK(vkAllocateMemory(vk_core.device, &mai, NULL, &buf->device_memory));
	XVK_CHECK(vkBindBufferMemory(vk_core.device, buf->buffer, buf->device_memory, 0));

	XVK_CHECK(vkMapMemory(vk_core.device, buf->device_memory, 0, bci.size, 0, &buf->mapped));

	return true;
}

void destroyBuffer(vk_buffer_t *buf) {
	vkUnmapMemory(vk_core.device, buf->device_memory);
	vkDestroyBuffer(vk_core.device, buf->buffer, NULL);
	vkFreeMemory(vk_core.device, buf->device_memory, NULL);
	memset(buf, 0, sizeof(*buf));
}
