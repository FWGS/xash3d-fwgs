#include "vk_denoiser.h"

#include "eiface.h" // ARRAYSIZE

static void blitImage( VkCommandBuffer cmdbuf, VkImage src, VkImage dst, int src_width, int src_height, int dst_width, int dst_height )
{
	// Blit raytraced image to frame buffer
	{
		VkImageBlit region = {0};
		region.srcOffsets[1].x = src_width;
		region.srcOffsets[1].y = src_height;
		region.srcOffsets[1].z = 1;
		region.dstOffsets[1].x = dst_width;
		region.dstOffsets[1].y = dst_height;
		region.dstOffsets[1].z = 1;
		region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
		vkCmdBlitImage(cmdbuf, src, VK_IMAGE_LAYOUT_GENERAL,
			dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
			VK_FILTER_NEAREST);
	}

	{
		VkImageMemoryBarrier image_barriers[] = {
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.image = dst,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.subresourceRange =
				(VkImageSubresourceRange){
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.baseMipLevel = 0,
					.levelCount = 1,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
		}};
		vkCmdPipelineBarrier(cmdbuf,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, ARRAYSIZE(image_barriers), image_barriers);
	}
}

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args ) {
	// Blit RTX frame onto swapchain image
	blitImage(args->cmdbuf, args->in.image, args->out.image, args->in.width, args->in.height, args->out.width, args->out.height);
}

qboolean XVK_DenoiserInit( void ) {
	return true;
}

void XVK_DenoiserDestroy( void ) {
}
