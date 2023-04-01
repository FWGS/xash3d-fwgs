#pragma once

#include "vk_core.h"

typedef struct vk_combuf_s {
	VkCommandBuffer cmdbuf;
	// VkSemaphore sema_done[2];
	// VkFence fence_done;
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

// TODO r_vkgpu_scopes_t *R_VkGpuScopesGet( VkCommandBuffer cmdbuf );
