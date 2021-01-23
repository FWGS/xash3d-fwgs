#include "vk_core.h"

typedef struct {
	VkPipelineLayout layout;
	VkVertexInputAttributeDescription *attribs;
	uint32_t num_attribs;

	VkPipelineShaderStageCreateInfo *stages;
	uint32_t num_stages;

	uint32_t vertex_stride;

  VkBool32 depthTestEnable;
  VkBool32 depthWriteEnable;
  VkCompareOp depthCompareOp;

  VkBool32                 blendEnable;
  VkBlendFactor            srcColorBlendFactor;
  VkBlendFactor            dstColorBlendFactor; VkBlendOp                colorBlendOp;
  VkBlendFactor            srcAlphaBlendFactor;
  VkBlendFactor            dstAlphaBlendFactor;
  VkBlendOp                alphaBlendOp;
} vk_pipeline_create_info_t;

VkPipeline createPipeline(const vk_pipeline_create_info_t *ci);
