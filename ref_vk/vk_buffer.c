#include "vk_buffer.h"

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

	if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) {
		memreq.alignment = ALIGN_UP(memreq.alignment, vk_core.physical_device.properties_ray_tracing_pipeline.shaderGroupBaseAlignment);
	}
	buf->devmem = VK_DevMemAllocate(debug_name, memreq, flags, usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0);
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

VkDeviceAddress XVK_BufferGetDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(vk_core.device, &bdai);
}

void R_DEBuffer_Init(r_debuffer_t *debuf, uint32_t static_size, uint32_t dynamic_size) {
	aloRingInit(&debuf->static_ring, static_size);
	aloRingInit(&debuf->dynamic_ring, dynamic_size);
	debuf->static_size = static_size;
	debuf->frame_dynamic_offset[0] = debuf->frame_dynamic_offset[1] = ALO_ALLOC_FAILED;
}

uint32_t R_DEBuffer_Alloc(r_debuffer_t* debuf, r_lifetime_t lifetime, uint32_t size, uint32_t align) {
	alo_ring_t * const ring = (lifetime == LifetimeStatic) ? &debuf->static_ring : &debuf->dynamic_ring;
	const uint32_t alloc_offset = aloRingAlloc(ring, size, align);
	const uint32_t offset = alloc_offset + ((lifetime == LifetimeDynamic) ? debuf->static_size : 0);

	if (alloc_offset == ALO_ALLOC_FAILED) {
		gEngine.Con_Printf(S_ERROR "Cannot allocate %d %s bytes\n",
			size,
			lifetime == LifetimeDynamic ? "dynamic" : "static");
		return ALO_ALLOC_FAILED;
	}

	// Store first dynamic allocation this frame
	if (lifetime == LifetimeDynamic && debuf->frame_dynamic_offset[1] == ALO_ALLOC_FAILED) {
		debuf->frame_dynamic_offset[1] = alloc_offset;
	}

	return offset;
}

void R_DEBuffer_Flip(r_debuffer_t* debuf) {
	if (debuf->frame_dynamic_offset[0] != ALO_ALLOC_FAILED)
		aloRingFree(&debuf->dynamic_ring, debuf->frame_dynamic_offset[0]);

	debuf->frame_dynamic_offset[0] = debuf->frame_dynamic_offset[1];
	debuf->frame_dynamic_offset[1] = ALO_ALLOC_FAILED;
}

