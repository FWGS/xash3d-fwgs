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
#include "platform/android/android_priv.h"
#include "errno.h"
#include <pthread.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#ifndef JNICALL
#define JNICALL // a1ba: workaround for my IDE, where Java files are not included
#define JNIEXPORT
#endif

convar_t *android_sleep;

static const int s_android_scantokey[] =
{
	0,				K_LEFTARROW,	K_RIGHTARROW,	K_AUX26,		K_ESCAPE,		// 0
	K_AUX26,		K_AUX25,		'0',			'1',			'2',			// 5
	'3',			'4',			'5',			'6',			'7',			// 10
	'8',			'9',			'*',			'#',			K_UPARROW,		// 15
	K_DOWNARROW,	K_LEFTARROW,	K_RIGHTARROW,	K_ENTER,		K_AUX32,		// 20
	K_AUX31,		K_AUX29,		K_AUX28,		K_AUX27,		'a',			// 25
	'b',			'c',			'd',			'e',			'f',			// 30
	'g',			'h',			'i',			'j',			'k',			// 35
	'l',			'm',			'n',			'o',			'p',			// 40
	'q',			'r',			's',			't',			'u',			// 45
	'v',			'w',			'x',			'y',			'z',			// 50
	',',			'.',			K_ALT,			K_ALT,			K_SHIFT,		// 55
	K_SHIFT,		K_TAB,			K_SPACE,		0,				0,				// 60
	0,				K_ENTER,		K_BACKSPACE,	'`',			'-',			// 65
	'=',			'[',			']',			'\\',			';',			// 70
	'\'',			'/',			'@',			K_KP_NUMLOCK,	0,				// 75
	0,				'+',			'`',			0,				0,				// 80
	0,				0,				0,				0,				0,				// 85
	0,				0,				K_PGUP,			K_PGDN,			0,				// 90
	0,				K_AUX1,			K_AUX2,			K_AUX14,		K_AUX3,			// 95
	K_AUX4,			K_AUX15,		K_AUX6,			K_AUX7,			K_JOY1,			// 100
	K_JOY2,			K_AUX10,		K_AUX11,		K_ESCAPE,		K_ESCAPE,		// 105
	0,				K_ESCAPE,		K_DEL,			K_CTRL,			K_CTRL,			// 110
	K_CAPSLOCK,		0,				0,				0,				0,				// 115
	0,				K_PAUSE,		K_HOME,			K_END,			K_INS,			// 120
	0,				0,				0,				0,				0,				// 125
	0,				K_F1,			K_F2,			K_F3,			K_F4,			// 130
	K_F5,			K_F6,			K_F7,			K_F8,			K_F9,			// 135
	K_F10,			K_F11,			K_F12,			K_KP_NUMLOCK,		K_KP_INS,			// 140
	K_KP_END,		K_KP_DOWNARROW,	K_KP_PGDN,		K_KP_LEFTARROW,	K_KP_5,			// 145
	K_KP_RIGHTARROW,K_KP_HOME,		K_KP_UPARROW,	K_KP_PGUP,		K_KP_SLASH,		// 150
	0,				K_KP_MINUS,		K_KP_PLUS,		K_KP_DEL,		',',			// 155
	K_KP_ENTER,		'=',			'(',			')'
};

#define ANDROID_MAX_EVENTS 64
#define MAX_FINGERS 10

typedef enum event_type
{
	event_touch_down = 0,
	event_touch_up,
	event_touch_move, // compatible with touchEventType
	event_key_down,
	event_key_up,
	event_set_pause,
	event_resize,
	event_joyhat,
	event_joyball,
	event_joybutton,
	event_joyaxis,
	event_joyadd,
	event_joyremove,
	event_onpause,
	event_ondestroy,
	event_onresume,
	event_onfocuschange,
	event_setwindow,
} eventtype_t;

typedef struct touchevent_s
{
	float x;
	float y;
	float dx;
	float dy;
} touchevent_t;

typedef struct joyball_s
{
	short xrel;
	short yrel;
	byte ball;
} joyball_t;

typedef struct joyhat_s
{
	byte hat;
	byte key;
} joyhat_t;

typedef struct joyaxis_s
{
	short val;
	byte axis;
} joyaxis_t;

typedef struct joybutton_s
{
	int down;
	byte button;
} joybutton_t;

typedef struct keyevent_s
{
	int code;
} keyevent_t;

typedef struct event_s
{
	eventtype_t type;
	int arg;
	union
	{
		touchevent_t touch;
		joyhat_t hat;
		joyball_t ball;
		joyaxis_t axis;
		joybutton_t button;
		keyevent_t key;
		ANativeWindow *window;
	};
} event_t;

typedef struct finger_s
{
	float x, y;
	qboolean down;
} finger_t;

static struct {
	pthread_mutex_t mutex; // this mutex is locked while not running frame, used for events synchronization
	pthread_mutex_t framemutex; // this mutex is locked while engine is running and unlocked while it reading events, used for pause in background.
	event_t queue[ANDROID_MAX_EVENTS];
	volatile int count;
	finger_t fingers[MAX_FINGERS];
	char inputtext[256];
	float mousex, mousey;
} events = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };

struct jnimethods_s jni;
struct nativeegl_s  negl;
struct jnimouse_s   jnimouse;

#define Android_Lock() pthread_mutex_lock(&events.mutex);
#define Android_Unlock() pthread_mutex_unlock(&events.mutex);
#define Android_PushEvent() Android_Unlock()

typedef void (*pfnChangeGame)( const char *progname );
int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );

/*
========================
Android_AllocEvent

Lock event queue and return pointer to next event.
Caller must do Android_PushEvent() to unlock queue after setting parameters.
========================
*/
event_t *Android_AllocEvent( void )
{
	Android_Lock();
	if( events.count == ANDROID_MAX_EVENTS )
	{
		events.count--; //override last event
		__android_log_print( ANDROID_LOG_ERROR, "Xash", "Too many events!!!" );
	}
	return &events.queue[ events.count++ ];
}


/*
=====================================================
JNI callbacks

On application start, setenv and onNativeResize called from
ui thread to set up engine configuration
nativeInit called directly from engine thread and will not return until exit.
These functions may be called from other threads at any time:
nativeKey
nativeTouch
onNativeResize
nativeString
nativeSetPause
=====================================================
*/
#define VA_ARGS(...) , ##__VA_ARGS__ // GCC extension
#define DECLARE_JNI_INTERFACE( ret, name, ... ) \
	JNIEXPORT ret JNICALL Java_su_xash_engine_XashActivity_##name( JNIEnv *env, jclass clazz VA_ARGS(__VA_ARGS__) )

DECLARE_JNI_INTERFACE( int, nativeInit, jobject array )
{
	int i;
	int argc;
	int status;
	/* Prepare the arguments. */

	int len = (*env)->GetArrayLength(env, array);
	char** argv = calloc( 1 + len + 1, sizeof( char ** ));
	argc = 0;
	argv[argc++] = strdup("app_process");
	for (i = 0; i < len; ++i) {
		const char* utf;
		char* arg = NULL;
		jstring string = (*env)->GetObjectArrayElement(env, array, i);
		if (string) {
			utf = (*env)->GetStringUTFChars(env, string, 0);
			if (utf) {
				arg = strdup(utf);
				(*env)->ReleaseStringUTFChars(env, string, utf);
			}
			(*env)->DeleteLocalRef(env, string);
		}
		if (!arg) {
			arg = strdup("");
		}
		argv[argc++] = arg;
	}
	argv[argc] = NULL;
	prctl(PR_SET_DUMPABLE, 1);

	/* Init callbacks. */

	jni.env = env;
	jni.actcls = (*env)->FindClass(env, "su/xash/engine/XashActivity");
	jni.enableTextInput = (*env)->GetStaticMethodID(env, jni.actcls, "showKeyboard", "(I)V");
	jni.vibrate = (*env)->GetStaticMethodID(env, jni.actcls, "vibrate", "(I)V" );
	jni.messageBox = (*env)->GetStaticMethodID(env, jni.actcls, "messageBox", "(Ljava/lang/String;Ljava/lang/String;)V");
	jni.notify = (*env)->GetStaticMethodID(env, jni.actcls, "engineThreadNotify", "()V");
	jni.setTitle = (*env)->GetStaticMethodID(env, jni.actcls, "setTitle", "(Ljava/lang/String;)V");
	jni.setIcon = (*env)->GetStaticMethodID(env, jni.actcls, "setIcon", "(Ljava/lang/String;)V");
	jni.getAndroidId = (*env)->GetStaticMethodID(env, jni.actcls, "getAndroidID", "()Ljava/lang/String;");
	jni.saveID = (*env)->GetStaticMethodID(env, jni.actcls, "saveID", "(Ljava/lang/String;)V");
	jni.loadID = (*env)->GetStaticMethodID(env, jni.actcls, "loadID", "()Ljava/lang/String;");
	jni.showMouse = (*env)->GetStaticMethodID(env, jni.actcls, "showMouse", "(I)V");
	jni.shellExecute = (*env)->GetStaticMethodID(env, jni.actcls, "shellExecute", "(Ljava/lang/String;)V");

	jni.swapBuffers = (*env)->GetStaticMethodID(env, jni.actcls, "swapBuffers", "()V");
	jni.toggleEGL = (*env)->GetStaticMethodID(env, jni.actcls, "toggleEGL", "(I)V");
	jni.createGLContext = (*env)->GetStaticMethodID(env, jni.actcls, "createGLContext", "([I[I)Z");
	jni.getGLAttribute = (*env)->GetStaticMethodID(env, jni.actcls, "getGLAttribute", "(I)I");
	jni.deleteGLContext = (*env)->GetStaticMethodID(env, jni.actcls, "deleteGLContext", "()Z");
	jni.getSelectedPixelFormat = (*env)->GetStaticMethodID(env, jni.actcls, "getSelectedPixelFormat", "()I");

	/* Run the application. */

	status = Host_Main( argc, argv, getenv("XASH3D_GAMEDIR"), false, NULL );

	/* Release the arguments. */

	for (i = 0; i < argc; ++i)
		free(argv[i]);
	free(argv);

	return status;
}

DECLARE_JNI_INTERFACE( void, onNativeResize, jint width, jint height )
{
	event_t *event;

	if( !width || !height )
		return;

	jni.width=width, jni.height=height;

	// alloc update event to change screen size
	event = Android_AllocEvent();
	event->type = event_resize;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeQuit )
{
}

DECLARE_JNI_INTERFACE( void, nativeSetPause, jint pause )
{
	event_t *event = Android_AllocEvent();
	event->type = event_set_pause;
	event->arg = pause;
	Android_PushEvent();

	// if pause enabled, hold engine by locking frame mutex.
	// Engine will stop after event reading and will not continue untill unlock
	if( android_sleep && android_sleep->value )
	{
		if( pause )
			pthread_mutex_lock( &events.framemutex );
		else
			pthread_mutex_unlock( &events.framemutex );
	}
}

DECLARE_JNI_INTERFACE( void, nativeUnPause )
{
	// UnPause engine before sending critical events
	if( android_sleep && android_sleep->value )
			pthread_mutex_unlock( &events.framemutex );
}

DECLARE_JNI_INTERFACE( void, nativeKey, jint down, jint code )
{
	event_t *event;

	if( code < 0 )
	{
		event = Android_AllocEvent();
		event->arg = (-code) & 255;
		event->type = down?event_key_down:event_key_up;
		Android_PushEvent();
	}
	else
	{
		if( code >= ( sizeof( s_android_scantokey ) / sizeof( s_android_scantokey[0] ) ) )
			return;

		event = Android_AllocEvent();
		event->type = down?event_key_down:event_key_up;
		event->arg = s_android_scantokey[code];
		Android_PushEvent();
	}
}

DECLARE_JNI_INTERFACE( void, nativeString, jobject string )
{
	char* str = (char *) (*env)->GetStringUTFChars(env, string, NULL);

	Android_Lock();
	strncat( events.inputtext, str, 256 );
	Android_Unlock();

	(*env)->ReleaseStringUTFChars(env, string, str);
}

#ifdef SOFTFP_LINK
DECLARE_JNI_INTERFACE( void, nativeTouch, jint finger, jint action, jfloat x, jfloat y ) __attribute__((pcs("aapcs")));
#endif
DECLARE_JNI_INTERFACE( void, nativeTouch, jint finger, jint action, jfloat x, jfloat y )
{
	float dx, dy;
	event_t *event;

	// if something wrong with android event
	if( finger > MAX_FINGERS )
		return;

	// not touch action?
	if( !( action >=0 && action <= 2 ) )
		return;

	// 0.0f .. 1.0f
	x /= jni.width;
	y /= jni.height;

	if( action )
		dx = x - events.fingers[finger].x, dy = y - events.fingers[finger].y;
	else
		dx = dy = 0.0f;
	events.fingers[finger].x = x, events.fingers[finger].y = y;

	// check if we should skip some events
	if( ( action == 2 ) && ( !dx && !dy ) )
		return;

	if( ( action == 0 ) && events.fingers[finger].down )
		return;

	if( ( action == 1 ) && !events.fingers[finger].down )
		return;

	if( action == 2 && !events.fingers[finger].down )
			action = 0;

	if( action == 0 )
		events.fingers[finger].down = true;
	else if( action == 1 )
		events.fingers[finger].down = false;

	event = Android_AllocEvent();
	event->arg = finger;
	event->type = action;
	event->touch.x = x;
	event->touch.y = y;
	event->touch.dx = dx;
	event->touch.dy = dy;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeBall, jint id, jbyte ball, jshort xrel, jshort yrel )
{
	event_t *event = Android_AllocEvent();

	event->type = event_joyball;
	event->arg = id;
	event->ball.ball = ball;
	event->ball.xrel = xrel;
	event->ball.yrel = yrel;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeHat, jint id, jbyte hat, jbyte key, jboolean down )
{
	event_t *event = Android_AllocEvent();
	static byte engineKeys;

	if( !key )
		engineKeys = 0; // centered;

	if( down )
		engineKeys |= key;
	else
		engineKeys &= ~key;

	event->type = event_joyhat;
	event->arg = id;
	event->hat.hat = hat;
	event->hat.key = engineKeys;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeAxis, jint id, jbyte axis, jshort val )
{
	event_t *event = Android_AllocEvent();
	event->type = event_joyaxis;
	event->arg = id;
	event->axis.axis = axis;
	event->axis.val = val;

	__android_log_print(ANDROID_LOG_VERBOSE, "Xash", "axis %i %i", axis, val );
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeJoyButton, jint id, jbyte button, jboolean down )
{
	event_t *event = Android_AllocEvent();
	event->type = event_joybutton;
	event->arg = id;
	event->button.button = button;
	event->button.down = down;
	__android_log_print(ANDROID_LOG_VERBOSE, "Xash", "button %i", button );
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeJoyAdd, jint id )
{
	event_t *event = Android_AllocEvent();
	event->type = event_joyadd;
	event->arg = id;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeJoyDel, jint id )
{
	event_t *event = Android_AllocEvent();
	event->type = event_joyremove;
	event->arg = id;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeOnResume )
{
	event_t *event = Android_AllocEvent();
	event->type = event_onresume;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeOnFocusChange )
{
	event_t *event = Android_AllocEvent();
	event->type = event_onfocuschange;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeOnPause )
{
	event_t *event = Android_AllocEvent();
	event->type = event_onpause;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( void, nativeOnDestroy )
{
	event_t *event = Android_AllocEvent();
	event->type = event_ondestroy;
	Android_PushEvent();
}

DECLARE_JNI_INTERFACE( int, setenv, jstring key, jstring value, jboolean overwrite )
{
	char* k = (char *) (*env)->GetStringUTFChars(env, key, NULL);
	char* v = (char *) (*env)->GetStringUTFChars(env, value, NULL);
	int err = setenv(k, v, overwrite);
	(*env)->ReleaseStringUTFChars(env, key, k);
	(*env)->ReleaseStringUTFChars(env, value, v);
	return err;
}


DECLARE_JNI_INTERFACE( void, nativeMouseMove, jfloat x, jfloat y )
{
	Android_Lock();
	events.mousex += x;
	events.mousey += y;
	Android_Unlock();
}

DECLARE_JNI_INTERFACE( int, nativeTestWritePermission, jstring jPath )
{
	char *path = (char *)(*env)->GetStringUTFChars(env, jPath, NULL);
	FILE *fd;
	char testFile[PATH_MAX];
	int ret = 0;

	// maybe generate new file everytime?
	Q_snprintf( testFile, PATH_MAX, "%s/.testfile", path );

	__android_log_print( ANDROID_LOG_VERBOSE, "Xash", "nativeTestWritePermission: file=%s", testFile );

	fd = fopen( testFile, "w+" );

	if( fd )
	{
		__android_log_print( ANDROID_LOG_VERBOSE, "Xash", "nativeTestWritePermission: passed" );
		ret = 1;
		fclose( fd );

		remove( testFile );
	}
	else
	{
		__android_log_print( ANDROID_LOG_VERBOSE, "Xash", "nativeTestWritePermission: error=%s", strerror( errno ) );
	}

	(*env)->ReleaseStringUTFChars( env, jPath, path );

	return ret;
}

DECLARE_JNI_INTERFACE( void, nativeSetSurface, jobject surface )
{
	Android_Lock();
	if( surface )
	{
		negl.window = ANativeWindow_fromSurface( env, surface );
	}
	else
	{
		if( negl.window )
		{
			ANativeWindow_release( negl.window );
			negl.window = NULL;
		}
	}
	Android_Unlock();
}

JNIEXPORT jint JNICALL JNI_OnLoad( JavaVM *vm, void *reserved )
{
	return JNI_VERSION_1_6;
}

/*
========================
Android_Init

Initialize android-related cvars
========================
*/
void Android_Init( void )
{
	android_sleep = Cvar_Get( "android_sleep", "1", FCVAR_ARCHIVE, "Enable sleep in background" );
}

/*
========================
Android_EnableTextInput

Show virtual keyboard
========================
*/
void Platform_EnableTextInput( qboolean enable )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.enableTextInput, enable );
}

/*
========================
Android_Vibrate
========================
*/
void Platform_Vibrate( float life, char flags )
{
	if( life )
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.vibrate, (int)life );
}

/*
========================
Android_GetNativeObject
========================
*/
void *Platform_GetNativeObject( const char *objName )
{
	static const char *availObjects[] = { "JNIEnv", "ActivityClass", NULL };
	void *object = NULL;

	if( !objName )
	{
		object = (void*)availObjects;
	}
	else if( !strcasecmp( objName, "JNIEnv" ) )
	{
		object = (void*)jni.env;
	}
	else if( !strcasecmp( objName, "ActivityClass" ) )
	{
		object = (void*)jni.actcls;
	}

	return object;
}

/*
========================
Android_MessageBox

Show messagebox and wait for OK button press
========================
*/
#if XASH_MESSAGEBOX == MSGBOX_ANDROID
void Platform_MessageBox( const char *title, const char *text, qboolean parentMainWindow )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.messageBox, (*jni.env)->NewStringUTF( jni.env, title ), (*jni.env)->NewStringUTF( jni.env ,text ) );
}
#endif // XASH_MESSAGEBOX == MSGBOX_ANDROID

/*
========================
Android_GetAndroidID
========================
*/
const char *Android_GetAndroidID( void )
{
	static char id[65];
	const char *resultCStr;
	jstring resultJNIStr;

	if( id[0] )
		return id;

	resultJNIStr = (jstring)(*jni.env)->CallStaticObjectMethod( jni.env, jni.actcls, jni.getAndroidId );
	resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, 64 );
	(*jni.env)->ReleaseStringUTFChars( jni.env, resultJNIStr, resultCStr );

	if( !id[0] )
		return NULL;

	return id;
}

/*
========================
Android_LoadID
========================
*/
const char *Android_LoadID( void )
{
	static char id[65];
	jstring resultJNIStr = (jstring)(*jni.env)->CallStaticObjectMethod( jni.env, jni.actcls, jni.loadID );
	const char *resultCStr = (*jni.env)->GetStringUTFChars( jni.env, resultJNIStr, NULL );
	Q_strncpy( id, resultCStr, 64 );
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
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.saveID, (*jni.env)->NewStringUTF( jni.env, id ) );
}

/*
========================
Android_MouseMove
========================
*/
void Android_MouseMove( float *x, float *y )
{
	*x = jnimouse.x;
	*y = jnimouse.y;
	jnimouse.x = 0;
	jnimouse.y = 0;
	// Con_Reportf( "Android_MouseMove: %f %f\n", *x, *y );
}

/*
========================
Android_AddMove
========================
*/
void Android_AddMove( float x, float y )
{
	jnimouse.x += x;
	jnimouse.y += y;
}

void Platform_GetMousePos( int *x, int *y )
{
	// stub
}

void Platform_SetMousePos( int x, int y )
{
	// stub
}

int Platform_JoyInit( int numjoy )
{
	// stub
	return 0;
}

/*
========================
Android_ShowMouse
========================
*/
void Android_ShowMouse( qboolean show )
{
	if( m_ignore->value )
		show = true;
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.showMouse, show );
}

/*
========================
Android_ShellExecute
========================
*/
void Platform_ShellExecute( const char *path, const char *parms )
{
	jstring jstr;

	if( !path )
		return; // useless

	// get java.lang.String
	jstr = (*jni.env)->NewStringUTF( jni.env, path );

	// open browser
	(*jni.env)->CallStaticVoidMethod(jni.env, jni.actcls, jni.shellExecute, jstr);

	// no need to free jstr
}

void Platform_GetClipboardText( char *buffer, size_t size )
{
	// stub
	if( size ) buffer[0] = 0;
}

void Platform_SetClipboardText( const char *buffer, size_t size )
{
	// stub
}

void Platform_PreCreateMove( void )
{
	// stub
}

/*
========================
Android_RunEvents

Execute all events from queue
========================
*/
void Platform_RunEvents( void )
{
	int i;

	// enter events read
	Android_Lock();
	pthread_mutex_unlock( &events.framemutex );

	for( i = 0; i < events.count; i++ )
	{
		switch( events.queue[i].type )
		{
		case event_touch_down:
		case event_touch_up:
		case event_touch_move:
			IN_TouchEvent( (touchEventType)events.queue[i].type, events.queue[i].arg,
						   events.queue[i].touch.x, events.queue[i].touch.y,
						   events.queue[i].touch.dx, events.queue[i].touch.dy );
			break;

		case event_key_down:
			Key_Event( events.queue[i].arg, true );

			if( events.queue[i].arg == K_AUX31 || events.queue[i].arg == K_AUX29 )
			{
				host.force_draw_version = true;
				host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			}
			break;
		case event_key_up:
			Key_Event( events.queue[i].arg, false );

			if( events.queue[i].arg == K_AUX31 || events.queue[i].arg == K_AUX29 )
			{
				host.force_draw_version = true;
				host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			}
			break;

		case event_set_pause:
			// destroy EGL surface when hiding application
			if( !events.queue[i].arg )
			{
				host.status = HOST_FRAME;
				SNDDMA_Activate( true );
				(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 1 );
				Android_UpdateSurface();
				SetBits( gl_vsync->flags, FCVAR_CHANGED ); // set swap interval
				host.force_draw_version = true;
				host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			}

			if( events.queue[i].arg )
			{
				host.status = HOST_NOFOCUS;
				SNDDMA_Activate( false );
				(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 0 );
				negl.valid = false;
			}
			break;

		case event_resize:
			// reinitialize EGL and change engine screen size
			if( host.status == HOST_FRAME &&( refState.width != jni.width || refState.height != jni.height ) )
			{
				(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 0 );
				(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 1 );
				Android_UpdateSurface();
				SetBits( gl_vsync->flags, FCVAR_CHANGED ); // set swap interval
				VID_SetMode();
			}
			break;
		case event_joyadd:
			Joy_AddEvent();
			break;
		case event_joyremove:
			Joy_RemoveEvent();
			break;
		case event_joyball:
			if( !Joy_IsActive() )
				Joy_AddEvent();
			Joy_BallMotionEvent( events.queue[i].ball.ball,
				events.queue[i].ball.xrel, events.queue[i].ball.yrel );
			break;
		case event_joyhat:
			if( !Joy_IsActive() )
				Joy_AddEvent();
			Joy_HatMotionEvent( events.queue[i].hat.hat, events.queue[i].hat.key );
			break;
		case event_joyaxis:
			if( !Joy_IsActive() )
				Joy_AddEvent();
			Joy_AxisMotionEvent( events.queue[i].axis.axis, events.queue[i].axis.val );
			break;
		case event_joybutton:
			if( !Joy_IsActive() )
				Joy_AddEvent();
			Joy_ButtonEvent( events.queue[i].button.button, (byte)events.queue[i].button.down );
			break;
		case event_ondestroy:
			//host.skip_configs = true; // skip config save, because engine may be killed during config save
			Sys_Quit();
			(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.notify );
			break;
		case event_onpause:
#ifdef PARANOID_CONFIG_SAVE
			switch( host.status )
			{
			case HOST_INIT:
			case HOST_CRASHED:
			case HOST_ERR_FATAL:
				Con_Reportf( S_WARN "Abnormal host state during onPause (%d), skipping config save!\n", host.status );
				break;
			default:
				// restore all latched cheat cvars
				Cvar_SetCheatState( true );
				Host_WriteConfig();
			}
#endif
			// disable sound during call/screen-off
			SNDDMA_Activate( false );
			host.status = HOST_NOFOCUS;
			// stop blocking UI thread
			(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.notify );

			break;
		case event_onresume:
			// re-enable sound after onPause
			host.status = HOST_FRAME;
			SNDDMA_Activate( true );
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			break;
		case event_onfocuschange:
			host.force_draw_version = true;
			host.force_draw_version_time = host.realtime + FORCE_DRAW_VERSION_TIME;
			break;
		case event_setwindow:
			negl.window = events.queue[i].window;
			break;
		}
	}

	events.count = 0; // no more events

	// text input handled separately to allow unicode symbols
	for( i = 0; events.inputtext[i]; i++ )
	{
		int ch;

		// if engine does not use utf-8, we need to convert it to preferred encoding
		if( !Q_stricmp( cl_charset->string, "utf-8" ) )
			ch = (unsigned char)events.inputtext[i];
		else
			ch = Con_UtfProcessCharForce( (unsigned char)events.inputtext[i] );

		if( !ch ) // utf-8
			continue;

		// some keyboards may send enter as text
		if( ch == '\n' )
		{
			Key_Event( K_ENTER, true );
			Key_Event( K_ENTER, false );
			continue;
		}

		// otherwise just push it by char, text render will decode unicode strings
		CL_CharEvent( ch );
	}
	events.inputtext[0] = 0; // no more text

	jnimouse.x += events.mousex;
	events.mousex = 0;
	jnimouse.y += events.mousey;
	events.mousey = 0;

	//end events read
	Android_Unlock();
	pthread_mutex_lock( &events.framemutex );
}

#endif // XASH_DEDICATED
