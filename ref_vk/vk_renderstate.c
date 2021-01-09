#include "cvardef.h"
#include "const.h"
#include "ref_api.h"
#include "com_strings.h"

extern ref_api_t gEngine;
extern ref_globals_t *gpGlobals;

typedef struct { uint8_t r, g, b, a; } color_rgba8_t;

typedef struct render_state_s {
	color_rgba8_t tri_color;
	qboolean fog_allowed;
	qboolean mode_2d;
	int blending_mode; // kRenderNormal, ...
} render_state_t;

render_state_t render_state = {0};

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
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%s(%d))\n", __FUNCTION__, renderModeName(renderMode), renderMode);

	render_state.blending_mode = renderMode;
}

void	TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d, %d, %d, %d)\n", __FUNCTION__, (int)r, (int)g, (int)b, (int)a);

	render_state.tri_color = (color_rgba8_t){r, g, b, a};
}

void R_AllowFog( qboolean allow )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d)\n", __FUNCTION__, allow);
	render_state.fog_allowed = allow;
}

void R_Set2DMode( qboolean enable )
{
	//gEngine.Con_Printf(S_WARN "VK FIXME: %s(%d)\n", __FUNCTION__, enable);
	render_state.mode_2d = enable;
}
