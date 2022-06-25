#pragma once

#include "vk_core.h"

qboolean R_VkStagingInit(void);
void R_VkStagingShutdown(void);

typedef int staging_handle_t;

typedef struct {
	void *ptr;
	staging_handle_t handle;
} vk_staging_region_t;

// Allocate region for uploadting to buffer
typedef struct {
	VkBuffer buffer;
	uint32_t offset;
	uint32_t size;
	uint32_t alignment;
} vk_staging_buffer_args_t;
vk_staging_region_t R_VkStagingLockForBuffer(vk_staging_buffer_args_t args);

// Allocate region for uploading to image
typedef struct {
	VkImage image;
	VkImageLayout layout;
	VkBufferImageCopy region;
	uint32_t size;
	uint32_t alignment;
} vk_staging_image_args_t;
vk_staging_region_t R_VkStagingLockForImage(vk_staging_image_args_t args);

// Mark allocated region as ready for upload
void R_VkStagingUnlock(staging_handle_t handle);

// Append copy commands to command buffer and mark staging as empty
// FIXME: it's not empty yet, as it depends on cmdbuf being actually submitted and completed
void R_VkStagingCommit(VkCommandBuffer cmdbuf);
void R_VkStagingFrameFlip(void);

// FIXME Remove this with proper staging
void R_VKStagingMarkEmpty_FIXME(void);

// Force commit synchronously
void R_VkStagingFlushSync(void);
