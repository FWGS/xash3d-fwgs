/*
android_nosdl.c - android backend
Copyright (C) 2016-2019 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform/platform.h"

#if !defined(XASH_DEDICATED)

#include "input.h"
#include "client.h"
#include "sound.h"
#include "errno.h"
#include <pthread.h>
#include <sys/prctl.h>

#include <android/log.h>
#include <jni.h>
#include <SDL.h>

struct jnimethods_s
{
	JNIEnv *env;
	jobject activity;
	jclass actcls;
	jmethodID getID;
	jmethodID saveID;
	jmethodID loadID;
	jmethodID getKeyboardHeight;
} jni;

void Android_Init( void )
{
	jni.env = (JNIEnv *)SDL_AndroidGetJNIEnv();
	jni.activity = (jobject)SDL_AndroidGetActivity();
	jni.actcls = (*jni.env)->GetObjectClass( jni.env, jni.activity );
	jni.loadID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "loadAndroidID", "()Ljava/lang/String;" );
	jni.getID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "getAndroidID", "()Ljava/lang/String;" );
	jni.saveID = (*jni.env)->GetMethodID( jni.env, jni.actcls, "saveAndroidID", "(Ljava/lang/String;)V" );
	jni.getKeyboardHeight = (*jni.env)->GetMethodID( jni.env, jni.actcls, "getKeyboardHeight", "()I" );

	SDL_SetHint( SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight" );
	SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1" );
	SDL_SetHint( SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0" );
	SDL_SetHint( SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO, "0" );
	SDL_SetHint( SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1" );
}

/*
========================
Android_GetNativeObject
========================
*/

void *Android_GetNativeObject( const char *name )
{
	if( !strcasecmp( name, "JNIEnv" ) )
	{
		return (void *)jni.env;
	}
	else if( !strcasecmp( name, "ActivityClass" ) )
	{
		return (void *)jni.actcls;
	}

	return NULL;
}

/*
========================
Android_GetAndroidID
========================
*/
const char *Android_GetAndroidID( void )
{
	static char id[32];
	jstring resultJNIStr;
	const char *resultCStr;

	if( COM_CheckString( id ) ) return id;

	resultJNIStr = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.getID );
	resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, sizeof( id ) );
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );

	return id;
}

/*
========================
Android_LoadID
========================
*/
const char *Android_LoadID( void )
{
	static char id[32];
	jstring resultJNIStr;
	const char *resultCStr;

	resultJNIStr = (*jni.env)->CallObjectMethod( jni.env, jni.activity, jni.loadID );
	resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, sizeof( id ) );
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );

	return id;
}

/*
========================
Android_SaveID
========================
*/
void Android_SaveID( const char *id )
{
	(*jni.env)->CallVoidMethod( jni.env, jni.activity, jni.saveID, (*jni.env)->NewStringUTF( jni.env, id ) );
}

/*
========================
Android_ShellExecute
========================
*/
void Platform_ShellExecute( const char *path, const char *parms )
{
#if SDL_VERSION_ATLEAST( 2, 0, 14 )
	SDL_OpenURL( path );
#endif
}

/*
========================
Android_GetKeyboardHeight
========================
*/
int Android_GetKeyboardHeight( void )
{
	return (*jni.env)->CallIntMethod( jni.env, jni.activity, jni.getKeyboardHeight );
}

#endif // XASH_DEDICATED
