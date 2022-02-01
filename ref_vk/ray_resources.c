#include "vk_ray_resources.h"
#include "vk_core.h"

#include <stdlib.h>

#define MAX_BARRIERS 16

void RayResourcesFill(VkCommandBuffer cmdbuf, ray_resources_fill_t fill) {
	VkImageMemoryBarrier image_barriers[MAX_BARRIERS];
	int image_barriers_count = 0;
	VkPipelineStageFlags src_stage_mask = VK_PIPELINE_STAGE_NONE_KHR;

	for (int i = 0; i < fill.count; ++i) {
		const qboolean write = fill.indices[i] < 0;
		const int index = abs(fill.indices[i]) - 1;
		ray_resource_t *const res = fill.resources->resources + index;

		ASSERT(index >= 0);
		ASSERT(index < RayResource__COUNT);

		if (res->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
			if (write) {
				// No reads are happening
				ASSERT(res->read.pipelines == 0);

				res->write = (ray_resource_state_t) {
					.access_mask = VK_ACCESS_SHADER_WRITE_BIT,
					.image_layout = VK_IMAGE_LAYOUT_GENERAL,
					.pipelines = fill.dest_pipeline,
				};

				image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.image = res->image->image,
					.srcAccessMask = 0,
					.dstAccessMask = res->write.access_mask,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = res->write.image_layout,
					.subresourceRange = (VkImageSubresourceRange) {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1,
					},
				};

			} else {
				// Write happened
				ASSERT(res->write.pipelines != 0);


				// No barrier was issued
				if (!(res->read.pipelines & fill.dest_pipeline)) {
					res->read.access_mask = VK_ACCESS_SHADER_READ_BIT;
					res->read.pipelines |= fill.dest_pipeline;
					res->read.image_layout = VK_IMAGE_LAYOUT_GENERAL;

					src_stage_mask |= res->write.pipelines;

					image_barriers[image_barriers_count++] = (VkImageMemoryBarrier) {
						.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
						.image = res->image->image,
						.srcAccessMask = res->write.access_mask,
						.dstAccessMask = res->read.access_mask,
						.oldLayout = res->write.image_layout,
						.newLayout = res->read.image_layout,
						.subresourceRange = (VkImageSubresourceRange) {
							.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							.baseMipLevel = 0,
							.levelCount = 1,
							.baseArrayLayer = 0,
							.layerCount = 1,
						},
					};
				}
			}
			fill.out_values[i].image = (VkDescriptorImageInfo) {
				.imageLayout = write ? res->write.image_layout : res->read.image_layout,
				.imageView = res->image->view,
				.sampler = VK_NULL_HANDLE,
			};
		} else {
			fill.out_values[i] = res->value;
		}
	}

	if (image_barriers_count) {
		if (!src_stage_mask)
			src_stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		vkCmdPipelineBarrier(cmdbuf,
			src_stage_mask,
			fill.dest_pipeline,
			0, 0, NULL, 0, NULL, image_barriers_count, image_barriers);
	}
}
