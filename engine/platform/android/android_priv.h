#pragma once
#ifndef ANDROID_PRIV_H
#define ANDROID_PRIV_H

#include <EGL/egl.h>
#include <android/log.h>
#include <jni.h>
#include <sys/prctl.h>

extern struct jnimethods_s
{
	jclass actcls;
	JavaVM *vm;
	JNIEnv *env;
	jmethodID enableTextInput;
	jmethodID vibrate;
	jmethodID messageBox;
	jmethodID notify;
	jmethodID setTitle;
	jmethodID setIcon;
	jmethodID getAndroidId;
	jmethodID saveID;
	jmethodID loadID;
	jmethodID showMouse;
	jmethodID shellExecute;
	jmethodID swapBuffers;
	jmethodID toggleEGL;
	jmethodID createGLContext;
	jmethodID getGLAttribute;
	jmethodID deleteGLContext;
	jmethodID getSelectedPixelFormat;
	int width, height;
} jni;

extern struct nativeegl_s
{
	qboolean valid;
	void *window;
	EGLDisplay dpy;
	EGLSurface surface;
	EGLContext context;
	EGLConfig cfg;
	EGLint numCfg;

	const char *extensions;
} negl;

extern struct jnimouse_s
{
	float x, y;
} jnimouse;

//
// vid_android.c
//
void Android_UpdateSurface( void );

#endif // ANDROID_PRIV_H
