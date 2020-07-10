#include "platform/platform.h"
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "platform/android/android_priv.h"
#include "vid_common.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

static struct vid_android_s
{
	int gl_attribs[REF_GL_ATTRIBUTES_COUNT];
	qboolean gl_attribs_set[REF_GL_ATTRIBUTES_COUNT];
	EGLint gl_api;
	qboolean gles1;
	void *libgles1, *libgles2;
	qboolean has_context;
	ANativeWindow* window;
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
dll_info_t android_info = { "libandroid.so", android_funcs, false };

static struct egl_s
{
	EGLSurface (*GetCurrentSurface)(EGLint readdraw);
	EGLDisplay (*GetCurrentDisplay)(void);
	EGLint (*GetError)(void);
	EGLBoolean (*SwapBuffers)(EGLDisplay dpy, EGLSurface surface);
	EGLBoolean (*SwapInterval)(EGLDisplay dpy, EGLint interval);
	void *(*GetProcAddress)(const char *procname);
} egl;
#undef GetProcAddress
#define EGL_FF(x) {"egl"#x, (void*)&egl.x}
static dllfunc_t egl_funcs[] =
{
	EGL_FF(SwapInterval),
	EGL_FF(SwapBuffers),
	EGL_FF(GetError),
	EGL_FF(GetCurrentDisplay),
	EGL_FF(GetCurrentSurface),
	EGL_FF(GetProcAddress),
	{ NULL, NULL }
};
#undef EGL_FF
dll_info_t egl_info = { "libEGL.so", egl_funcs, false };

static struct nativeegl_s
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

/*
========================
Android_SwapInterval
========================
*/
static void Android_SwapInterval( int interval )
{
	if( negl.valid )
		egl.SwapInterval( negl.dpy, interval );
}

/*
========================
Android_SetTitle
========================
*/
static void Android_SetTitle( const char *title )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.setTitle, (*jni.env)->NewStringUTF( jni.env, title ) );
}

/*
========================
Android_SetIcon
========================
*/
static void Android_SetIcon( const char *path )
{
	(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.setIcon, (*jni.env)->NewStringUTF( jni.env, path ) );
}

/*
========================
Android_GetScreenRes

Resolution got from last resize event
========================
*/
static void Android_GetScreenRes( int *width, int *height )
{
	*width=jni.width, *height=jni.height;
}

/*
========================
Android_SwapBuffers

Update screen. Use native EGL if possible
========================
*/
void GL_SwapBuffers( void )
{
	if( negl.valid )
	{
		egl.SwapBuffers( negl.dpy, negl.surface );
	}
	else
	{
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.swapBuffers );
	}
}

/*
========================
Android_UpdateSurface

Check if we may use native EGL without jni calls
========================
*/
void Android_UpdateSurface( qboolean active )
{
	negl.valid = false;

	if( nw.release )
	{
		if( vid_android.window && !active )
		{
			nw.release( vid_android.window );
			vid_android.window = NULL;
		}
	
		if( active )
		{
			jobject surf;
			if( vid_android.window )
				nw.release( vid_android.window );
			surf = (*jni.env)->CallStaticObjectMethod(jni.env, jni.actcls, jni.getSurface);
			Con_Printf("s %p\n", surf);
			vid_android.window = nw.fromSurface(jni.env, surf);
			Con_Printf("w %p\n", vid_android.window);
			nw.setBuffersGeometry(vid_android.window, 0, 0, WINDOW_FORMAT_RGB_565 );
			(*jni.env)->DeleteLocalRef( jni.env, surf );
		}
		return;
	}

	if( !vid_android.has_context )
		return;

	if( ( active && host.status == HOST_FRAME ) || !active )
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 0 );

	if( active )
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.toggleEGL, 1 );

	if( !Sys_CheckParm("-nativeegl") || !active )
		return; // enabled by user

	if( !egl.GetCurrentDisplay )
		return;

	negl.dpy = egl.GetCurrentDisplay();

	if( negl.dpy == EGL_NO_DISPLAY )
		return;

	negl.surface = egl.GetCurrentSurface(EGL_DRAW);

	if( negl.surface == EGL_NO_SURFACE )
		return;

	// now check if swapBuffers does not give error
	if( egl.SwapBuffers( negl.dpy, negl.surface ) == EGL_FALSE )
		return;

	// double check
	if( egl.GetError() != EGL_SUCCESS )
		return;

	__android_log_print( ANDROID_LOG_VERBOSE, "Xash", "native EGL enabled" );

	negl.valid = true;
}

/*
========================
Android_GetGLAttribute
========================
*/
static int Android_GetGLAttribute( int eglAttr )
{
	int ret = (*jni.env)->CallStaticIntMethod( jni.env, jni.actcls, jni.getGLAttribute, eglAttr );
	// Con_Reportf( "Android_GetGLAttribute( %i ) => %i\n", eglAttr, ret );
	return ret;
}

int Android_GetSelectedPixelFormat( void )
{
	return (*jni.env)->CallStaticIntMethod( jni.env, jni.actcls, jni.getSelectedPixelFormat );
}

qboolean  R_Init_Video( const int type )
{
	string safe;
	qboolean retval;

	switch( Android_GetSelectedPixelFormat() )
	{
	case 1:
		refState.desktopBitsPixel = 16;
		break;
	case 2:
		refState.desktopBitsPixel = 8;
		break;
	default:
		refState.desktopBitsPixel = 32;
		break;
	}

	if( FS_FileExists( GI->iconpath, true ) )
	{
		if( host.rodir[0] )
		{
			Android_SetIcon( va( "%s/%s/%s", host.rodir, GI->gamefolder, GI->iconpath ) );
		}
		else
		{
			Android_SetIcon( va( "%s/%s/%s", host.rootdir, GI->gamefolder, GI->iconpath ) );
		}
	}

	Android_SetTitle( GI->title );

	VID_StartupGamma();

	switch( type )
	{
	case REF_SOFTWARE:
		glw_state.software = true;
		break;
	case REF_GL:
		glw_state.software = false;
		Sys_LoadLibrary( &egl_info );

		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
			glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );

		break;
	default:
		Host_Error( "Can't initialize unknown context type %d!\n", type );
		break;
	}

	if( glw_state.software )
	{
		uint arg;
//		Con_Reportf( S_ERROR "Native software mode isn't supported on Android yet! :(\n" );
//		return false;
		Sys_LoadLibrary( &android_info );
		Android_UpdateSurface( true );
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

	return true;
}

void R_Free_Video( void )
{
	// (*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.deleteGLContext );

	// VID_DestroyWindow ();

	// R_FreeVideoModes();
	Sys_FreeLibrary( &android_info );
	Sys_FreeLibrary( &egl_info );
	vid_android.has_context = false;
	ref.dllFuncs.GL_ClearExtensions();
}

#define COPY_ATTR_IF_SET( refattr, attr ) \
	if( vid_android.gl_attribs_set[refattr] ) \
	{ \
		attribs[i++] = attr; \
		attribs[i++] = vid_android.gl_attribs[refattr]; \
	}

static size_t VID_GenerateConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;

	memset( attribs, 0, size * sizeof( EGLint ) );
	vid_android.gles1 = false;
	memset( vid_android.gl_attribs, 0, sizeof( vid_android.gl_attribs ));
	memset( vid_android.gl_attribs_set, 0, sizeof( vid_android.gl_attribs_set ));

	// refdll can request some attributes
	ref.dllFuncs.GL_SetupAttributes( glw_state.safe );

	COPY_ATTR_IF_SET( REF_GL_RED_SIZE, EGL_RED_SIZE );
	COPY_ATTR_IF_SET( REF_GL_GREEN_SIZE, EGL_GREEN_SIZE );
	COPY_ATTR_IF_SET( REF_GL_BLUE_SIZE, EGL_BLUE_SIZE );
	COPY_ATTR_IF_SET( REF_GL_ALPHA_SIZE, EGL_ALPHA_SIZE );
	COPY_ATTR_IF_SET( REF_GL_DEPTH_SIZE, EGL_DEPTH_SIZE );
	COPY_ATTR_IF_SET( REF_GL_STENCIL_SIZE, EGL_STENCIL_SIZE );
	COPY_ATTR_IF_SET( REF_GL_MULTISAMPLEBUFFERS, EGL_SAMPLE_BUFFERS );
	COPY_ATTR_IF_SET( REF_GL_MULTISAMPLESAMPLES, EGL_SAMPLES );

	if( vid_android.gl_attribs_set[REF_GL_ACCELERATED_VISUAL] )
	{
		attribs[i++] = EGL_CONFIG_CAVEAT;
		attribs[i++] = vid_android.gl_attribs[REF_GL_ACCELERATED_VISUAL] ? EGL_NONE : EGL_DONT_CARE;
	}

	// BigGL support
	attribs[i++] = EGL_RENDERABLE_TYPE;
	vid_android.gl_api = EGL_OPENGL_ES_API;

	if( vid_android.gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] &&
		!( vid_android.gl_attribs[REF_GL_CONTEXT_PROFILE_MASK] & REF_GL_CONTEXT_PROFILE_ES ))
	{
		attribs[i++] = EGL_OPENGL_BIT;
		vid_android.gl_api = EGL_OPENGL_API;
	}
	else if( vid_android.gl_attribs_set[REF_GL_CONTEXT_MAJOR_VERSION] &&
		vid_android.gl_attribs[REF_GL_CONTEXT_MAJOR_VERSION] >= 2 )
	{
		attribs[i++] = EGL_OPENGL_ES2_BIT;
	}
	else
	{
		i--; // erase EGL_RENDERABLE_TYPE
		vid_android.gles1 = true;
	}

	attribs[i++] = EGL_NONE;

	return i;
}

static size_t VID_GenerateContextConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;

	memset( attribs, 0, size * sizeof( EGLint ));

	/*if( Q_strcmp( negl.extensions, " EGL_KHR_create_context ") )
	{
		if( vid_android.gl_attribs_set[REF_GL_CONTEXT_FLAGS] )
		{
			attribs[i++] = 0x30FC; // EGL_CONTEXT_FLAGS_KHR
			attribs[i++] = vid_android.gl_attribs[REF_GL_CONTEXT_FLAGS] & ((REF_GL_CONTEXT_ROBUST_ACCESS_FLAG << 1) - 1);
		}

		if( vid_android.gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] )
		{
			int val = vid_android.gl_attribs[REF_GL_CONTEXT_PROFILE_MASK];

			if( val & ( (REF_GL_CONTEXT_PROFILE_COMPATIBILITY << 1) - 1 ) )
			{
				attribs[i++] = 0x30FD; // EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
				attribs[i++] = val;
			}
		}

		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MAJOR_VERSION, EGL_CONTEXT_CLIENT_VERSION );
		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MINOR_VERSION, 0x30FB );
	}
	else*/
	{
		// without extension we can set only major version
		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MAJOR_VERSION, EGL_CONTEXT_CLIENT_VERSION );
	}

	attribs[i++] = EGL_NONE;

	return i;
}

qboolean VID_SetMode( void )
{
	EGLint format;
	jintArray attribs, contextAttribs;
	static EGLint nAttribs[32+1], nContextAttribs[32+1];
	const size_t attribsSize = ARRAYSIZE( nAttribs );
	size_t s1, s2;

	if( vid_android.has_context )
	{
		R_ChangeDisplaySettings( 0, 0, false ); // width and height are ignored anyway
		return true;
	}

	s1 = VID_GenerateConfig(nAttribs, attribsSize);
	s2 = VID_GenerateContextConfig(nContextAttribs, attribsSize);

	attribs = (*jni.env)->NewIntArray( jni.env, s1 );
	contextAttribs = (*jni.env)->NewIntArray( jni.env, s2 );

	(*jni.env)->SetIntArrayRegion( jni.env, attribs, 0, s1, nAttribs );
	(*jni.env)->SetIntArrayRegion( jni.env, contextAttribs, 0, s2, nContextAttribs );

	R_ChangeDisplaySettings( 0, 0, false ); // width and height are ignored anyway

	if( glw_state.software )
		return true;

	if( (*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.createGLContext, attribs, contextAttribs ) )
	{
		vid_android.has_context = true;
		return true;
	}

	return false;
}

rserr_t   R_ChangeDisplaySettings( int width, int height, qboolean fullscreen )
{
	int render_w, render_h;
	uint rotate = vid_rotate->value;

	Android_GetScreenRes(&width, &height);

	render_w = width;
	render_h = height;

	Con_Reportf( "R_ChangeDisplaySettings: forced resolution to %dx%d)\n", width, height);

	if( ref.dllFuncs.R_SetDisplayTransform( rotate, 0, 0, vid_scale->value, vid_scale->value ) )
	{
		if( rotate & 1 )
		{
			int swap = render_w;

			render_w = render_h;
			render_h = swap;
		}

		render_h /= vid_scale->value;
		render_w /= vid_scale->value;
	}
	else
	{
		Con_Printf( S_WARN "failed to setup screen transform\n" );
	}

	R_SaveVideoMode( width, height, render_w, render_h );

	refState.wideScreen = true; // V_AdjustFov will check for widescreen

	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	if( attr < 0 || attr >= REF_GL_ATTRIBUTES_COUNT )
		return -1;

	vid_android.gl_attribs[attr] = val;
	vid_android.gl_attribs_set[attr] = true;
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	EGLBoolean ret;

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
	void *gles;
	void *addr;

	if( vid_android.gles1 )
	{
		if( !vid_android.libgles1 )
			vid_android.libgles1 = dlopen("libGLESv1_CM.so", RTLD_NOW);
		gles = vid_android.libgles1;
	}
	else
	{
		if( !vid_android.libgles2 )
			vid_android.libgles2 = dlopen("libGLESv2.so", RTLD_NOW);
		gles = vid_android.libgles2;
	}

	if( gles && ( addr = dlsym(gles, name ) ) )
		return addr;

	if( !egl.GetProcAddress )
		return NULL;

	return egl.GetProcAddress( name );
}

void GL_UpdateSwapInterval( void )
{
	// disable VSync while level is loading
	if( cls.state < ca_active )
	{
		Android_SwapInterval( 0 );
		SetBits( gl_vsync->flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync->flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync->flags, FCVAR_CHANGED );
		Android_SwapInterval( gl_vsync->value );
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
		Con_Printf( "SW_CreateBuffer: buffer too small %d %d\n", width, height );
		// resize event missed?
		if( jni.width < buffer.width )
			jni.width = buffer.width;
		if( jni.height < buffer.height )
			jni.width = buffer.height;
		VID_SetMode();
		Android_UpdateSurface( 1 );
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
