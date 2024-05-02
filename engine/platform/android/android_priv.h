#pragma once
#ifndef ANDROID_PRIV_H
#define ANDROID_PRIV_H

#include <EGL/egl.h>
#include <android/log.h>
#include <jni.h>

extern struct jnimethods_s
{
	jclass actcls;
	jclass bindcls;
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
	jmethodID getSurface;
	jmethodID preShutdown;
	int width, height;
} jni;

typedef enum surfacestate_e
{
	surface_pause,
	surface_active,
	surface_dummy,

} surfacestate_t;


extern struct jnimouse_s
{
	float x, y;
} jnimouse;

//
// vid_android.c
//
void Android_UpdateSurface( surfacestate_t state );

#endif // ANDROID_PRIV_H
