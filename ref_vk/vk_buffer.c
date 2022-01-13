#include "vk_buffer.h"

#include <memory.h>

vk_global_buffer_t g_vk_buffers = {0};

#define DEFAULT_STAGING_SIZE (16*1024*1024)

qboolean VK_BuffersInit( void ) {
	if (!VK_BufferCreate("staging", &g_vk_buffers.staging, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	return true;
}

void VK_BuffersDestroy( void ) {
	VK_BufferDestroy(&g_vk_buffers.staging);
}

qboolean VK_BufferCreate(const char *debug_name, vk_buffer_t *buf, uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags flags)
{
	VkBufferCreateInfo bci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkMemoryRequirements memreq;
	XVK_CHECK(vkCreateBuffer(vk_core.device, &bci, NULL, &buf->buffer));
	SET_DEBUG_NAME(buf->buffer, VK_OBJECT_TYPE_BUFFER, debug_name);

	vkGetBufferMemoryRequirements(vk_core.device, buf->buffer, &memreq);
	gEngine.Con_Reportf("memreq: memoryTypeBits=0x%x alignment=%zu size=%zu\n", memreq.memoryTypeBits, memreq.alignment, memreq.size);

	buf->devmem = VK_DevMemAllocate(memreq, flags, usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0);
	XVK_CHECK(vkBindBufferMemory(vk_core.device, buf->buffer, buf->devmem.device_memory, buf->devmem.offset));

	buf->mapped = buf->devmem.mapped;

	buf->size = size;

	return true;
}

void VK_BufferDestroy(vk_buffer_t *buf) {
	if (buf->buffer) {
		vkDestroyBuffer(vk_core.device, buf->buffer, NULL);
		buf->buffer = VK_NULL_HANDLE;
	}

	// FIXME when there are many allocation per VkDeviceMemory, fix this
	if (buf->devmem.device_memory) {
		VK_DevMemFree(&buf->devmem);
		buf->devmem.device_memory = VK_NULL_HANDLE;
		buf->devmem.offset = 0;
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
		available -= (offset - buf->permanent_size) - tail;
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
