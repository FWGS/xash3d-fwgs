#pragma once

#include "vk_core.h"

#define MAX_QUERY_COUNT 128

typedef struct {
	VkQueryPool pool;
	int used;
	uint64_t results[MAX_QUERY_COUNT];
} vk_query_pool_t;

qboolean R_VkQueryPoolInit( vk_query_pool_t *pool );
void R_VkQueryPoolDestroy( vk_query_pool_t *pool );
int R_VkQueryPoolTimestamp( vk_query_pool_t *pool, VkCommandBuffer cmdbuf, VkPipelineStageFlagBits stage);
void R_VkQueryPoolBegin( vk_query_pool_t *pool, VkCommandBuffer cmdbuf );
void R_VkQueryPoolEnd( vk_query_pool_t *pool, VkCommandBuffer cmdbuf );

void R_VkQueryPoolGetFrameResults( vk_query_pool_t *pool );
