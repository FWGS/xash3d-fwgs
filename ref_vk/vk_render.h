#pragma once
#include "vk_common.h"
#include "vk_const.h"

typedef struct {
	matrix4x4 mvp;
	vec4_t color;
} uniform_data_t;

#define MAX_UNIFORM_SLOTS (MAX_SCENE_ENTITIES * 2 /* solid + trans */ + 1)

uniform_data_t *getUniformSlot(int index);
