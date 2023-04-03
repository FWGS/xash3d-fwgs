#pragma once

#include "vk_core.h"

#define MAX_GPU_SCOPES 64

typedef struct vk_combuf_s {
	VkCommandBuffer cmdbuf;
} vk_combuf_t;

qboolean R_VkCombuf_Init( void );
void R_VkCombuf_Destroy( void );

vk_combuf_t* R_VkCombufOpen( void );
void R_VkCombufClose( vk_combuf_t* );

void R_VkCombufBegin( vk_combuf_t* );
void R_VkCombufEnd( vk_combuf_t* );


int R_VkGpuScope_Register(const char *name);

int R_VkCombufScopeBegin(vk_combuf_t*, int scope_id);
void R_VkCombufScopeEnd(vk_combuf_t*, int begin_index, VkPipelineStageFlagBits pipeline_stage);

typedef struct {
	const char *name;
} vk_combuf_scope_t;

typedef struct vk_combuf_scopes_s {
	const uint64_t *timestamps;
	const vk_combuf_scope_t *scopes;
	const int *entries; // index into scopes; each entry consumes 2 values from timestamps array sequentially
	int entries_count;
} vk_combuf_scopes_t;

// Reads all the scope timing data (timestamp queries) and returns a list of things happened this frame.
// Prerequisite: all relevant recorded command buffers should've been completed and waited on already.
// The returned pointer remains valid until any next R_VkGpu*() call.
vk_combuf_scopes_t R_VkCombufScopesGet( vk_combuf_t * );
