#include "vk_combuf.h"
#include "vk_commandpool.h"

#define MAX_COMMANDBUFFERS 4
#define MAX_QUERY_COUNT 128

typedef struct {
	vk_combuf_t public;
	int used;
	struct {
		// First two is entire command buffer time [begin, end]
		uint32_t timestamps_offset;
	} profiler;
} vk_combuf_impl_t;

static struct {
	vk_command_pool_t pool;

	vk_combuf_impl_t combufs[MAX_COMMANDBUFFERS];

	struct {
		VkQueryPool pool;
		uint64_t values[MAX_QUERY_COUNT * MAX_COMMANDBUFFERS];
	} timestamp;
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

		/* for (int j = 0; j < COUNTOF(cb->public.sema_done); ++j) { */
		/* 	cb->public.sema_done[j] = R_VkSemaphoreCreate(); */
		/* 	ASSERT(cb->public.sema_done[j]); */
		/* 	SET_DEBUG_NAMEF(cb->public.sema_done[j], VK_OBJECT_TYPE_SEMAPHORE, "done[%d][%d]", i, j); */
		/* } */
	}

	return true;
}

void R_VkCombuf_Destroy( void ) {
	vkDestroyQueryPool(vk_core.device, g_combuf.timestamp.pool, NULL);
	R_VkCommandPoolDestroy(&g_combuf.pool);
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
}

void R_VkCombufBegin( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;

	const VkCommandBufferBeginInfo beginfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	XVK_CHECK(vkBeginCommandBuffer(cb->public.cmdbuf, &beginfo));

	vkCmdResetQueryPool(cb->public.cmdbuf, g_combuf.timestamp.pool, cb->profiler.timestamps_offset, MAX_QUERY_COUNT);
	vkCmdWriteTimestamp(cb->public.cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_combuf.timestamp.pool, cb->profiler.timestamps_offset + 0);
}

void R_VkCombufEnd( vk_combuf_t* pub ) {
	vk_combuf_impl_t *const cb = (vk_combuf_impl_t*)pub;
	vkCmdWriteTimestamp(cb->public.cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_combuf.timestamp.pool, cb->profiler.timestamps_offset + 1);
	XVK_CHECK(vkEndCommandBuffer(cb->public.cmdbuf));
}

int R_VkGpuScope_Register(const char *name) {
	// FIXME
	return -1;
}

int R_VkCombufScopeBegin(vk_combuf_t* combuf, int scope_id) {
	// FIXME
	return -1;
}

void R_VkCombufScopeEnd(vk_combuf_t* combuf, int begin_index, VkPipelineStageFlagBits pipeline_stage) {
	// FIXME
}
