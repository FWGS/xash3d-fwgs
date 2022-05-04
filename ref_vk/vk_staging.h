#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

//void *R_VkStagingAlloc(size_t size, VkBuffer dest, size_t dest_offset);

typedef int staging_handle_t;

typedef struct {
	void *ptr;
	size_t size;
	staging_handle_t handle;
} vk_staging_region_t;
vk_staging_region_t R_VkStagingLock(uint32_t size, uint32_t alignment);
void R_VkStagingUnlockToBuffer(staging_handle_t handle, VkBuffer dest, size_t dest_offset);
void R_VkStagingUnlockToImage(staging_handle_t handle, VkBufferImageCopy* dest_region, VkImageLayout layout, VkImage dest);

void R_VkStagingCommit(VkCommandBuffer cmdbuf);

// FIXME Remove this with proper staging
void R_VKStagingMarkEmpty_FIXME(void);

// Force commit synchronously
void R_VkStagingFlushSync(void);

// TODO
// - [x] call init/shutdown from vk_core.ckkjkj
// - [x] use this in vk_texture.c
// - [ ] use this in vk_render.c
