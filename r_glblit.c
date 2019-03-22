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
unsigned short *buffer;

#define LOAD(x) p##x = gEngfuncs.GL_GetProcAddress(#x)

void R_BuildScreenMap()
{
	int i;
#ifdef SEPARATE_BLIT
	for( i = 0; i < 256; i++ )
	{
		unsigned int r,g,b;

		// 332 to 565
		r = ((i >> (8 - 3) )<< 2 ) & MASK(5);
		g = ((i >> (8 - 3 - 3)) << 3) & MASK(6);
		b = ((i >> (8 - 3 - 3 - 2)) << 3) & MASK(5);
		vid.screen_major[i] = r << (6 + 5) | (g << 5) | b;


		// restore minor GBRGBRGB
		r = MOVE_BIT(i, 5, 1) | MOVE_BIT(i, 2, 0);
		g = MOVE_BIT(i, 7, 2) | MOVE_BIT(i, 4, 1) | MOVE_BIT(i, 1, 0);
		b = MOVE_BIT(i, 6, 2) | MOVE_BIT(i, 3, 1) | MOVE_BIT(i, 0, 0);
		vid.screen_minor[i] = r << (6 + 5) | (g << 5) | b;

	}
#else
	for( i = 0; i < 256; i++ )
	{
		unsigned int r,g,b , major, j;

		// 332 to 565
		r = ((i >> (8 - 3) )<< 2 ) & MASK(5);
		g = ((i >> (8 - 3 - 3)) << 3) & MASK(6);
		b = ((i >> (8 - 3 - 3 - 2)) << 3) & MASK(5);
		major = r << (6 + 5) | (g << 5) | b;


		for( j = 0; j < 256; j++ )
		{
			// restore minor GBRGBRGB
			r = MOVE_BIT(j, 5, 1) | MOVE_BIT(j, 2, 0);
			g = MOVE_BIT(j, 7, 2) | MOVE_BIT(j, 4, 1) | MOVE_BIT(j, 1, 0);
			b = MOVE_BIT(j, 6, 2) | MOVE_BIT(j, 3, 1) | MOVE_BIT(j, 0, 0);
			vid.screen[(i<<8)|j] = r << (6 + 5) | (g << 5) | b | major;

		}

	}
#endif
}

#define FOR_EACH_COLOR(x) 	for( r##x = 0; r##x < BIT(3); r##x++ ) for( g##x = 0; g##x < BIT(3); g##x++ ) for( b##x = 0; b##x < BIT(2); b##x++ )

void R_BuildBlendMaps()
{
	unsigned int r1, g1, b1;
	unsigned int r2, g2, b2;

	FOR_EACH_COLOR(1)FOR_EACH_COLOR(2)
	{
		unsigned int r, g, b;
		unsigned short index1 = r1 << (2 + 3) | g1 << 2 | b1;
		unsigned short index2 = (r2 << (2 + 3) | g2 << 2 | b2) << 8;
		unsigned int a;

		r = r1 + r2;
		g = g1 + g2;
		b = b1 + b2;
		if( r > MASK(2) )
			r = MASK(2);
		if( g > MASK(2) )
			g = MASK(2);
		if( b > MASK(1) )
			b = MASK(1);
		ASSERT(!vid.addmap[index2|index1]);

		vid.addmap[index2|index1] =  r << (2 + 3) | g << 2 | b;
		r = r1 * r2 / MASK(2);
		g = g1 * g2 / MASK(2);
		b = b1 * b2 / MASK(1);

		vid.modmap[index2|index1] =  r << (2 + 3) | g << 2 | b;

		for( a = 0; a < 8; a++ )
		{
			r = r1 * (7 - a) / 7 + r2 * a / 7;
			g = g1 * (7 - a) / 7 + g2 * a / 7;
			b = b1 * (7 - a) / 7 + b2 * a / 7;
			//if( b == 1 ) b = 0;
			vid.alphamap[a << 16|index2|index1] =  r << (2 + 3) | g << 2 | b;
		}

	}
}

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
#ifdef GLDEBUG
	if( gpGlobals->developer )
	{
		gEngfuncs.Con_Reportf( "Installing GL_DebugOutput...\n");
		pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

		// force everything to happen in the main thread instead of in a separate driver thread
		pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );
	}

	// enable all the low priority messages
	pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
#endif

	buffer = Mem_Malloc( r_temppool, 1920*1080*2 );

	R_BuildScreenMap();
	R_BuildBlendMaps();
}

void R_BlitScreen()
{
	//memset( vid.buffer, 10, vid.width * vid.height );
	int i;
	byte *buf = vid.buffer;

	for( i = 0; i < vid.width * vid.height;i++)
	{
#ifdef SEPARATE_BLIT
		// need only 1024 bytes table, but slower
		// wtf?? maybe some prefetch???
		byte major = buf[(i<<1)+1];
		byte minor = buf[(i<<1)];

		buffer[i] = vid.screen_major[major] |vid.screen_minor[minor];
#else
		buffer[i] = vid.screen[vid.buffer[i]];
#endif
	}

	pglViewport( 0, 0, gpGlobals->width, gpGlobals->height );
	pglMatrixMode( GL_PROJECTION );
	pglLoadIdentity();
	pglOrtho( 0, gpGlobals->width, gpGlobals->height, 0, -99999, 99999 );
	pglMatrixMode( GL_MODELVIEW );
	pglLoadIdentity();

	pglEnable( GL_TEXTURE_2D );
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buffer );
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
//	memset( vid.buffer, 0, vid.width * vid.height * 2 );
}
