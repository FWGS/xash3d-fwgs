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
	if( negl.valid )
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
void GL_SwapBuffers( void )
{
	if( negl.valid )
	{
		eglSwapBuffers( negl.dpy, negl.surface );
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
void Android_UpdateSurface( void )
{
	negl.valid = false;

	if( !Sys_CheckParm("-nativeegl") )
		return; // enabled by user

	negl.dpy = eglGetCurrentDisplay();

	if( negl.dpy == EGL_NO_DISPLAY )
		return;

	negl.surface = eglGetCurrentSurface(EGL_DRAW);

	if( negl.surface == EGL_NO_SURFACE )
		return;

	// now check if swapBuffers does not give error
	if( eglSwapBuffers( negl.dpy, negl.surface ) == EGL_FALSE )
		return;

	// double check
	if( eglGetError() != EGL_SUCCESS )
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

	if( glw_state.software )
	{
		Con_Reportf( S_ERROR "Native software mode isn't supported on Android yet! :(\n" );
		return false;
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
	// (*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.deleteGLContext );

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

static size_t VID_GenerateConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;
	memset( attribs, 0, size * sizeof( EGLint ) );

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

	attribs[i++] = EGL_NONE;

	return i;
}

static size_t VID_GenerateContextConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;

	memset( attribs, 0, size * sizeof( EGLint ));

	/*if( Q_strcmp( negl.extensions, " EGL_KHR_create_context ") )
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

	size_t s1 = VID_GenerateConfig(nAttribs, attribsSize);
	size_t s2 = VID_GenerateContextConfig(nContextAttribs, attribsSize);

	attribs = (*jni.env)->NewIntArray( jni.env, s1 );
	contextAttribs = (*jni.env)->NewIntArray( jni.env, s2 );

	(*jni.env)->SetIntArrayRegion( jni.env, attribs, 0, s1, nAttribs );
	(*jni.env)->SetIntArrayRegion( jni.env, contextAttribs, 0, s2, nContextAttribs );

	R_ChangeDisplaySettings( 0, 0, false ); // width and height are ignored anyway

	return (*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.createGLContext, attribs, contextAttribs );
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

void *SW_LockBuffer( void )
{
	return NULL;
}

void SW_UnlockBuffer( void )
{

}

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	return false;
}
