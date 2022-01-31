#pragma once

#include "vk_core.h"
#include "vk_pipeline.h"
#include "vk_descriptor.h"

typedef const char* ray_pass_shader_t;

typedef struct {
	ray_pass_shader_t closest;
	ray_pass_shader_t any;
} ray_pass_hit_group_t;

typedef struct {
	ray_pass_shader_t raygen;

	const ray_pass_shader_t *miss;
	int miss_count;

	const ray_pass_hit_group_t *hit;
	int hit_count;

	const VkSpecializationInfo *specialization;
} ray_pass_tracing_t;

// enum {
// 	RVkRayPassType_Compute,
// 	RVkRayPassType_Tracing,
// };


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

typedef struct {
	// TODO enum type

	const char *debug_name;
	ray_pass_layout_t layout;

	union {
		ray_pass_tracing_t tracing;
	};
} ray_pass_create_t;

struct ray_pass_s;
struct ray_pass_s *RayPassCreate( const ray_pass_create_t *create );
void RayPassDestroy( struct ray_pass_s *pass );

struct vk_ray_resources_s;
void RayPassPerform( VkCommandBuffer cmdbuf, struct ray_pass_s *pass, const struct vk_ray_resources_s *res);
