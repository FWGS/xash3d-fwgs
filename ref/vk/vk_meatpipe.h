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

	// Index+1 of resource image to read data from if this resource is a "previous frame" contents of another one.
	// Value of zero means that it is a standalone resource. The real index is the value - 1.
	int prev_frame_index_plus_1;
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

struct vk_resource_s;
typedef struct vk_resource_s* vk_resource_p;
typedef struct vk_meatpipe_perfrom_args_s {
	int frame_set_slot; // 0 or 1, until we do num_frame_slots
	int width, height;
	const vk_resource_p *resources;
} vk_meatpipe_perfrom_args_t;

struct vk_combuf_s;
void R_VkMeatpipePerform(vk_meatpipe_t *mp, struct vk_combuf_s *combuf, vk_meatpipe_perfrom_args_t args);
