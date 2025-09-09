#include "r_local.h"
#define APIENTRY_LINKAGE static
#include "../gl/gl_export.h"

struct swblit_s
{
	uint     stride;
	uint     bpp;
	uint     rmask, gmask, bmask;
	void     *(*pLockBuffer)( void );
	void     (*pUnlockBuffer)( void );
	qboolean (*pCreateBuffer)( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b );
	uint     rotate;
	qboolean gl1;
} swblit;


qboolean R_SetDisplayTransform( ref_screen_rotation_t rotate, int offset_x, int offset_y, float scale_x, float scale_y )
{
	qboolean ret = true;
	if( rotate > 1 )
	{
		gEngfuncs.Con_Printf( "only 0-1 rotation supported\n" );
		ret = false;
	}
	else
		swblit.rotate = rotate;

	if( offset_x || offset_y )
	{
		// it is possible implement for offset > 0
		gEngfuncs.Con_Printf( "offset transform not supported\n" );
		ret = false;
	}

	if( scale_x != 1.0f || scale_y != 1.0f )
	{
		// maybe implement 2x2?
		gEngfuncs.Con_Printf( "scale transform not supported\n" );
		ret = false;
	}

	return ret;
}

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
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
		gEngfuncs.Con_Printf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PORTABILITY_ARB:
		gEngfuncs.Con_Reportf( S_OPENGL_WARN "%s\n", message );
		break;
	case GL_DEBUG_TYPE_PERFORMANCE_ARB:
	case GL_DEBUG_TYPE_OTHER_ARB:
	default:
		gEngfuncs.Con_Printf( S_OPENGL_NOTE "%s\n", message );
		break;
	}
}


static unsigned short *glbuf;
static int tex;

#define LOAD( x ) p ## x = gEngfuncs.GL_GetProcAddress(#x ); \
	gEngfuncs.Con_Printf(#x " : %p\n", p ## x )


void GAME_EXPORT GL_SetupAttributes( int safegl )
{
#if GLDEBUG
	gEngfuncs.Con_Reportf( "Creating an extended GL context for debug...\n" );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_FLAGS, REF_GL_CONTEXT_DEBUG_FLAG );
#endif
	// untill we have any blitter in ref api, setup GL
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_PROFILE_MASK, REF_GL_CONTEXT_PROFILE_ES );
	gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_EGL, 1 );
//	safegl=1;
	if( safegl )
	{
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 1 );
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 1 );
		swblit.gl1 = true;
	}
	else
	{
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MAJOR_VERSION, 3 );
		gEngfuncs.GL_SetAttribute( REF_GL_CONTEXT_MINOR_VERSION, 0 );
	}
	gEngfuncs.GL_SetAttribute( REF_GL_DOUBLEBUFFER, 1 );

	gEngfuncs.GL_SetAttribute( REF_GL_RED_SIZE, 5 );
	gEngfuncs.GL_SetAttribute( REF_GL_GREEN_SIZE, 6 );
	gEngfuncs.GL_SetAttribute( REF_GL_BLUE_SIZE, 5 );
}

void (*pglOrthof)( GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar );
void GL_FUNCTION( glBindBuffer )( GLenum target, GLuint buffer );

void GL_FUNCTION( glBufferData )( GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage );
void GL_FUNCTION( glGenBuffers )( GLsizei n, GLuint *buffers );
void GL_FUNCTION( glDeleteBuffers )( GLsizei n, const GLuint *buffers );
GLvoid * GL_FUNCTION( glMapBufferOES )( GLenum target, GLenum access );
GLboolean GL_FUNCTION( glUnmapBufferOES )( GLenum target );
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_FRAMEBUFFER         0x8D40
#define GL_COLOR_ATTACHMENT0   0x8CE0
#define GL_READ_FRAMEBUFFER    0x8CA8
#define GL_DRAW_FRAMEBUFFER    0x8CA9
void GAME_EXPORT GL_InitExtensions( void )
{
	LOAD( glBegin );
	LOAD( glEnd );
	LOAD( glTexCoord2f );
	LOAD( glVertex2f );
	LOAD( glEnable );
	LOAD( glDisable );
	LOAD( glTexImage2D );
	LOAD( glOrtho );
	LOAD( glOrthof );
	LOAD( glMatrixMode );
	LOAD( glLoadIdentity );
	LOAD( glViewport );
	LOAD( glBindTexture );
	LOAD( glDebugMessageCallbackARB );
	LOAD( glDebugMessageControlARB );
	LOAD( glGetError );
	LOAD( glGenTextures );
	LOAD( glTexParameteri );
	LOAD( glEnableClientState );
	LOAD( glDisableClientState );
	LOAD( glVertexPointer );
	LOAD( glTexCoordPointer );
	LOAD( glDrawElements );
	LOAD( glClear );
	LOAD( glClearColor );
	LOAD( glGetString );
	LOAD( glColor4f );
	LOAD( glDrawArrays );
	LOAD( glBindBuffer );
	LOAD( glBufferData );
	LOAD( glGenBuffers );
	LOAD( glDeleteBuffers );
	LOAD( glMapBufferOES );
	if( !pglMapBufferOES )
		pglMapBufferOES = gEngfuncs.GL_GetProcAddress( "glMapBuffer" );
	LOAD( glUnmapBufferOES );
	if( !pglUnmapBufferOES )
		pglUnmapBufferOES = gEngfuncs.GL_GetProcAddress( "glUnmapBuffer" );
	LOAD( glGenFramebuffers );
	LOAD( glBindFramebuffer );
	LOAD( glFramebufferTexture2D );
	LOAD( glBlitFramebuffer );
	LOAD( glGenTextures );
	gEngfuncs.Con_Printf( "version:%s\n", pglGetString( GL_VERSION ));
#if GLDEBUG
	if( gpGlobals->developer )
	{
		gEngfuncs.Con_Reportf( "Installing GL_DebugOutput...\n" );
		pglDebugMessageCallbackARB( GL_DebugOutput, NULL );

		// force everything to happen in the main thread instead of in a separate driver thread
		pglEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB );
	}

	// enable all the low priority messages
	pglDebugMessageControlARB( GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, true );
#endif

}
void GAME_EXPORT GL_ClearExtensions( void )
{

}

static void *R_Lock_GL1( void )
{
	return glbuf;
}

static void R_Unlock_GL1( void )
{

	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, glbuf );
	// gEngfuncs.Con_Printf("%d\n",pglGetError());
	pglBegin( GL_QUADS );
	pglTexCoord2f( 0, 0 );
	pglVertex2f( 0, 0 );

	pglTexCoord2f( 1, 0 );
	pglVertex2f( 1, 0 );

	pglTexCoord2f( 1, 1 );
	pglVertex2f( 1, 1 );

	pglTexCoord2f( 0, 1 );
	pglVertex2f( 0, 1 );
	pglEnd();
	gEngfuncs.GL_SwapBuffers();
}


static void R_Unlock_GLES1( void )
{
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, glbuf );
	pglDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

	gEngfuncs.GL_SwapBuffers();
}

static qboolean R_CreateBuffer_GL1( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	pglViewport( 0, 0, width, height );
	pglMatrixMode( GL_PROJECTION );
	pglLoadIdentity();
	pglOrtho( 0, 1, 1, 0, -99999, 99999 );
	pglMatrixMode( GL_MODELVIEW );
	pglLoadIdentity();

	pglEnable( GL_TEXTURE_2D );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

	if( glbuf )
		Mem_Free( glbuf );

	glbuf = Mem_Malloc( r_temppool, width * height * 2 );

	*stride = width;
	*bpp = 2;
	*r = MASK( 5 ) << ( 6 + 5 );
	*g = MASK( 6 ) << 5;
	*b = MASK( 5 );

	return true;
}

static qboolean R_CreateBuffer_GLES1( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	float data[] = {
		// quad verts match texcoords
		0, 0,
		1, 0,
		1, 1,
		0, 1,
	};
	int   vbo;

	pglViewport( 0, 0, width, height );
	pglMatrixMode( GL_PROJECTION );
	pglLoadIdentity();
	// project 0..1 to screen size
	pglOrthof( 0, 1, 1, 0, -99999, 99999 );
	pglMatrixMode( GL_MODELVIEW );
	pglLoadIdentity();

	pglEnable( GL_TEXTURE_2D );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

	// if( vbo )
	//	pglDeleteBuffers( 1,&vbo );

	pglGenBuffers( 1, &vbo );
	pglBindBuffer( GL_ARRAY_BUFFER_ARB, vbo );
	pglBufferData( GL_ARRAY_BUFFER_ARB, sizeof( data ), data, GL_STATIC_DRAW_ARB );

	pglEnableClientState( GL_VERTEX_ARRAY );
	pglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	pglVertexPointer( 2, GL_FLOAT, 8, 0 );
	pglTexCoordPointer( 2, GL_FLOAT, 8, 0 );
	pglBindBuffer( GL_ARRAY_BUFFER_ARB, 0 );
	pglColor4f( 1, 1, 1, 1 );


	if( glbuf )
		Mem_Free( glbuf );

	glbuf = Mem_Malloc( r_temppool, width * height * 2 );

	*stride = width;
	*bpp = 2;
	*r = MASK( 5 ) << ( 6 + 5 );
	*g = MASK( 6 ) << 5;
	*b = MASK( 5 );

	return true;
}

static void *R_Lock_GLES3( void )
{
	void *buf = NULL;

	if( !vid.width || !vid.height )
		return NULL;

	if( glbuf )
		return glbuf;

	pglBufferData( GL_PIXEL_UNPACK_BUFFER, vid.width * vid.height * 2, 0, GL_STREAM_DRAW_ARB );
	if( pglMapBufferOES )
		buf = pglMapBufferOES( GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY_ARB );
	if( !buf )
	{
		if( pglUnmapBufferOES )
			pglUnmapBufferOES( GL_PIXEL_UNPACK_BUFFER );
		pglBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
		glbuf = Mem_Malloc( r_temppool, vid.width * vid.height * 2 );
		pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, glbuf );
		return glbuf;
	}
	else
		return buf;
}


static void R_Unlock_GLES3( void )
{
	gEngfuncs.GL_SwapBuffers();
	if( glbuf )
		pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, glbuf );
	else
	{
		if( pglUnmapBufferOES )
			pglUnmapBufferOES( GL_PIXEL_UNPACK_BUFFER );
		pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vid.width, vid.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0 );
	}
	// pglDrawArrays( GL_TRIANGLE_FAN, 0,4 );
	pglBlitFramebuffer( 0, vid.height, vid.width, 0, 0, 0, vid.width, vid.height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
}

static qboolean R_CreateBuffer_GLES3( int width, int height, uint *stride, uint *bpp, uint *r, uint *g, uint *b )
{
	float  data[] = {
		// quad verts match texcoords
		0, 0,
		1, 0,
		1, 1,
		0, 1,
	};
	GLuint vbo, pbo, fbo, to;

	// shitty fbo does not work without texture objects :(
	pglGenTextures( 1, &to );
	pglBindTexture( GL_TEXTURE_2D, to );
	pglViewport( 0, 0, width, height );
	/*
	pglMatrixMode( GL_PROJECTION );
	pglLoadIdentity();
	// project 0..1 to screen size
	pglOrtho( 0, 1, 1, 0, -99999, 99999 );
	pglMatrixMode( GL_MODELVIEW );
	pglLoadIdentity();

	pglEnable( GL_TEXTURE_2D );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

	if( vbo )
		pglDeleteBuffers( 1,&vbo );

	if( pbo )
		pglDeleteBuffers( 1,&pbo );
	*/

	// pglGenBuffers( 1,&vbo );
	pglGenBuffers( 1, &pbo );
	// pglBindBuffer( GL_ARRAY_BUFFER_ARB, vbo );
	// pglBufferData( GL_ARRAY_BUFFER_ARB, sizeof(data), data, GL_STATIC_DRAW_ARB );

	// pglEnableClientState( GL_VERTEX_ARRAY );
	// pglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	// pglVertexPointer( 2, GL_FLOAT, 8, 0 );
	// pglTexCoordPointer( 2, GL_FLOAT, 8, 0 );
	// pglBindBuffer( GL_ARRAY_BUFFER_ARB, 0 );

	pglBindBuffer( GL_PIXEL_UNPACK_BUFFER, pbo );
	pglBufferData( GL_PIXEL_UNPACK_BUFFER, width * height * 2, 0, GL_STREAM_DRAW_ARB );
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0 );

	pglGenFramebuffers( 1, &fbo );
	pglBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
	pglFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0 );
	pglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

	// pglColor4f( 1, 1, 1, 1 );


	*stride = width;
	*bpp = 2;
	*r = MASK( 5 ) << ( 6 + 5 );
	*g = MASK( 6 ) << 5;
	*b = MASK( 5 );

	return true;
}



static int FIRST_BIT( uint mask )
{
	uint i;

	for( i = 0; !( BIT( i ) & mask ); i++ )
		;

	return i;
}

static int COUNT_BITS( uint mask )
{
	uint i;

	for( i = 0; mask; mask = mask >> 1 )
		i += mask & 1;

	return i;
}

static void R_BuildScreenMap( void )
{
	int  i;
	uint rshift = FIRST_BIT( swblit.rmask ), gshift = FIRST_BIT( swblit.gmask ), bshift = FIRST_BIT( swblit.bmask );
	uint rbits = COUNT_BITS( swblit.rmask ), gbits = COUNT_BITS( swblit.gmask ), bbits = COUNT_BITS( swblit.bmask );
	uint rmult = BIT( rbits ), gmult = BIT( gbits ), bmult = BIT( bbits );
	uint rdiv = MASK( 5 ), gdiv = MASK( 6 ), bdiv = MASK( 5 );

	gEngfuncs.Con_Printf( "Blit table: %d %d %d %d %d %d\n", rmult, gmult, bmult, rdiv, gdiv, bdiv );

#ifdef SEPARATE_BLIT
	for( i = 0; i < 256; i++ )
	{
		unsigned int r, g, b;

		// 332 to 565
		r = (( i >> ( 8 - 3 )) << 2 ) & MASK( 5 );
		g = (( i >> ( 8 - 3 - 3 )) << 3 ) & MASK( 6 );
		b = (( i >> ( 8 - 3 - 3 - 2 )) << 3 ) & MASK( 5 );
		vid.screen_major[i] = r << ( 6 + 5 ) | ( g << 5 ) | b;


		// restore minor GBRGBRGB
		r = MOVE_BIT( i, 5, 1 ) | MOVE_BIT( i, 2, 0 );
		g = MOVE_BIT( i, 7, 2 ) | MOVE_BIT( i, 4, 1 ) | MOVE_BIT( i, 1, 0 );
		b = MOVE_BIT( i, 6, 2 ) | MOVE_BIT( i, 3, 1 ) | MOVE_BIT( i, 0, 0 );
		vid.screen_minor[i] = r << ( 6 + 5 ) | ( g << 5 ) | b;

	}
#else
	for( i = 0; i < 256; i++ )
	{
		unsigned int r, g, b, major, j;

		// 332 to 565
		r = (( i >> ( 8 - 3 )) << 2 ) & MASK( 5 );
		g = (( i >> ( 8 - 3 - 3 )) << 3 ) & MASK( 6 );
		b = (( i >> ( 8 - 3 - 3 - 2 )) << 3 ) & MASK( 5 );
		// major = r << (6 + 5) | (g << 5) | b;
		major = ( r * rmult / rdiv ) << rshift | ( g * gmult / gdiv ) << gshift | ( b * bmult / bdiv ) << bshift;


		for( j = 0; j < 256; j++ )
		{
			uint minor;
			// restore minor GBRGBRGB
			r = MOVE_BIT( j, 5, 1 ) | MOVE_BIT( j, 2, 0 );
			g = MOVE_BIT( j, 7, 2 ) | MOVE_BIT( j, 4, 1 ) | MOVE_BIT( j, 1, 0 );
			b = MOVE_BIT( j, 6, 2 ) | MOVE_BIT( j, 3, 1 ) | MOVE_BIT( j, 0, 0 );
			// vid.screen[(i<<8)|j] = r << (6 + 5) | (g << 5) | b | major;
			minor = ( r * rmult / rdiv ) << rshift | ( g * gmult / gdiv ) << gshift | ( b * bmult / bdiv ) << bshift;

			if( swblit.bpp == 2 )
				vid.screen[( i << 8 ) | j] = major | minor;
			else
				vid.screen32[( i << 8 ) | j] = major | minor;

		}

	}
#endif
}

#define FOR_EACH_COLOR( x ) for( r ## x = 0; r ## x < BIT( 3 ); r ## x++ ) for( g ## x = 0; g ## x < BIT( 3 ); g ## x++ ) for( b ## x = 0; b ## x < BIT( 2 ); b ## x++ )

static void R_BuildBlendMaps( void )
{
	unsigned int r1, g1, b1;
	unsigned int r2, g2, b2;
	unsigned int i, j;

	FOR_EACH_COLOR( 1 ) FOR_EACH_COLOR( 2 )
	{
		unsigned int   r, g, b;
		unsigned short index1 = r1 << ( 2 + 3 ) | g1 << 2 | b1;
		unsigned short index2 = ( r2 << ( 2 + 3 ) | g2 << 2 | b2 ) << 8;
		unsigned int   a;

		r = r1 + r2;
		g = g1 + g2;
		b = b1 + b2;
		if( r > MASK( 3 ))
			r = MASK( 3 );
		if( g > MASK( 3 ))
			g = MASK( 3 );
		if( b > MASK( 2 ))
			b = MASK( 2 );
		ASSERT( !vid.addmap[index2 | index1] );

		vid.addmap[index2 | index1] = r << ( 2 + 3 ) | g << 2 | b;
		r = r1 * r2 / MASK( 3 );
		g = g1 * g2 / MASK( 3 );
		b = b1 * b2 / MASK( 2 );

		vid.modmap[index2 | index1] = r << ( 2 + 3 ) | g << 2 | b;
	}
	for( i = 0; i < 8192; i++ )
	{
		unsigned int   r, g, b;
		uint           color = i << 3;
		uint           m = color >> 8;
		uint           j = color & 0xff;
		unsigned short index1 = i;

		r1 = (( m >> ( 8 - 3 )) << 2 ) & MASK( 5 );
		g1 = (( m >> ( 8 - 3 - 3 )) << 3 ) & MASK( 6 );
		b1 = (( m >> ( 8 - 3 - 3 - 2 )) << 3 ) & MASK( 5 );
		r1 |= MOVE_BIT( j, 5, 1 ) | MOVE_BIT( j, 2, 0 );
		g1 |= MOVE_BIT( j, 7, 2 ) | MOVE_BIT( j, 4, 1 ) | MOVE_BIT( j, 1, 0 );
		b1 |= MOVE_BIT( j, 6, 2 ) | MOVE_BIT( j, 3, 1 ) | MOVE_BIT( j, 0, 0 );


		for( j = 0; j < 32; j++ )
		{
			unsigned int index2 = j << 13;
			unsigned int major, minor;
			r = r1 * j / 32;
			g = g1 * j / 32;
			b = b1 * j / 32;
			major = ((( r >> 2 ) & MASK( 3 )) << 5 ) | (((( g >> 3 ) & MASK( 3 )) << 2 )) | ((( b >> 3 ) & MASK( 2 )));

			// save minor GBRGBRGB
			minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 ) | MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 ) | MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );

			vid.colormap[index2 | index1] = major << 8 | ( minor & 0xFF );
		}
	}

	for( i = 0; i < 1024; i++ )
	{
		unsigned int   r, g, b;
		uint           color = i << 6 | BIT( 5 ) | BIT( 4 ) | BIT( 3 );
		uint           m = color >> 8;
		uint           j = color & 0xff;
		unsigned short index1 = i;

		r1 = (( m >> ( 8 - 3 )) << 2 ) & MASK( 5 );
		g1 = (( m >> ( 8 - 3 - 3 )) << 3 ) & MASK( 6 );
		b1 = (( m >> ( 8 - 3 - 3 - 2 )) << 3 ) & MASK( 5 );
		r1 |= MOVE_BIT( j, 5, 1 ) | MOVE_BIT( j, 2, 0 );
		g1 |= MOVE_BIT( j, 7, 2 ) | MOVE_BIT( j, 4, 1 ) | MOVE_BIT( j, 1, 0 );
		b1 |= MOVE_BIT( j, 6, 2 ) | MOVE_BIT( j, 3, 1 ) | MOVE_BIT( j, 0, 0 );


		FOR_EACH_COLOR( 2 )
		{
			unsigned int index2 = ( r2 << ( 2 + 3 ) | g2 << 2 | b2 ) << 10;
			unsigned int k;
			for( k = 0; k < 3; k++ )
			{
				unsigned int major, minor;
				unsigned int a = k + 2;


				r = r1 * ( 7 - a ) / 7 + ( r2 << 2 | BIT( 2 )) * a / 7;
				g = g1 * ( 7 - a ) / 7 + ( g2 << 3 | MASK( 2 )) * a / 7;
				b = b1 * ( 7 - a ) / 7 + ( b2 << 3 | MASK( 2 )) * a / 7;
				if( r > MASK( 5 ))
					r = MASK( 5 );
				if( g > MASK( 6 ))
					g = MASK( 6 );
				if( b > MASK( 5 ))
					b = MASK( 5 );


				ASSERT( b < 32 );
				major = ((( r >> 2 ) & MASK( 3 )) << 5 ) | (((( g >> 3 ) & MASK( 3 )) << 2 )) | ((( b >> 3 ) & MASK( 2 )));

				// save minor GBRGBRGB
				minor = MOVE_BIT( r, 1, 5 ) | MOVE_BIT( r, 0, 2 ) | MOVE_BIT( g, 2, 7 ) | MOVE_BIT( g, 1, 4 ) | MOVE_BIT( g, 0, 1 ) | MOVE_BIT( b, 2, 6 ) | MOVE_BIT( b, 1, 3 ) | MOVE_BIT( b, 0, 0 );
				minor = minor & ~0x3f;

				vid.alphamap[k << 18 | index2 | index1] = major << 8 | ( minor & 0xFF );
			}
		}
	}
}

static qboolean R_AllocScreen( void );

qboolean R_InitBlit( qboolean glblit )
{
	R_BuildBlendMaps();

	if( glblit && swblit.gl1 )
	{
		swblit.pLockBuffer = R_Lock_GL1;
		swblit.pUnlockBuffer = R_Unlock_GLES1;
		swblit.pCreateBuffer = R_CreateBuffer_GLES1;
	}
	else if( glblit )
	{
		swblit.pLockBuffer = R_Lock_GLES3;
		swblit.pUnlockBuffer = R_Unlock_GLES3;
		swblit.pCreateBuffer = R_CreateBuffer_GLES3;
	}
	else
	{
		swblit.pLockBuffer = gEngfuncs.SW_LockBuffer;
		swblit.pUnlockBuffer = gEngfuncs.SW_UnlockBuffer;
		swblit.pCreateBuffer = gEngfuncs.SW_CreateBuffer;
	}

	return R_AllocScreen();
}

static qboolean R_AllocScreen( void )
{
	int w, h;

	if( gpGlobals->width < 128 )
		gpGlobals->width = 128;
	if( gpGlobals->height < 128 )
		gpGlobals->height = 128;

	R_InitCaches();

	if( swblit.rotate )
	{
		w = gpGlobals->height;
		h = gpGlobals->width;
	}
	else
	{
		w = gpGlobals->width;
		h = gpGlobals->height;
	}

	if( !swblit.pCreateBuffer( w, h, &swblit.stride, &swblit.bpp,
				   &swblit.rmask, &swblit.gmask, &swblit.bmask ))
		return false;

	R_BuildScreenMap();
	vid.width = gpGlobals->width;
	vid.height = gpGlobals->height;
	vid.rowbytes = gpGlobals->width; // rowpixels
	if( d_pzbuffer )
		free( d_pzbuffer );
	d_pzbuffer = malloc( vid.width * vid.height * 2 + 64 );

	if( vid.buffer )
		free( vid.buffer );
	vid.buffer = malloc( vid.width * vid.height * sizeof( pixel_t ));

	return true;
}

void R_BlitScreen( void )
{
	int  u, v;
	void *buffer = swblit.pLockBuffer();
//	gEngfuncs.Con_Printf("blit begin\n");
	// memset( vid.buffer, 10, vid.width * vid.height );

	if( !buffer || gpGlobals->width != vid.width || gpGlobals->height != vid.height )
	{
		gEngfuncs.Con_Printf( "pre allocscrn\n" );
		R_AllocScreen();
		gEngfuncs.Con_Printf( "post allocscrn\n" );
		return;
	}
	// return;
	// byte *buf = vid.buffer;

	// #pragma omp parallel for schedule(static)
	// gEngfuncs.Con_Printf("swblit %d %d", swblit.bpp, vid.height );
	if( swblit.rotate )
	{
		if( swblit.bpp == 2 )
		{
			unsigned short *pbuf = buffer;
			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint d = swblit.stride - v - 1;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen[vid.buffer[start + u]];
					pbuf[d] = s;
					d += swblit.stride;
				}
			}
		}
		else if( swblit.bpp == 4 )
		{
			unsigned int *pbuf = buffer;

			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint d = swblit.stride - v - 1;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen32[vid.buffer[start + u]];
					pbuf[d] = s;
					d += swblit.stride;
				}
			}
		}
		else if( swblit.bpp == 3 )
		{
			byte *pbuf = buffer;
			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint d = swblit.stride - v - 1;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen32[vid.buffer[start + u]];
					pbuf[( d ) * 3] = s;
					s = s >> 8;
					pbuf[( d ) * 3 + 1] = s;
					s = s >> 8;
					pbuf[( d ) * 3 + 2] = s;
					d += swblit.stride;
				}
			}
		}
	}
	else
	{
		if( swblit.bpp == 2 )
		{
			unsigned short *pbuf = buffer;
			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint dstart = swblit.stride * v;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen[vid.buffer[start + u]];
					pbuf[dstart + u] = s;
				}
			}
		}
		else if( swblit.bpp == 4 )
		{
			unsigned int *pbuf = buffer;

			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint dstart = swblit.stride * v;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen32[vid.buffer[start + u]];
					pbuf[dstart + u] = s;
				}
			}
		}
		else if( swblit.bpp == 3 )
		{
			byte *pbuf = buffer;
			for( v = 0; v < vid.height; v++ )
			{
				uint start = vid.rowbytes * v;
				uint dstart = swblit.stride * v;

				for( u = 0; u < vid.width; u++ )
				{
					unsigned int s = vid.screen32[vid.buffer[start + u]];
					pbuf[( dstart + u ) * 3] = s;
					s = s >> 8;
					pbuf[( dstart + u ) * 3 + 1] = s;
					s = s >> 8;
					pbuf[( dstart + u ) * 3 + 2] = s;
				}
			}
		}
	}

	swblit.pUnlockBuffer();
//	gEngfuncs.Con_Printf("blit end\n");
}

static uint32_t Get8888PixelAt( int u, int start )
{
	uint32_t s;
	switch( swblit.bpp )
	{
	case 2:
	{
		pixel_t color = vid.screen[vid.buffer[start + u]];
		uint8_t c[3];
		c[0] = (((( color >> 11 ) & 0x1F ) * 527 ) + 23 ) >> 6;
		c[1] = (((( color >> 5 ) & 0x3F ) * 259 ) + 33 ) >> 6;
		c[2] = (((( color ) & 0x1F ) * 527 ) + 23 ) >> 6;

		s = c[0] << 16 | c[1] << 8 | c[2];
		break;
	}
	case 3:
	case 4:
	default:
		s = vid.screen32[vid.buffer[start + u]];
		break;
	}
	return s | 0xFF000000;
}

qboolean GAME_EXPORT VID_ScreenShot( const char *filename, int shot_type )
{
	rgbdata_t *r_shot;
	uint      flags = IMAGE_FLIP_Y;
	int       width = 0, height = 0, u, v;
	qboolean  result;

	r_shot = Mem_Calloc( r_temppool, sizeof( rgbdata_t ));
	r_shot->width = ( vid.width + 3 ) & ~3;
	r_shot->height = ( vid.height + 3 ) & ~3;
	r_shot->flags = IMAGE_HAS_COLOR;
	r_shot->type = PF_BGRA_32; // was RGBA
	r_shot->size = r_shot->width * r_shot->height * gEngfuncs.Image_GetPFDesc( r_shot->type )->bpp;
	r_shot->palette = NULL;
	r_shot->buffer = Mem_Malloc( r_temppool, r_shot->size );

	// get screen frame
	if( swblit.rotate )
	{
		uint32_t *pbuf = (uint32_t *)r_shot->buffer;

		for( v = 0; v < vid.height; v++ )
		{
			uint start = vid.rowbytes * ( vid.height - v );
			uint d = swblit.stride - v - 1;

			for( u = 0; u < vid.width; u++ )
			{
				pbuf[d] = Get8888PixelAt( u, start );
				d += swblit.stride;
			}
		}
	}
	else
	{
		uint32_t *pbuf = (uint32_t *)r_shot->buffer;

		for( v = 0; v < vid.height; v++ )
		{
			uint start = vid.rowbytes * ( vid.height - v );
			uint dstart = swblit.stride * v;

			for( u = 0; u < vid.width; u++ )
			{
				pbuf[dstart + u] = Get8888PixelAt( u, start );
			}
		}
	}

	switch( shot_type )
	{
	case VID_SCREENSHOT:
		break;
	case VID_SNAPSHOT:
		gEngfuncs.fsapi->AllowDirectPaths( true );
		break;
	case VID_LEVELSHOT:
	case VID_MINISHOT:
		flags |= IMAGE_RESAMPLE;
		height = shot_type == VID_MINISHOT ? 200 : 480;
		width = Q_rint( height * ((double)r_shot->width / r_shot->height ));
		break;
	case VID_MAPSHOT:
		flags |= IMAGE_RESAMPLE | IMAGE_QUANTIZE; // GoldSrc request overviews in 8-bit format
		height = 768;
		width = 1024;
		break;
	}

	gEngfuncs.Image_Process( &r_shot, width, height, flags, 0.0f );

	// write image
	result = gEngfuncs.FS_SaveImage( filename, r_shot );
	gEngfuncs.fsapi->AllowDirectPaths( false ); // always reset after store screenshot
	gEngfuncs.FS_FreeImage( r_shot );

	return result;
}

