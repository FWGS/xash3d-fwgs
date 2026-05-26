#include "platform/platform.h"

#if XASH_VIDEO == VIDEO_WII
#include "common.h"
#include "client.h"
#include "ref_common.h"
#include "filesystem.h"
#include "vid_common.h"

qboolean R_Init_Video( ref_graphic_apis_t type ){

	qboolean retval;

	refState.desktopBitsPixel = 32;

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	ref.dllFuncs.GL_InitExtensions();

	host.renderinfo_changed = false;

	return true;
}

rserr_t R_ChangeDisplaySettings( int width, int height, window_mode_t window_mode )
{
	int render_w, render_h;

	width = 640;
	height = 480;

	render_w = width;
	render_h = height;

	Con_Reportf( "%s: %dx%d\n", __func__, width, height );

	R_SaveVideoMode( width, height, render_w, render_h, false );

	return rserr_ok;
}

int R_MaxVideoModes( void )
{
	return 1;
}

vidmode_t *R_GetVideoMode( int num )
{
	return NULL;
}

void R_Free_Video( void )
{
	// Free any allocated video resources if necessary
	// VID_DestroyWindow();
	// R_FreeVideoModes();
	ref.dllFuncs.GL_ClearExtensions();
}

//Some software rendering stuff, stolen from the xbox port
qboolean SW_CreateBuffer( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b ){return false;}
void *SW_LockBuffer( void ){return NULL;}
void SW_UnlockBuffer( void ){}
void VID_RestoreScreenResolution( void ){}
void VID_Info_f( void ){}
#endif //XASH_VIDEO == VIDEO_WII
