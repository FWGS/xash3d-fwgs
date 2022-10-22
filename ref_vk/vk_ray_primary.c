#include "ray_resources.h"
#include "ray_pass.h"

#define LIST_COMMON_BINDINGS(X) \
	X(1, tlas, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR) \
	X(2, ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(3, kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(4, indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(5, vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(6, all_textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(7, skybox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR) \

static const VkDescriptorSetLayoutBinding bindings[] = {
#define INIT_BINDING(index, name, type, count, stages) \
	{ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = stages, \
	},
#define INIT_IMAGE(index, name, ...) \
		INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR)

	LIST_COMMON_BINDINGS(INIT_BINDING)
	RAY_PRIMARY_OUTPUTS(INIT_IMAGE)

#undef INIT_IMAGE
#undef INIT_BINDING
};

static const int semantics[] = {
#define IN(index, name, ...) (RayResource_##name + 1),
#define OUT(index, name, ...) -(RayResource_##name + 1),
	LIST_COMMON_BINDINGS(IN)
	RAY_PRIMARY_OUTPUTS(OUT)
#undef IN
#undef OUT
};

const ray_pass_layout_t ray_primary_layout_fixme = {
	.bindings = bindings,
	.bindings_semantics = semantics,
	.bindings_count = COUNTOF(bindings),
	.push_constants = {0},
};
