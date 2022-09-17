#include "vk_commandpool.h"

vk_command_pool_t R_VkCommandPoolCreate( int count ) {
	vk_command_pool_t ret = {0};

	const VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};

	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandBufferCount = count,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	};

	XVK_CHECK(vkCreateCommandPool(vk_core.device, &cpci, NULL, &ret.pool));

	cbai.commandPool = ret.pool;
	ret.buffers = Mem_Malloc(vk_core.pool, sizeof(VkCommandBuffer) * count);
	ret.buffers_count = count;
	XVK_CHECK(vkAllocateCommandBuffers(vk_core.device, &cbai, ret.buffers));

	return ret;
}

void R_VkCommandPoolDestroy( vk_command_pool_t *pool ) {
	ASSERT(pool->buffers);
	vkDestroyCommandPool(vk_core.device, pool->pool, NULL);
	Mem_Free(pool->buffers);
}
