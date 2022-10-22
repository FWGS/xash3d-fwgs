#include "ray_pass.h"
#include "ray_resources.h"
#include "vk_pipeline.h"
#include "vk_descriptor.h"

// FIXME this is only needed for MAX_CONCURRENT_FRAMES
#include "vk_framectl.h"

#define MAX_STAGES 16
#define MAX_MISS_GROUPS 8
#define MAX_HIT_GROUPS 8

typedef enum {
	RayPassType_Compute,
	RayPassType_Tracing,
} ray_pass_type_t;

typedef struct ray_pass_s {
	ray_pass_type_t type;
	char debug_name[32];

	struct {
		vk_descriptors_t riptors;
		VkDescriptorSet sets[MAX_CONCURRENT_FRAMES];
		int *binding_semantics;
	} desc;
} ray_pass_t;

typedef struct {
	ray_pass_t header;
	vk_pipeline_ray_t pipeline;
} ray_pass_tracing_impl_t;

typedef struct {
	ray_pass_t header;
	VkPipeline pipeline;
} ray_pass_compute_impl_t;

static void initPassDescriptors( ray_pass_t *header, const ray_pass_layout_t *layout ) {
	header->desc.riptors = (vk_descriptors_t) {
		.bindings = layout->bindings,
		.num_bindings = layout->bindings_count,
		.num_sets = COUNTOF(header->desc.sets),
		.desc_sets = header->desc.sets,
		.push_constants = layout->push_constants,
	};

	VK_DescriptorsCreate(&header->desc.riptors);
}

static void finalizePassDescriptors( ray_pass_t *header, const ray_pass_layout_t *layout ) {
	const size_t semantics_size = sizeof(int) * layout->bindings_count;
	header->desc.binding_semantics = Mem_Malloc(vk_core.pool, semantics_size);
	memcpy(header->desc.binding_semantics, layout->bindings_semantics, semantics_size);

	header->desc.riptors.values = Mem_Malloc(vk_core.pool, sizeof(header->desc.riptors.values[0]) * layout->bindings_count);
}

struct ray_pass_s *RayPassCreateTracing( const ray_pass_create_tracing_t *create ) {
	ray_pass_tracing_impl_t *const pass = Mem_Malloc(vk_core.pool, sizeof(*pass));
	ray_pass_t *const header = &pass->header;

	// TODO support external specialization
	ASSERT(!create->specialization);

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

	initPassDescriptors(header, &create->layout);

	{
		int stage_index = 0;
		vk_shader_stage_t stages[MAX_STAGES];
		int miss_index = 0;
		int misses[MAX_MISS_GROUPS];
		int hit_index = 0;
		vk_pipeline_ray_hit_group_t hits[MAX_HIT_GROUPS];

		vk_pipeline_ray_create_info_t prci = {
			.debug_name = create->debug_name,
			.layout = header->desc.riptors.pipeline_layout,
			.stages = stages,
			.groups = {
				.hit = hits,
				.miss = misses,
			},
		};

		stages[stage_index++] = (vk_shader_stage_t) {
			.module = create->raygen_module,
			.filename = create->raygen,
			.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.specialization_info = &spec,
		};

		for (int i = 0; i < create->miss_count; ++i) {
			const char* shader_filename = create->miss ? create->miss[i] : NULL;
			const VkShaderModule shader_module = create->miss_module ? create->miss_module[i] : VK_NULL_HANDLE;

			ASSERT(stage_index < MAX_STAGES);
			ASSERT(miss_index < MAX_MISS_GROUPS);

			// TODO handle duplicate filenames
			// TODO really, there should be a global table of shader modules as some of them are used across several passes (e.g. any hit alpha test)
			misses[miss_index++] = stage_index;
			stages[stage_index++] = (vk_shader_stage_t) {
				.module = shader_module,
				.filename = shader_filename,
				.stage = VK_SHADER_STAGE_MISS_BIT_KHR,
				.specialization_info = &spec,
			};
		}

		for (int i = 0; i < create->hit_count; ++i) {
			const ray_pass_hit_group_t *const group = create->hit + i;

			ASSERT(hit_index < MAX_HIT_GROUPS);

			// TODO handle duplicate filenames
			if (group->any_module) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].any = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.module = group->any_module,
					.filename = NULL,
					.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
					.specialization_info = &spec,
				};
			} if (group->any) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].any = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.filename = group->any,
					.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
					.specialization_info = &spec,
				};
			} else {
				hits[hit_index].any = -1;
			}

			if (group->closest_module) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].closest = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.module = group->closest_module,
					.filename = NULL,
					.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
					.specialization_info = &spec,
				};
			} if (group->closest) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].closest = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.filename = group->closest,
					.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
					.specialization_info = &spec,
				};
			} else {
				hits[hit_index].closest = -1;
			}

			++hit_index;
		}

		prci.groups.hit_count = hit_index;
		prci.groups.miss_count = miss_index;
		prci.stages_count = stage_index;

		pass->pipeline = VK_PipelineRayTracingCreate(&prci);
	}

	if (pass->pipeline.pipeline == VK_NULL_HANDLE) {
		VK_DescriptorsDestroy(&header->desc.riptors);
		Mem_Free(pass);
		return NULL;
	}

	finalizePassDescriptors(header, &create->layout);

	Q_strncpy(header->debug_name, create->debug_name, sizeof(header->debug_name));
	header->type = RayPassType_Tracing;

	return header;
}

struct ray_pass_s *RayPassCreateCompute( const ray_pass_create_compute_t *create ) {
	ray_pass_compute_impl_t *const pass = Mem_Malloc(vk_core.pool, sizeof(*pass));
	ray_pass_t *const header = &pass->header;

	initPassDescriptors(header, &create->layout);

	const vk_pipeline_compute_create_info_t pcci = {
		.layout = header->desc.riptors.pipeline_layout,
		.shader_module = create->shader_module,
		.shader_filename = create->shader,
		.specialization_info = create->specialization,
	};

	pass->pipeline = VK_PipelineComputeCreate( &pcci );
	if (pass->pipeline == VK_NULL_HANDLE) {
		VK_DescriptorsDestroy(&header->desc.riptors);
		Mem_Free(pass);
		return NULL;
	}

	finalizePassDescriptors(header, &create->layout);

	Q_strncpy(header->debug_name, create->debug_name, sizeof(header->debug_name));
	header->type = RayPassType_Compute;

	return header;
}

void RayPassDestroy( struct ray_pass_s *pass ) {
	switch (pass->type) {
		case RayPassType_Tracing:
			{
				ray_pass_tracing_impl_t *tracing = (ray_pass_tracing_impl_t*)pass;
				VK_PipelineRayTracingDestroy(&tracing->pipeline);
				break;
			}
		case RayPassType_Compute:
			{
				ray_pass_compute_impl_t *compute = (ray_pass_compute_impl_t*)pass;
				vkDestroyPipeline(vk_core.device, compute->pipeline, NULL);
				break;
			}
	}

	VK_DescriptorsDestroy(&pass->desc.riptors);
	Mem_Free(pass->desc.riptors.values);
	Mem_Free(pass->desc.binding_semantics);
	Mem_Free(pass);
}

static void performTracing( VkCommandBuffer cmdbuf, int set_slot, const ray_pass_tracing_impl_t *tracing, const struct vk_ray_resources_s *res) {
	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, tracing->pipeline.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, tracing->header.desc.riptors.pipeline_layout, 0, 1, tracing->header.desc.riptors.desc_sets + set_slot, 0, NULL);
	VK_PipelineRayTracingTrace(cmdbuf, &tracing->pipeline, res->width, res->height);
}

static void performCompute( VkCommandBuffer cmdbuf, int set_slot, const ray_pass_compute_impl_t *compute, const struct vk_ray_resources_s *res) {
	const uint32_t WG_W = 8;
	const uint32_t WG_H = 8;

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, compute->header.desc.riptors.pipeline_layout, 0, 1, compute->header.desc.riptors.desc_sets + set_slot, 0, NULL);
	vkCmdDispatch(cmdbuf, (res->width + WG_W - 1) / WG_W, (res->height + WG_H - 1) / WG_H, 1);
}

void RayPassPerform( VkCommandBuffer cmdbuf, int frame_set_slot, struct ray_pass_s *pass, struct vk_ray_resources_s *res) {
	{
		 ray_resources_fill_t fill = {
			.resources = res,
			.count = pass->desc.riptors.num_bindings,
			.indices = pass->desc.binding_semantics,
			.out_values = pass->desc.riptors.values,
		};

		switch (pass->type) {
			case RayPassType_Tracing:
				fill.dest_pipeline = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
				break;
			case RayPassType_Compute:
				fill.dest_pipeline = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;
			default:
				ASSERT(!"Unexpected pass type");
		}

		RayResourcesFill(cmdbuf, fill);

		VK_DescriptorsWrite(&pass->desc.riptors, frame_set_slot);
	}

	DEBUG_BEGIN(cmdbuf, pass->debug_name);

	switch (pass->type) {
		case RayPassType_Tracing:
			{
				ray_pass_tracing_impl_t *tracing = (ray_pass_tracing_impl_t*)pass;
				performTracing(cmdbuf, frame_set_slot, tracing, res);
				break;
			}
		case RayPassType_Compute:
			{
				ray_pass_compute_impl_t *compute = (ray_pass_compute_impl_t*)pass;
				performCompute(cmdbuf, frame_set_slot, compute, res);
				break;
			}
	}

	DEBUG_END(cmdbuf);
}
