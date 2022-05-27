/*
vid_psp.c - PSP video component
Copyright (C) 2021 Sergey Galushko

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#if XASH_VIDEO == VIDEO_PSP

#if !XASH_DEDICATED
#include <pspkernel.h>
#include <pspdisplay.h>

#include "common.h"
#include "client.h"
#include "mod_local.h"
#include "input.h"
#include "vid_common.h"

// Set frame buffer
#define PSP_FB_WIDTH		480
#define PSP_FB_HEIGHT		272
#define PSP_FB_BWIDTH		512
#define PSP_FB_FORMAT		PSP_DISPLAY_PIXEL_FORMAT_565 //4444,5551,565,8888

#if   PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_4444
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_5551
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_565
#define PSP_FB_BPP			2
#elif PSP_FB_FORMAT == PSP_DISPLAY_PIXEL_FORMAT_8888
#define PSP_FB_BPP			4
#endif

#if 0
// Set up screen scaling
#define PSP_WIDTH_AR	30
#define PSP_HEIGHT_AR	17
#define PSP_MIN_MAR		12
#define PSP_MAX_MAR		16
#define PSP_MIN_MAR_W	(PSP_WIDTH_AR * PSP_MIN_MAR)
#define PSP_MIN_MAR_H	(PSP_HEIGHT_AR * PSP_MIN_MAR)
#define PSP_MAX_MAR_W	(PSP_WIDTH_AR * PSP_MAX_MAR)
#define PSP_MAX_MAR_H	(PSP_HEIGHT_AR * PSP_MAX_MAR)
#endif


static qboolean vsync;
#if 1
static vidmode_t vidmodes[] = { "480x272", PSP_FB_WIDTH, PSP_FB_HEIGHT};
static int num_vidmodes = 1;
#else
static vidmode_t *vidmodes = NULL;
static int num_vidmodes = 0;
#endif
static struct
{
#if 0
	int		in_width, in_height;
	int		out_width, out_height;
#endif
	void	*draw_buffer;
	void	*disp_buffer;
}vid_psp;

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	*stride = PSP_FB_BWIDTH;
	*r = 31;
	*g = 63 << 5;
	*b = 31 << 11;
	*bpp = 2;

	vid_psp.draw_buffer = (void*)malloc( PSP_FB_HEIGHT * PSP_FB_BWIDTH * PSP_FB_BPP );
	if( !vid_psp.draw_buffer )
		Host_Error( "Memory allocation failled! (vid_psp.draw_buffer)\n");

	vid_psp.disp_buffer = (void*)malloc( PSP_FB_HEIGHT * PSP_FB_BWIDTH * PSP_FB_BPP );
	if( !vid_psp.disp_buffer )
		Host_Error( "Memory allocation failled! (vid_psp.disp_buffer)\n");

	sceDisplaySetMode(0, PSP_FB_WIDTH, PSP_FB_HEIGHT);
	sceDisplaySetFrameBuf(vid_psp.disp_buffer, PSP_FB_BWIDTH, PSP_FB_FORMAT, 1);

	return true;
}

void *SW_LockBuffer( void )
{
	return vid_psp.draw_buffer;
}

void SW_UnlockBuffer( void )
{
	void* p_swap = vid_psp.disp_buffer;
	vid_psp.disp_buffer = vid_psp.draw_buffer;
	vid_psp.draw_buffer = p_swap;

	sceKernelDcacheWritebackInvalidateAll();
	sceDisplaySetFrameBuf(vid_psp.disp_buffer, PSP_FB_BWIDTH, PSP_FB_FORMAT, PSP_DISPLAY_SETBUF_NEXTFRAME);

	if ( vsync )
	{
		sceDisplayWaitVblankStart();
	}
}

int R_MaxVideoModes( void )
{
	return num_vidmodes;
}

vidmode_t *R_GetVideoMode( int num )
{
	if( !vidmodes || num < 0 || num >= R_MaxVideoModes() )
	{
		return NULL;
	}

	return vidmodes + num;
}
#if 0
static void R_InitVideoModes( void )
{
	int i;

	vidmodes = Mem_Malloc( host.mempool, (PSP_MAX_MAR - PSP_MIN_MAR) * sizeof( vidmode_t ) );

	// from smallest to largest
	for(i = PSP_MIN_MAR ; i <= PSP_MAX_MAR; i++ )
	{
		vidmodes[num_vidmodes].width  = PSP_WIDTH_AR  * i;
		vidmodes[num_vidmodes].height = PSP_HEIGHT_AR * i;
		vidmodes[num_vidmodes].desc =
			copystring( va( "%ix%i", vidmodes[num_vidmodes].width, vidmodes[num_vidmodes].height ));

		num_vidmodes++;
	}
}

static void R_FreeVideoModes( void )
{
	int i;

	if(!vidmodes)
		return;

	for( i = 0; i < num_vidmodes; i++ )
		Mem_Free( (char*)vidmodes[i].desc );
	Mem_Free( vidmodes );

	vidmodes = NULL;
}
#endif
/*
=================
GL_GetProcAddress
=================
*/
void *GL_GetProcAddress( const char *name )
{
	return NULL;
}

/*
===============
GL_UpdateSwapInterval
===============
*/
void GL_UpdateSwapInterval( void )
{
	// disable VSync while level is loading
	if( cls.state < ca_active )
	{
		// setup vsync here
		vsync = false;
		SetBits( gl_vsync->flags, FCVAR_CHANGED );
	}
	else if( FBitSet( gl_vsync->flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync->flags, FCVAR_CHANGED );
		vsync = (gl_vsync->value > 0) ? true : false;
	}
}

static qboolean VID_SetScreenResolution( int width, int height )
{
	int render_w = width, render_h = height;
	uint rotate = vid_rotate->value;

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

	return true;
}

void GL_SwapBuffers( void )
{
	if ( vsync ) sceDisplayWaitVblankStart();
}

int GL_SetAttribute( int attr, int val )
{
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	return 0;
}

/*
==================
R_Init_Video
==================
*/
qboolean R_Init_Video( const int type )
{
	string safe;
	qboolean retval;

	refState.desktopBitsPixel = ( PSP_FB_BPP * 8 );

	VID_StartupGamma();

	switch( type )
	{
	case REF_SOFTWARE:
		glw_state.software = true;
		break;
	case REF_GL:
		if( !glw_state.safe && Sys_GetParmFromCmdLine( "-safegl", safe ) )
			glw_state.safe = bound( SAFE_NO, Q_atoi( safe ), SAFE_DONTCARE );
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
#if 0
	R_InitVideoModes();
#endif
	host.renderinfo_changed = false;

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, qboolean fullscreen )
{
	Con_Reportf( "R_ChangeDisplaySettings: Setting video mode to %dx%d %s\n", width, height, fullscreen ? "fullscreen" : "windowed" );

	refState.fullScreen = fullscreen;

	if( !VID_SetScreenResolution( width, height ) )
		return rserr_invalid_fullscreen;

	return rserr_ok;
}

/*
==================
VID_SetMode

Set the described video mode
==================
*/
qboolean VID_SetMode( void )
{
	qboolean	fullscreen = false;
	int iScreenWidth, iScreenHeight;
	rserr_t	err;

#if 1
	iScreenWidth = PSP_FB_WIDTH;
	iScreenHeight = PSP_FB_HEIGHT;
#else
	vid_psp.out_width  = PSP_FB_WIDTH;
	vid_psp.out_height = PSP_FB_HEIGHT;

	iScreenWidth = Cvar_VariableInteger( "width" );
	iScreenHeight = Cvar_VariableInteger( "height" );

	if( iScreenWidth < PSP_MIN_MAR_W ||
		iScreenHeight < PSP_MIN_MAR_H )	// trying to get resolution automatically by default
	{
		iScreenWidth = PSP_MIN_MAR_W;
		iScreenHeight = PSP_MIN_MAR_H;
	}

	if( iScreenWidth > PSP_MAX_MAR_W ||
		iScreenHeight > PSP_MAX_MAR_H )	// trying to get resolution automatically by default
	{
		iScreenWidth = PSP_MAX_MAR_W;
		iScreenHeight = PSP_MAX_MAR_H;
	}
#endif
	if( !FBitSet( vid_fullscreen->flags, FCVAR_CHANGED ) )
		Cvar_SetValue( "fullscreen", DEFAULT_FULLSCREEN );
	else
		ClearBits( vid_fullscreen->flags, FCVAR_CHANGED );

	SetBits( gl_vsync->flags, FCVAR_CHANGED );
	fullscreen = true;//Cvar_VariableInteger("fullscreen") != 0;

	if(( err = R_ChangeDisplaySettings( iScreenWidth, iScreenHeight, fullscreen )) == rserr_ok )
	{
#if 0
		vid_psp.in_width   = iScreenWidth;
		vid_psp.in_height  = iScreenHeight;
#endif
	}
	else
		return false;

	return true;
}

/*
==================
R_Free_Video
==================
*/
void R_Free_Video( void )
{
#if 0
	R_FreeVideoModes();
#endif
	ref.dllFuncs.GL_ClearExtensions();
	if( glw_state.software )
	{
		if( vid_psp.draw_buffer )
			free( vid_psp.draw_buffer );
		if( vid_psp.disp_buffer )
			free( vid_psp.disp_buffer );
		
		vid_psp.draw_buffer = NULL;
		vid_psp.disp_buffer = NULL;
	}
}

#endif // XASH_DEDICATED
#endif // XASH_VIDEO