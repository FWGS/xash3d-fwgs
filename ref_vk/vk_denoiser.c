#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"

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

enum {
	DenoiserBinding_SourceImage = 0,
	DenoiserBinding_DestImage = 1,

	DenoiserBinding_COUNT
};

static struct {
	vk_descriptors_t descriptors;
	vk_descriptor_value_t desc_values[DenoiserBinding_COUNT];

	VkDescriptorSetLayoutBinding desc_bindings[DenoiserBinding_COUNT];
	VkDescriptorSet desc_sets[1];

	VkPipeline pipeline;
} g_denoiser = {0};

static void createLayouts( void ) {
	g_denoiser.descriptors.bindings = g_denoiser.desc_bindings;
	g_denoiser.descriptors.num_bindings = ARRAYSIZE(g_denoiser.desc_bindings);
	g_denoiser.descriptors.values = g_denoiser.desc_values;
	g_denoiser.descriptors.num_sets = 1;
	g_denoiser.descriptors.desc_sets = g_denoiser.desc_sets;
	g_denoiser.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = 0,
		.stageFlags = 0,
	};

	g_denoiser.desc_bindings[DenoiserBinding_DestImage] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_DestImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_SourceImage] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_SourceImage,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	VK_DescriptorsCreate(&g_denoiser.descriptors);
}

static VkPipeline createPipeline( void ) {
	const vk_pipeline_compute_create_info_t pcci = {
		.layout = g_denoiser.descriptors.pipeline_layout,
		.shader_filename = "denoiser.comp.spv",
		.specialization_info = NULL,
	};

	return VK_PipelineComputeCreate( &pcci );
}

qboolean XVK_DenoiserInit( void ) {
	ASSERT(vk_core.rtx);

	createLayouts();

	ASSERT(!g_denoiser.pipeline);
	g_denoiser.pipeline = createPipeline();

	return g_denoiser.pipeline != VK_NULL_HANDLE;
}

void XVK_DenoiserDestroy( void ) {
	ASSERT(vk_core.rtx);
	ASSERT(g_denoiser.pipeline);

	vkDestroyPipeline(vk_core.device, g_denoiser.pipeline, NULL);
	VK_DescriptorsDestroy(&g_denoiser.descriptors);
}

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args ) {
	const uint32_t WG_W = 16;
	const uint32_t WG_H = 8;

	g_denoiser.desc_values[DenoiserBinding_SourceImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->in.image_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->out.image_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	vkCmdBindPipeline(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
	vkCmdBindDescriptorSets(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
	vkCmdDispatch(args->cmdbuf, (args->out.width + WG_W - 1) / WG_W, (args->out.height + WG_H - 1) / WG_H, 1);
}
