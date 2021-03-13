#include "vk_core.h"

typedef struct {
  const char *filename;
	VkShaderStageFlagBits stage;
	VkSpecializationInfo *specialization_info;
} vk_shader_stage_t;

typedef struct {
	VkPipelineLayout layout;
	VkVertexInputAttributeDescription *attribs;
	uint32_t num_attribs;

	vk_shader_stage_t *stages;
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
  const char *shader_filename;
	VkSpecializationInfo *specialization_info;
} vk_pipeline_compute_create_info_t;

VkPipeline VK_PipelineComputeCreate(const vk_pipeline_compute_create_info_t *ci);

qboolean VK_PipelineInit( void );
void VK_PipelineShutdown( void );
