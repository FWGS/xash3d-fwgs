#pragma once

#include "cvardef.h"

#define CVAR_TO_BOOL( x )		((x) && ((x)->value != 0.0f) ? true : false )

void VK_LoadCvars( void );

#define DECLARE_CVAR(X) \
	X(r_lighting_modulate) \
	X(cl_lightstyle_lerping) \
	X(vk_rtx_bounces) \
	X(vk_rtx_prev_frame_blend_factor) \
	X(vk_rtx_light_begin) \
	X(vk_rtx_light_end) \
	X(r_lightmap) \

#define EXTERN_CVAR(cvar) extern cvar_t *cvar;
DECLARE_CVAR(EXTERN_CVAR)
#undef EXTERN_CVAR
