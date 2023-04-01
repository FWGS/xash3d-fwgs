#pragma once

#include "vk_core.h"

// TODO these should be like:
// - parse the entire layout from shaders
// - expose it as a struct[] interface of the pass
// - resource/interface should prepare descriptors outside of pass code and just pass them to pass
typedef struct {
	const VkDescriptorSetLayoutBinding *bindings;
	int bindings_count;
	int write_from;

	VkPushConstantRange push_constants;
} ray_pass_layout_t;


struct ray_pass_s;
typedef struct ray_pass_s* ray_pass_p;

typedef struct {
	// TODO int num_frame_slots;
	const char *debug_name;
	ray_pass_layout_t layout;

	VkShaderModule shader_module;
	const VkSpecializationInfo *specialization;
} ray_pass_create_compute_t;

struct ray_pass_s *RayPassCreateCompute( const ray_pass_create_compute_t *create );


typedef struct {
	VkShaderModule closest_module;
	VkShaderModule any_module;
} ray_pass_hit_group_t;

typedef struct {
	// TODO int num_frame_slots;
	const char *debug_name;
	ray_pass_layout_t layout;

	// TODO make a single tables of all shader modules
	// and then reference them by index in raygen/miss/hit tables
	// like it's done in vk_pipeline_ray_create_info_t
	VkShaderModule raygen_module;

	VkShaderModule *miss_module;
	int miss_count;

	const ray_pass_hit_group_t *hit;
	int hit_count;

	const VkSpecializationInfo *specialization;
} ray_pass_create_tracing_t;

struct ray_pass_s *RayPassCreateTracing( const ray_pass_create_tracing_t *create );


void RayPassDestroy( struct ray_pass_s *pass );

struct vk_resource_s;
typedef struct vk_resource_s *vk_resource_p;

typedef struct ray_pass_perform_args_s {
	int frame_set_slot; // 0 or 1, until we do num_frame_slots
	int width, height;
	const vk_resource_p *resources;
	const int *resources_map;
} ray_pass_perform_args_t;

struct vk_combuf_s;
void RayPassPerform(struct ray_pass_s *pass, struct vk_combuf_s* combuf, ray_pass_perform_args_t args );

