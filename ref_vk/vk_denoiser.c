#include "vk_denoiser.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_ray_resources.h"

#define LIST_BINDINGS(X) \
	X(0, denoised) \
	X(1, base_color_a) \
	X(2, light_poly_diffuse) \
	X(3, light_poly_specular) \
	X(4, light_point_diffuse) \
	X(5, light_point_specular) \

enum {
#define X(index, name) DenoiserBinding_##name,
	LIST_BINDINGS(X)
#undef X

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
	g_denoiser.descriptors.num_bindings = COUNTOF(g_denoiser.desc_bindings);
	g_denoiser.descriptors.values = g_denoiser.desc_values;
	g_denoiser.descriptors.num_sets = 1;
	g_denoiser.descriptors.desc_sets = g_denoiser.desc_sets;
	g_denoiser.descriptors.push_constants = (VkPushConstantRange){
		.offset = 0,
		.size = 0,
		.stageFlags = 0,
	};

#define BIND_IMAGE(index, name) \
	g_denoiser.desc_bindings[DenoiserBinding_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
		.descriptorCount = 1, \
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, \
	};
LIST_BINDINGS(BIND_IMAGE)
#undef BIND_IMAGE

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

#define COPY_VALUE(index, name) \
	g_denoiser.desc_values[DenoiserBinding_##name] = res->values[RayResource_##name];
	LIST_BINDINGS(COPY_VALUE)
#undef COPY_VALUE

	VK_DescriptorsWrite(&g_denoiser.descriptors);

	DEBUG_BEGIN(cmdbuf, "denoiser");
		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.pipeline);
		vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_denoiser.descriptors.pipeline_layout, 0, 1, g_denoiser.descriptors.desc_sets + 0, 0, NULL);
		vkCmdDispatch(cmdbuf, (res->width + WG_W - 1) / WG_W, (res->height + WG_H - 1) / WG_H, 1);
	DEBUG_END(cmdbuf);
}
