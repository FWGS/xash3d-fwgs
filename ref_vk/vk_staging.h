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

// Append copy commands to command buffer.
VkCommandBuffer R_VkStagingCommit(void);

// Mark previous frame data as uploaded and safe to use.
void R_VkStagingFrameBegin(void);

// Uploads staging contents and returns the command buffer ready to be submitted.
// Can return NULL if there's nothing to upload.
VkCommandBuffer R_VkStagingFrameEnd(void);

// Gets the current command buffer.
// WARNING: Can be invalidated by any of the Lock calls
VkCommandBuffer R_VkStagingGetCommandBuffer(void);
