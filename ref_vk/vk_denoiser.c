#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"

#include "eiface.h" // ARRAYSIZE

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
		.imageView = args->view_src,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->view_dst,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	vkCmdBindPipeline(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
	vkCmdBindDescriptorSets(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
	vkCmdDispatch(args->cmdbuf, (args->width + WG_W - 1) / WG_W, (args->height + WG_H - 1) / WG_H, 1);
}
