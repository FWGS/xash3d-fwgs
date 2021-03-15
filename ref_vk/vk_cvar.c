#include "vk_cvar.h"
#include "vk_common.h"

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
}
