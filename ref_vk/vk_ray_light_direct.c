#include "vk_ray_light_direct.h"

#include "vk_ray_resources.h"
#include "ray_pass.h"

enum {
	// TODO set 0 ?
	RtLDir_Desc_TLAS,
	RtLDir_Desc_UBO,
	RtLDir_Desc_Kusochki,
	RtLDir_Desc_Indices,
	RtLDir_Desc_Vertices,
	RtLDir_Desc_Textures,

	// TODO set 1
	RtLDir_Desc_Lights,
	RtLDir_Desc_LightClusters,

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

	INIT_BINDING(1, TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1);
	INIT_BINDING(2, UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(3, Kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(4, Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(5, Vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
	INIT_BINDING(6, Textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES);
	INIT_BINDING(7, Lights, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);
	INIT_BINDING(8, LightClusters, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);

//#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X
#define X(index, name, ...) INIT_BINDING(index, name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X
#undef INIT_BINDING
}

static void writeValues( vk_descriptor_value_t *values, const vk_ray_resources_t* res ) {
#define X(index, name, ...) \
	values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
	RAY_LIGHT_DIRECT_INPUTS(X)
#undef X

#define X(index, name, ...) \
	values[RtLDir_Desc_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
		RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
#undef X

	values[RtLDir_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &res->scene.tlas,
	};

#define DESC_SET_BUFFER(index, buffer_) \
	values[index].buffer = (VkDescriptorBufferInfo){ \
		.buffer = res->scene.buffer_.buffer, \
		.offset = res->scene.buffer_.offset, \
		.range = res->scene.buffer_.size, \
	}

	DESC_SET_BUFFER(RtLDir_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtLDir_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtLDir_Desc_Indices, indices);
	DESC_SET_BUFFER(RtLDir_Desc_Vertices, vertices);
	DESC_SET_BUFFER(RtLDir_Desc_Lights, lights);
	DESC_SET_BUFFER(RtLDir_Desc_LightClusters, light_clusters);

#undef DESC_SET_BUFFER

	values[RtLDir_Desc_Textures].image_array = res->scene.all_textures;
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

	values[RtLDir_Desc_light_poly_diffuse].image = (VkDescriptorImageInfo) {
		.sampler = VK_NULL_HANDLE,
		.imageView = res->light_point_diffuse,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	values[RtLDir_Desc_light_poly_specular].image = (VkDescriptorImageInfo) {
		.sampler = VK_NULL_HANDLE,
		.imageView = res->light_point_specular,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};
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
