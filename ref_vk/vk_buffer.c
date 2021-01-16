#include "vk_buffer.h"

#include <memory.h>

qboolean createBuffer(vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkMemoryRequirements memreq;
	XVK_CHECK(vkCreateBuffer(vk_core.device, &bci, NULL, &buf->buffer));

	vkGetBufferMemoryRequirements(vk_core.device, buf->buffer, &memreq);
	gEngine.Con_Reportf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu\n", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	buf->device_memory = allocateDeviceMemory(memreq, flags);
	XVK_CHECK(vkBindBufferMemory(vk_core.device, buf->buffer, buf->device_memory.device_memory, buf->device_memory.offset));

	// FIXME when there are many allocation per VkDeviceMemory, fix this
	XVK_CHECK(vkMapMemory(vk_core.device, buf->device_memory.device_memory, 0, bci.size, 0, &buf->mapped));

	return true;
}

void destroyBuffer(vk_buffer_t *buf) {
	// FIXME when there are many allocation per VkDeviceMemory, fix this
	vkUnmapMemory(vk_core.device, buf->device_memory.device_memory);
	vkDestroyBuffer(vk_core.device, buf->buffer, NULL);
	freeDeviceMemory(&buf->device_memory);
	memset(buf, 0, sizeof(*buf));
}
