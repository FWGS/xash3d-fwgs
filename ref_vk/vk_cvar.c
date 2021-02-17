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
	r_drawentities = gEngine.Cvar_Get( "r_drawentities", "1", FCVAR_CHEAT, "render entities" );
}
