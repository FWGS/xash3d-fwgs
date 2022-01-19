#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"

#include "eiface.h" // ARRAYSIZE

enum {
	DenoiserBinding_DestImage = 0,

	DenoiserBinding_Source_BaseColor = 1,
	DenoiserBinding_Source_DiffuseGI = 2,
	DenoiserBinding_Source_Specular = 3,
	DenoiserBinding_Source_Additive = 4,
	DenoiserBinding_Source_Normals = 5,

	DenoiserBinding_Source_PositionT = 6,

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

	g_denoiser.desc_bindings[DenoiserBinding_Source_BaseColor] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_BaseColor,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_DiffuseGI] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_DiffuseGI,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Specular] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Specular,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Additive] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Additive,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_Normals] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_Normals,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	g_denoiser.desc_bindings[DenoiserBinding_Source_PositionT] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_PositionT,
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

void XVK_DenoiserReloadPipeline( void ) {
	// TODO handle errors gracefully
	vkDestroyPipeline(vk_core.device, g_denoiser.pipeline, NULL);
	g_denoiser.pipeline = createPipeline();
}

void XVK_DenoiserDenoise( const xvk_denoiser_args_t* args ) {
	const uint32_t WG_W = 16;
	const uint32_t WG_H = 8;

	g_denoiser.desc_values[DenoiserBinding_Source_BaseColor].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.base_color_a_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_DiffuseGI].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.diffuse_gi_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Specular].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.specular_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Additive].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.additive_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_Normals].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.normals_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_DestImage].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->dst_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	g_denoiser.desc_values[DenoiserBinding_Source_PositionT].image = (VkDescriptorImageInfo){
		.sampler = VK_NULL_HANDLE,
		.imageView = args->src.position_t_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	DEBUG_BEGIN(args->cmdbuf, "denoiser");
		vkCmdBindPipeline(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
		vkCmdBindDescriptorSets(args->cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
		vkCmdDispatch(args->cmdbuf, (args->width + WG_W - 1) / WG_W, (args->height + WG_H - 1) / WG_H, 1);
	DEBUG_END(args->cmdbuf);
}
