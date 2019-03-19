#include "r_local.h"
#include "../ref_gl/gl_export.h"


/*
========================
DebugCallback

For ARB_debug_output
========================
*/
static void APIENTRY GL_DebugOutput( GLuint source, GLuint type, GLuint id, GLuint severity, GLint length, const GLcharARB *message, GLvoid *userParam )
{
	switch( type )
	{
	case GL_DEBUG_TYPE_ERROR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_ERROR "%s\n", message );
		break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		gEngfuncs.Con_Reportf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	case GL_DEBUG_TYPE_OTHER_ARB:
	default:
		gEngfuncs.Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	}
}
int tex;

#define LOAD(x) p##x = gEngfuncs.GL_GetProcAddress(#x)
void R_InitBlit()
{
	LOAD(glBegin);
	LOAD(glEnd);
	LOAD(glTexCoord2f);
	LOAD(glVertex2f);
	LOAD(glEnable);
	LOAD(glDisable);
	LOAD(glTexImage2D);
	LOAD(glOrtho);
	LOAD(glMatrixMode);
	LOAD(glLoadIdentity);
	LOAD(glViewport);
	LOAD(glBindTexture);
	LOAD(glDebugMessageCallbackARB);
	LOAD(glDebugMessageControlARB);
	LOAD(glGetError);
	LOAD(glGenTextures);
	LOAD(glTexParameteri);

	if( gpGlobals->developer )
	{
		gEngfuncs.Con_Reportf( "Installing GL_DebugOutput...\n");
		pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

		// force everything to happen in the main thread instead of in a separate driver thread
		pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );
	}

	// enable all the low priority messages
	pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
	pglGenTextures( 1, &tex );
}


void R_BlitScreen()
{
	//memset( vid.buffer, 10, vid.width * vid.height );
	pglBindTexture(GL_TEXTURE_2D, tex);
	pglViewport( 0, 0, gpGlobals->width, gpGlobals->height );
	pglMatrixMode( GL_PROJECTION );
	pglLoadIdentity();
	pglOrtho( 0, gpGlobals->width, gpGlobals->height, 0, -99999, 99999 );
	pglMatrixMode( GL_MODELVIEW );
	pglLoadIdentity();

	pglEnable( GL_TEXTURE_2D );
	pglBindTexture(GL_TEXTURE_2D, tex);
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//gEngfuncs.Con_Printf("%d\n",pglGetError());
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, vid.buffer );
	//gEngfuncs.Con_Printf("%d\n",pglGetError());
	pglBegin( GL_QUADS );
		pglTexCoord2f( 0, 0 );
		pglVertex2f( 0, 0 );

		pglTexCoord2f( 1, 0 );
		pglVertex2f( vid.width, 0 );

		pglTexCoord2f( 1, 1 );
		pglVertex2f( vid.width, vid.height );

		pglTexCoord2f( 0, 1 );
		pglVertex2f( 0, vid.height );
	pglEnd();
	pglDisable( GL_TEXTURE_2D );
	gEngfuncs.GL_SwapBuffers();
}
