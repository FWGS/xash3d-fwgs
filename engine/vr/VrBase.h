#pragma once

#include "build.h"

#if XASH_ANDROID
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenXR", __VA_ARGS__);
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "OpenXR", __VA_ARGS__);
#else
#include <stdio.h>
#define ALOGE(...) printf(__VA_ARGS__)
#define ALOGV(...) printf(__VA_ARGS__)
#endif

#include <EGL/egl.h>

#if XASH_ANDROID
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID 1
#endif

#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdbool.h>

#if defined(_DEBUG) && (defined(XR_USE_GRAPHICS_API_OPENGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES))

void GLCheckErrors(const char* file, int line);

#define GL(func) func; GLCheckErrors(__FILE__ , __LINE__);
#else
#define GL(func) func;
#endif

#if defined(_DEBUG)
static void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError) {
	if (XR_FAILED(result)) {
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, errorBuffer);
		if (failOnError) {
			ALOGE("OpenXR error: %s: %s\n", function, errorBuffer);
		} else {
			ALOGV("OpenXR error: %s: %s\n", function, errorBuffer);
		}
	}
}
#define OXR(func) OXR_CheckErrors(VR_GetEngine()->appState.Instance, func, #func, true);
#else
#define OXR(func) func;
#endif

enum { ovrMaxLayerCount = 2 };
enum { ovrMaxNumEyes = 2 };

typedef union {
	XrCompositionLayerProjection Projection;
	XrCompositionLayerCylinderKHR Cylinder;
} ovrCompositorLayer_Union;

typedef struct {
	XrSwapchain Handle;
	uint32_t Width;
	uint32_t Height;
} ovrSwapChain;

typedef struct {
	int Width;
	int Height;
	int Multisamples;
	bool UseMultiview;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
	unsigned int* DepthBuffers;
	unsigned int* FrameBuffers;
} ovrFramebuffer;

typedef struct {
	bool Multiview;
	ovrFramebuffer FrameBuffer[ovrMaxNumEyes];
} ovrRenderer;

typedef struct {
	int Focused;

	XrInstance Instance;
	XrSession Session;
	XrViewConfigurationProperties ViewportConfig;
	XrViewConfigurationView ViewConfigurationView[ovrMaxNumEyes];
	XrSystemId SystemId;
	XrSpace HeadSpace;
	XrSpace StageSpace;
	XrSpace FakeStageSpace;
	XrSpace CurrentSpace;
	int SessionActive;

	int SwapInterval;
	// These threads will be marked as performance threads.
	int MainThreadTid;
	int RenderThreadTid;
	ovrRenderer Renderer;
} ovrApp;

#if XASH_ANDROID
typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
} ovrJava;
#endif

typedef struct {
	uint64_t frameIndex;
	ovrApp appState;
	XrTime predictedDisplayTime;
} engine_t;

enum VRPlatformFlag {
	VR_PLATFORM_CONTROLLER_PICO,
	VR_PLATFORM_CONTROLLER_QUEST,
	VR_PLATFORM_EXTENSION_INSTANCE,
	VR_PLATFORM_EXTENSION_PERFORMANCE,
	VR_PLATFORM_EXTENSION_REFRESH,
	VR_PLATFORM_TRACKING_FLOOR,
	VR_PLATFORM_VIEWPORT_SQUARE,
	VR_PLATFORM_VIEWPORT_UNCENTERED,
	VR_PLATFORM_MAX
};

void VR_Init( void* system, const char* name, int version );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );
bool VR_GetPlatformFlag(enum VRPlatformFlag flag);
void VR_SetPlatformFLag(enum VRPlatformFlag flag, bool value);

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);
