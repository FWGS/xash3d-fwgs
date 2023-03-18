#include "vk_querypool.h"

qboolean R_VkQueryPoolInit( vk_query_pool_t* pool ) {
	const VkQueryPoolCreateInfo qpci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.pNext = NULL,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = MAX_QUERY_COUNT,
		.flags = 0,
	};

	XVK_CHECK(vkCreateQueryPool(vk_core.device, &qpci, NULL, &pool->pool));
	return true;
}

void R_VkQueryPoolDestroy( vk_query_pool_t *pool ) {
	vkDestroyQueryPool(vk_core.device, pool->pool, NULL);
}

int R_VkQueryPoolTimestamp( vk_query_pool_t *pool, VkCommandBuffer cmdbuf, VkPipelineStageFlagBits stage) {
	if (pool->used >= MAX_QUERY_COUNT)
		return -1;

	vkCmdWriteTimestamp(cmdbuf, stage, pool->pool, pool->used);
	return pool->used++;
}

void R_VkQueryPoolBegin( vk_query_pool_t *pool, VkCommandBuffer cmdbuf ) {
	pool->used = 0;
	vkCmdResetQueryPool(cmdbuf, pool->pool, 0, MAX_QUERY_COUNT);
	R_VkQueryPoolTimestamp(pool, cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
}

void R_VkQueryPoolEnd( vk_query_pool_t *pool, VkCommandBuffer cmdbuf ) {
	R_VkQueryPoolTimestamp(pool, cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void R_VkQueryPoolGetFrameResults( vk_query_pool_t *pool ) {
	if (!pool->used)
		return;

	vkGetQueryPoolResults(vk_core.device, pool->pool, 0, pool->used, pool->used * sizeof(uint64_t), pool->results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
}
