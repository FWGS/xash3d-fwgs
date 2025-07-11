#include "VrBase.h"
#include "VrInput.h"
#include "VrRenderer.h"

#include <assert.h>
#include <malloc.h>
#include <string.h>

XrFovf fov;
XrView* projections;
bool initialized = false;
bool recenterCalled = false;
bool stageBoundsDirty = true;
bool stageSupported = false;
int vrConfig[VR_CONFIG_MAX] = {};
float vrConfigFloat[VR_CONFIG_FLOAT_MAX] = {};
PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate = NULL;
PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate = NULL;

void VR_UpdateStageBounds(ovrApp* pappState) {
	XrExtent2Df stageBounds = {};

	XrResult result;
	OXR(result = xrGetReferenceSpaceBoundsRect(pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
	if (result != XR_SUCCESS) {
		stageBounds.width = 1.0f;
		stageBounds.height = 1.0f;

		pappState->CurrentSpace = pappState->FakeStageSpace;
	}
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight) {
	static int width = 0;
	static int height = 0;

	if (engine) {
		// Enumerate the viewport configurations.
		uint32_t viewportConfigTypeCount = 0;
		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance, engine->appState.SystemId, 0, &viewportConfigTypeCount, NULL));

		XrViewConfigurationType* viewportConfigurationTypes =
				(XrViewConfigurationType*)malloc(viewportConfigTypeCount * sizeof(XrViewConfigurationType));

		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance,
				engine->appState.SystemId,
				viewportConfigTypeCount,
				&viewportConfigTypeCount,
				viewportConfigurationTypes));

		ALOGV("Available Viewport Configuration Types: %d", viewportConfigTypeCount);

		for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
			const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];

			ALOGV(
					"Viewport configuration type %d : %s",
					viewportConfigType,
					viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "Selected" : "");

			XrViewConfigurationProperties viewportConfig;
			viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
			OXR(xrGetViewConfigurationProperties(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, &viewportConfig));
			ALOGV(
					"FovMutable=%s ConfigurationType %d",
					viewportConfig.fovMutable ? "true" : "false",
					viewportConfig.viewConfigurationType);

			uint32_t viewCount;
			OXR(xrEnumerateViewConfigurationViews(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, 0, &viewCount, NULL));

			if (viewCount > 0) {
				XrViewConfigurationView* elements =
						(XrViewConfigurationView*)malloc(viewCount * sizeof(XrViewConfigurationView));

				for (uint32_t e = 0; e < viewCount; e++) {
					elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
					elements[e].next = NULL;
				}

				OXR(xrEnumerateViewConfigurationViews(
						engine->appState.Instance,
						engine->appState.SystemId,
						viewportConfigType,
						viewCount,
						&viewCount,
						elements));

				// Cache the view config properties for the selected config type.
				if (viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
					assert(viewCount == ovrMaxNumEyes);
					for (uint32_t e = 0; e < viewCount; e++) {
						engine->appState.ViewConfigurationView[e] = elements[e];
					}
				}

				free(elements);
			} else {
				ALOGE("Empty viewport configuration type: %d", viewCount);
			}
		}

		free(viewportConfigurationTypes);

		*pWidth = width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
		*pHeight = height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
	} else {
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}

	//Apply supersampling
	float supersampling = VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING);
	if (supersampling > 0) {
		*pWidth *= supersampling;
		*pHeight *= supersampling;
	}

	//Force square resolution
	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_SQUARE)) {
		*pHeight = *pWidth;
	}
}

void VR_Recenter(engine_t* engine) {

	// Calculate recenter reference
	XrReferenceSpaceCreateInfo spaceCreateInfo = {};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();
	if (engine->appState.CurrentSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc = {};
		loc.type = XR_TYPE_SPACE_LOCATION;
		OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
		XrVector3f hmdangles = XrQuaternionf_ToEulerAngles(loc.pose.orientation);

		VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW) + hmdangles.y);
		float recenterYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW));
		spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.y = sinf(recenterYaw / 2);
		spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.w = cosf(recenterYaw / 2);
	}

	// Delete previous space instances
	if (engine->appState.StageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.StageSpace));
	}
	if (engine->appState.FakeStageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
	}

	// Create a default stage space to use if SPACE_TYPE_STAGE is not
	// supported, or calls to xrGetReferenceSpaceBoundsRect fail.
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceCreateInfo.poseInReferenceSpace = XrPosef_Identity();
	if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
		spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
	}
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
	ALOGV("Created fake stage space from local space with offset");
	engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

	if (stageSupported) {
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace.position.y = 0.0;
		OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
		ALOGV("Created stage space");
		if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
			engine->appState.CurrentSpace = engine->appState.StageSpace;
		}
	}

	// Update menu orientation
	VR_SetConfigFloat(VR_CONFIG_MENU_YAW, 0.0f);
	stageBoundsDirty = true;
	recenterCalled = true;
}

bool VR_DidRecenter() {
	bool output = recenterCalled;
	recenterCalled = false;
	return output;
}

void VR_InitRenderer( engine_t* engine, bool multiview ) {
	if (initialized) {
		VR_DestroyRenderer(engine);
	}

	int eyeW, eyeH;
	VR_GetResolution(engine, &eyeW, &eyeH);
	VR_SetConfig(VR_CONFIG_VIEWPORT_WIDTH, eyeW);
	VR_SetConfig(VR_CONFIG_VIEWPORT_HEIGHT, eyeH);

	// Get the viewport configuration info for the chosen viewport configuration type.
	engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	OXR(xrGetViewConfigurationProperties(engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

	uint32_t numOutputSpaces = 0;
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));
	XrReferenceSpaceType* referenceSpaces = (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

	for (uint32_t i = 0; i < numOutputSpaces; i++) {
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			stageSupported = true;
			break;
		}
	}

	free(referenceSpaces);

	if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
		VR_Recenter(engine);
	}

	projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		memset(&projections[eye], 0, sizeof(XrView));
		projections[eye].type = XR_TYPE_VIEW;
	}

	int msaa = VR_GetConfig(VR_CONFIG_VIEWPORT_MSAA);
	ovrRenderer_Create(engine->appState.Session, &engine->appState.Renderer, multiview, eyeW, eyeH, msaa > 0 ? msaa : 1);
	initialized = true;
}

void VR_DestroyRenderer( engine_t* engine ) {
	ovrRenderer_Destroy(&engine->appState.Renderer);
	free(projections);
	initialized = false;
}

bool VR_InitFrame( engine_t* engine ) {
	if (ovrApp_HandleXrEvents(&engine->appState)) {
		VR_Recenter(engine);
	}
	if (engine->appState.SessionActive == false) {
		return false;
	}

	if (stageBoundsDirty) {
		VR_UpdateStageBounds(&engine->appState);
		stageBoundsDirty = false;
	}

	XrFrameState frameState = {};
	frameState.type = XR_TYPE_FRAME_STATE;
	frameState.next = NULL;
	OXR(xrWaitFrame(engine->appState.Session, 0, &frameState));
	engine->predictedDisplayTime = frameState.predictedDisplayTime;

	// Update HMD
	XrViewLocateInfo projectionInfo = {};
	projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
	projectionInfo.displayTime = frameState.predictedDisplayTime;
	projectionInfo.space = engine->appState.CurrentSpace;
	XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};
	uint32_t projectionCapacityInput = ovrMaxNumEyes;
	uint32_t projectionCountOutput = projectionCapacityInput;
	OXR(xrLocateViews(
			engine->appState.Session,
			&projectionInfo,
			&viewState,
			projectionCapacityInput,
			&projectionCountOutput,
			projections));

	// Update controllers
	IN_VRInputFrame(engine);

	float fovx = 0;
	float fovy = 0;
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		fovx += fabs(projections[eye].fov.angleDown - projections[eye].fov.angleUp) / 2.0f;
		fovy += fabs(projections[eye].fov.angleRight - projections[eye].fov.angleLeft) / 2.0f;
	}

	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_UNCENTERED)) {
		fovy *= 1.1f;
	}

	if (VR_GetPlatformFlag(VR_PLATFORM_VIEWPORT_SQUARE)) {
		VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVX, ToDegrees(fovy));
		fov.angleLeft = -fovy / 2.0f;
		fov.angleRight = fovy / 2.0f;
	} else {
	   VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVX, ToDegrees(fovx));
	   fov.angleLeft = -fovx / 2.0f;
	   fov.angleRight = fovx / 2.0f;
	}
	VR_SetConfigFloat(VR_CONFIG_VIEWPORT_FOVY, ToDegrees(fovy));
	fov.angleDown = -fovy / 2.0f;
	fov.angleUp = fovy / 2.0f;
	return true;
}

void VR_BeginFrame( engine_t* engine, int fboIndex ) {
	// Get the HMD pose, predicted for the middle of the time period during which
	// the new eye images will be displayed. The number of frames predicted ahead
	// depends on the pipeline depth of the engine and the synthesis rate.
	// The better the prediction, the less black will be pulled in at the edges.
	if (fboIndex == 0) {
		XrFrameBeginInfo beginFrameDesc = {};
		beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
		beginFrameDesc.next = NULL;
		OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));
	}

	ovrFramebuffer_Acquire(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_EndFrame( engine_t* engine, int fboIndex ) {
	VR_BindFramebuffer(engine);

	// Show mouse cursor
	int vrMode = vrConfig[VR_CONFIG_MODE];
	bool screenMode = (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
	if (screenMode && (vrConfig[VR_CONFIG_MOUSE_SIZE] > 0)) {
		int x = vrConfig[VR_CONFIG_MOUSE_X];
		int y = vrConfig[VR_CONFIG_MOUSE_Y];
		int sx = vrConfig[VR_CONFIG_MOUSE_SIZE];
		int sy = (int)((float)sx * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT));
		ovrRenderer_MouseCursor(&engine->appState.Renderer, x, y, sx, sy);
	}

	ovrFramebuffer_Resolve(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_Release(&engine->appState.Renderer.FrameBuffer[fboIndex]);
	ovrFramebuffer_SetNone();
}

void VR_FinishFrame( engine_t* engine ) {
	int layerCount = 0;
	ovrCompositorLayer_Union layerUnion[ovrMaxLayerCount];
	memset(layerUnion, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);

	int vrMode = vrConfig[VR_CONFIG_MODE];
	XrCompositionLayerProjectionView projection_layer_elements[2] = {};
	if ((vrMode == VR_MODE_MONO_6DOF) || (vrMode == VR_MODE_STEREO_6DOF)) {
		VR_SetConfigFloat(VR_CONFIG_MENU_YAW, XrQuaternionf_ToEulerAngles(projections[0].pose.orientation).y);

		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
			int imageLayer = engine->appState.Renderer.Multiview ? eye : 0;
			int framebufferIndex = engine->appState.Renderer.Multiview ? 0 : eye;
			ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[vrMode == VR_MODE_MONO_6DOF ? 0 : framebufferIndex];
			memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
			projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_layer_elements[eye].pose = projections[eye].pose;
			projection_layer_elements[eye].fov = fov;

			memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
			projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
			projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
			projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
			projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
			projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
			projection_layer_elements[eye].subImage.imageArrayIndex = vrMode == VR_MODE_MONO_6DOF ? 0 : imageLayer;
		}

		XrCompositionLayerProjection projection_layer = {};
		projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection_layer.space = engine->appState.CurrentSpace;
		projection_layer.viewCount = ovrMaxNumEyes;
		projection_layer.views = projection_layer_elements;

		layerUnion[layerCount++].Projection = projection_layer;
	} else if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN)) {

		// Flat screen pose
		float distance = VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE);
		float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
		XrVector3f pos = {
				projections[0].pose.position.x - sinf(menuYaw) * distance,
				projections[0].pose.position.y - 1.5f,
				projections[0].pose.position.z - cosf(menuYaw) * distance
		};
		XrVector3f yawAxis = {0, 1, 0};
		XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle(yawAxis, menuYaw);

		// Setup the cylinder layer
		XrCompositionLayerCylinderKHR cylinder_layer = {};
		cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
		cylinder_layer.space = engine->appState.CurrentSpace;
		memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
		cylinder_layer.subImage.imageRect.offset.x = 0;
		cylinder_layer.subImage.imageRect.offset.y = 0;
		cylinder_layer.subImage.imageRect.extent.width = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
		cylinder_layer.subImage.imageRect.extent.height = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
		cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
		cylinder_layer.subImage.imageArrayIndex = 0;
		cylinder_layer.pose.orientation = yaw;
		cylinder_layer.pose.position = pos;
		cylinder_layer.radius = 12.0f;
		cylinder_layer.centralAngle = (float)(M_PI * 0.5);
		cylinder_layer.aspectRatio = VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);

		// Build the cylinder layer
		if (vrMode == VR_MODE_MONO_SCREEN) {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
		} else if (engine->appState.Renderer.Multiview) {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
			cylinder_layer.subImage.imageArrayIndex = 1;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
		} else {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
			cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[1].ColorSwapChain.Handle;
			layerUnion[layerCount++].Cylinder = cylinder_layer;
		}
	} else {
		assert(false);
	}

	// Compose the layers for this frame.
	const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {};
	for (int i = 0; i < layerCount; i++) {
		layers[i] = (const XrCompositionLayerBaseHeader*)&layerUnion[i];
	}

	XrFrameEndInfo endFrameInfo = {};
	endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
	endFrameInfo.displayTime = engine->predictedDisplayTime;
	endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endFrameInfo.layerCount = layerCount;
	endFrameInfo.layers = layers;
	OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));

	if (VR_GetConfig(VR_CONFIG_NEED_RECENTER)) {
		VR_SetConfig(VR_CONFIG_NEED_RECENTER, false);
		VR_Recenter(engine);
	}
}

int VR_GetConfig(enum VRConfig config ) {
	return vrConfig[config];
}

void VR_SetConfig(enum VRConfig config, int value) {
	vrConfig[config] = value;
}

float VR_GetConfigFloat(enum VRConfigFloat config) {
	return vrConfigFloat[config];
}

void VR_SetConfigFloat(enum VRConfigFloat config, float value) {
	vrConfigFloat[config] = value;
}

void VR_BindFramebuffer(engine_t *engine) {
	if (!initialized) return;
	ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer);
}

XrPosef VR_GetView(int eye) {
	return projections[eye].pose;
}

int VR_GetRefreshRate() {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnGetDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrGetDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnGetDisplayRefreshRate)));
		}

		float currentDisplayRefreshRate = 0.0f;
		OXR(pfnGetDisplayRefreshRate(VR_GetEngine()->appState.Session, &currentDisplayRefreshRate));
		return (int)currentDisplayRefreshRate;
	}
	return 72;
}

void VR_SetRefreshRate(int refresh) {
	if (VR_GetPlatformFlag(VR_PLATFORM_EXTENSION_REFRESH)) {
		if (!pfnRequestDisplayRefreshRate) {
			OXR(xrGetInstanceProcAddr(
					VR_GetEngine()->appState.Instance,
					"xrRequestDisplayRefreshRateFB",
					(PFN_xrVoidFunction*)(&pfnRequestDisplayRefreshRate)));
		}
		OXR(pfnRequestDisplayRefreshRate(VR_GetEngine()->appState.Session, 72.0f));
		OXR(pfnRequestDisplayRefreshRate(VR_GetEngine()->appState.Session, (float)refresh));
	}
}
