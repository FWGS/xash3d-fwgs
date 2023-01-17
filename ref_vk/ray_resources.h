#pragma once

#include "vk_const.h"
#include "vk_image.h"
#include "vk_descriptor.h"

#include "shaders/ray_interop.h"

//qboolean R_VkResourceInit(void);
//void R_VkResourceShutdown();

typedef enum {
	ResourceUnknown,
	ResourceBuffer,
	ResourceImage,
} ray_resource_type_e;

typedef struct {
	ray_resource_type_e type;
	int image_format; // if type == ResourceImage
} ray_resource_desc_t;

qboolean R_VkResourceSetExternal(const char* name, VkDescriptorType type, vk_descriptor_value_t value, int count, ray_resource_desc_t desc, const xvk_image_t *image);

/*
typedef struct {
	int semantic;
	int count;
} ray_resource_binding_desc_fixme_t;

ray_resource_binding_desc_fixme_t RayResouceGetBindingForName_FIXME(const char *name, ray_resource_desc_t desc);
*/

#define RAY_SCENE_RESOURCES(X) \
	X(TLAS, tlas) \
	X(Buffer, ubo) \
	X(Buffer, kusochki) \
	X(Buffer, indices) \
	X(Buffer, vertices) \
	X(Buffer, lights) \
	X(Buffer, light_clusters) \
	X(Texture, all_textures) \
	X(Texture, skybox) \

enum {
#define X(type, name, ...) RayResource_##name,
	RAY_SCENE_RESOURCES(X)
	RAY_PRIMARY_OUTPUTS(X)
	RAY_LIGHT_DIRECT_POLY_OUTPUTS(X)
	RAY_LIGHT_DIRECT_POINT_OUTPUTS(X)
	X(-1, denoised)
#undef X
	RayResource__COUNT
};

typedef struct vk_ray_resources_s {
	uint32_t width, height;
} vk_ray_resources_t;

typedef struct {
	VkAccessFlags access_mask;
	VkImageLayout image_layout;
	VkPipelineStageFlagBits pipelines;
} ray_resource_state_t;

typedef struct vk_resource_s {
	VkDescriptorType type;
	ray_resource_state_t write, read;
	union {
		vk_descriptor_value_t value;
		const xvk_image_t *image;
	};
} vk_resource_t;

typedef struct vk_resource_s *vk_resource_p;

typedef struct {
	VkPipelineStageFlagBits pipeline;
	vk_resource_p resource;
	vk_descriptor_value_t* value;
	qboolean write;
} vk_resource_write_descriptor_args_t;

void R_VkResourceWriteToDescriptorValue(VkCommandBuffer cmdbuf, vk_resource_write_descriptor_args_t args);
