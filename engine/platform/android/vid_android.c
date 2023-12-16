#include "platform/platform.h"
#if !XASH_SDL
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "platform/android/android_priv.h"
#include "vid_common.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "eglutil.h"


static struct vid_android_s
{
	qboolean has_context;
	ANativeWindow* window;
	qboolean nativeegl;
} vid_android;

static struct nw_s
{
	void (*release)(ANativeWindow* window);
	int32_t (*getWidth)(ANativeWindow* window);
	int32_t (*getHeight)(ANativeWindow* window);
	int32_t (*getFormat)(ANativeWindow* window);
	int32_t (*setBuffersGeometry)(ANativeWindow* window, int32_t width, int32_t height, int32_t format);
	int32_t (*lock)(ANativeWindow* window, ANativeWindow_Buffer* outBuffer, ARect* inOutDirtyBounds);
	int32_t (*unlockAndPost)(ANativeWindow* window);
	ANativeWindow* (*fromSurface)(JNIEnv* env, jobject surface);
} nw;

#define NW_FF(x) {"ANativeWindow_"#x, (void*)&nw.x}


static dllfunc_t android_funcs[] =
{
	NW_FF(release),
	NW_FF(getWidth),
	NW_FF(getHeight),
	NW_FF(getFormat),
	NW_FF(setBuffersGeometry),
	NW_FF(lock),
	NW_FF(unlockAndPost),
	NW_FF(fromSurface),
	{ NULL, NULL }
};
#undef NW_FF
static dll_info_t android_info = { "libandroid.so", android_funcs, false };

/*
========================
Android_SwapInterval
========================
*/
static void Android_SwapInterval( int interval )
{
	if( vid_android.nativeegl && eglstate.valid )
		egl.SwapInterval( eglstate.dpy, interval );
}

/*
========================
Android_SetTitle
========================
*/
static void Android_SetTitle( const char *title )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.bindcls, jni.setTitle, (*jni.env)->NewStringUTF( jni.env, title ) );
}

/*
========================
Android_SetIcon
========================
*/
static void Android_SetIcon( const char *path )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.bindcls, jni.setIcon, (*jni.env)->NewStringUTF( jni.env, path ) );
}

/*
========================
Android_GetScreenRes

Resolution got from last resize event
========================
*/
static void Android_GetScreenRes( int *width, int *height )
{
	*width = jni.width;
	*height = jni.height;
}

/*
========================
Android_SwapBuffers

Update screen. Use native EGL if possible
========================
*/
void GL_SwapBuffers( void )
{
	if( vid_android.nativeegl && eglstate.valid )
	{
		egl.SwapBuffers( eglstate.dpy, eglstate.surface );
	}
	else
	{
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.bindcls, jni.swapBuffers );
	}
}

/*
========================
Android_UpdateSurface

Check if we may use native EGL without jni calls
========================
*/
void Android_UpdateSurface( surfacestate_t state )
{
	qboolean active = state == surface_active;
	vid_android.nativeegl = false;

	if( glw_state.software || ( eglstate.valid && !eglstate.imported ))
	{
		if( vid_android.window )
		{
			EGL_UpdateSurface( NULL, false );
			nw.release( vid_android.window );
			vid_android.window = NULL;
		}
		if( state == surface_dummy && glw_state.software )
			return;
		// first, ask EGL for surfaceless mode
		if( state == surface_dummy && EGL_UpdateSurface( NULL, true ))
		{
			vid_android.nativeegl = true;
			return;
		}

		if( state != surface_pause )
		{
			EGLint format = WINDOW_FORMAT_RGB_565;
			jobject surf;
			if( vid_android.window )
				nw.release( vid_android.window );
			surf = (*jni.env)->CallStaticObjectMethod( jni.env, jni.bindcls, jni.getSurface, state );
			Con_DPrintf("Surface handle %p\n", surf);
			if( surf )
			{
				vid_android.window = nw.fromSurface(jni.env, surf);
				Con_DPrintf("NativeWindow %p\n", vid_android.window);

				if( eglstate.valid )
					egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_NATIVE_VISUAL_ID, &format );

				nw.setBuffersGeometry(vid_android.window, 0, 0, format );

				(*jni.env)->DeleteLocalRef( jni.env, surf );
			}
		}

		if( eglstate.valid && !eglstate.imported )
		{
			EGL_UpdateSurface( vid_android.window, state == surface_dummy );
			vid_android.nativeegl = true;
		}
		return;
	}

	if( !vid_android.has_context )
		return;

	(*jni.env)->CallStaticVoidMethod( jni.env, jni.bindcls, jni.toggleEGL, (int)state );
	host.status = HOST_FRAME; // active ? HOST_FRAME : HOST_SLEEP;

	// todo: check opengl context here and set HOST_SLEEP if not

	if( !Sys_CheckParm("-nativeegl") || !active )
		return; // enabled by user

	vid_android.nativeegl = EGL_ImportContext();
	if( vid_android.nativeegl )
		Con_DPrintf( "nativeEGL success\n");
}

/*
========================
Android_GetGLAttribute
========================
*/
static int Android_GetGLAttribute( int eglAttr )
{
	int ret = (*jni.env)->CallStaticIntMethod( jni.env, jni.bindcls, jni.getGLAttribute, eglAttr );
	// Con_Reportf( "Android_GetGLAttribute( %i ) => %i\n", eglAttr, ret );
	return ret;
}

qboolean  R_Init_Video( const int type )
{
	char buf[MAX_VA_STRING];
	qboolean retval;

	if( FS_FileExists( GI->iconpath, true ) )
	{
		Q_snprintf( buf, sizeof( buf ), "%s/%s/%s", COM_CheckStringEmpty( host.rodir ) ? host.rodir : host.rootdir, GI->gamefolder, GI->iconpath );
		Android_SetIcon( buf );
	}

	Android_SetTitle( GI->title );

	VID_StartupGamma();

	Sys_LoadLibrary( &android_info );

	switch( type )
	{
	case REF_SOFTWARE:
		glw_state.software = true;
		break;
	case REF_GL:
		glw_state.software = false;
		EGL_LoadLibrary();

		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", buf ) )
			glw_state.safe = bound( SAFE_NO, Q_atoi( buf ), SAFE_DONTCARE );

		break;
	default:
		Host_Error( "Can't initialize unknown context type %d!\n", type );
		break;
	}

	if( glw_state.software )
	{
		uint arg;

		if( !nw.release )
		{
			Con_Reportf( S_ERROR "Native software mode unavailiable\n" );
			return false;
		}
		Android_UpdateSurface( surface_active );
		if( !SW_CreateBuffer( jni.width, jni.height, &arg, &arg, &arg, &arg, &arg ) )
			return false;
	}

	while( !(retval = VID_SetMode()) )
	{
		glw_state.safe++;
		if( glw_state.safe > SAFE_LAST )
			return false;
	}

	switch( type )
	{
	case REF_GL:
		// refdll also can check extensions
		ref.dllFuncs.GL_InitExtensions();
		break;
	case REF_SOFTWARE:
	default:
		break;
	}

	host.renderinfo_changed = false;

	host.status = HOST_FRAME; // where it should we done? We have broken host.status on all non-SDL platforms!
	return true;
}

void R_Free_Video( void )
{
	// (*jni.env)->CallStaticBooleanMethod( jni.env, jni.bindcls, jni.deleteGLContext );

	// VID_DestroyWindow ();

	// R_FreeVideoModes();
	Sys_FreeLibrary( &android_info );
	vid_android.has_context = false;
	ref.dllFuncs.GL_ClearExtensions();
}

void *Android_GetWindow( void )
{
	EGLint format;

	if( !vid_android.window )
	{
		return NULL;
	}

	if( !egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_NATIVE_VISUAL_ID, &format) )
	{
		Con_Reportf( S_ERROR "eglGetConfigAttrib(VISUAL_ID) returned error: 0x%x\n", egl.GetError() );
		return NULL;
	}

	if( nw.setBuffersGeometry( vid_android.window, 0, 0, format ) )
	{
		Con_Reportf( S_ERROR "ANativeWindow_setBuffersGeometry returned error\n" );
		return NULL;
	}

	return vid_android.window;
}


qboolean VID_SetMode( void )
{
	EGLint format;
	jintArray attribs, contextAttribs;
	static EGLint nAttribs[32+1], nContextAttribs[32+1];
	const size_t attribsSize = ARRAYSIZE( nAttribs );
	size_t s1, s2;

	// create context on egl side by user request
	if( !vid_android.has_context && Sys_CheckParm("-egl") )
	{
		vid_android.has_context = vid_android.nativeegl = EGL_CreateContext();

		if( vid_android.has_context )
			Android_UpdateSurface( surface_active );
		else
			return false;
	}

	if( vid_android.has_context || glw_state.software )
	{
		R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_WINDOWED ); // width and height are ignored anyway
		return true;
	}

	s1 = EGL_GenerateConfig(nAttribs, attribsSize);
	s2 = EGL_GenerateContextConfig(nContextAttribs, attribsSize);

	attribs = (*jni.env)->NewIntArray( jni.env, s1 );
	contextAttribs = (*jni.env)->NewIntArray( jni.env, s2 );

	(*jni.env)->SetIntArrayRegion( jni.env, attribs, 0, s1, nAttribs );
	(*jni.env)->SetIntArrayRegion( jni.env, contextAttribs, 0, s2, nContextAttribs );

	R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_WINDOWED ); // width and height are ignored anyway


	if( (*jni.env)->CallStaticBooleanMethod( jni.env, jni.bindcls, jni.createGLContext, attribs, contextAttribs ) )
	{
		vid_android.has_context = true;
		return true;
	}

	return false;
}

rserr_t   R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	int render_w, render_h;

	Android_GetScreenRes(&width, &height);

	render_w = width;
	render_h = height;

	Con_Reportf( "R_ChangeDisplaySettings: forced resolution to %dx%d)\n", width, height);

	VID_SetDisplayTransform( &render_w, &render_h );

	R_SaveVideoMode( width, height, render_w, render_h, true );

	refState.wideScreen = true; // V_AdjustFov will check for widescreen

	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	return EGL_SetAttribute( attr, val );
}

int GL_GetAttribute( int attr, int *val )
{
	EGLBoolean ret;

	if( eglstate.valid )
		return EGL_GetAttribute( attr, val );

	if( attr < 0 || attr >= REF_GL_ATTRIBUTES_COUNT )
		return -1;

	if( !val )
		return -1;

	switch( attr )
	{
	case REF_GL_RED_SIZE:
		*val = Android_GetGLAttribute( EGL_RED_SIZE );
		return 0;
	case REF_GL_GREEN_SIZE:
		*val = Android_GetGLAttribute( EGL_GREEN_SIZE );
		return 0;
	case REF_GL_BLUE_SIZE:
		*val = Android_GetGLAttribute( EGL_BLUE_SIZE );
		return 0;
	case REF_GL_ALPHA_SIZE:
		*val = Android_GetGLAttribute( EGL_ALPHA_SIZE );
		return 0;
	case REF_GL_DEPTH_SIZE:
		*val = Android_GetGLAttribute( EGL_DEPTH_SIZE );
		return 0;
	case REF_GL_STENCIL_SIZE:
		*val = Android_GetGLAttribute( EGL_STENCIL_SIZE );
		return 0;
	case REF_GL_MULTISAMPLESAMPLES:
		*val = Android_GetGLAttribute( EGL_SAMPLES );
		return 0;
	}

	return -1;
}

int R_MaxVideoModes( void )
{
	return 0;
}

vidmode_t* R_GetVideoMode( int num )
{
	return NULL;
}

void* GL_GetProcAddress( const char *name ) // RenderAPI requirement
{
	return EGL_GetProcAddress( name );
}

void GL_UpdateSwapInterval( void )
{
	// disable VSync while level is loading
	if( cls.state < ca_active )
	{
		Android_SwapInterval( 0 );
		SetBits( gl_vsync.flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );
		Android_SwapInterval( gl_vsync.value );
	}
}

void *SW_LockBuffer( void )
{
	ANativeWindow_Buffer buffer;
	if( !nw.lock || !vid_android.window )
		return NULL;
	if( nw.lock( vid_android.window, &buffer, NULL ) )
		return NULL;
	if( buffer.width < refState.width || buffer.height < refState.height )
		return NULL;
	return buffer.bits;
}

void SW_UnlockBuffer( void )
{
	if( nw.unlockAndPost )
		nw.unlockAndPost( vid_android.window );
}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	ANativeWindow_Buffer buffer;
	int lock;
	if( !nw.lock || !vid_android.window )
		return false;
	nw.unlockAndPost( vid_android.window );

	if( ( lock = nw.lock( vid_android.window, &buffer, NULL ) ) )
	{
		Con_Printf( "SW_CreateBuffer: lock %d\n", lock );
		return false;
	}
	nw.unlockAndPost( vid_android.window );
	Con_Printf( "SW_CreateBuffer: buffer %d %d %x %d %p\n", buffer.width, buffer.height, buffer.format, buffer.stride, buffer.bits );
	if( width > buffer.width || height > buffer.height )
	{
		// resize event missed? do not resize now, wait for REAL resize event or when java code will be fixed
		Con_Printf( S_ERROR "SW_CreateBuffer: buffer too small, need %dx%d, got %dx%d, java part probably sucks\n", width, height, buffer.width, buffer.height );
#if 0
		if( jni.width < buffer.width )
			jni.width = buffer.width;
		if( jni.height < buffer.height )
			jni.width = buffer.height;
		VID_SetMode();
		Android_UpdateSurface( true );
#endif
		return false;
	}
	if( buffer.format != WINDOW_FORMAT_RGB_565 )
	{
		Con_Printf( "SW_CreateBuffer: wrong format %d\n", buffer.format );
		return false;
	}
	Con_Printf( "SW_CreateBuffer: ok\n" );
	*stride = buffer.stride;

	*bpp = 2;
	*r = (((1 << 5) - 1) << (5+6));
	*g = (((1 << 6) - 1) << (5));
	*b = (((1 << 5) - 1) << (0));
	return true;
}
#endif
