#pragma once

#include "xash3d_types.h"

typedef struct vk_global_camera_s {
	vec3_t vieworg; // locked vieworigin
	vec3_t viewangles;
	vec3_t vforward;
	vec3_t vright;
	vec3_t vup;

	float fov_x, fov_y; // current view fov

	int viewport[4];
	//gl_frustum_t frustum;

	matrix4x4 objectMatrix; // currententity matrix
	matrix4x4 worldviewMatrix; // modelview for world
	matrix4x4 modelviewMatrix; // worldviewMatrix * objectMatrix

	matrix4x4 projectionMatrix;
	matrix4x4 worldviewProjectionMatrix; // worldviewMatrix * projectionMatrix
} vk_global_camera_t;

extern vk_global_camera_t g_camera;

struct ref_viewpass_s;

void R_SetupCamera( const struct ref_viewpass_s *rvp );

int R_WorldToScreen( const vec3_t point, vec3_t screen );
int TriWorldToScreen( const float *world, float *screen );

