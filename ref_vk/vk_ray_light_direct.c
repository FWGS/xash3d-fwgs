#include "vk_ray_light_direct.h"

#include "vk_ray_resources.h"
#include "ray_pass.h"

#define LIST_SCENE_BINDINGS(X) \
	X(1, tlas, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1) \
	X(2, ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1) \
	X(3, kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1) \
	X(4, indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1) \
	X(5, vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1) \
	X(6, all_textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES) \
	X(7, lights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1) \
	X(8, light_clusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1) \

#define LIST_COMMON_BINDINGS(X) \
	LIST_SCENE_BINDINGS(X) \
	RAY_LIGHT_DIRECT_INPUTS(X)

enum {
#define X(index, name, ...) Binding_##name,
	LIST_COMMON_BINDINGS(X)
	// FIXME it's an artifact that point and poly outputs have same bindings indices
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X
	Binding__COUNT
};

static VkDescriptorSetLayoutBinding bindings[Binding__COUNT];
static const int semantics_poly[Binding__COUNT] = {
#define X(index, name, ...) RayResource_##name,
	LIST_COMMON_BINDINGS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X
};

static const int semantics_point[Binding__COUNT] = {
#define X(index, name, ...) RayResource_##name,
	LIST_COMMON_BINDINGS(X)
	RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
#undef X
};

static void initDescriptors( void ) {
	// FIXME more conservative shader stages
#define INIT_BINDING(index, name, type, count) \
	bindings[Binding_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, \
	};
	LIST_SCENE_BINDINGS(INIT_BINDING)
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_INPUTS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X
#undef INIT_BINDING
}

struct ray_pass_s *R_VkRayLightDirectPolyPassCreate( void ) {
	// FIXME move this into vk_pipeline
	const struct SpecializationData {
		uint32_t sbt_record_size;
	} spec_data = {
		.sbt_record_size = vk_core.physical_device.sbt_record_size,
	};
	const VkSpecializationMapEntry spec_map[] = {
		{.constantID = SPEC_SBT_RECORD_SIZE_INDEX, .offset = offsetof(struct SpecializationData, sbt_record_size), .size = sizeof(uint32_t) },
	};
	VkSpecializationInfo spec = {
		.mapEntryCount = COUNTOF(spec_map),
		.pMapEntries = spec_map,
		.dataSize = sizeof(spec_data),
		.pData = &spec_data,
	};

	const ray_pass_shader_t miss[] = {
		"ray_shadow.rmiss.spv"
	};

	const ray_pass_hit_group_t hit[] = { {
		 .closest = NULL,
		 .any = "ray_common_alphatest.rahit.spv",
		},
	};

	const ray_pass_create_tracing_t rpc = {
		.debug_name = "light direct poly",
		.layout = {
			.bindings = bindings,
			.bindings_semantics = semantics_poly,
			.bindings_count = COUNTOF(bindings),
			.push_constants = {0},
		},
		.raygen = "ray_light_poly_direct.rgen.spv",
		.miss = miss,
		.miss_count = COUNTOF(miss),
		.hit = hit,
		.hit_count = COUNTOF(hit),
		.specialization = &spec,
	};

	initDescriptors();
	return RayPassCreateTracing( &rpc );
}

struct ray_pass_s *R_VkRayLightDirectPointPassCreate( void ) {
	// FIXME move this into vk_pipeline
	const struct SpecializationData {
		uint32_t sbt_record_size;
	} spec_data = {
		.sbt_record_size = vk_core.physical_device.sbt_record_size,
	};
	const VkSpecializationMapEntry spec_map[] = {
		{.constantID = SPEC_SBT_RECORD_SIZE_INDEX, .offset = offsetof(struct SpecializationData, sbt_record_size), .size = sizeof(uint32_t) },
	};
	VkSpecializationInfo spec = {
		.mapEntryCount = COUNTOF(spec_map),
		.pMapEntries = spec_map,
		.dataSize = sizeof(spec_data),
		.pData = &spec_data,
	};

	const ray_pass_shader_t miss[] = {
		"ray_shadow.rmiss.spv"
	};

	const ray_pass_hit_group_t hit[] = { {
		 .closest = NULL,
		 .any = "ray_common_alphatest.rahit.spv",
		},
	};

	const ray_pass_create_tracing_t rpc = {
		.debug_name = "light direct point",
		.layout = {
			.bindings = bindings,
			.bindings_semantics = semantics_point,
			.bindings_count = COUNTOF(bindings),
			.push_constants = {0},
		},
		.raygen = "ray_light_direct_point.rgen.spv",
		.miss = miss,
		.miss_count = COUNTOF(miss),
		.hit = hit,
		.hit_count = COUNTOF(hit),
		.specialization = &spec,
	};

	initDescriptors();
	return RayPassCreateTracing( &rpc );
}
