#include "ray_pass.h"
#include "shaders/ray_interop.h" // for SPEC_SBT_RECORD_SIZE_INDEX
#include "ray_resources.h"
#include "vk_pipeline.h"
#include "vk_descriptor.h"
#include "vk_combuf.h"

// FIXME this is only needed for MAX_CONCURRENT_FRAMES
// TODO specify it externally as ctor arg
#include "vk_framectl.h"

#define MAX_STAGES 16
#define MAX_MISS_GROUPS 8
#define MAX_HIT_GROUPS 8

typedef enum {
	RayPassType_Compute,
	RayPassType_Tracing,
} ray_pass_type_t;

typedef struct ray_pass_s {
	ray_pass_type_t type; // TODO remove this in favor of VkPipelineStageFlagBits
	VkPipelineStageFlagBits pipeline_type;
	char debug_name[32];
	int gpurofl_scope_id;

	struct {
		int write_from;
		vk_descriptors_t riptors;
		VkDescriptorSet sets[MAX_CONCURRENT_FRAMES];
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

	header->desc.write_from = layout->write_from;
}

static void finalizePassDescriptors( ray_pass_t *header, const ray_pass_layout_t *layout ) {
	const size_t bindings_size = sizeof(layout->bindings[0]) * layout->bindings_count;
	VkDescriptorSetLayoutBinding *bindings = Mem_Malloc(vk_core.pool, bindings_size);
	memcpy(bindings, layout->bindings, bindings_size);
	header->desc.riptors.bindings = bindings;

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
			.filename = NULL,
			.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.specialization_info = &spec,
		};

		for (int i = 0; i < create->miss_count; ++i) {
			const VkShaderModule shader_module = create->miss_module ? create->miss_module[i] : VK_NULL_HANDLE;

			ASSERT(stage_index < MAX_STAGES);
			ASSERT(miss_index < MAX_MISS_GROUPS);

			// TODO handle duplicate filenames
			// TODO really, there should be a global table of shader modules as some of them are used across several passes (e.g. any hit alpha test)
			misses[miss_index++] = stage_index;
			stages[stage_index++] = (vk_shader_stage_t) {
				.module = shader_module,
				.filename = NULL,
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
	header->pipeline_type = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
	header->gpurofl_scope_id = R_VkGpuScope_Register(create->debug_name);

	return header;
}

struct ray_pass_s *RayPassCreateCompute( const ray_pass_create_compute_t *create ) {
	ray_pass_compute_impl_t *const pass = Mem_Malloc(vk_core.pool, sizeof(*pass));
	ray_pass_t *const header = &pass->header;

	initPassDescriptors(header, &create->layout);

	const vk_pipeline_compute_create_info_t pcci = {
		.layout = header->desc.riptors.pipeline_layout,
		.shader_module = create->shader_module,
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
	header->pipeline_type = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	header->gpurofl_scope_id = R_VkGpuScope_Register(create->debug_name);

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
	Mem_Free((void*)pass->desc.riptors.bindings);
	Mem_Free(pass);
}

static void performTracing( vk_combuf_t* combuf, int set_slot, const ray_pass_tracing_impl_t *tracing, int width, int height, int scope_id ) {
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, tracing->pipeline.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, tracing->header.desc.riptors.pipeline_layout, 0, 1, tracing->header.desc.riptors.desc_sets + set_slot, 0, NULL);
	VK_PipelineRayTracingTrace(combuf, &tracing->pipeline, width, height, scope_id);
}

static void performCompute( vk_combuf_t *combuf, int set_slot, const ray_pass_compute_impl_t *compute, int width, int height, int scope_id) {
	const uint32_t WG_W = 8;
	const uint32_t WG_H = 8;
	const VkCommandBuffer cmdbuf = combuf->cmdbuf;

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, compute->header.desc.riptors.pipeline_layout, 0, 1, compute->header.desc.riptors.desc_sets + set_slot, 0, NULL);

	const int begin_id = R_VkCombufScopeBegin(combuf, scope_id);
	vkCmdDispatch(cmdbuf, (width + WG_W - 1) / WG_W, (height + WG_H - 1) / WG_H, 1);
	R_VkCombufScopeEnd(combuf, begin_id, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void RayPassPerform(struct ray_pass_s *pass, vk_combuf_t *combuf, ray_pass_perform_args_t args ) {
	R_VkResourcesPrepareDescriptorsValues(combuf->cmdbuf,
		(vk_resources_write_descriptors_args_t){
			.pipeline = pass->pipeline_type,
			.resources = args.resources,
			.resources_map = args.resources_map,
			.values = pass->desc.riptors.values,
			.count = pass->desc.riptors.num_bindings,
			.write_begin = pass->desc.write_from,
		}
	);

	VK_DescriptorsWrite(&pass->desc.riptors, args.frame_set_slot);

	DEBUG_BEGIN(combuf->cmdbuf, pass->debug_name);

	switch (pass->type) {
		case RayPassType_Tracing:
			{
				ray_pass_tracing_impl_t *tracing = (ray_pass_tracing_impl_t*)pass;
				performTracing(combuf, args.frame_set_slot, tracing, args.width, args.height, pass->gpurofl_scope_id);
				break;
			}
		case RayPassType_Compute:
			{
				ray_pass_compute_impl_t *compute = (ray_pass_compute_impl_t*)pass;
				performCompute(combuf, args.frame_set_slot, compute, args.width, args.height, pass->gpurofl_scope_id);
				break;
			}
	}

	DEBUG_END(combuf->cmdbuf);
}
