#include "vk_renderstate.h"

#include "vk_core.h"

#include "cvardef.h"
#include "const.h"
#include "ref_api.h"
#include "com_strings.h"
#include "eiface.h" // ARRAYSIZE

render_state_t vk_renderstate = {0};

static const char *renderModeName(int mode)
{
	switch(mode)
	{
		case kRenderNormal: return "kRenderNormal";
		case kRenderTransColor: return "kRenderTransColor";
		case kRenderTransTexture: return "kRenderTransTexture";
		case kRenderGlow: return "kRenderGlow";
		case kRenderTransAlpha: return "kRenderTransAlpha";
		case kRenderTransAdd: return "kRenderTransAdd";
		default: return "INVALID";
	}
}

void GL_SetRenderMode( int renderMode )
{
	vk_renderstate.blending_mode = renderMode;
}

void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	vk_renderstate.tri_color = (color_rgba8_t){r, g, b, a};
}

void R_AllowFog( qboolean allow )
{
	vk_renderstate.fog_allowed = allow;
}

void R_Set2DMode( qboolean enable )
{
	vk_renderstate.mode_2d = enable;
}

