#pragma once

#include "vk_core.h"

// TODO these should be like:
// - parse the entire layout from shaders
// - expose it as a struct[] interface of the pass
// - resource/interface should prepare descriptors outside of pass code and just pass them to pass
typedef struct {
	const int *bindings_semantics; // RayResource_something
	const VkDescriptorSetLayoutBinding *bindings;
	int bindings_count;

	VkPushConstantRange push_constants;
} ray_pass_layout_t;


struct ray_pass_s;
typedef struct ray_pass_s* ray_pass_p;

typedef const char* ray_pass_shader_t;

typedef struct {
	const char *debug_name;
	ray_pass_layout_t layout;

	ray_pass_shader_t shader; // FIXME remove
	VkShaderModule shader_module;
	const VkSpecializationInfo *specialization;
} ray_pass_create_compute_t;

struct ray_pass_s *RayPassCreateCompute( const ray_pass_create_compute_t *create );


typedef struct {
	ray_pass_shader_t closest; // FIXME remove
	VkShaderModule closest_module;
	ray_pass_shader_t any; // FIXME remove
	VkShaderModule any_module;
} ray_pass_hit_group_t;

typedef struct {
	const char *debug_name;
	ray_pass_layout_t layout;

	ray_pass_shader_t raygen; // FIXME remove
	VkShaderModule raygen_module;

	const ray_pass_shader_t *miss;
	VkShaderModule *miss_module;
	int miss_count;

	const ray_pass_hit_group_t *hit;
	int hit_count;

	const VkSpecializationInfo *specialization;
} ray_pass_create_tracing_t;

struct ray_pass_s *RayPassCreateTracing( const ray_pass_create_tracing_t *create );


void RayPassDestroy( struct ray_pass_s *pass );

struct vk_ray_resources_s;
void RayPassPerform( VkCommandBuffer cmdbuf, int frame_set_slot, struct ray_pass_s *pass, struct vk_ray_resources_s *res);
