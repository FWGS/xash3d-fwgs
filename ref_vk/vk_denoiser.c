#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"

#include "eiface.h" // ARRAYSIZE

enum {
	DenoiserBinding_DestImage = 0,

	DenoiserBinding_Source_BaseColor = 1,

	DenoiserBinding_Source_LightPolyDiffuse = 2,
	DenoiserBinding_Source_LightPolySpecular = 3,
	DenoiserBinding_Source_LightPointDiffuse = 4,
	DenoiserBinding_Source_LightPointSpecular = 5,

	/* DenoiserBinding_Source_Specular = 3, */
	/* DenoiserBinding_Source_Additive = 4, */
	/* DenoiserBinding_Source_Normals = 5, */
  /*  */
	/* DenoiserBinding_Source_PositionT = 6, */

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

	g_denoiser.desc_bindings[DenoiserBinding_Source_LightPolyDiffuse] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_LightPolyDiffuse,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};
	g_denoiser.desc_bindings[DenoiserBinding_Source_LightPolySpecular] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_LightPolySpecular,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};
	g_denoiser.desc_bindings[DenoiserBinding_Source_LightPointDiffuse] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_LightPointDiffuse,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
	};
	g_denoiser.desc_bindings[DenoiserBinding_Source_LightPointSpecular] = (VkDescriptorSetLayoutBinding){
		.binding = DenoiserBinding_Source_LightPointSpecular,
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

void XVK_DenoiserDenoise( VkCommandBuffer cmdbuf, const vk_ray_resources_t* res ) {
	const uint32_t WG_W = 8;
	const uint32_t WG_H = 8;

#define BIND_IMAGE(index_name, name) \
	g_denoiser.desc_values[DenoiserBinding_##index_name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	}

	BIND_IMAGE(DestImage, denoised);
	BIND_IMAGE(Source_BaseColor, base_color_a);

	BIND_IMAGE(Source_LightPolyDiffuse, light_poly_diffuse);
	BIND_IMAGE(Source_LightPolySpecular, light_poly_specular);
	BIND_IMAGE(Source_LightPointDiffuse, light_point_diffuse);
	BIND_IMAGE(Source_LightPointSpecular, light_point_specular);

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	DEBUG_BEGIN(cmdbuf, "denoiser");
		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
		vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
		vkCmdDispatch(cmdbuf, (res->width + WG_W - 1) / WG_W, (res->height + WG_H - 1) / WG_H, 1);
	DEBUG_END(cmdbuf);
}
