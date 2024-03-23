/*
eglutil.c - EGL context utility
Copyright (C) 2023 mittorn

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
#if !defined(XASH_DEDICATED) && !XASH_SDL
#include "eglutil.h"
#include "client.h"
#include "vid_common.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

struct eglapi_s egl;

#undef GetProcAddress // windows.h?
#define EGL_FF(x) {"egl"#x, (void*)&egl.x}
static dllfunc_t egl_funcs[] =
{
	EGL_FF(SwapInterval),
	EGL_FF(SwapBuffers),
	EGL_FF(GetError),
	EGL_FF(GetCurrentDisplay),
	EGL_FF(GetCurrentSurface),
	EGL_FF(GetProcAddress),
	EGL_FF(GetConfigAttrib),
	EGL_FF(GetDisplay),
	EGL_FF(Initialize),
	EGL_FF(Terminate),
	EGL_FF(QueryString),
	EGL_FF(ChooseConfig),
	EGL_FF(CreateWindowSurface),
	EGL_FF(CreateContext),
	EGL_FF(DestroyContext),
	EGL_FF(MakeCurrent),
	EGL_FF(BindAPI),
	EGL_FF(DestroySurface),
	{ NULL, NULL }
};
#undef EGL_FF
static dll_info_t egl_info = { "libEGL.so", egl_funcs, false };

struct eglstate_s eglstate;



/*
===================

refapi/egl wrappers

===================
*/

int EGL_SetAttribute( int attr, int val )
{
	if( attr < 0 || attr >= REF_GL_ATTRIBUTES_COUNT )
		return -1;

	eglstate.gl_attribs[attr] = val;
	eglstate.gl_attribs_set[attr] = true;
	return 0;
}

#define COPY_ATTR_IF_SET( refattr, attr ) \
	if( eglstate.gl_attribs_set[refattr] ) \
	{ \
		attribs[i++] = attr; \
		attribs[i++] = eglstate.gl_attribs[refattr]; \
	}

size_t EGL_GenerateConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;

	memset( attribs, 0, size * sizeof( EGLint ) );
	eglstate.gles1 = false;
	memset( eglstate.gl_attribs, 0, sizeof( eglstate.gl_attribs ));
	memset( eglstate.gl_attribs_set, 0, sizeof( eglstate.gl_attribs_set ));

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

	if( eglstate.gl_attribs_set[REF_GL_ACCELERATED_VISUAL] )
	{
		attribs[i++] = EGL_CONFIG_CAVEAT;
		attribs[i++] = eglstate.gl_attribs[REF_GL_ACCELERATED_VISUAL] ? EGL_NONE : EGL_DONT_CARE;
	}

	// BigGL support
	attribs[i++] = EGL_RENDERABLE_TYPE;
	eglstate.gl_api = EGL_OPENGL_ES_API;

	if( eglstate.gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] &&
		!( eglstate.gl_attribs[REF_GL_CONTEXT_PROFILE_MASK] & REF_GL_CONTEXT_PROFILE_ES ))
	{
		attribs[i++] = EGL_OPENGL_BIT;
		eglstate.gl_api = EGL_OPENGL_API;
	}
	else if( eglstate.gl_attribs_set[REF_GL_CONTEXT_MAJOR_VERSION] &&
		eglstate.gl_attribs[REF_GL_CONTEXT_MAJOR_VERSION] >= 2 )
	{
		attribs[i++] = EGL_OPENGL_ES2_BIT;
	}
	else
	{
		i--; // erase EGL_RENDERABLE_TYPE
		eglstate.gles1 = true;
	}

	attribs[i++] = EGL_NONE;

	return i;
}


size_t EGL_GenerateContextConfig( EGLint *attribs, size_t size )
{
	size_t i = 0;

	memset( attribs, 0, size * sizeof( EGLint ));

	if( Q_strstr( eglstate.extensions, "EGL_KHR_create_context") )
	{
		Con_DPrintf( S_NOTE "EGL_KHR_create_context found, setting additional context flags\n");

		if( eglstate.gl_attribs_set[REF_GL_CONTEXT_FLAGS] )
		{
			attribs[i++] = 0x30FC; // EGL_CONTEXT_FLAGS_KHR
			attribs[i++] = eglstate.gl_attribs[REF_GL_CONTEXT_FLAGS] & ((REF_GL_CONTEXT_ROBUST_ACCESS_FLAG << 1) - 1);
		}

		if( eglstate.gl_attribs_set[REF_GL_CONTEXT_PROFILE_MASK] )
		{
			int val = eglstate.gl_attribs[REF_GL_CONTEXT_PROFILE_MASK];

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
		if( eglstate.gl_attribs_set[REF_GL_CONTEXT_FLAGS] && (eglstate.gl_attribs[REF_GL_CONTEXT_FLAGS] & REF_GL_CONTEXT_DEBUG_FLAG ))
		{
			attribs[i++] = 0x31B0; // EGL_CONTEXT_OPENGL_DEBUG;
			attribs[i++] = EGL_TRUE;
		}
	}


	attribs[i++] = EGL_NONE;

	return i;
}



/*
=========================
EGL_GetAttribute

query
=========================
*/
int EGL_GetAttribute( int attr, int *val )
{
	EGLBoolean ret;

	if( attr < 0 || attr >= REF_GL_ATTRIBUTES_COUNT )
		return -1;

	if( !val )
		return -1;

	switch( attr )
	{
	case REF_GL_RED_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_RED_SIZE, val );
		return 0;
	case REF_GL_GREEN_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_GREEN_SIZE, val );
		return 0;
	case REF_GL_BLUE_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_BLUE_SIZE, val );
		return 0;
	case REF_GL_ALPHA_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_ALPHA_SIZE, val );
		return 0;
	case REF_GL_DEPTH_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_DEPTH_SIZE, val );
		return 0;
	case REF_GL_STENCIL_SIZE:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_STENCIL_SIZE, val );
		return 0;
	case REF_GL_MULTISAMPLESAMPLES:
		ret = egl.GetConfigAttrib( eglstate.dpy, eglstate.cfg, EGL_SAMPLES, val );
		return 0;
	}

	return -1;
}


/*
=========================
EGL_UpdateSurface

destroy old surface, recreate and make context current
must be called with valid context
=========================
*/
qboolean EGL_UpdateSurface( void *window, qboolean dummy )
{
	if( !eglstate.valid )
		return false;

	egl.MakeCurrent( eglstate.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
	host.status = HOST_SLEEP;

	if( eglstate.surface )
	{
		egl.DestroySurface( eglstate.dpy, eglstate.surface );
		eglstate.surface = EGL_NO_SURFACE;
	}

	if( !window )
	{

		if( dummy && eglstate.support_surfaceless_context )
		{
			if( egl.MakeCurrent( eglstate.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, eglstate.context ))
			{
				Con_Reportf( S_NOTE "EGL_UpdateSurface: using surfaceless mode\n" );
				return true;
			}
			else
			{
				egl.MakeCurrent( eglstate.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
				eglstate.support_surfaceless_context = false;
			}
			Con_Reportf( S_NOTE "EGL_UpdateSurface: missing native window, detaching context\n" );
		}

		return false; // let platform fallback to dummy surface or pause engine
	}

	if(( eglstate.surface = egl.CreateWindowSurface( eglstate.dpy, eglstate.cfg, window, NULL )) == EGL_NO_SURFACE )
	{
		Con_Reportf( S_ERROR "eglCreateWindowSurface returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	if( !egl.MakeCurrent( eglstate.dpy, eglstate.surface, eglstate.surface, eglstate.context ))
	{
		Con_Reportf( S_ERROR "eglMakeCurrent returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	Con_DPrintf( S_NOTE "restored current context\n" );
	if( !dummy)
		host.status = HOST_FRAME;

	return true;

}


/*
=========================
EGL_CreateContext

query attributes for ref and create context
=========================
*/
qboolean EGL_CreateContext( void )
{
	EGLint attribs[32+1], contextAttribs[32+1];
	const size_t attribsSize = ARRAYSIZE( attribs );
	size_t s1, s2;

	if( !eglstate.dpy && ( eglstate.dpy = egl.GetDisplay( EGL_DEFAULT_DISPLAY )) == EGL_NO_DISPLAY )
	{
		Con_Reportf( S_ERROR "eglGetDisplay returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	eglstate.extensions = egl.QueryString( eglstate.dpy, EGL_EXTENSIONS );

	s1 = EGL_GenerateConfig(attribs, attribsSize);
	s2 = EGL_GenerateContextConfig(contextAttribs, attribsSize);


	if( !egl.BindAPI( eglstate.gl_api ))
	{
		Con_Reportf( S_ERROR "eglBindAPI returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	if( !egl.Initialize( eglstate.dpy, NULL, NULL ))
	{
		Con_Reportf( S_ERROR "eglInitialize returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	if( !egl.ChooseConfig( eglstate.dpy, attribs, &eglstate.cfg, 1, &eglstate.numCfg ))
	{
		Con_Reportf( S_ERROR "eglChooseConfig returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	if(( eglstate.context = egl.CreateContext( eglstate.dpy, eglstate.cfg, NULL, contextAttribs )) == EGL_NO_CONTEXT )
	{
		Con_Reportf( S_ERROR "eglCreateContext returned error: 0x%x\n", egl.GetError() );
		return false;
	}

	eglstate.valid = true;
	eglstate.imported = false;

	// now check if it's safe to use surfaceless context here without surface fallback
	if( eglstate.extensions && Q_strstr( eglstate.extensions, "EGL_KHR_surfaceless_context" ))
		eglstate.support_surfaceless_context = true;


	return true;
}

/*
===========================
EGL_ImportContext

import current egl context to use EGL functions
===========================
*/
qboolean EGL_ImportContext( void )
{
	if( !egl.GetCurrentDisplay )
		return false;

	eglstate.dpy = egl.GetCurrentDisplay();

	if( eglstate.dpy == EGL_NO_DISPLAY )
		return false;

	eglstate.surface = egl.GetCurrentSurface( EGL_DRAW );

	if( eglstate.surface == EGL_NO_SURFACE )
		return false;

	// now check if swapBuffers does not give error
	if( egl.SwapBuffers( eglstate.dpy, eglstate.surface ) == EGL_FALSE )
		return false;

	// double check
	if( egl.GetError() != EGL_SUCCESS )
		return false;

	eglstate.extensions = egl.QueryString( eglstate.dpy, EGL_EXTENSIONS );

	eglstate.valid = eglstate.imported = true;
	return true;
}

/*
=========================
EGL_Terminate
=========================
*/
void EGL_Terminate( void )
{
	egl.MakeCurrent( eglstate.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );

	egl.DestroyContext( eglstate.dpy, eglstate.context );

	egl.DestroySurface( eglstate.dpy, eglstate.surface );

	egl.Terminate( eglstate.dpy );

	Sys_FreeLibrary( &egl_info );
}

/*
=========================
EGL_GetProcAddress

eglGetProcAddress/dlsym wrapper
=========================
*/
void *EGL_GetProcAddress( const char *name )
{
	void *gles;
	void *addr;

	// TODO: cross-platform loading
	if( eglstate.gles1 )
	{
		if( !eglstate.libgles1 )
			eglstate.libgles1 = dlopen( "libGLESv1_CM.so", RTLD_NOW );
		gles = eglstate.libgles1;
	}
	else
	{
		if( !eglstate.libgles2 )
			eglstate.libgles2 = dlopen( "libGLESv2.so", RTLD_NOW );
		gles = eglstate.libgles2;
	}

	if( gles && ( addr = dlsym( gles, name )))
		return addr;

	if( !egl.GetProcAddress )
		return NULL;

	return egl.GetProcAddress( name );
}

/*
=========================
EGL_LoadLibrary
=========================
*/
void EGL_LoadLibrary( void )
{
	Sys_LoadLibrary( &egl_info );
}
#endif
