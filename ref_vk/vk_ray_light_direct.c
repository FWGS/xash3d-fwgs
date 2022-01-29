#include "vk_ray_light_direct.h"

#include "vk_ray_resources.h"
#include "ray_pass.h"

enum {
	// TODO set 0 ?
	RtLDir_Desc_tlas,
	RtLDir_Desc_ubo,
	RtLDir_Desc_kusochki,
	RtLDir_Desc_indices,
	RtLDir_Desc_vertices,
	RtLDir_Desc_all_textures,

	// TODO set 1
	RtLDir_Desc_lights,
	RtLDir_Desc_light_clusters,

	// TODO set 1
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

		// FIXME it's an artifact that point and poly outputs have same bindings
#define X(index, name, ...) RtLDir_Desc_##name,
		RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X

	RtLDir_Desc_COUNT
};

static VkDescriptorSetLayoutBinding bindings[RtLDir_Desc_COUNT];

static void initDescriptors( void ) {
	// FIXME more conservative shader stages
#define INIT_BINDING(index, name, type, count) \
	bindings[RtLDir_Desc_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, \
	}

	INIT_BINDING(1, tlas, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
	INIT_BINDING(2, ubo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(3, kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(4, indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(5, vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(6, all_textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES);
	INIT_BINDING(7, lights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(8, light_clusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_INPUTS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X
#undef INIT_BINDING
}

static void writeValues( vk_descriptor_value_t *values, const vk_ray_resources_t* res ) {
#define X(index, name, ...) \
	values[RtLDir_Desc_##name] = res->values[RayResource_##name];
	RAY_LIGHT_DIRECT_INPUTS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
	X(-1, tlas)
	X(-1, ubo);
	X(-1, kusochki);
	X(-1, indices);
	X(-1, vertices);
	X(-1, all_textures);
	X(-1, lights);
	X(-1, light_clusters);
#undef X
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

	const ray_pass_create_t rpc = {
		.debug_name = "light direct poly",
		.layout = {
			.bindings = bindings,
			.bindings_count = COUNTOF(bindings),
			.write_values_func = writeValues,
			.push_constants = {0},
		},
		.tracing = {
			.raygen = "ray_light_poly_direct.rgen.spv",
			.miss = miss,
			.miss_count = COUNTOF(miss),
			.hit = hit,
			.hit_count = COUNTOF(hit),
			.specialization = &spec,
		},
	};

	initDescriptors();
	return RayPassCreate( &rpc );
}

static void writePointValues( vk_descriptor_value_t *values, const vk_ray_resources_t* res ) {
	writeValues( values, res );
	values[RtLDir_Desc_light_poly_diffuse] = res->values[RayResource_light_point_diffuse];
	values[RtLDir_Desc_light_poly_specular] = res->values[RayResource_light_point_specular];
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

	const ray_pass_create_t rpc = {
		.debug_name = "light direct point",
		.layout = {
			.bindings = bindings,
			.bindings_count = COUNTOF(bindings),
			.write_values_func = writePointValues,
			.push_constants = {0},
		},
		.tracing = {
			.raygen = "ray_light_direct_point.rgen.spv",
			.miss = miss,
			.miss_count = COUNTOF(miss),
			.hit = hit,
			.hit_count = COUNTOF(hit),
			.specialization = &spec,
		},
	};

	initDescriptors();
	return RayPassCreate( &rpc );
}
