#include "vk_studio.h"
#include "vk_common.h"
#include "vk_textures.h"
#include "vk_render.h"
#include "vk_geometry.h"
#include "camera.h"

#include "xash3d_mathlib.h"
#include "const.h"
#include "r_studioint.h"
#include "triangleapi.h"
#include "studio.h"
#include "pm_local.h"
#include "cl_tent.h"
#include "pmtrace.h"
#include "protocol.h"
#include "enginefeatures.h"
#include "pm_movevars.h"

#include <memory.h>
#include <stdlib.h>

#define PREV_STATES_COUNT 1024
#define PREV_FRAMES_COUNT 2

typedef struct {
	matrix3x4 bones_worldtransform[MAXSTUDIOBONES];
	matrix4x4 model_transform;
	float time;
	int bones_update_frame_index;
} prev_state_t;

typedef struct {
	prev_state_t prev_states[PREV_FRAMES_COUNT][PREV_STATES_COUNT];
	int frame_index;
	int current_frame_id;
	int previous_frame_id;
} prev_states_storage_t;

prev_states_storage_t g_prev = { 0 };

inline int clampIndex( int index, int array_length )
{
	if (index < 0)
		return 0;
	else if (index >= array_length)
		return array_length - 1;

	return index;
}

prev_state_t* prevStateInArrayBounds( int frame_storage_id, int entity_id )
{
	int clamped_frame_id = clampIndex( frame_storage_id, PREV_FRAMES_COUNT );
	int clamped_entity_id = clampIndex( entity_id, PREV_STATES_COUNT );
	return &g_prev.prev_states[clamped_frame_id][clamped_entity_id];
}

#define PREV_FRAME() prevStateInArrayBounds( g_prev.previous_frame_id, entity_id )
#define CURRENT_FRAME() prevStateInArrayBounds( g_prev.current_frame_id, entity_id )

void R_PrevFrame_StartFrame( void )
{
	g_prev.frame_index++;
	g_prev.current_frame_id = g_prev.frame_index % PREV_FRAMES_COUNT;
	g_prev.previous_frame_id = g_prev.frame_index - 1;
}

void R_PrevFrame_SaveCurrentBoneTransforms( int entity_id, matrix3x4* bones_transforms )
{
	prev_state_t *state = CURRENT_FRAME();

	if (state->bones_update_frame_index == g_prev.frame_index)
		return; // already updated for this entity

	state->bones_update_frame_index = g_prev.frame_index;

	for( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		Matrix3x4_Copy(state->bones_worldtransform[i], bones_transforms[i]);
	}
}

void R_PrevFrame_SaveCurrentState( int entity_id, matrix4x4 model_transform )
{
	prev_state_t* state = CURRENT_FRAME();
	Matrix4x4_Copy( state->model_transform, model_transform );
	state->time = gpGlobals->time;
}

matrix3x4* R_PrevFrame_BoneTransforms( int entity_id )
{
	return PREV_FRAME()->bones_worldtransform;
}

void R_PrevFrame_ModelTransform( int entity_id, matrix4x4 model_matrix )
{
	Matrix4x4_Copy(model_matrix, PREV_FRAME()->model_transform);
}

float R_PrevFrame_Time( int entity_id )
{
	return PREV_FRAME()->time;
}
