#include "ray_pass.h"
#include "vk_descriptor.h"
#include "vk_ray_resources.h"
#include "vk_ray_resources.h"

#define MAX_STAGES 16
#define MAX_MISS_GROUPS 8
#define MAX_HIT_GROUPS 8

typedef struct ray_pass_s {
	// TODO enum type

	struct {
		vk_descriptors_t riptors;
		VkDescriptorSet sets[1];
		int *binding_semantics;
	} desc;

	union {
		vk_pipeline_ray_t tracing;
	};
} ray_pass_t;

#if 0 // TODO
qboolean createLayout( const ray_pass_layout_t *layout, ray_pass_t *pass ){
	// TODO return false on fail instead of crashing
	{
		const VkDescriptorSetLayoutCreateInfo dslci = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = layout->bindings_count,
			.pBindings = layout->bindings,
		};
		XVK_CHECK(vkCreateDescriptorSetLayout(vk_core.device, &dslci, NULL, &pass->desc_set_layout));
	}

	{
		const VkPipelineLayoutCreateInfo plci = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = 1,
			.pSetLayouts = &pass->desc_set_layout,
			.pushConstantRangeCount = layout->push_constants.size > 0 ? 1 : 0,
			.pPushConstantRanges = &layout->push_constants,
		};
		XVK_CHECK(vkCreatePipelineLayout(vk_core.device, &plci, NULL, &pass->pipeline_layout));
	}

	return true;
}
#endif

static ray_pass_t *createRayPass( const ray_pass_create_t *create ) {
	ray_pass_t *pass = Mem_Malloc(vk_core.pool, sizeof(*pass));

	{
		pass->desc.riptors = (vk_descriptors_t) {
			.bindings = create->layout.bindings,
			.num_bindings = create->layout.bindings_count,
			.num_sets = COUNTOF(pass->desc.sets),
			.desc_sets = pass->desc.sets,
			.push_constants = create->layout.push_constants,
		};

		VK_DescriptorsCreate(&pass->desc.riptors);
	}

	{
		int stage_index = 0;
		vk_shader_stage_t stages[MAX_STAGES];
		int miss_index = 0;
		int misses[MAX_MISS_GROUPS];
		int hit_index = 0;
		vk_pipeline_ray_hit_group_t hits[MAX_HIT_GROUPS];

		vk_pipeline_ray_create_info_t prci = {
			.debug_name = create->debug_name,
			.layout = pass->desc.riptors.pipeline_layout,
			.stages = stages,
			.groups = {
				.hit = hits,
				.miss = misses,
			},
		};

		stages[stage_index++] = (vk_shader_stage_t) {
			.filename = create->tracing.raygen,
			.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.specialization_info = create->tracing.specialization,
		};

		for (int i = 0; i < create->tracing.miss_count; ++i) {
			const ray_pass_shader_t *const shader = create->tracing.miss + i;

			ASSERT(stage_index < MAX_STAGES);
			ASSERT(miss_index < MAX_MISS_GROUPS);

			// TODO handle duplicate filenames
			// TODO really, there should be a global table of shader modules as some of them are used across several passes (e.g. any hit alpha test)
			misses[miss_index++] = stage_index;
			stages[stage_index++] = (vk_shader_stage_t) {
				.filename = *shader,
				.stage = VK_SHADER_STAGE_MISS_BIT_KHR,
				.specialization_info = create->tracing.specialization,
			};
		}

		for (int i = 0; i < create->tracing.hit_count; ++i) {
			const ray_pass_hit_group_t *const group = create->tracing.hit + i;

			ASSERT(hit_index < MAX_HIT_GROUPS);

			// TODO handle duplicate filenames
			if (group->any) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].any = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.filename = group->any,
					.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
					.specialization_info = create->tracing.specialization,
				};
			} else {
				hits[hit_index].any = -1;
			}

			if (group->closest) {
				ASSERT(stage_index < MAX_STAGES);
				hits[hit_index].closest = stage_index;
				stages[stage_index++] = (vk_shader_stage_t) {
					.filename = group->closest,
					.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
					.specialization_info = create->tracing.specialization,
				};
			} else {
				hits[hit_index].closest = -1;
			}

			++hit_index;
		}

		prci.groups.hit_count = hit_index;
		prci.groups.miss_count = miss_index;
		prci.stages_count = stage_index;

		pass->tracing = VK_PipelineRayTracingCreate(&prci);
	}

	if (pass->tracing.pipeline == VK_NULL_HANDLE) {
		VK_DescriptorsDestroy(&pass->desc.riptors);
		Mem_Free(pass);
		return NULL;
	}

	pass->desc.riptors.values = Mem_Malloc(vk_core.pool, sizeof(pass->desc.riptors.values[0]) * create->layout.bindings_count);

	{
		const size_t semantics_size = sizeof(int) * create->layout.bindings_count;
		pass->desc.binding_semantics = Mem_Malloc(vk_core.pool, semantics_size);
		memcpy(pass->desc.binding_semantics, create->layout.bindings_semantics, semantics_size);
	}

	return pass;
}

struct ray_pass_s *RayPassCreate( const ray_pass_create_t *create ) {
	return createRayPass(create);
}

void RayPassDestroy( struct ray_pass_s *pass ) {
	VK_PipelineRayTracingDestroy(&pass->tracing);
	VK_DescriptorsDestroy(&pass->desc.riptors);
	Mem_Free(pass->desc.riptors.values);
	Mem_Free(pass);
}


void RayPassPerform( VkCommandBuffer cmdbuf, struct ray_pass_s *pass, const struct vk_ray_resources_s *res) {
	for (int i = 0; i < pass->desc.riptors.num_bindings; ++i) {
		const int res_index = pass->desc.binding_semantics[i];
		// TODO check early
		ASSERT(res_index >= 0);
		ASSERT(res_index < RayResource__COUNT);
		pass->desc.riptors.values[i] = res->values[res_index];
	}

	VK_DescriptorsWrite(&pass->desc.riptors);

	vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pass->tracing.pipeline);
	vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pass->desc.riptors.pipeline_layout, 0, 1, pass->desc.riptors.desc_sets + 0, 0, NULL);
	VK_PipelineRayTracingTrace(cmdbuf, &pass->tracing, res->width, res->height);
}
