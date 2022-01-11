#include "vk_ray_light_direct.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"

#include "eiface.h" // ARRAYSIZE

enum {
	// TODO set 0
	//RtLDir_Desc_TLAS,
	RtLDir_Desc_UBO,
	RtLDir_Desc_Kusochki,
	RtLDir_Desc_Indices,
	RtLDir_Desc_Vertices,
	RtLDir_Desc_Textures,
	RtLDir_Desc_Lights,
	RtLDir_Desc_LightClusters,

	// TODO set 1
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X

	RtLDir_Desc_COUNT
};

static struct {
	struct {
		vk_descriptors_t riptors;
		VkDescriptorSetLayoutBinding bindings[RtLDir_Desc_COUNT];
		vk_descriptor_value_t values[RtLDir_Desc_COUNT];

		// TODO: split into two sets, one common to all rt passes (tlas, kusochki, etc), another one this pass only
		VkDescriptorSet sets[1];
	} desc;

	VkPipeline pipeline;
} g_ray_light_direct;

static void initDescriptors( void ) {
	g_ray_light_direct.desc.riptors = (vk_descriptors_t) {
		.bindings = g_ray_light_direct.desc.bindings,
		.num_bindings = ARRAYSIZE(g_ray_light_direct.desc.bindings),
		.values = g_ray_light_direct.desc.values,
		.num_sets = ARRAYSIZE(g_ray_light_direct.desc.sets),
		.desc_sets = g_ray_light_direct.desc.sets,
		/* .push_constants = (VkPushConstantRange){ */
		/* 	.offset = 0, */
		/* 	.size = sizeof(vk_rtx_push_constants_t), */
		/* 	.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, */
		/* }, */
	};

#define INIT_BINDING(index, name, type, count) \
	g_ray_light_direct.desc.bindings[RtLDir_Desc_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, \
	}

	//INIT_BINDING(1, TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
	INIT_BINDING(2, UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(3, Kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(4, Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(5, Vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(6, Textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES);
	INIT_BINDING(7, Lights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	//INIT_BINDING(7, Lights, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(8, LightClusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

//#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_OUTPUTS(X)
#undef X
#undef INIT_BINDING

	VK_DescriptorsCreate(&g_ray_light_direct.desc.riptors);
}

static void updateDescriptors( const xvk_ray_trace_light_direct_t* args ) {
#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = args->in.name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

#define X(index, name, ...) \
	g_ray_light_direct.desc.values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = args->out.name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_OUTPUTS(X)

	/* g_ray_light_direct.desc.values[RtLDir_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){ */
	/* 	.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, */
	/* 	.accelerationStructureCount = 1, */
	/* 	.pAccelerationStructures = &args->in.tlas, */
	/* }; */

#define DESC_SET_BUFFER(index, buffer_) \
	g_ray_light_direct.desc.values[index].buffer = (VkDescriptorBufferInfo){ \
		.buffer = args->in.buffer_.buffer, \
		.offset = args->in.buffer_.offset, \
		.range = args->in.buffer_.size, \
	}

	DESC_SET_BUFFER(RtLDir_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtLDir_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtLDir_Desc_Indices, indices);
	DESC_SET_BUFFER(RtLDir_Desc_Vertices, vertices);
	DESC_SET_BUFFER(RtLDir_Desc_Lights, lights);
	DESC_SET_BUFFER(RtLDir_Desc_LightClusters, light_clusters);

#undef DESC_SET_BUFFER

	g_ray_light_direct.desc.values[RtLDir_Desc_Textures].image_array = args->in.all_textures;

	VK_DescriptorsWrite(&g_ray_light_direct.desc.riptors);
}

static VkPipeline createPipeline( void ) {
	const vk_pipeline_compute_create_info_t pcci = {
		.layout = g_ray_light_direct.desc.riptors.pipeline_layout,
		.shader_filename = "ray_light_direct.comp.spv",
		.specialization_info = NULL,
	};

	return VK_PipelineComputeCreate( &pcci );
}

qboolean XVK_RayTraceLightDirectInit( void ) {
	initDescriptors();

	g_ray_light_direct.pipeline = createPipeline();
	ASSERT(g_ray_light_direct.pipeline != VK_NULL_HANDLE);

	return true;
}

void XVK_RayTraceLightDirectDestroy( void ) {
	vkDestroyPipeline(vk_core.device, g_ray_light_direct.pipeline, NULL);
	VK_DescriptorsDestroy(&g_ray_light_direct.desc.riptors);
}

void XVK_RayTraceLightDirectReloadPipeline( void ) {
	VkPipeline pipeline = createPipeline();
	if (pipeline == VK_NULL_HANDLE)
		return;

	vkDestroyPipeline(vk_core.device, g_ray_light_direct.pipeline, NULL);
	g_ray_light_direct.pipeline = pipeline;
}

void XVK_RayTraceLightDirect( VkCommandBuffer cmdbuf, const xvk_ray_trace_light_direct_t *args ) {
	const uint32_t WG_W = 16;
	const uint32_t WG_H = 8;

	updateDescriptors( args );

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_ray_light_direct.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, g_ray_light_direct.desc.riptors.pipeline_layout, 0, 1, g_ray_light_direct.desc.riptors.desc_sets + 0, 0, NULL);
	vkCmdDispatch(cmdbuf, (args->width + WG_W - 1) / WG_W, (args->height + WG_H - 1) / WG_H, 1);
}

