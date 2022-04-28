#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

//void *R_VkStagingAlloc(size_t size, VkBuffer dest, size_t dest_offset);

typedef struct {
	void *ptr;
	size_t size;
	int internal_id_;
} vk_staging_region_t;
vk_staging_region_t R_VkStagingLock(size_t size);
void R_VkStagingUnlockToBuffer(const vk_staging_region_t* region, VkBuffer dest, size_t dest_offset);
void R_VkStagingUnlockToImage(const vk_staging_region_t* region, VkBufferImageCopy* dest_region, VkImageLayout layout, VkImage dest);

void R_VkStagingCommit(VkCommandBuffer cmdbuf);

// Force commit synchronously
void R_VkStagingFlushSync(void);

// TODO
// - [x] call init/shutdown from vk_core.ckkjkj
// - [x] use this in vk_texture.c
// - [ ] use this in vk_render.c
