#include "vk_devmem.h"

static uint32_t findMemoryWithType(uint32_t type_index_bits, VkMemoryPropertyFlags flags) {
	for (uint32_t i = 0; i < vk_core.physical_device.memory_properties2.memoryProperties.memoryTypeCount; ++i) {
		if (!(type_index_bits & (1 << i)))
			continue;

		if ((vk_core.physical_device.memory_properties2.memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
			return i;
	}

	return UINT32_MAX;
}

vk_devmem_t VK_DevMemAllocate(VkMemoryRequirements req, VkMemoryPropertyFlags props, VkMemoryAllocateFlags flags) {
	// TODO coalesce allocations, ...
	vk_devmem_t ret = {0};

	const VkMemoryAllocateFlagsInfo mafi = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = flags,
	};

	const VkMemoryAllocateInfo mai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = flags ? &mafi : NULL,
		.allocationSize = req.size,
		.memoryTypeIndex = findMemoryWithType(req.memoryTypeBits, props),
	};

	gEngine.Con_Reportf("allocateDeviceMemory size=%zu memoryTypeBits=0x%x memoryProperties=%c%c%c%c%c flags=0x%x => typeIndex=%d\n", req.size, req.memoryTypeBits,
		props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ? 'D' : '.',
		props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? 'V' : '.',
		props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? 'C' : '.',
		props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ? '$' : '.',
		props & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? 'L' : '.',
		flags,
		mai.memoryTypeIndex);

	ASSERT(mai.memoryTypeIndex != UINT32_MAX);
	XVK_CHECK(vkAllocateMemory(vk_core.device, &mai, NULL, &ret.device_memory));
	return ret;
}

void VK_DevMemFree(vk_devmem_t *mem) {
	vkFreeMemory(vk_core.device, mem->device_memory, NULL);
	mem->device_memory = VK_NULL_HANDLE;
}
