#include "platform/platform.h"

#if XASH_VIDEO == VIDEO_OGC
#include "common.h"
#include "client.h"
#include "ref_common.h"
#include "filesystem.h"
#include "vid_common.h"

#include <GL/gl.h>
#include <opengx.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glext.h>
#include <hal/video.h>
#include <string.h>

/*
=============
GL proc address lookup table
=============
*/
typedef struct
{
	const char *name;
	void *func;
} gl_func_t;

static const gl_func_t gl_funcs[] =
{
	/* GL 1.1 core */
	{ "glAlphaFunc", (void *)glAlphaFunc },
	{ "glBegin", (void *)glBegin },
	{ "glBindTexture", (void *)glBindTexture },
	{ "glBlendFunc", (void *)glBlendFunc },
	{ "glClear", (void *)glClear },
	{ "glClearColor", (void *)glClearColor },
	{ "glClearDepth", (void *)glClearDepth },
	{ "glClearStencil", (void *)glClearStencil },
	{ "glColor3f", (void *)glColor3f },
	{ "glColor3fv", (void *)glColor3fv },
	{ "glColor3ub", (void *)glColor3ub },
	{ "glColor3ubv", (void *)glColor3ubv },
	{ "glColor4f", (void *)glColor4f },
	{ "glColor4fv", (void *)glColor4fv },
	{ "glColor4ub", (void *)glColor4ub },
	{ "glColor4ubv", (void *)glColor4ubv },
	{ "glColorMask", (void *)glColorMask },
	{ "glColorMaterial", (void *)glColorMaterial },
	{ "glColorPointer", (void *)glColorPointer },
	{ "glCopyTexImage2D", (void *)glCopyTexImage2D },
	{ "glCopyTexSubImage2D", (void *)glCopyTexSubImage2D },
	{ "glCullFace", (void *)glCullFace },
	{ "glDeleteTextures", (void *)glDeleteTextures },
	{ "glDepthFunc", (void *)glDepthFunc },
	{ "glDepthMask", (void *)glDepthMask },
	{ "glDepthRange", (void *)glDepthRange },
	{ "glDisable", (void *)glDisable },
	{ "glDisableClientState", (void *)glDisableClientState },
	{ "glDrawArrays", (void *)glDrawArrays },
	{ "glDrawElements", (void *)glDrawElements },
	{ "glEnable", (void *)glEnable },
	{ "glEnableClientState", (void *)glEnableClientState },
	{ "glEnd", (void *)glEnd },
	{ "glFinish", (void *)glFinish },
	{ "glFlush", (void *)glFlush },
	{ "glFogf", (void *)glFogf },
	{ "glFogfv", (void *)glFogfv },
	{ "glFogi", (void *)glFogi },
	{ "glFogiv", (void *)glFogiv },
	{ "glFrontFace", (void *)glFrontFace },
	{ "glFrustum", (void *)glFrustum },
	{ "glGenTextures", (void *)glGenTextures },
	{ "glGetBooleanv", (void *)glGetBooleanv },
	{ "glGetError", (void *)glGetError },
	{ "glGetFloatv", (void *)glGetFloatv },
	{ "glGetIntegerv", (void *)glGetIntegerv },
	{ "glGetString", (void *)glGetString },
	{ "glGetTexImage", (void *)glGetTexImage },
	{ "glGetTexParameterfv", (void *)glGetTexParameterfv },
	{ "glGetTexParameteriv", (void *)glGetTexParameteriv },
	{ "glHint", (void *)glHint },
	{ "glIsEnabled", (void *)glIsEnabled },
	{ "glIsTexture", (void *)glIsTexture },
	{ "glLightModelf", (void *)glLightModelf },
	{ "glLightModelfv", (void *)glLightModelfv },
	{ "glLightModeli", (void *)glLightModeli },
	{ "glLightModeliv", (void *)glLightModeliv },
	{ "glLightf", (void *)glLightf },
	{ "glLightfv", (void *)glLightfv },
	{ "glLighti", (void *)glLighti },
	{ "glLightiv", (void *)glLightiv },
	{ "glLineWidth", (void *)glLineWidth },
	{ "glLoadIdentity", (void *)glLoadIdentity },
	{ "glLoadMatrixd", (void *)glLoadMatrixd },
	{ "glLoadMatrixf", (void *)glLoadMatrixf },
	{ "glMaterialf", (void *)glMaterialf },
	{ "glMaterialfv", (void *)glMaterialfv },
	{ "glMatrixMode", (void *)glMatrixMode },
	{ "glMultMatrixd", (void *)glMultMatrixd },
	{ "glMultMatrixf", (void *)glMultMatrixf },
	{ "glNormal3d", (void *)glNormal3d },
	{ "glNormal3dv", (void *)glNormal3dv },
	{ "glNormal3f", (void *)glNormal3f },
	{ "glNormal3fv", (void *)glNormal3fv },
	{ "glNormalPointer", (void *)glNormalPointer },
	{ "glOrtho", (void *)glOrtho },
	{ "glPixelStorei", (void *)glPixelStorei },
	{ "glPointSize", (void *)glPointSize },
	{ "glPolygonMode", (void *)glPolygonMode },
	{ "glPolygonOffset", (void *)glPolygonOffset },
	{ "glPopMatrix", (void *)glPopMatrix },
	{ "glPushMatrix", (void *)glPushMatrix },
	{ "glReadPixels", (void *)glReadPixels },
	{ "glRectf", (void *)glRectf },
	{ "glRectfv", (void *)glRectfv },
	{ "glRecti", (void *)glRecti },
	{ "glRectiv", (void *)glRectiv },
	{ "glRotated", (void *)glRotated },
	{ "glRotatef", (void *)glRotatef },
	{ "glScaled", (void *)glScaled },
	{ "glScalef", (void *)glScalef },
	{ "glScissor", (void *)glScissor },
	{ "glShadeModel", (void *)glShadeModel },
	{ "glStencilFunc", (void *)glStencilFunc },
	{ "glStencilMask", (void *)glStencilMask },
	{ "glStencilOp", (void *)glStencilOp },
	{ "glTexCoord2f", (void *)glTexCoord2f },
	{ "glTexCoord2fv", (void *)glTexCoord2fv },
	{ "glTexCoord2i", (void *)glTexCoord2i },
	{ "glTexCoord2iv", (void *)glTexCoord2iv },
	{ "glTexCoord3f", (void *)glTexCoord3f },
	{ "glTexCoord3fv", (void *)glTexCoord3fv },
	{ "glTexCoord3i", (void *)glTexCoord3i },
	{ "glTexCoord3iv", (void *)glTexCoord3iv },
	{ "glTexCoord4f", (void *)glTexCoord4f },
	{ "glTexCoordPointer", (void *)glTexCoordPointer },
	{ "glTexEnvf", (void *)glTexEnvf },
	{ "glTexEnvfv", (void *)glTexEnvfv },
	{ "glTexEnvi", (void *)glTexEnvi },
	{ "glTexEnviv", (void *)glTexEnviv },
	{ "glTexGenf", (void *)glTexGenf },
	{ "glTexGenfv", (void *)glTexGenfv },
	{ "glTexGeni", (void *)glTexGeni },
	{ "glTexGeniv", (void *)glTexGeniv },
	{ "glTexImage2D", (void *)glTexImage2D },
	{ "glColorTableEXT", (void *)glColorTableEXT },
	{ "glTexParameterf", (void *)glTexParameterf },
	{ "glTexParameteri", (void *)glTexParameteri },
	{ "glTexSubImage2D", (void *)glTexSubImage2D },
	{ "glTranslated", (void *)glTranslated },
	{ "glTranslatef", (void *)glTranslatef },
	{ "glVertex2f", (void *)glVertex2f },
	{ "glVertex2fv", (void *)glVertex2fv },
	{ "glVertex2i", (void *)glVertex2i },
	{ "glVertex2iv", (void *)glVertex2iv },
	{ "glVertex3f", (void *)glVertex3f },
	{ "glVertex3fv", (void *)glVertex3fv },
	{ "glVertex3i", (void *)glVertex3i },
	{ "glVertex3iv", (void *)glVertex3iv },
	{ "glVertex4f", (void *)glVertex4f },
	{ "glVertex4fv", (void *)glVertex4fv },
	{ "glVertex4i", (void *)glVertex4i },
	{ "glVertex4iv", (void *)glVertex4iv },
	{ "glVertexPointer", (void *)glVertexPointer },
	{ "glViewport", (void *)glViewport },

	/* GL 1.3+ / ARB extensions provided by pbgl */
	{ "glActiveTexture", (void *)glActiveTexture },
	{ "glActiveTextureARB", (void *)glActiveTexture },
	{ "glClientActiveTexture", (void *)glClientActiveTexture },
	{ "glClientActiveTextureARB", (void *)glClientActiveTexture },
	{ "glMultiTexCoord2f", (void *)glMultiTexCoord2f },
	{ "glMultiTexCoord2fARB", (void *)glMultiTexCoord2f },
	{ "glMultiTexCoord3f", (void *)glMultiTexCoord3f },
	{ "glMultiTexCoord3fARB", (void *)glMultiTexCoord3f },
	{ "glMultiTexCoord4f", (void *)glMultiTexCoord4f },
	{ "glMultiTexCoord4fARB", (void *)glMultiTexCoord4f },
	{ "glLoadTransposeMatrixf", (void *)glLoadTransposeMatrixf },
	{ "glLoadTransposeMatrixd", (void *)glLoadTransposeMatrixd },
	{ "glMultTransposeMatrixf", (void *)glMultTransposeMatrixf },
	{ "glMultTransposeMatrixd", (void *)glMultTransposeMatrixd },

	/* EXT / misc */
	{ "glFogCoordf", (void *)glFogCoordf },
	{ "glFogCoordPointer", (void *)glFogCoordPointer },
	{ "glSecondaryColor3f", (void *)glSecondaryColor3f },
	{ "glSecondaryColor4f", (void *)glSecondaryColor4f },
	{ "glSecondaryColor3ub", (void *)glSecondaryColor3ub },
	{ "glSecondaryColorPointer", (void *)glSecondaryColorPointer },
	{ "glBlendColor", (void *)glBlendColor },
	{ "glBlendEquation", (void *)glBlendEquation },
	{ "glColorTableEXT", (void *)glColorTableEXT },
	{ "glGenerateMipmap", (void *)glGenerateMipmap },

	{ NULL, NULL }
};

qboolean R_Init_Video( ref_graphic_apis_t type ){

	qboolean retval;

	refState.desktopBitsPixel = 16;

	if( !(retval = VID_SetMode()) )
	{
		return retval;
	}

	ogx_initialize();

	ref.dllFuncs.GL_InitExtensions();

	glw_state.safe = 0;

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

qboolean GL_DeleteContext( void )
{
	return false;
}

static qboolean GL_CreateContext( void )
{
	return true;
}

static qboolean GL_UpdateContext( void )
{
	return true;
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
#endif //XASH_VIDEO == VIDEO_OGC
