#pragma once

#include "vk_core.h"

// Return scope_id for the new scope. -1 if failed
// name is expected to be statically allocated
int R_VkGpuScopeRegister(const char *name);

typedef struct vk_query_pool_s vk_query_pool_t;
void R_VkGpuBegin(VkCommandBuffer cmdbuf, vk_query_pool_t *qpool);

// Returns begin_index to use in R_VkGpuScopeEnd
int R_VkGpuScopeBegin(VkCommandBuffer cmdbuf, int scope_id);

void R_VkGpuScopeEnd(VkCommandBuffer cmdbuf, int begin_index, VkPipelineStageFlagBits pipeline_stage);

typedef struct {
	const char *name;
	uint64_t begin_ns, end_ns;
} r_vkgpu_scope_entry_t;

typedef struct {
	r_vkgpu_scope_entry_t *scopes;
	int scopes_count;
} r_vkgpu_scopes_t;

// Reads all the scope timing data (timestamp queries) and returns a list of things happened this frame.
// Prerequisite: all relevant recorded command buffers should've been completed and waited on already.
// The returned pointer remains valid until any next R_VkGpu*() call.
r_vkgpu_scopes_t *R_VkGpuScopesGet( VkCommandBuffer cmdbuf );
