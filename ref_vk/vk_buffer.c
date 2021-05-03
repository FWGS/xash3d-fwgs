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

	buf->device_memory = allocateDeviceMemory(memreq, flags, usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0);
	XVK_CHECK(vkBindBufferMemory(vk_core.device, buf->buffer, buf->device_memory.device_memory, buf->device_memory.offset));

	// FIXME when there are many allocation per VkDeviceMemory, fix this
	if (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT & flags)
		XVK_CHECK(vkMapMemory(vk_core.device, buf->device_memory.device_memory, 0, bci.size, 0, &buf->mapped));

	buf->size = size;

	return true;
}

void destroyBuffer(vk_buffer_t *buf) {
	// FIXME when there are many allocation per VkDeviceMemory, fix this
	if (buf->buffer)
	{
		vkDestroyBuffer(vk_core.device, buf->buffer, NULL);
		buf->buffer = VK_NULL_HANDLE;
	}

	if (buf->device_memory.device_memory)
	{
		if (buf->mapped)
			vkUnmapMemory(vk_core.device, buf->device_memory.device_memory);

		freeDeviceMemory(&buf->device_memory);
		buf->device_memory.device_memory = VK_NULL_HANDLE;
		buf->device_memory.offset = 0;
		buf->mapped = 0;
		buf->size = 0;
	}
}

void VK_RingBuffer_Clear(vk_ring_buffer_t* buf) {
	buf->offset_free = 0;
	buf->permanent_size = 0;
	buf->free = buf->size;
}

//     <                 v->
// |MAP|.........|FRAME|...|
//               ^      XXXXX

uint32_t VK_RingBuffer_Alloc(vk_ring_buffer_t* buf, uint32_t size, uint32_t align) {
	uint32_t offset = ALIGN_UP(buf->offset_free, align);
	const uint32_t align_diff = offset - buf->offset_free;
	uint32_t available = buf->free - align_diff;
	const uint32_t tail = buf->size - offset;

	if (available < size)
		return AllocFailed;

	if (size > tail) {
		offset = ALIGN_UP(buf->permanent_size, align);
		const uint32_t align_diff = offset - buf->permanent_size;
		available -= align_diff - tail;
	}

	if (available < size)
		return AllocFailed;

	buf->offset_free = offset + size;
	buf->free = available - size;

	return offset;
}

void VK_RingBuffer_Fix(vk_ring_buffer_t* buf) {
	ASSERT(buf->permanent_size == 0);
	buf->permanent_size = buf->offset_free;
}

void VK_RingBuffer_ClearFrame(vk_ring_buffer_t* buf) {
	buf->offset_free = buf->permanent_size;
	buf->free = buf->size - buf->permanent_size;
}
