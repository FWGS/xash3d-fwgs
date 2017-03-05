/*
gpgs_support.cpp -- a Google Play Games support library
Copyright (C) 2016 a1batross
*/

#include <jni.h>
#include <android/log.h>

#if defined(GOOGLE_PLAY_BUILD)
#include <gpg/game_services.h>
#include <gpg/android_initialization.h>
using gpg::GameServices;
#else
typedef void *GameServices;
#endif

GameServices *services = nullptr;

extern "C"
{
// After construction of GameServices, pass pointer to this function
// GameServices is recommended to be created in client.dll
void SetGameServicesPtr( GameServices *ptr );

// Share a pointer to external code, like server library or menu.
GameServices *GetGameServicesPtr( void );
}

void SetGameServicesPtr( GameServices *ptr )
{
#ifdef GOOGLE_PLAY_BUILD
	static bool once;
	
	if( once ) 
	{
		__android_log_print( ANDROID_LOG_ERROR, "GPGSSupport", "To prevent overwriting of GameServices pointer, setting pointer twice is prohibited" );
		return;
	}
	
	services = ptr;
	once = true;
#else
	__android_log_print( ANDROID_LOG_WARN, "GPGSSupport", "Not a Google Play build of engine!" );
#endif
}

GameServices *GetGameServicesPtr( void )
{
#ifdef GOOGLE_PLAY_BUILD
	return services;
#else
	return nullptr;
#endif
}

extern "C" __attribute__((visibility("default"))) jint JNI_OnLoad( JavaVM *vm, void *reserved )
{
#ifdef GOOGLE_PLAY_BUILD
	gpg::AndroidInitialization::JNI_OnLoad( vm );

	__android_log_print( ANDROID_LOG_VERBOSE, "GPGSSupport", "%s", __PRETTY_FUNCTION__ );
#endif

	return JNI_VERSION_1_6;
}
