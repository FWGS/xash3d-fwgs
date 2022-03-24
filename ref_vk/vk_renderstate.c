#include "const.h"
#include "cvardef.h"
#include "ref_api.h"
#include "com_strings.h"
#include <stdint.h>

extern ref_api_t gEngine;
extern ref_globals_t* gGlobals;

typedef struct ColorRGBA8 {
    uint8_t r, g, b, a;
}color_rgba8_t;

/// TODO 
typedef struct render_state_s
{
    color_rgba8_t tri_color;
    qboolean blending_mode;
    qboolean mode_2d;
    qboolean fog_allowed;

}render_state_t;

render_state_t render_state;

static const char* renderModeName(int mode)
{
    #define CASE_STR(s) case s : return #s;
    switch (mode)
    {
        CASE_STR(kRenderNormal)
        CASE_STR(kRenderTransColor)
        CASE_STR(kRenderTransTexture)
        CASE_STR(kRenderGlow)
        CASE_STR(kRenderTransAlpha)
        CASE_STR(kRenderTransAdd)
        default: return "INVALID";
    }
    #undef CASE_STR
}

void R_SetRenderMode( int renderMode )
{
    gEngine.Con_Printf("");
    render_state.blending_mode = renderMode;
}

void TriColor4ub( unsigned char r, unsigned char g, unsigned char b, unsigned char a )
{
    gEngine.Con_Printf("");
    render_state.tri_color = (color_rgba8_t){r, g ,b, a};
}

void R_AllowFog(qboolean allow)
{
    render_state.fog_allowed = allow;
}

void R_Set2DMode(qboolean enable)
{
    render_state.mode_2d = enable;
}