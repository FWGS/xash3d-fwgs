#include "vk_staging.h"
#include "vk_buffer.h"
#include "alolcator.h"

#include <memory.h>

#define DEFAULT_STAGING_SIZE (64*1024*1024)
#define MAX_STAGING_ALLOCS (2048)

typedef struct {
	VkImage image;
	VkImageLayout layout;
} staging_image_t;

static struct {
	vk_buffer_t buffer;
	alo_ring_t ring;

	struct {
		VkBuffer dest[MAX_STAGING_ALLOCS];
		VkBufferCopy copy[MAX_STAGING_ALLOCS];

		int count;
		int committed;
	} buffers;

	struct {
		staging_image_t dest[MAX_STAGING_ALLOCS];
		VkBufferImageCopy copy[MAX_STAGING_ALLOCS];
		int count;
		int committed;
	} images;

	struct {
		uint32_t offset;
	} frames[2];
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	aloRingInit(&g_staging.ring, DEFAULT_STAGING_SIZE);

	return true;
}

void R_VkStagingShutdown(void) {
	VK_BufferDestroy(&g_staging.buffer);
}

vk_staging_region_t R_VkStagingLockForBuffer(vk_staging_buffer_args_t args) {
	if ( g_staging.buffers.count >= MAX_STAGING_ALLOCS )
		return (vk_staging_region_t){0};

	const int index = g_staging.buffers.count;

	const uint32_t offset = aloRingAlloc(&g_staging.ring, args.size, args.alignment < 1 ? 1 : args.alignment );
	if (offset == ALO_ALLOC_FAILED)
		return (vk_staging_region_t){0};
	if (g_staging.frames[1].offset == ALO_ALLOC_FAILED)
		g_staging.frames[1].offset = offset;

	g_staging.buffers.dest[index] = args.buffer;
	g_staging.buffers.copy[index] = (VkBufferCopy){
		.srcOffset = offset,
		.dstOffset = args.offset,
		.size = args.size,
	};

	g_staging.buffers.count++;

	return (vk_staging_region_t){
		.ptr = (char*)g_staging.buffer.mapped + offset,
		.handle = index,
	};
}

vk_staging_region_t R_VkStagingLockForImage(vk_staging_image_args_t args) {
	if ( g_staging.images.count >= MAX_STAGING_ALLOCS )
		return (vk_staging_region_t){0};

	const int index = g_staging.images.count;
	staging_image_t *const dest = g_staging.images.dest + index;

	const uint32_t offset = aloRingAlloc(&g_staging.ring, args.size, args.alignment);
	if (offset == ALO_ALLOC_FAILED)
		return (vk_staging_region_t){0};
	if (g_staging.frames[1].offset == ALO_ALLOC_FAILED)
		g_staging.frames[1].offset = offset;

	dest->image = args.image;
	dest->layout = args.layout;
	g_staging.images.copy[index] = args.region;
	g_staging.images.copy[index].bufferOffset += offset;

	g_staging.images.count++;

	return (vk_staging_region_t){
		.ptr = (char*)g_staging.buffer.mapped + offset,
		.handle = index + MAX_STAGING_ALLOCS,
	};
}

void R_VkStagingUnlock(staging_handle_t handle) {
	ASSERT(handle >= 0);
	ASSERT(handle < MAX_STAGING_ALLOCS * 2);

	// FIXME mark and check ready
}

static void commitBuffers(VkCommandBuffer cmdbuf) {
	// TODO better coalescing:
	// - upload once per buffer
	// - join adjacent regions

	VkBuffer prev_buffer = VK_NULL_HANDLE;
	int first_copy = 0;
	for (int i = g_staging.buffers.committed; i < g_staging.buffers.count; i++) {
		if (prev_buffer == g_staging.buffers.dest[i])
			continue;

		if (prev_buffer != VK_NULL_HANDLE) {
			vkCmdCopyBuffer(cmdbuf, g_staging.buffer.buffer,
				prev_buffer,
				i - first_copy, g_staging.buffers.copy + first_copy);
		}

		prev_buffer = g_staging.buffers.dest[i];
		first_copy = i;
	}

	if (prev_buffer != VK_NULL_HANDLE) {
		vkCmdCopyBuffer(cmdbuf, g_staging.buffer.buffer,
			prev_buffer,
			g_staging.buffers.count - first_copy, g_staging.buffers.copy + first_copy);
	}

	g_staging.buffers.committed = g_staging.buffers.count;
}

static void commitImages(VkCommandBuffer cmdbuf) {
	for (int i = g_staging.images.committed; i < g_staging.images.count; i++) {
		vkCmdCopyBufferToImage(cmdbuf, g_staging.buffer.buffer,
			g_staging.images.dest[i].image,
			g_staging.images.dest[i].layout,
			1, g_staging.images.copy + i);
	}

	g_staging.images.committed = g_staging.images.count;
}


void R_VkStagingCommit(VkCommandBuffer cmdbuf) {
	commitBuffers(cmdbuf);
	commitImages(cmdbuf);
}

void R_VkStagingFrameFlip(void) {
	if (g_staging.frames[0].offset != ALO_ALLOC_FAILED)
		aloRingFree(&g_staging.ring, g_staging.frames[0].offset);

	g_staging.frames[0] = g_staging.frames[1];
	g_staging.frames[1].offset = ALO_ALLOC_FAILED;

	g_staging.buffers.committed = g_staging.buffers.count = 0;
	g_staging.images.committed = g_staging.images.count = 0;
}

void R_VKStagingMarkEmpty_FIXME(void) {
	g_staging.buffers.committed = g_staging.buffers.count = 0;
	g_staging.images.committed = g_staging.images.count = 0;
	g_staging.frames[0].offset = g_staging.frames[1].offset = ALO_ALLOC_FAILED;
	aloRingInit(&g_staging.ring, DEFAULT_STAGING_SIZE);
}

void R_VkStagingFlushSync(void) {
	if ( g_staging.buffers.count == g_staging.buffers.committed
		&& g_staging.images.count == g_staging.images.committed)
		return;

	{
		// FIXME get the right one
		const VkCommandBuffer cmdbuf = vk_core.upload_pool.buffers[0];

		const VkCommandBufferBeginInfo beginfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		const VkSubmitInfo subinfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmdbuf,
		};

		XVK_CHECK(vkBeginCommandBuffer(cmdbuf, &beginfo));
		R_VkStagingCommit(cmdbuf);
		XVK_CHECK(vkEndCommandBuffer(cmdbuf));
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));

		R_VKStagingMarkEmpty_FIXME();
	}
}
