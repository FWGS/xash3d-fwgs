#include "vk_cvar.h"
#include "vk_common.h"
#include "vk_render.h"

#define NONEXTERN_CVAR(cvar) cvar_t *cvar;
DECLARE_CVAR(NONEXTERN_CVAR)
#undef NONEXTERN_CVAR

static cvar_t *r_drawentities;

void VK_LoadCvars( void )
{
	r_lighting_modulate = gEngine.Cvar_Get( "r_lighting_modulate", "0.6", FCVAR_ARCHIVE, "lightstyles modulate scale" );
	cl_lightstyle_lerping = gEngine.pfnGetCvarPointer( "cl_lightstyle_lerping", 0 );
	r_drawentities = gEngine.pfnGetCvarPointer( "r_drawentities", 0 );
	vk_rtx_bounces = gEngine.Cvar_Get( "vk_rtx_bounces", "2", FCVAR_ARCHIVE, "RTX path tracing ray bounces" );
	vk_rtx_prev_frame_blend_factor = gEngine.Cvar_Get("vk_rtx_prev_frame_blend_factor", "0.1", FCVAR_ARCHIVE, "RTX path tracer ghetto temporal denoiser strength");

	vk_rtx_light_begin = gEngine.Cvar_Get( "vk_rtx_light_begin", "0", 0, "DEBUG: disable lights with index lower than this");
	vk_rtx_light_end = gEngine.Cvar_Get( "vk_rtx_light_end", "0", 0, "DEBUG: disable lights with index higher than this ");
	r_lightmap = gEngine.Cvar_Get( "r_lightmap", "0", FCVAR_CHEAT, "lightmap debugging tool" );
	ui_infotool = gEngine.Cvar_Get( "ui_infotool", "0", FCVAR_CHEAT, "DEBUG: print entity info under crosshair" );
	vk_rtx_extension = gEngine.Cvar_Get( "vk_rtx_extension", vk_core.rtx ? "1" : "0", FCVAR_READ_ONLY, "" );
	if (vk_core.rtx) {
		vk_rtx = gEngine.Cvar_Get( "vk_rtx", "1", FCVAR_GLCONFIG, "Enable or disable Ray Tracing mode" );
	} else {
		vk_rtx = gEngine.Cvar_Get( "vk_rtx", "0", FCVAR_READ_ONLY, "DISABLED: not supported without -rtx" );
	}
}
