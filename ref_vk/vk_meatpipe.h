#pragma once

#include "vk_core.h"

enum {
	MEATPIPE_RES_WRITE = (1<<0),
	MEATPIPE_RES_CREATE = (1<<1),
	// TMP ..
};

typedef struct {
	char name[64];
	uint32_t descriptor_type;
	int count;
	uint32_t flags;
	union {
		uint32_t image_format;
	};
} vk_meatpipe_resource_t;


struct vk_meatpipe_pass_s;
typedef struct {
	int passes_count;
	struct vk_meatpipe_pass_s *passes;

	int resources_count;
	vk_meatpipe_resource_t *resources;
} vk_meatpipe_t;

vk_meatpipe_t* R_VkMeatpipeCreateFromFile(const char *filename);
void R_VkMeatpipeDestroy(vk_meatpipe_t *mp);

struct vk_ray_resource_handle_s;
typedef struct vk_meatpipe_perfrom_args_s {
	int frame_set_slot; // 0 or 1, until we do num_frame_slots
	int width, height;
	const struct vk_ray_resource_handle_s *resources;
} vk_meatpipe_perfrom_args_t;

void R_VkMeatpipePerform(vk_meatpipe_t *mp, VkCommandBuffer cmdbuf, vk_meatpipe_perfrom_args_t args);
