#pragma once

#include "VrFramebuffer.h"
#include "VrMath.h"

enum VRConfig {
	//switching between 2D and 3D
	VR_CONFIG_MODE, VR_CONFIG_NEED_RECENTER,
	//mouse cursor
	VR_CONFIG_MOUSE_SIZE, VR_CONFIG_MOUSE_X, VR_CONFIG_MOUSE_Y,
	//viewport setup
	VR_CONFIG_VIEWPORT_WIDTH, VR_CONFIG_VIEWPORT_HEIGHT, VR_CONFIG_VIEWPORT_VALID, VR_CONFIG_VIEWPORT_MSAA,

	//end
	VR_CONFIG_MAX
};

enum VRConfigFloat {
	// 2D canvas positioning
	VR_CONFIG_CANVAS_DISTANCE, VR_CONFIG_MENU_YAW, VR_CONFIG_RECENTER_YAW,
	VR_CONFIG_CANVAS_ASPECT,

	// Viewport setup
	VR_CONFIG_VIEWPORT_FOVX, VR_CONFIG_VIEWPORT_FOVY, VR_CONFIG_VIEWPORT_SUPERSAMPLING,

	VR_CONFIG_FLOAT_MAX
};

enum VRMode {
	VR_MODE_MONO_SCREEN,
	VR_MODE_STEREO_SCREEN,
	VR_MODE_MONO_6DOF,
	VR_MODE_STEREO_6DOF
};

void VR_GetResolution( engine_t* engine, int *pWidth, int *pHeight );
void VR_InitRenderer( engine_t* engine, bool multiview );
void VR_DestroyRenderer( engine_t* engine );

bool VR_InitFrame( engine_t* engine );
void VR_BeginFrame( engine_t* engine, int fboIndex );
void VR_EndFrame( engine_t* engine, int fboIndex );
void VR_FinishFrame( engine_t* engine );

int VR_GetConfig( enum VRConfig config );
void VR_SetConfig( enum VRConfig config, int value);
float VR_GetConfigFloat( enum VRConfigFloat config );
void VR_SetConfigFloat( enum VRConfigFloat config, float value );

void VR_BindFramebuffer(engine_t *engine);
XrPosef VR_GetView(int eye);
int VR_GetRefreshRate();
void VR_SetRefreshRate(int refresh);
