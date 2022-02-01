#include "vk_ray_primary.h"

#include "ray_resources.h"
#include "ray_pass.h"

#define LIST_COMMON_BINDINGS(X) \
	X(1, tlas, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR) \
	X(2, ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(3, kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(4, indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(5, vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \
	X(6, all_textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR) \

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

struct ray_pass_s *R_VkRayPrimaryPassCreate( void ) {
	// FIXME move this into vk_pipeline or something
	const struct SpecializationData {
		uint32_t sbt_record_size;
	} spec_data = {
		.sbt_record_size = vk_core.physical_device.sbt_record_size,
	};
	const VkSpecializationMapEntry spec_map[] = {
		{.constantID = SPEC_SBT_RECORD_SIZE_INDEX, .offset = offsetof(struct SpecializationData, sbt_record_size), .size = sizeof(uint32_t) },
	};
	const VkSpecializationInfo spec = {
		.mapEntryCount = COUNTOF(spec_map),
		.pMapEntries = spec_map,
		.dataSize = sizeof(spec_data),
		.pData = &spec_data,
	};

	const ray_pass_shader_t miss[] = {
		"ray_primary.rmiss.spv"
	};

	const ray_pass_hit_group_t hit[] = { {
		 .closest = "ray_primary.rchit.spv",
		 .any = NULL,
		}, {
		 .closest = "ray_primary.rchit.spv",
		 .any = "ray_common_alphatest.rahit.spv",
		},
	};

	const ray_pass_create_tracing_t rpc = {
		.debug_name = "primary ray",
		.layout = {
			.bindings = bindings,
			.bindings_semantics = semantics,
			.bindings_count = COUNTOF(bindings),
			.push_constants = {0},
		},
		.raygen = "ray_primary.rgen.spv",
		.miss = miss,
		.miss_count = COUNTOF(miss),
		.hit = hit,
		.hit_count = COUNTOF(hit),
		.specialization = &spec,
	};

	return RayPassCreateTracing( &rpc );
}
