/*
vid_dos.c - DOS VGA/VESA driver
Copyright (C) 2020 mittorn

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
#if XASH_VIDEO == VIDEO_DOS
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "vid_common.h"
#include <fcntl.h>
#include <errno.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void Platform_Minimize_f( void )
{
	// stub
}

/*
========================
Android_SwapBuffers

Update screen. Use native EGL if possible
========================
*/
void GL_SwapBuffers( void )
{
}

static void DOS_GetScreenRes( int *x, int *y )
{
	*x = 320;
	*y = 200;
}

qboolean  R_Init_Video( const int type )
{
	qboolean retval;

	if( type != REF_SOFTWARE )
		return false; /// glide???

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	host.renderinfo_changed = false;

	return true;
}

void R_Free_Video( void )
{
	// restore text mode
	union REGS regs;
	regs.w.ax = 3;
	int386(0x10,&regs,&regs);

	ref.dllFuncs.GL_ClearExtensions();
}


qboolean VID_SetMode( void )
{
	R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_FULLSCREEN ); // width and height are ignored anyway

	return true;
}

rserr_t   R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	int render_w, render_h;
	uint rotate = vid_rotate->value;

	DOS_GetScreenRes( &width, &height );

	render_w = width;
	render_h = height;

	Con_Reportf( "%s: forced resolution to %dx%d)\n", __func__, width, height );

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

	return rserr_ok;
}

int GL_SetAttribute( int attr, int val )
{
	return 0;
}

int GL_GetAttribute( int attr, int *val )
{
	return 0;
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
	return NULL;
}

static qboolean vsync;

void GL_UpdateSwapInterval( void )
{
	if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );
		vsync = gl_vsync.value;
	}
}

/*
================================

VESA structures

================================
*/

typedef struct RealPointer
{
	short			Segment;	//The real mode segment (offset is 0).
	short			Selector;	//In protected mode, you need to chuck this dude into a segment register and use an offset 0.
} RealPointer;

typedef struct VesaInfo
{
	char		Signature[4];
	short		Version;
	short		OEMNameOffset;
	short		OEMNameSegment;			//Pointer to OEM name?
	char		Capabilities[4];
	short		SupportedModesOffset;
	short		SupportedModesSegment;		//Pointer to list of supported VESA and OEM modes (terminated with 0xffff).
	char		Reserved[238];
} VesaInfo;

typedef struct VesaModeData
{
	short		ModeAttributes;
	char		WindowAAttributes;
	char		WindowBAttributes;
	short		WindowGranularity;
	short		WindowSize;
	short		StartSegmentWindowA;
	short		StartSegmentWindowB;
	void		(*WindowPositioningFunction)(int page);
	short		BytesPerScanLine;

	//Remainder of this structure is optional for VESA modes in v1.0/1.1, needed for OEM modes.

	short		PixelWidth;
	short		PixelHeight;
	char		CharacterCellPixelWidth;
	char		CharacterCellPixelHeight;
	char		NumberOfMemoryPlanes;
	char		BitsPerPixel;
	char		NumberOfBanks;
	char		MemoryModelType;
	char		SizeOfBank;
	char		NumberOfImagePages;
	char		Reserved1;

	//VBE v1.2+

	char		RedMaskSize;
	char		RedFieldPosition;
	char		GreenMaskSize;
	char		GreenFieldPosition;
	char		BlueMaskSize;
	char		BlueFieldPosition;
	char		ReservedMaskSize;
	char		ReservedFieldPosition;
	char		DirectColourModeInfo;
	char		Reserved2[216];
} VesaModeData;

typedef struct RMREGS
{
	int		edi;
	int		esi;
	int		ebp;
	int		reserved;
	int		ebx;
	int		edx;
	int		ecx;
	int		eax;
	short		flags;
	short		es,ds,fs,gs,ip,cs,sp,ss;
} RMREGS;

static VesaInfo	far	*vesa_info		= NULL;
static VesaModeData	far	*vesa_mode_data		= NULL;
static int			vesa_granularity;
static int			vesa_page;
static int			vesa_width;
static int			vesa_height;
static int			vesa_bits_per_pixel;

static RealPointer				vesa_info_rp;
static RealPointer				vesa_mode_data_rp;


static void far *allocate_dos_memory( RealPointer * rp,int bytes_to_allocate )
{
	void	far	*ptr	= NULL;
	union	REGS		regs;

	bytes_to_allocate = ((bytes_to_allocate + 15) & 0xfffffff0);	//Round up to nearest paragraph.
	memset(&regs,0,sizeof(regs));
	regs.w.ax = 0x100;
	regs.w.bx = (short)(bytes_to_allocate >> 4);		//Allocate dos memory in pages, so convert bytes to pages.
	int386(0x31,&regs,&regs);
	if(regs.x.cflag == 0)
	{	//Everything OK.
		rp->Segment = regs.w.ax;
		rp->Selector = regs.w.dx;
		ptr = MK_FP(regs.w.dx,0);
	}
	return(ptr);
}

/****************************************************************************/
/* Free an area of DOS memory.												*/
/****************************************************************************/

static void free_dos_memory( RealPointer * rp )
{
	union	REGS		regs;

	regs.w.ax = 0x101;
	regs.w.dx = rp->Selector;
	int386(0x31,&regs,&regs);
}


/****************************************************************************/
/* Get information about the VESA driver.					*/
/*										*/
/* Returns:									*/
/*	TRUE		-	VESA driver present.  vesa_info set to point to a	*/
/*					a structure containing capability info etc.	*/
/*	FALSE		-	No VESA driver.					*/
/*										*/
/****************************************************************************/

static int vesa_get_info( void )
{
    union		REGS	regs;
    struct		SREGS	sregs;
    RMREGS	rmregs;
    int ok = 0;

    if((vesa_info = (VesaInfo far *)allocate_dos_memory(&vesa_info_rp,sizeof(VesaInfo))) != NULL)
    {
	memset(&rmregs,0,sizeof(rmregs));
	memset(&regs,0,sizeof(regs));
	memset(&sregs,0,sizeof(sregs));
	segread(&sregs);
	rmregs.eax = 0x4f00;
	rmregs.es = vesa_info_rp.Segment;
	rmregs.ds = vesa_info_rp.Segment;
	regs.x.eax = 0x300;
	regs.x.ebx = 0x10;
	regs.x.ecx = 0;
	sregs.es = FP_SEG(&rmregs);
	regs.x.edi = FP_OFF(&rmregs);
	int386x(0x31,&regs,&regs,&sregs);	//Get vesa info.

	if(rmregs.eax == 0x4f) ok=1;
    }
    return(ok);
}

/****************************************************************************/
/* Free the information allocated with vesa_get_info()						*/
/****************************************************************************/

static int vesa_free_info( void )
{
	free_dos_memory( &vesa_info_rp );
}

/****************************************************************************/
/* Get VESA mode information.  Information is loaded into vesa_mode_data.	*/
/* vesa_get_info() must be called before calling this function.				*/
/* Should error check this really.											*/
/*																			*/
/* Inputs:																	*/
/*	vmode	-	The mode that you wish to get information about.			*/
/*																			*/
/* Returns:																	*/
/*	TRUE	-	vesa_mode_data points to a filled VesaModeData structure.	*/
/*	FALSE	-	vesa_mode_data probably invalid.  Mode probably not			*/
/*				supported.													*/
/*																			*/
/****************************************************************************/

static int vesa_get_mode_info( short vmode )
{
	union	REGS	regs;
	struct	SREGS	sregs;
	RMREGS			rmregs;
	int ok=0;

	if((vesa_mode_data = (VesaModeData far *)allocate_dos_memory(&vesa_mode_data_rp,sizeof(VesaModeData))) != NULL)
	{
		memset(&rmregs,0,sizeof(rmregs));
		rmregs.es = vesa_mode_data_rp.Segment;
		rmregs.ds = vesa_mode_data_rp.Segment;
		rmregs.edi = 0;
		rmregs.eax = 0x4f01;
		rmregs.ecx = vmode;
		memset(&regs,0,sizeof(regs));
		memset(&sregs,0,sizeof(sregs));
		segread(&sregs);
		regs.x.eax = 0x300;
		regs.x.ebx = 0x10;
		regs.x.edi = (int)&rmregs;
		int386x(0x31,&regs,&regs,&sregs);
		if(regs.h.al == 0)
		{

			//cache a few important items in protected mode memory area.

			vesa_granularity	= vesa_mode_data->WindowGranularity;
			vesa_width			= vesa_mode_data->PixelWidth;
			vesa_height			= vesa_mode_data->PixelHeight;
			vesa_bits_per_pixel	= vesa_mode_data->BitsPerPixel;
			ok = 1;
		}
	}
	return(ok);
}

/****************************************************************************/
/* Set the current vesa screen page.										*/
/*																			*/
/* Inputs:																	*/
/*	vpage	-	Page number to set.											*/
/*																			*/
/****************************************************************************/

static void vesa_set_page( int vpage )
{
	union	REGS	regs;

	vesa_page = vpage;
	regs.w.ax = 0x4f05;
	regs.w.bx = 0;
	regs.w.cx = 0;
	regs.w.dx = (short)(vpage == 0 ? 0 : (short)((vpage * 64) / vesa_granularity));
	int386(0x10,&regs,&regs);
	regs.w.ax = 0x4f05;
	regs.w.bx = 1;
	regs.w.cx = 0;
	regs.w.dx = (short)(vpage == 0 ? 0 : (short)((vpage * 64) / vesa_granularity));
	int386(0x10,&regs,&regs);
}

/****************************************************************************/
/* Set VESA video mode.  You MUST have previously called vesa_get_info().	*/
/*																			*/
/* Inputs:																	*/
/*	vmode	-	The VESA mode that you want. e.g. 0x101 is 640x480 256		*/
/*				colours.													*/
/*																			*/
/* Returns:																	*/
/*	TRUE	-	The mode has been set.										*/
/*	FALSE	-	The mode has not been set, probably not supported.			*/
/*																			*/
/****************************************************************************/

int vesa_set_mode( short vmode )
{
	union	REGS	regs;
	int ok=0;

	memset(&regs,0,sizeof(regs));
	regs.w.ax = 0x4f02;
	regs.w.bx = vmode;
	int386(0x10,&regs,&regs);
	if(regs.w.ax == 0x004f)
	{
		vesa_get_mode_info(vmode);
		vesa_set_page(0);			//Probably not needed, but here for completeness.
		ok = 1;
	}
	return(ok);
}

/****************************************************************************/
/* Free the info previously allocated with vesa_get_info()					*/
/****************************************************************************/

void vesa_free_mode_info( void )
{
	free_dos_memory(&vesa_mode_data_rp);
}

void init_13h( void )
{
	int r, g, b;
	union	REGS	regs;

	// set 13h vga mode
	memset(&regs,0,sizeof(regs));
	regs.w.ax = 0x13;
	int386(0x10,&regs,&regs);

	// set 332 palette
	outp(0x3c8, 0);
	for (r=0; r<8; r++)
		for (b=0; b<4; b++)
			for (g=0; g<8; g++)
			{
				outp(0x3c9, (b<<4)+8);
				outp(0x3c9, (g<<3)+4);
				outp(0x3c9, (r<<3)+4);
			}
}



void waitvbl()
{
    while (!(inp (0x3da) & 8)) {}
    while (inp (0x3da) & 8) {}
}


static byte *fb;

void *SW_LockBuffer( void )
{
	return fb;
}

void SW_UnlockBuffer( void )
{

	int left = 320*200*2;
	int done = 0;
	int vpage = 0;

	if( vsync )
		waitvbl();

	// no separate fb
	if( fb == 0xa0000 )
	    return;

	while( left )
	{
		int flip = left;

		if( flip > 65536 )
			flip = 65536;

		vesa_set_page(vpage);
		memcpy((void *)0xa0000, fb+done, flip);

		done += flip;
		left -= flip;
		vpage++;
	}
}

#define FB_BF_TO_MASK(x) (((1 << x.length) - 1) << (x.offset))

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	int i;

	*stride = 320;

	vesa_get_info();

	if( !Sys_CheckParm("-novesa") && vesa_set_mode( 0x10E ) )
	{
		fb = malloc( 320*200*2 );
		*bpp = 2;
		*b = 31;
		*g = 63 << 5;
		*r = 31 << 11;
	}
	else
	{
		// fallback to 256 color
		*b = 2 << 6;
		*g = 7 << 0;
		*r = 7 << 3;
		*bpp = 1;
		init_13h();
		fb = 0xa0000;
	}

	return true;
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	return REF_WINDOW_TYPE_NULL;
}

#endif
