#include "vk_gpurofl.h"
#include "vk_querypool.h"

#define MAX_SCOPES 64
#define MAX_COMMANDBUFFERS 8

typedef struct {
	const char *name;
} r_vkgpu_scope_t;

#define EVENT_BEGIN 0x100

// B....E
//    B....E
// -> B..B.E..E
// -> B.......E
// ->    B.E

typedef struct {
	VkCommandBuffer cmdbuf;
	vk_query_pool_t *qpool;

	uint32_t events[MAX_QUERY_COUNT];
} r_vkgpu_cmdbuf_assoc_t;

static struct {
	r_vkgpu_scope_t scopes[MAX_SCOPES];
	int scopes_count;

	// FIXME couple these more tightly
	r_vkgpu_cmdbuf_assoc_t assocs[MAX_COMMANDBUFFERS];

	r_vkgpu_scopes_t last_frame;
} g_purofl;

int R_VkGpuScopeRegister(const char *name) {
	if (g_purofl.scopes_count == MAX_SCOPES) {
		gEngine.Con_Printf(S_ERROR "Cannot register GPU profiler scope \"%s\": max number of scope %d reached\n", name, MAX_SCOPES);
		return -1;
	}

	g_purofl.scopes[g_purofl.scopes_count].name = name;

	return g_purofl.scopes_count++;
}

void R_VkGpuBegin(VkCommandBuffer cmdbuf, vk_query_pool_t *qpool) {
	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		r_vkgpu_cmdbuf_assoc_t *const assoc = g_purofl.assocs + i;
		if (!assoc->cmdbuf) {
			assoc->cmdbuf = cmdbuf;
			assoc->qpool = qpool;
			return;
		}

		if (assoc->cmdbuf == cmdbuf) {
			assoc->qpool = qpool;
			return;
		}
	}

	ASSERT(!"FIXME Cannot associate cmdbuf with query pool, slots exceeded");
}

static vk_query_pool_t *getQueryPool(VkCommandBuffer cmdbuf) {
	for (int i = 0; i < MAX_COMMANDBUFFERS; ++i) {
		r_vkgpu_cmdbuf_assoc_t *const assoc = g_purofl.assocs + i;
		if (!assoc->cmdbuf)
			break;

		if (assoc->cmdbuf == cmdbuf)
			return assoc->qpool;
	}

	return NULL;
}

static void writeTimestamp(VkCommandBuffer cmdbuf, int scope_id, VkPipelineStageFlagBits stage, int begin) {
	if (scope_id < 0)
		return;

	// 1. Find query pool for the cmdbuf
	vk_query_pool_t *const qpool = getQueryPool(cmdbuf);
	if (!qpool) // TODO complain?
		return;

	// 2. Write timestamp
	const int timestamp_id = R_VkQueryPoolTimestamp(qpool, cmdbuf, stage);

	// 3. Associate timestamp index with scope_begin
}

/* int R_VkGpuScopeBegin(VkCommandBuffer cmdbuf, int scope_id) { */
/* 	writeTimestamp(cmdbuf, scope_id, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 1); */
/* } */
/*  */
/* void R_VkGpuScopeEnd(VkCommandBuffer cmdbuf, int begin_index, VkPipelineStageFlagBits pipeline_stage) { */
/* 	writeTimestamp(cmdbuf, scope_id, pipeline_stage, 0); */
/* } */
