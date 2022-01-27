#include "vk_ray_primary.h"
#include "vk_ray_internal.h"

#include "vk_descriptor.h"
#include "vk_ray_resources.h"
#include "ray_pass.h"

enum {
	// TODO set 0
	RtPrim_Desc_TLAS,
	RtPrim_Desc_UBO,
	RtPrim_Desc_Kusochki,
	RtPrim_Desc_Indices,
	RtPrim_Desc_Vertices,
	RtPrim_Desc_Textures,

	// TODO set 1
#define X(index, name, ...) RtPrim_Desc_Out_##name,
RAY_PRIMARY_OUTPUTS(X)
#undef X

	RtPrim_Desc_COUNT
};

static VkDescriptorSetLayoutBinding bindings[RtPrim_Desc_COUNT];

static void initDescriptors( void ) {
#define INIT_BINDING(index, name, type, count, stages) \
	bindings[RtPrim_Desc_##name] = (VkDescriptorSetLayoutBinding){ \
		.binding = index, \
		.descriptorType = type, \
		.descriptorCount = count, \
		.stageFlags = stages, \
	}

	INIT_BINDING(1, TLAS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	INIT_BINDING(2, UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(3, Kusochki, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(4, Indices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(5, Vertices, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
	INIT_BINDING(6, Textures, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEXTURES, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

#define X(index, name, ...) \
	INIT_BINDING(index, Out_##name, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
RAY_PRIMARY_OUTPUTS(X)
#undef X
#undef INIT_BINDING
}

static void writeValues( vk_descriptor_value_t *values, const vk_ray_resources_t* res ) {
#define X(index, name, ...) \
	values[RtPrim_Desc_Out_##name].image = (VkDescriptorImageInfo){ \
		.sampler = VK_NULL_HANDLE, \
		.imageView = res->name, \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
	};
RAY_PRIMARY_OUTPUTS(X)
#undef X

	values[RtPrim_Desc_TLAS].accel = (VkWriteDescriptorSetAccelerationStructureKHR){
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

	DESC_SET_BUFFER(RtPrim_Desc_UBO, ubo);
	DESC_SET_BUFFER(RtPrim_Desc_Kusochki, kusochki);
	DESC_SET_BUFFER(RtPrim_Desc_Indices, indices);
	DESC_SET_BUFFER(RtPrim_Desc_Vertices, vertices);

#undef DESC_SET_BUFFER

	values[RtPrim_Desc_Textures].image_array = res->scene.all_textures;
}

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

	const ray_pass_create_t rpc = {
		.debug_name = "primary ray",
		.layout = {
			.bindings = bindings,
			.bindings_count = COUNTOF(bindings),
			.write_values_func = writeValues,
			.push_constants = {0},
		},
		.tracing = {
			.raygen = "ray_primary.rgen.spv",
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
