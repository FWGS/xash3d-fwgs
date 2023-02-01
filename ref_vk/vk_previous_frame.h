#pragma once

#include "vk_common.h"

void R_PrevFrame_StartFrame(void);
void R_PrevFrame_SaveCurrentBoneTransforms(int entity_id, matrix3x4* bones_transforms);
void R_PrevFrame_SaveCurrentState(int entity_id, matrix4x4 model_transform);
matrix3x4* R_PrevFrame_BoneTransforms(int entity_id);
void R_PrevFrame_ModelTransform( int entity_id, matrix4x4 model_matrix );
float R_PrevFrame_Time(int entity_id);
