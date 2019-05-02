#include "platform/platform.h"
#if defined XASH_VIDEO == VIDEO_ANDROID || 1
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "platform/android/android_priv.h"
#include "vid_common.h"

/*
========================
Android_SwapInterval
========================
*/
static void Android_SwapInterval( int interval )
{
	// there is no eglSwapInterval in EGL10/EGL11 classes,
	// so only native backend supported
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
Android_UpdateSurface

Check if we may use native EGL without jni calls
========================
*/
void Android_UpdateSurface( void )
{
	negl.valid = false;

	if( Sys_CheckParm("-nonativeegl") )
		return; //disabled by user

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

/*
========================
Android_InitGL
========================
*/
qboolean Android_InitGL()
{
	int colorBits[3];
	qboolean result;

	// result = (*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.createGLContext, (int)gl_stencilbits->value );

	/*colorBits[0] = Android_GetGLAttribute( EGL_RED_SIZE );
	colorBits[1] = Android_GetGLAttribute( EGL_GREEN_SIZE );
	colorBits[2] = Android_GetGLAttribute( EGL_BLUE_SIZE );
	glConfig.color_bits = colorBits[0] + colorBits[1] + colorBits[2];
	glConfig.alpha_bits = Android_GetGLAttribute( EGL_ALPHA_SIZE );
	glConfig.depth_bits = Android_GetGLAttribute( EGL_DEPTH_SIZE );
	glConfig.stencil_bits = Android_GetGLAttribute( EGL_STENCIL_SIZE );
	glState.stencilEnabled = glConfig.stencil_bits ? true : false;*/

	Android_UpdateSurface();

	return result;
}

/*
========================
Android_ShutdownGL
========================
*/
void Android_ShutdownGL()
{
	(*jni.env)->CallStaticBooleanMethod( jni.env, jni.actcls, jni.deleteGLContext );
}

/*
========================
Android_SwapBuffers

Update screen. Use native EGL if possible
========================
*/
void GL_SwapBuffers()
{
	if( negl.valid )
	{
		eglSwapBuffers( negl.dpy, negl.surface );
	}
	else
	{
		// nanoGL_Flush();
		(*jni.env)->CallStaticVoidMethod( jni.env, jni.actcls, jni.swapBuffers );
	}
}

qboolean  R_Init_Video( const int type )
{
	string safe;
	qboolean retval;

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
	// GL_DeleteContext ();

	// VID_DestroyWindow ();

	// R_FreeVideoModes();

	ref.dllFuncs.GL_ClearExtensions();
}


qboolean  VID_SetMode( void )
{
	R_ChangeDisplaySettings( 0, 0, false ); // width and height are ignored anyway
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

}

int GL_GetAttribute( int attr, int *val )
{

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
	return NULL; // not implemented, only static for now
}

void      GL_UpdateSwapInterval( void )
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
