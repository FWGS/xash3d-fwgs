#include "vk_denoiser.h"

#include "vk_ray_resources.h"
#include "ray_pass.h"

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

static const VkDescriptorSetLayoutBinding bindings[] = {
#define BIND_IMAGE(index, name) \
	{ \
		.binding = index, \
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
		.descriptorCount = 1, \
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, \
	},
	LIST_BINDINGS(BIND_IMAGE)
#undef BIND_IMAGE
};

static const int semantics[] = {
#define X(index, name) RayResource_##name,
	LIST_BINDINGS(X)
#undef BIND_IMAGE
};


struct ray_pass_s *R_VkRayDenoiserCreate( void ) {
	const ray_pass_create_compute_t rpcc = {
		.debug_name = "denoiser",
		.layout = {
			.bindings = bindings,
			.bindings_semantics = semantics,
			.bindings_count = COUNTOF(bindings),
			.push_constants = {0},
		},
		.shader = "denoiser.comp.spv",
		.specialization = NULL,
	};

	return RayPassCreateCompute( &rpcc );
}

