#include "platform/platform.h"
#if defined XASH_VIDEO == VIDEO_ANDROID || 1
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "platform/android/android_priv.h"
#include "vid_common.h"
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

static int gl_attribs[REF_GL_ATTRIBUTES_COUNT] = { 0 };
static qboolean gl_attribs_set[REF_GL_ATTRIBUTES_COUNT] = { 0 };
static EGLint gl_api = EGL_OPENGL_ES_API;

/*
========================
Android_SwapInterval
========================
*/
static void Android_SwapInterval( int interval )
{
	eglSwapInterval( negl.dpy, interval );
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
void GL_SwapBuffers()
{
	eglSwapBuffers( negl.dpy, negl.surface );
}

qboolean  R_Init_Video( const int type )
{
	string safe;
	qboolean retval;

	memset( gl_attribs, 0, sizeof( gl_attribs ));
	memset( gl_attribs_set, 0, sizeof( gl_attribs_set ));

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
		Host_Error( "software mode isn't supported on Android yet! :(\n", type );
		break;
	case REF_GL:
		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
			glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );

		// refdll can request some attributes
		ref.dllFuncs.GL_SetupAttributes( glw_state.safe );
		break;
	default:
		Host_Error( "Can't initialize unknown context type %d!\n", type );
		break;
	}

	if( !(retval = VID_SetMode()) )
	{
		return retval;
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
	eglMakeCurrent(negl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	eglDestroyContext(negl.dpy, negl.context);

	eglDestroySurface(negl.dpy, negl.surface);

	eglTerminate(negl.dpy);

	// VID_DestroyWindow ();

	// R_FreeVideoModes();

	ref.dllFuncs.GL_ClearExtensions();
}

#define COPY_ATTR_IF_SET( refattr, attr ) \
	if( gl_attribs_set[refattr] ) \
	{ \
		attribs[i++] = attr; \
		attribs[i++] = gl_attribs[refattr]; \
	}

static const EGLint *VID_GenerateConfig( void )
{
	int i = 0;
	static EGLint attribs[32 + 1];

	memset( attribs, 0, sizeof( attribs ));

	COPY_ATTR_IF_SET( REF_GL_RED_SIZE, EGL_RED_SIZE );
	COPY_ATTR_IF_SET( REF_GL_GREEN_SIZE, EGL_GREEN_SIZE );
	COPY_ATTR_IF_SET( REF_GL_BLUE_SIZE, EGL_BLUE_SIZE );
	COPY_ATTR_IF_SET( REF_GL_ALPHA_SIZE, EGL_ALPHA_SIZE );
	COPY_ATTR_IF_SET( REF_GL_DEPTH_SIZE, EGL_DEPTH_SIZE );
	COPY_ATTR_IF_SET( REF_GL_STENCIL_SIZE, EGL_STENCIL_SIZE );
	COPY_ATTR_IF_SET( REF_GL_MULTISAMPLEBUFFERS, EGL_SAMPLE_BUFFERS );
	COPY_ATTR_IF_SET( REF_GL_MULTISAMPLESAMPLES, EGL_SAMPLES );

	if( gl_attribs_set[REF_GL_ACCELERATED_VISUAL] )
	{
		attribs[i++] = EGL_CONFIG_CAVEAT;
		attribs[i++] = gl_attribs[REF_GL_ACCELERATED_VISUAL] ? EGL_NONE : EGL_DONT_CARE;
	}

	// BigGL support
	attribs[i++] = EGL_RENDERABLE_TYPE;
	gl_api = EGL_OPENGL_ES_API;

	if( gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] &&
		!( gl_attribs[REF_GL_CONTEXT_PROFILE_MASK] & REF_GL_CONTEXT_PROFILE_ES ))
	{
		attribs[i++] = EGL_OPENGL_BIT;
		gl_api = EGL_OPENGL_API;
	}
	else if( gl_attribs_set[REF_GL_CONTEXT_MAJOR_VERSION] &&
		gl_attribs[REF_GL_CONTEXT_MAJOR_VERSION] >= 2 )
	{
		attribs[i++] = EGL_OPENGL_ES2_BIT;
	}
	else
	{
		attribs[i++] = EGL_OPENGL_ES_BIT;
	}

	attribs[i] = EGL_NONE;

	return attribs;
}

static const EGLint *VID_GenerateContextConfig( void )
{
	int i = 0;
	static EGLint attribs[32 + 1];

	memset( attribs, 0, sizeof( attribs ));

	if( Q_strcmp( negl.extensions, " EGL_KHR_create_context ") )
	{
		if( gl_attribs_set[REF_GL_CONTEXT_FLAGS] )
		{
			attribs[i++] = 0x30FC; // EGL_CONTEXT_FLAGS_KHR
			attribs[i++] = gl_attribs[REF_GL_CONTEXT_FLAGS] & ((REF_GL_CONTEXT_ROBUST_ACCESS_FLAG << 1) - 1);
		}

		if( gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] )
		{
			int val = gl_attribs[REF_GL_CONTEXT_PROFILE_MASK];

			if( val & ( (REF_GL_CONTEXT_PROFILE_COMPATIBILITY << 1) - 1 ) )
			{
				attribs[i++] = 0x30FD; // EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
				attribs[i++] = val;
			}
		}

		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MAJOR_VERSION, EGL_CONTEXT_CLIENT_VERSION );
		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MINOR_VERSION, 0x30FB );
	}
	else
	{
		// without extension we can set only major version
		COPY_ATTR_IF_SET( REF_GL_CONTEXT_MAJOR_VERSION, EGL_CONTEXT_CLIENT_VERSION );
	}


	attribs[i] = EGL_NONE;

	return attribs;
}

qboolean VID_SetMode( void )
{
	EGLint format;
	const EGLint *attribs = VID_GenerateConfig();
	const EGLint *contextAttribs = VID_GenerateContextConfig();

	R_ChangeDisplaySettings( 0, 0, false ); // width and height are ignored anyway

	negl.valid = false;

	if( ( negl.dpy = eglGetDisplay( EGL_DEFAULT_DISPLAY )) == EGL_NO_DISPLAY )
	{
		Con_Reportf( S_ERROR "eglGetDisplay returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !eglInitialize( negl.dpy, NULL, NULL ))
	{
		Con_Reportf( S_ERROR "eglInitialize returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !(negl.extensions = eglQueryString( negl.dpy, EGL_EXTENSIONS ) ) )
	{
		Con_Reportf( S_ERROR "eglQueryString(EGL_EXTENSIONS) returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !eglChooseConfig( negl.dpy, attribs, &negl.cfg, 1, &negl.numCfg ))
	{
		Con_Reportf( S_ERROR "eglChooseConfig returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_NATIVE_VISUAL_ID, &format) )
	{
		Con_Reportf( S_ERROR "eglGetConfigAttrib returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( ANativeWindow_setBuffersGeometry( negl.window, 0, 0, format ) )
	{
		Con_Reportf( S_ERROR "ANativeWindow_setBuffersGeometry returned error\n" );
		return false;
	}

	if( !( negl.surface = eglCreateWindowSurface( negl.dpy, negl.cfg, negl.window, NULL )))
	{
		Con_Reportf( S_ERROR "eglCreateWindowSurface returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !( negl.context = eglCreateContext( negl.dpy, negl.cfg, NULL, contextAttribs )))
	{
		Con_Reportf( S_ERROR "eglCreateContext returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !eglMakeCurrent( negl.dpy, negl.surface, negl.surface, negl.context ))
	{
		Con_Reportf( S_ERROR "eglMakeCurrent returned error: 0x%x\n", eglGetError() );
		return false;
	}

	if( !eglBindAPI( gl_api ))
	{
		Con_Reportf( S_ERROR "eglBindAPI returned error: 0x%x\n", eglGetError() );
		return false;
	}

	__android_log_print( ANDROID_LOG_VERBOSE, "Xash", "native EGL enabled" );

	negl.valid = true;

	return true;
}

rserr_t   R_ChangeDisplaySettings( int width, int height, qboolean fullscreen )
{
	Android_GetScreenRes(&width, &height);

	Con_Reportf( "R_ChangeDisplaySettings: forced resolution to %dx%d)\n", width, height);

	R_SaveVideoMode( width, height );

	host.window_center_x = width / 2;
	host.window_center_y = height / 2;

	refState.wideScreen = true; // V_AdjustFov will check for widescreen

	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	if( attr < 0 || attr >= REF_GL_ATTRIBUTES_COUNT )
		return -1;

	gl_attribs[attr] = val;
	gl_attribs_set[attr] = true;
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
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_RED_SIZE, val );
		break;
	case REF_GL_GREEN_SIZE:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_GREEN_SIZE, val );
		break;
	case REF_GL_BLUE_SIZE:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_BLUE_SIZE, val );
		break;
	case REF_GL_ALPHA_SIZE:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_ALPHA_SIZE, val );
		break;
	case REF_GL_DEPTH_SIZE:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_DEPTH_SIZE, val );
		break;
	case REF_GL_STENCIL_SIZE:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_STENCIL_SIZE, val );
		break;
	case REF_GL_MULTISAMPLESAMPLES:
		ret = eglGetConfigAttrib( negl.dpy, negl.cfg, EGL_SAMPLES, val );
		break;
	}

	if( !ret )
		return -1;

	return 0;
}

int R_MaxVideoModes()
{
	return 0;
}

vidmode_t* R_GetVideoMode( int num )
{
	return NULL;
}

void* GL_GetProcAddress( const char *name ) // RenderAPI requirement
{
	return eglGetProcAddress( name );
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

void *SW_LockBuffer()
{
	return NULL;
}

void SW_UnlockBuffer()
{

}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	return false;
}

#endif
