#pragma once
#include <stdint.h>

#include "xash3d_types.h"

// enum {
// 	Flag_NonWritable = (1<<0),
// 	Flag_NonReadable = (1<<1),
// };

typedef struct {
	char *name;
	int descriptor_set;
	int binding;

	// TODO: in-out, type, image format, etc
	//uint32_t flags;
} vk_binding_t;

typedef struct {
	vk_binding_t *bindings;
	int bindings_count;

	// TODO:
	// - push constants
	// - specialization
	// - types
} vk_spirv_t;

qboolean R_VkSpirvParse(vk_spirv_t *out, const uint32_t *instructions, int instructions_count);
void R_VkSpirvFree(vk_spirv_t *spirv);
