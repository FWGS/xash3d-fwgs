#include "platform/platform.h"
#if XASH_VIDEO == VIDEO_FBDEV
#include "input.h"
#include "client.h"
#include "filesystem.h"
#include "vid_common.h"
#include <fcntl.h>
#include <errno.h>

#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#if XASH_ANDROID
#include <linux/kd.h>
#else
#include <sys/kd.h>
#endif

struct fb_s
{
	int fd, tty_fd;
	void *map;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	qboolean vsync;
	int doublebuffer;
} fb;

#define DEFAULT_FBDEV "/dev/fb0"

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

void FB_GetScreenRes( int *x, int *y )
{
	*x = fb.vinfo.xres;
	*y = fb.vinfo.yres;
}

qboolean  R_Init_Video( const int type )
{
	qboolean retval;
	string fbdev = DEFAULT_FBDEV;
	fb.fd = -1;

	if( type != REF_SOFTWARE )
		return false;

	Sys_GetParmFromCmdLine( "-fbdev", fbdev );

	fb.fd = open( fbdev, O_RDWR );

	if( fb.fd < 0 )
	{
		Con_Printf( S_ERROR "failed to open framebuffer device: %s\n", strerror(errno));
	}

	if( Sys_CheckParm( "-ttygfx" ) )
		fb.tty_fd = open( "/dev/tty", O_RDWR ); // only need this to set graphics mode, optional

	ioctl(fb.fd, FBIOGET_FSCREENINFO, &fb.finfo);
	ioctl(fb.fd, FBIOGET_VSCREENINFO, &fb.vinfo);

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	host.renderinfo_changed = false;

	return true;
}

void R_Free_Video( void )
{
	// VID_DestroyWindow ();

	// R_FreeVideoModes();
	if( fb.doublebuffer )
	{
		fb.vinfo.yoffset = 0;
		fb.vinfo.yres_virtual >>= 1;
		ioctl( fb.fd, FBIOPAN_DISPLAY, &fb.vinfo );
	}
	if( fb.map )
		munmap( fb.map, fb.finfo.smem_len );
	close( fb.fd );

	fb.fd = -1;
	fb.map = NULL;

	if( fb.tty_fd >= 0 )
	{
		ioctl( fb.tty_fd, KDSETMODE, KD_TEXT );
		close( fb.tty_fd );
		fb.tty_fd = -1;
	}

	ref.dllFuncs.GL_ClearExtensions();
}


qboolean VID_SetMode( void )
{
	if( fb.tty_fd > 0 )
		ioctl( fb.tty_fd, KDSETMODE, KD_GRAPHICS );
	R_ChangeDisplaySettings( 0, 0, WINDOW_MODE_FULLSCREEN ); // width and height are ignored anyway

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	int render_w, render_h;

	FB_GetScreenRes( &width, &height );

	render_w = width;
	render_h = height;

	Con_Reportf( "%s: forced resolution to %dx%d)\n", __func__, width, height );

	VID_SetDisplayTransform( &render_w, &render_h );
	R_SaveVideoMode( width, height, render_w, render_h, false );

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

void GL_UpdateSwapInterval( void )
{
	if( FBitSet( gl_vsync.flags, FCVAR_CHANGED ))
	{
		ClearBits( gl_vsync.flags, FCVAR_CHANGED );
		fb.vsync = gl_vsync.value;
	}
}

void *SW_LockBuffer( void )
{
	if( fb.vsync )
	{
		 int stub = 0;
		 ioctl(fb.fd, FBIO_WAITFORVSYNC, &stub);
	}
	if( fb.doublebuffer )
	{
		static int page = 0;
		page = !page;
		fb.vinfo.yoffset = page * fb.vinfo.yres;
		return fb.map + page * fb.doublebuffer;
	}
	else
		return fb.map;
}

void SW_UnlockBuffer( void )
{
	// some single-buffer fb devices need this too
	ioctl(fb.fd, FBIOPAN_DISPLAY, &fb.vinfo);
}

#define FB_BF_TO_MASK(x) (((1 << x.length) - 1) << (x.offset))

qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	if( width > fb.vinfo.xres_virtual || height > fb.vinfo.yres_virtual )
	{
		Con_Printf( S_ERROR "requested size %dx%d not fit to framebuffer size %dx%d\n",
					width, height, fb.vinfo.xres_virtual, fb.vinfo.yres_virtual );
		return false;
	}

	*bpp = fb.vinfo.bits_per_pixel >> 3;
	*stride = fb.vinfo.xres_virtual;
	*r = FB_BF_TO_MASK(fb.vinfo.red);
	*g = FB_BF_TO_MASK(fb.vinfo.green);
	*b = FB_BF_TO_MASK(fb.vinfo.blue);

	if( Sys_CheckParm("-doublebuffer") )
	{
		fb.doublebuffer = *bpp * *stride * fb.vinfo.yres;
		fb.vinfo.yres_virtual = fb.vinfo.yres * 2;
		if(ioctl (fb.fd, FBIOPUT_VSCREENINFO, &fb.vinfo ))
		{
			fb.vinfo.transp.length = fb.vinfo.transp.offset = 0;
			if( ioctl (fb.fd, FBIOPUT_VSCREENINFO, &fb.vinfo ) )
			{
				Con_Printf( S_ERROR "failed to enable double buffering!\n" );
			}
		}

		ioctl( fb.fd, FBIOGET_FSCREENINFO, &fb.finfo );
		ioctl( fb.fd, FBIOGET_VSCREENINFO, &fb.vinfo );
		ioctl( fb.fd, FBIOPAN_DISPLAY, &fb.vinfo );

		if( fb.finfo.smem_len < fb.doublebuffer * 2 )
		{
			Con_Printf( S_ERROR "not enough memory for double buffering, disabling!\n" );
			fb.doublebuffer = 0;
		}
	}
	if( fb.map )
		munmap(fb.map, fb.finfo.smem_len);
	fb.map = mmap(0, fb.finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);

	if( !fb.map )
		return false;
	return true;
}

ref_window_type_t R_GetWindowHandle( void **handle, ref_window_type_t type )
{
	return REF_WINDOW_TYPE_NULL;
}

#endif
