#include "vk_core.h"
#include "vk_buffer.h"

typedef struct {
  const char *filename;
	VkShaderStageFlagBits stage;
	VkSpecializationInfo *specialization_info;
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
  const char *shader_filename;
	VkSpecializationInfo *specialization_info;
} vk_pipeline_compute_create_info_t;

VkPipeline VK_PipelineComputeCreate(const vk_pipeline_compute_create_info_t *ci);

typedef struct {
	int closest;
	int any;
} vk_pipeline_ray_hit_group_t;

typedef struct {
	const char *debug_name;
	VkPipelineLayout layout;

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
void VK_PipelineRayTracingTrace(VkCommandBuffer cmdbuf, const vk_pipeline_ray_t *pipeline, uint32_t width, uint32_t height);
void VK_PipelineRayTracingDestroy(vk_pipeline_ray_t* pipeline);


qboolean VK_PipelineInit( void );
void VK_PipelineShutdown( void );

extern VkPipelineCache g_pipeline_cache;
