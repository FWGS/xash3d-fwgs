#include "vk_combuf.h"
#include "vk_commandpool.h"

#include "profiler.h"

#define MAX_COMMANDBUFFERS 4
#define MAX_QUERY_COUNT 128
#define MAX_SCOPES 64

#define BEGIN_INDEX_TAG 0x10000000

typedef struct {
	vk_combuf_t public;
	int used;
	struct {
		int timestamps_offset;
		int scopes[MAX_SCOPES];
		int scopes_count;
	} profiler;
} vk_combuf_impl_t;

static struct {
	vk_command_pool_t pool;

	vk_combuf_impl_t combufs[MAX_COMMANDBUFFERS];
	struct {
		VkQueryPool pool;
		uint64_t values[MAX_QUERY_COUNT * MAX_COMMANDBUFFERS];
	} timestamp;

	vk_combuf_scope_t scopes[MAX_SCOPES];
	int scopes_count;

	int entire_combuf_scope_id;
} g_combuf;

qboolean R_VkCombuf_Init( void ) {
	g_combuf.pool = R_VkCommandPoolCreate(MAX_COMMANDBUFFERS);
	if (!g_combuf.pool.pool)
		return false;

	const VkQueryPoolCreateInfo qpci = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.pNext = NULL,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = COUNTOF(g_combuf.timestamp.values),
		.flags = 0,
	};

	XVK_CHECK(vkCreateQueryPool(vk_core.device, &qpci, NULL, &g_combuf.timestamp.pool));

	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		vk_combuf_impl_t *const cb = g_combuf.combufs + i;

		cb->public.cmdbuf = g_combuf.pool.buffers[i];
		SET_DEBUG_NAMEF(cb->public.cmdbuf, VK_OBJECT_TYPE_COMMAND_BUFFER, "cmdbuf[%d]", i);

		cb->profiler.timestamps_offset = i * MAX_QUERY_COUNT;
	}

	g_combuf.entire_combuf_scope_id = R_VkGpuScope_Register("GPU");

	return true;
}

void R_VkCombuf_Destroy( void ) {
	vkDestroyQueryPool(vk_core.device, g_combuf.timestamp.pool, NULL);
	R_VkCommandPoolDestroy(&g_combuf.pool);

	for (int i = 0; i < g_combuf.scopes_count; ++i) {
		Mem_Free((char*)g_combuf.scopes[i].name);
	}
}

vk_combuf_t* R_VkCombufOpen( void ) {
	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		vk_combuf_impl_t *const cb = g_combuf.combufs + i;
		if (!cb->used) {
			cb->used = 1;
			return &cb->public;
		}
	}

	return NULL;
}

void R_VkCombufClose( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;
	cb->used = 0;

	// TODO synchronize?
	// For now, external synchronization expected
}

void R_VkCombufBegin( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	cb->profiler.scopes_count = 0;

	const VkCommandBufferBeginInfo beginfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	XVK_CHECK(vkBeginCommandBuffer(cb->public.cmdbuf, &beginfo));

	vkCmdResetQueryPool(cb->public.cmdbuf, g_combuf.timestamp.pool, cb->profiler.timestamps_offset, MAX_QUERY_COUNT);
	R_VkCombufScopeBegin(pub, g_combuf.entire_combuf_scope_id);
}

void R_VkCombufEnd( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;
	R_VkCombufScopeEnd(pub, 0 | BEGIN_INDEX_TAG, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	XVK_CHECK(vkEndCommandBuffer(cb->public.cmdbuf));
}

static const char* myStrdup(const char *src) {
	const int len = strlen(src);
	char *ret = Mem_Malloc(vk_core.pool, len + 1);
	memcpy(ret, src, len);
	ret[len] = '\0';
	return ret;
}

int R_VkGpuScope_Register(const char *name) {
	if (g_combuf.scopes_count == MAX_SCOPES) {
		gEngine.Con_Printf(S_ERROR "Cannot register GPU profiler scope \"%s\": max number of scope %d reached\n", name, MAX_SCOPES);
		return -1;
	}

	g_combuf.scopes[g_combuf.scopes_count].name = myStrdup(name);

	return g_combuf.scopes_count++;
}

int R_VkCombufScopeBegin(vk_combuf_t* cumbuf, int scope_id) {
	if (scope_id < 0)
		return -1;

	ASSERT(scope_id < g_combuf.scopes_count);

	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)cumbuf;
	if (cb->profiler.scopes_count == MAX_SCOPES)
		return -1;

	cb->profiler.scopes[cb->profiler.scopes_count] = scope_id;

	vkCmdWriteTimestamp(cb->public.cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_combuf.timestamp.pool, cb->profiler.timestamps_offset + cb->profiler.scopes_count * 2);

	return (cb->profiler.scopes_count++) | BEGIN_INDEX_TAG;
}

void R_VkCombufScopeEnd(vk_combuf_t* combuf, int begin_index, VkPipelineStageFlagBits pipeline_stage) {
	if (begin_index < 0)
		return;

	ASSERT(begin_index & BEGIN_INDEX_TAG);
	begin_index ^= BEGIN_INDEX_TAG;

	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)combuf;

	vkCmdWriteTimestamp(cb->public.cmdbuf, pipeline_stage, g_combuf.timestamp.pool, cb->profiler.timestamps_offset + begin_index * 2 + 1);
}

static uint64_t getGpuTimestampOffsetNs( uint64_t latest_gpu_timestamp, uint64_t latest_cpu_timestamp_ns ) {
	// FIXME this is an incorrect check, we need to carry per-device extensions availability somehow. vk_core-vs-device refactoring pending
	if (!vkGetCalibratedTimestampsEXT) {
		// Estimate based on supposed submission time, assuming that we submit, and it starts computing right after cmdbuffer closure
		// which may not be true. But it's all we got
		// TODO alternative approach: estimate based on end timestamp
		const uint64_t gpu_begin_ns = (double) latest_gpu_timestamp * vk_core.physical_device.properties.limits.timestampPeriod;
		return latest_cpu_timestamp_ns - gpu_begin_ns;
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

vk_combuf_scopes_t R_VkCombufScopesGet( vk_combuf_t *pub ) {
	APROF_SCOPE_DECLARE_BEGIN(function, __FUNCTION__);
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	uint64_t *const timestamps = g_combuf.timestamp.values + cb->profiler.timestamps_offset;
	const int timestamps_count = cb->profiler.scopes_count * 2;

	vkGetQueryPoolResults(vk_core.device, g_combuf.timestamp.pool, cb->profiler.timestamps_offset, timestamps_count, timestamps_count * sizeof(uint64_t), timestamps, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

	const uint64_t timestamp_offset_ns = getGpuTimestampOffsetNs( timestamps[1], aprof_time_now_ns() );
	const double timestamp_period = vk_core.physical_device.properties.limits.timestampPeriod;

	for (int i = 0; i < timestamps_count; ++i) {
		const uint64_t gpu_ns = timestamps[i] * timestamp_period;
		timestamps[i] = timestamp_offset_ns + gpu_ns;
	}

	APROF_SCOPE_END(function);

	return (vk_combuf_scopes_t){
		.timestamps = g_combuf.timestamp.values + cb->profiler.timestamps_offset,
		.scopes = g_combuf.scopes,
		.entries = cb->profiler.scopes,
		.entries_count = cb->profiler.scopes_count,
	};
}
