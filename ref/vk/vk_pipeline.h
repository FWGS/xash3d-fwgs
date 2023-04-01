#pragma once

#include "vk_core.h"
#include "vk_buffer.h"

VkShaderModule R_VkShaderLoadFromMem(const void *ptr, uint32_t size, const char *name);
void R_VkShaderDestroy(VkShaderModule module);

typedef struct {
	VkShaderModule module;
	const char *filename;
	VkShaderStageFlagBits stage;
	const VkSpecializationInfo *specialization_info;
} vk_shader_stage_t;

typedef struct {
	VkPipelineLayout layout;
	const VkVertexInputAttributeDescription *attribs;
	uint32_t num_attribs;

	const vk_shader_stage_t *stages;
	uint32_t num_stages;

	uint32_t vertex_stride;

  VkBool32 depthTestEnable;
  VkBool32 depthWriteEnable;
  VkCompareOp depthCompareOp;

  VkBool32                 blendEnable;
  VkBlendFactor            srcColorBlendFactor;
  VkBlendFactor            dstColorBlendFactor;
  VkBlendOp                colorBlendOp;
  VkBlendFactor            srcAlphaBlendFactor;
  VkBlendFactor            dstAlphaBlendFactor;
  VkBlendOp                alphaBlendOp;

  VkCullModeFlags cullMode;
} vk_pipeline_graphics_create_info_t;

VkPipeline VK_PipelineGraphicsCreate(const vk_pipeline_graphics_create_info_t *ci);

typedef struct {
	VkPipelineLayout layout;
	VkShaderModule shader_module;
	const VkSpecializationInfo *specialization_info;
} vk_pipeline_compute_create_info_t;

VkPipeline VK_PipelineComputeCreate(const vk_pipeline_compute_create_info_t *ci);

typedef struct {
	int closest;
	int any;
} vk_pipeline_ray_hit_group_t;

typedef struct {
	const char *debug_name;
	VkPipelineLayout layout;

	// FIXME make this pointer to shader modules, add int raygen_index
	const vk_shader_stage_t *stages;
	int stages_count;

	struct {
		const int *miss;
		int miss_count;

		const vk_pipeline_ray_hit_group_t *hit;
		int hit_count;
	} groups;
} vk_pipeline_ray_create_info_t;

typedef struct {
	VkPipeline pipeline;
	vk_buffer_t sbt_buffer; // TODO suballocate this from a single central buffer or something
	struct {
		VkStridedDeviceAddressRegionKHR raygen, miss, hit, callable;
	} sbt;
	char debug_name[32];
} vk_pipeline_ray_t;

vk_pipeline_ray_t VK_PipelineRayTracingCreate(const vk_pipeline_ray_create_info_t *create);
struct vk_combuf_s;
void VK_PipelineRayTracingTrace(struct vk_combuf_s *combuf, const vk_pipeline_ray_t *pipeline, uint32_t width, uint32_t height, int scope_id);
void VK_PipelineRayTracingDestroy(vk_pipeline_ray_t* pipeline);


qboolean VK_PipelineInit( void );
void VK_PipelineShutdown( void );

extern VkPipelineCache g_pipeline_cache;
