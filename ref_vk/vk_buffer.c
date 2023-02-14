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

VkDeviceAddress R_VkBufferGetDeviceAddress(VkBuffer buffer) {
	const VkBufferDeviceAddressInfo bdai = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer};
	return vkGetBufferDeviceAddress(vk_core.device, &bdai);
}

void R_FlippingBuffer_Init(r_flipping_buffer_t *flibuf, uint32_t size) {
	aloRingInit(&flibuf->ring, size);
	R_FlippingBuffer_Clear(flibuf);
}

void R_FlippingBuffer_Clear(r_flipping_buffer_t *flibuf) {
	aloRingInit(&flibuf->ring, flibuf->ring.size);
	flibuf->frame_offsets[0] = flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}

uint32_t R_FlippingBuffer_Alloc(r_flipping_buffer_t* flibuf, uint32_t size, uint32_t align) {
	const uint32_t offset = aloRingAlloc(&flibuf->ring, size, align);
	if (offset == ALO_ALLOC_FAILED)
		return ALO_ALLOC_FAILED;

	if (flibuf->frame_offsets[1] == ALO_ALLOC_FAILED)
		flibuf->frame_offsets[1] = offset;

	return offset;
}

void R_FlippingBuffer_Flip(r_flipping_buffer_t* flibuf) {
	if (flibuf->frame_offsets[0] != ALO_ALLOC_FAILED)
		aloRingFree(&flibuf->ring, flibuf->frame_offsets[0]);

	flibuf->frame_offsets[0] = flibuf->frame_offsets[1];
	flibuf->frame_offsets[1] = ALO_ALLOC_FAILED;
}

void R_DEBuffer_Init(r_debuffer_t *debuf, uint32_t static_size, uint32_t dynamic_size) {
	R_FlippingBuffer_Init(&debuf->dynamic, dynamic_size);
	debuf->static_size = static_size;
	debuf->static_offset = 0;
}

uint32_t R_DEBuffer_Alloc(r_debuffer_t* debuf, r_lifetime_t lifetime, uint32_t size, uint32_t align) {
	switch (lifetime) {
		case LifetimeDynamic:
		{
			const uint32_t offset = R_FlippingBuffer_Alloc(&debuf->dynamic, size, align);
			if (offset == ALO_ALLOC_FAILED)
				return ALO_ALLOC_FAILED;
			return offset + debuf->static_size;
		}
		case LifetimeStatic:
		{
			const uint32_t offset = ALIGN_UP(debuf->static_offset, align);
			const uint32_t end = offset + size;
			if (end > debuf->static_size)
				return ALO_ALLOC_FAILED;

			debuf->static_offset = end;
			return offset;
		}
	}

	return ALO_ALLOC_FAILED;
}

void R_DEBuffer_Flip(r_debuffer_t* debuf) {
	R_FlippingBuffer_Flip(&debuf->dynamic);
}
