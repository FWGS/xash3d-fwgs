#include "VrFramebuffer.h"
#include "VrMath.h"

#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>
#include <malloc.h>
#include <string.h>

#if !defined(_WIN32)
#include <pthread.h>
#endif

void GLCheckErrors(const char* file, int line) {
    for (int i = 0; i < 10; i++) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        ALOGE("GL error on line %s:%d %d", file, line, error);
    }
}

typedef void (GL_APIENTRYP PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)(
        GLenum target,
        GLsizei samples,
        GLenum internalformat,
        GLsizei width,
        GLsizei height);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)(
        GLenum target,
        GLenum attachment,
        GLenum textarget,
        GLuint texture,
        GLint level,
        GLsizei samples);

typedef void(GL_APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(
		GLenum target,
		GLenum attachment,
		GLuint texture,
		GLint level,
		GLint baseViewIndex,
		GLsizei numViews);

typedef void(GL_APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(
        GLenum target,
        GLenum attachment,
        GLuint texture,
        GLint level,
        GLsizei samples,
        GLint baseViewIndex,
        GLsizei numViews);

/*
================================================================================

ovrFramebuffer

================================================================================
*/


void ovrFramebuffer_Clear(ovrFramebuffer* frameBuffer) {
	frameBuffer->Width = 0;
	frameBuffer->Height = 0;
	frameBuffer->Multisamples = 0;
	frameBuffer->TextureSwapChainLength = 0;
	frameBuffer->TextureSwapChainIndex = 0;
	frameBuffer->ColorSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->ColorSwapChain.Width = 0;
	frameBuffer->ColorSwapChain.Height = 0;
	frameBuffer->ColorSwapChainImage = NULL;
	frameBuffer->DepthBuffers = NULL;
	frameBuffer->FrameBuffers = NULL;
}

bool ovrFramebuffer_Create(
		XrSession session,
		ovrFramebuffer* frameBuffer,
		const bool useMultiview,
		const int width,
		const int height,
		const int multisamples) {

	frameBuffer->Width = width;
	frameBuffer->Height = height;
	frameBuffer->Multisamples = multisamples;
	frameBuffer->UseMultiview = useMultiview;

	PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT =
			(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)eglGetProcAddress(
					"glRenderbufferStorageMultisampleEXT");
	PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT =
			(PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)eglGetProcAddress(
					"glFramebufferTexture2DMultisampleEXT");

	PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR =
			(PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)eglGetProcAddress(
					"glFramebufferTextureMultiviewOVR");
	PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR =
			(PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)eglGetProcAddress(
					"glFramebufferTextureMultisampleMultiviewOVR");

	XrSwapchainCreateInfo swapChainCreateInfo;
	memset(&swapChainCreateInfo, 0, sizeof(swapChainCreateInfo));
	swapChainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	swapChainCreateInfo.sampleCount = multisamples;
	swapChainCreateInfo.width = width;
	swapChainCreateInfo.height = height;
	swapChainCreateInfo.faceCount = 1;
	swapChainCreateInfo.arraySize = useMultiview ? 2 : 1;
	swapChainCreateInfo.mipCount = 1;

	frameBuffer->ColorSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->ColorSwapChain.Height = swapChainCreateInfo.height;

	// Create the color swapchain.
	swapChainCreateInfo.format = GL_SRGB8_ALPHA8;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->ColorSwapChain.Handle));

	// Get the number of swapchain images.
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle, 0, &frameBuffer->TextureSwapChainLength, NULL));

	// Allocate the swapchain images array.
	frameBuffer->ColorSwapChainImage = (XrSwapchainImageOpenGLESKHR*)malloc(
			frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageOpenGLESKHR));

	// Populate the swapchain image array.
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		frameBuffer->ColorSwapChainImage[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
		frameBuffer->ColorSwapChainImage[i].next = NULL;
	}
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->ColorSwapChainImage));

	frameBuffer->DepthBuffers =
			(GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));
	frameBuffer->FrameBuffers =
			(GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));

	ALOGV("		frameBuffer->UseMultiview = %d", frameBuffer->UseMultiview);

	for (int i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		// Create the color buffer texture.
		const GLuint colorTexture = frameBuffer->ColorSwapChainImage[i].image;
		GLenum colorTextureTarget = frameBuffer->UseMultiview ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
		GL(glBindTexture(colorTextureTarget, colorTexture));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
		GLfloat borderColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
		GL(glTexParameterfv(colorTextureTarget, GL_TEXTURE_BORDER_COLOR, borderColor));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(glBindTexture(colorTextureTarget, 0));

		if (frameBuffer->UseMultiview) {
			// Create the depth buffer texture.
			GL(glGenTextures(1, &frameBuffer->DepthBuffers[i]));
			GL(glBindTexture(GL_TEXTURE_2D_ARRAY, frameBuffer->DepthBuffers[i]));
			GL(glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH24_STENCIL8, width, height, 2));
			GL(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

			// Create the frame buffer.
			GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
			GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
			if (multisamples > 1 && (glFramebufferTextureMultisampleMultiviewOVR != NULL)) {
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						colorTexture,
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
			} else {
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						colorTexture,
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
			}

			GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
			GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
			if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
				ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
				return false;
			}
		} else {
			if (multisamples > 1 && glRenderbufferStorageMultisampleEXT != NULL &&
				glFramebufferTexture2DMultisampleEXT != NULL) {
				// Create multisampled depth buffer.
				GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
				GL(glRenderbufferStorageMultisampleEXT(
						GL_RENDERBUFFER, multisamples, GL_DEPTH24_STENCIL8, width, height));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

				// Create the frame buffer.
				// NOTE: glFramebufferTexture2DMultisampleEXT only works with GL_FRAMEBUFFER.
				GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
				GL(glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
				GL(glFramebufferTexture2DMultisampleEXT(
						GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D,
						colorTexture,
						0,
						multisamples));
				GL(glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER));
				GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
				if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
					ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
					return false;
				}
			} else {
				// Create depth buffer.
				GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
				GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

				// Create the frame buffer.
				GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
				GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferTexture2D(
						GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0));
				GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
				GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
				if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
					ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
					return false;
				}
			}
		}
	}

	return true;
}

void ovrFramebuffer_Destroy(ovrFramebuffer* frameBuffer) {
	GL(glDeleteFramebuffers(frameBuffer->TextureSwapChainLength, frameBuffer->FrameBuffers));
	OXR(xrDestroySwapchain(frameBuffer->ColorSwapChain.Handle));
	free(frameBuffer->ColorSwapChainImage);
	if (frameBuffer->UseMultiview) {
		GL(glDeleteTextures(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
	} else {
		GL(glDeleteRenderbuffers(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
	}
	free(frameBuffer->DepthBuffers);
	free(frameBuffer->FrameBuffers);

	ovrFramebuffer_Clear(frameBuffer);
}

void ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer) {
	GL(glBindFramebuffer(
			GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]));

	//This is a bit of a hack, but we need to do this to correct for the fact that the engine uses linear RGB colorspace
	//but openxr uses SRGB (or something, must admit I don't really understand, but adding this works to make it look good again)
	glDisable( GL_FRAMEBUFFER_SRGB_EXT );
}

void ovrFramebuffer_SetNone( void ) {
	GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
}

void ovrFramebuffer_Resolve(ovrFramebuffer* frameBuffer) {
	// Discard the depth buffer, so the tiler won't need to write it back out to memory.
	const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
	glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);
}

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer) {
	// Acquire the swapchain image
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
	OXR(xrAcquireSwapchainImage(
			frameBuffer->ColorSwapChain.Handle, &acquireInfo, &frameBuffer->TextureSwapChainIndex));

	XrSwapchainImageWaitInfo waitInfo;
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	waitInfo.next = NULL;
	waitInfo.timeout = XR_INFINITE_DURATION;
	OXR(xrWaitSwapchainImage(frameBuffer->ColorSwapChain.Handle, &waitInfo));
}

void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer) {
	XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, NULL};
	OXR(xrReleaseSwapchainImage(frameBuffer->ColorSwapChain.Handle, &releaseInfo));
}

/*
================================================================================

ovrRenderer

================================================================================
*/

void ovrRenderer_Clear(ovrRenderer* renderer) {
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		ovrFramebuffer_Clear(&renderer->FrameBuffer[eye]);
	}
}

void ovrRenderer_Create(
		XrSession session,
		ovrRenderer* renderer,
		bool useMultiview,
		int suggestedEyeTextureWidth,
		int suggestedEyeTextureHeight,
		int multisamples) {
	// Create the frame buffers.
	renderer->Multiview = useMultiview;
	int count = renderer->Multiview ? 1 : ovrMaxNumEyes;
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		ovrFramebuffer_Create(
				session,
				&renderer->FrameBuffer[eye],
				useMultiview,
				suggestedEyeTextureWidth,
				suggestedEyeTextureHeight,
				multisamples);
	}
}

void ovrRenderer_Destroy(ovrRenderer* renderer) {
	int count = renderer->Multiview ? 1 : ovrMaxNumEyes;
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		ovrFramebuffer_Destroy(&renderer->FrameBuffer[eye]);
	}
}

void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int sx, int sy) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glEnable(GL_SCISSOR_TEST));
	GL(glScissor(x, y, sx, sy));
	GL(glViewport(x, y, sx, sy));
	GL(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GL(glClear(GL_COLOR_BUFFER_BIT));
	GL(glDisable(GL_SCISSOR_TEST));
#endif
}

/*
================================================================================

ovrApp

================================================================================
*/

void ovrApp_Clear(ovrApp* app) {
	app->Focused = false;
	app->Instance = XR_NULL_HANDLE;
	app->Session = XR_NULL_HANDLE;
	memset(&app->ViewportConfig, 0, sizeof(XrViewConfigurationProperties));
	memset(&app->ViewConfigurationView, 0, ovrMaxNumEyes * sizeof(XrViewConfigurationView));
	app->SystemId = XR_NULL_SYSTEM_ID;
	app->HeadSpace = XR_NULL_HANDLE;
	app->StageSpace = XR_NULL_HANDLE;
	app->FakeStageSpace = XR_NULL_HANDLE;
	app->CurrentSpace = XR_NULL_HANDLE;
	app->SessionActive = false;
	app->SwapInterval = 1;
	app->MainThreadTid = 0;
	app->RenderThreadTid = 0;

	ovrRenderer_Clear(&app->Renderer);
}

void ovrApp_Destroy(ovrApp* app) {
	ovrApp_Clear(app);
}


void ovrApp_HandleSessionStateChanges(ovrApp* app, XrSessionState state) {
	if (state == XR_SESSION_STATE_READY) {
		XrSessionBeginInfo sessionBeginInfo;
		memset(&sessionBeginInfo, 0, sizeof(sessionBeginInfo));
		sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		sessionBeginInfo.next = NULL;
		sessionBeginInfo.primaryViewConfigurationType = app->ViewportConfig.viewConfigurationType;

		XrResult result;
		OXR(result = xrBeginSession(app->Session, &sessionBeginInfo));
		app->SessionActive = (result == XR_SUCCESS);
	} else if (state == XR_SESSION_STATE_STOPPING) {
		OXR(xrEndSession(app->Session));
		app->SessionActive = false;
	}
}

int ovrApp_HandleXrEvents(ovrApp* app) {
	XrEventDataBuffer eventDataBuffer = {};
	int recenter = 0;

	// Poll for events
	for (;;) {
		XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&eventDataBuffer);
		baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
		baseEventHeader->next = NULL;
		XrResult r;
		OXR(r = xrPollEvent(app->Instance, &eventDataBuffer));
		if (r != XR_SUCCESS) {
			break;
		}

		switch (baseEventHeader->type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event");
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				const XrEventDataInstanceLossPending* instance_loss_pending_event =
						(XrEventDataInstanceLossPending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event: time %f",
						FromXrTime(instance_loss_pending_event->lossTime));
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event");
				break;
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				const XrEventDataPerfSettingsEXT* perf_settings_event =
						(XrEventDataPerfSettingsEXT*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
						perf_settings_event->type,
						perf_settings_event->subDomain,
						perf_settings_event->fromLevel,
						perf_settings_event->toLevel);
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* ref_space_change_event =
						(XrEventDataReferenceSpaceChangePending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event: changed space: %d for session %p at time %f",
						ref_space_change_event->referenceSpaceType,
						(void*)ref_space_change_event->session,
						FromXrTime(ref_space_change_event->changeTime));
				recenter = 1;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged* session_state_changed_event =
						(XrEventDataSessionStateChanged*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d for session %p at time %f",
						session_state_changed_event->state,
						(void*)session_state_changed_event->session,
						FromXrTime(session_state_changed_event->time));

				switch (session_state_changed_event->state) {
					case XR_SESSION_STATE_FOCUSED:
						app->Focused = true;
						break;
					case XR_SESSION_STATE_VISIBLE:
						app->Focused = false;
						break;
					case XR_SESSION_STATE_READY:
					case XR_SESSION_STATE_STOPPING:
						ovrApp_HandleSessionStateChanges(app, session_state_changed_event->state);
						break;
					default:
						break;
				}
			} break;
			default:
				ALOGV("xrPollEvent: Unknown event");
				break;
		}
	}
	return recenter;
}
