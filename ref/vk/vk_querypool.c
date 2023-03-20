#include "vk_querypool.h"
#include "profiler.h" // for aprof_time_now_ns()

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
	pool->end_timestamp_ns = aprof_time_now_ns();
}

static uint64_t getGpuTimestampOffsetNs( const vk_query_pool_t *pool ) {
	// FIXME this is an incorrect check, we need to carry per-device extensions availability somehow. vk_core-vs-device refactoring pending
	if (!vkGetCalibratedTimestampsEXT) {
		// Estimate based on supposed submission time, assuming that we submit, and it starts computing right after cmdbuffer closure
		// which may not be true. But it's all we got
		// TODO alternative approach: estimate based on end timestamp
		const uint64_t gpu_begin_ns = (double)pool->results[0] * vk_core.physical_device.properties.limits.timestampPeriod;
		return pool->end_timestamp_ns - gpu_begin_ns;
	}

	const VkCalibratedTimestampInfoEXT cti[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
			.pNext = NULL,
			.timeDomain = VK_TIME_DOMAIN_DEVICE_EXT,
		},
		{
			.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
			.pNext = NULL,
#if defined(_WIN32)
			.timeDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT,
#else
			.timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
#endif
		},
	};

	uint64_t timestamps[2] = {0};
	uint64_t max_deviation[2] = {0};
	vkGetCalibratedTimestampsEXT(vk_core.device, 2, cti, timestamps, max_deviation);

	const uint64_t cpu = aprof_time_platform_to_ns(timestamps[1]);
	const uint64_t gpu = (double)timestamps[0] * vk_core.physical_device.properties.limits.timestampPeriod;
	return cpu - gpu;
}

void R_VkQueryPoolGetFrameResults( vk_query_pool_t *pool ) {
	if (!pool->used)
		return;

	vkGetQueryPoolResults(vk_core.device, pool->pool, 0, pool->used, pool->used * sizeof(uint64_t), pool->results, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

	const uint64_t timestamp_offset_ns = getGpuTimestampOffsetNs( pool );

	for (int i = 0; i < pool->used; ++i) {
		const uint64_t gpu_ns = pool->results[i] * (double)vk_core.physical_device.properties.limits.timestampPeriod;
		pool->results[i] = timestamp_offset_ns + gpu_ns;
	}
}
