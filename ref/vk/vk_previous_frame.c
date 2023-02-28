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
	uint bones_frame_updated;
	uint frame_updated;
} prev_state_t;

typedef struct {
	prev_state_t prev_states[PREV_FRAMES_COUNT][PREV_STATES_COUNT];
	uint frame_index;
	uint prev_frame_index;
	uint current_frame_id;
	uint previous_frame_id;
} prev_states_storage_t;

prev_states_storage_t g_prev = { 0 };

prev_state_t* prevStateInArrayBounds( int frame_storage_id, int entity_id )
{
	int clamped_entity_id = entity_id;

	if (entity_id >= PREV_STATES_COUNT)
	{
		gEngine.Con_Printf("Previous frame states data for entity %d overflows storage (size is %d). Increase it\n", entity_id, PREV_STATES_COUNT);
		clamped_entity_id = PREV_STATES_COUNT - 1; // fallback to last correct value
	}
	else if (entity_id < 0)
	{
		clamped_entity_id = 0; // fallback to correct value
	}

	return &g_prev.prev_states[frame_storage_id][clamped_entity_id];
}

#define PREV_FRAME() prevStateInArrayBounds( g_prev.previous_frame_id, entity_id )
#define CURRENT_FRAME() prevStateInArrayBounds( g_prev.current_frame_id, entity_id )

void R_PrevFrame_StartFrame( void )
{
	g_prev.frame_index++;
	g_prev.current_frame_id = g_prev.frame_index % PREV_FRAMES_COUNT;
	g_prev.previous_frame_id = (g_prev.frame_index - 1) % PREV_FRAMES_COUNT;
}

void R_PrevFrame_SaveCurrentBoneTransforms( int entity_id, matrix3x4* bones_transforms )
{
	prev_state_t *current_frame = CURRENT_FRAME();

	if (current_frame->bones_frame_updated == g_prev.frame_index)
		return; // already updated for this entity

	current_frame->bones_frame_updated = g_prev.frame_index;

	for( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		Matrix3x4_Copy( current_frame->bones_worldtransform[i], bones_transforms[i] );
	}
}

void R_PrevFrame_SaveCurrentState( int entity_id, matrix4x4 model_transform )
{
	prev_state_t* current_frame = CURRENT_FRAME();

	if (current_frame->frame_updated == g_prev.frame_index)
		return; // already updated for this entity

	Matrix4x4_Copy( current_frame->model_transform, model_transform );
	current_frame->time = gpGlobals->time;
	current_frame->frame_updated = g_prev.frame_index;
}

matrix3x4* R_PrevFrame_BoneTransforms( int entity_id )
{
	prev_state_t* prev_frame = PREV_FRAME();

	// fallback to current frame if previous is outdated
	if (prev_frame->bones_frame_updated != g_prev.frame_index - 1)
		return CURRENT_FRAME()->bones_worldtransform;

	return prev_frame->bones_worldtransform;
}

void R_PrevFrame_ModelTransform( int entity_id, matrix4x4 model_matrix )
{
	prev_state_t* prev_frame = PREV_FRAME();

	// fallback to current frame if previous is outdated
	if (prev_frame->frame_updated != g_prev.frame_index - 1)
		prev_frame = CURRENT_FRAME();

	Matrix4x4_Copy(model_matrix, prev_frame->model_transform);
}

float R_PrevFrame_Time( int entity_id )
{
	prev_state_t* prev_frame = PREV_FRAME();

	// fallback to current frame if previous is outdated
	if (prev_frame->frame_updated != g_prev.frame_index - 1)
		return gpGlobals->time;

	return prev_frame->time;
}
