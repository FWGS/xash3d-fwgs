#include "vk_staging.h"
#include "vk_buffer.h"

#include <memory.h>

#define DEFAULT_STAGING_SIZE (16*1024*1024)
#define MAX_STAGING_ALLOCS (1024)

typedef struct {
	int offset, size;
	enum { DestNone, DestBuffer, DestImage } dest_type;
	union {
		struct {
			VkBuffer buffer;
			VkDeviceSize offset;
		} buffer;
		struct {
			VkImage image;
			VkImageLayout layout;
			VkBufferImageCopy region;
		} image;
	};
} staging_alloc_t;

static struct {
	vk_buffer_t buffer;
	staging_alloc_t allocs[MAX_STAGING_ALLOCS];
	int num_allocs;
	int num_committed;
} g_staging = {0};

qboolean R_VkStagingInit(void) {
	if (!VK_BufferCreate("staging", &g_staging.buffer, DEFAULT_STAGING_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
		return false;

	return true;
}

void R_VkStagingShutdown(void) {
	VK_BufferDestroy(&g_staging.buffer);
}

vk_staging_region_t R_VkStagingLock(uint32_t size, uint32_t alignment) {
	const int offset = g_staging.num_allocs > 0
		? ALIGN_UP(g_staging.allocs[g_staging.num_allocs - 1].offset + g_staging.allocs[g_staging.num_allocs - 1].size, alignment)
		: 0;

	if ( g_staging.num_allocs >= MAX_STAGING_ALLOCS )
		return (vk_staging_region_t){0};

	if ( offset + size > g_staging.buffer.size )
		return (vk_staging_region_t){0};

	memset(g_staging.allocs + g_staging.num_allocs, 0, sizeof(staging_alloc_t));
	g_staging.allocs[g_staging.num_allocs].offset = offset;
	g_staging.allocs[g_staging.num_allocs].size = size;
	g_staging.num_allocs++;
	return (vk_staging_region_t){(char*)g_staging.buffer.mapped + offset, size, g_staging.num_allocs - 1};
}

void R_VkStagingUnlockToBuffer(staging_handle_t handle, VkBuffer dest, size_t dest_offset) {
	staging_alloc_t *alloc;
	ASSERT(handle >= 0 && handle < g_staging.num_allocs);
	ASSERT(g_staging.allocs[handle].dest_type == DestNone);

	alloc = g_staging.allocs + handle;
	alloc->dest_type = DestBuffer;

	alloc->buffer.buffer = dest;
	alloc->buffer.offset = dest_offset;
}

void R_VkStagingUnlockToImage(staging_handle_t handle, VkBufferImageCopy* dest_region, VkImageLayout layout, VkImage dest) {
	staging_alloc_t *alloc;
	ASSERT(handle >= 0 && handle < g_staging.num_allocs);
	ASSERT(g_staging.allocs[handle].dest_type == DestNone);

	alloc = g_staging.allocs + handle;
	alloc->dest_type = DestImage;
	alloc->image.layout = layout;
	alloc->image.image = dest;
	alloc->image.region = *dest_region;
	alloc->image.region.bufferOffset += alloc->offset;
}

void R_VkStagingCommit(VkCommandBuffer cmdbuf) {
	for ( int i = g_staging.num_committed; i < g_staging.num_allocs; i++ ) {
		staging_alloc_t *const alloc = g_staging.allocs + i;
		ASSERT(alloc->dest_type != DestNone);
		switch (alloc->dest_type) {
			case DestImage:
				vkCmdCopyBufferToImage(cmdbuf, g_staging.buffer.buffer, alloc->image.image, alloc->image.layout, 1, &alloc->image.region);
				break;
			case DestBuffer:
				// TODO coalesce staging regions for the same dest buffer
				gEngine.Con_Printf("vkCmdCopyBuffer %d src_offset=%d dst_offset=%d size=%d\n", i, alloc->offset, alloc->buffer.offset, alloc->size);
				vkCmdCopyBuffer(cmdbuf, g_staging.buffer.buffer, alloc->buffer.buffer, 1, &(VkBufferCopy){alloc->offset, alloc->buffer.offset, alloc->size});
				break;
		}

		alloc->dest_type = DestNone;
	}

	g_staging.num_committed = g_staging.num_allocs;
}

void R_VKStagingMarkEmpty_FIXME(void) {
	g_staging.num_committed = g_staging.num_allocs = 0;
}

void R_VkStagingFlushSync(void) {
	if ( !g_staging.num_allocs )
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
		R_VKStagingMarkEmpty_FIXME();
		XVK_CHECK(vkEndCommandBuffer(cmdbuf));
		XVK_CHECK(vkQueueSubmit(vk_core.queue, 1, &subinfo, VK_NULL_HANDLE));
		XVK_CHECK(vkQueueWaitIdle(vk_core.queue));
	}
}
