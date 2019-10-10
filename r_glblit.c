#include "r_local.h"
#include "../ref_gl/gl_export.h"

struct swblit_s
{
	uint stride;
	uint bpp;
	uint rmask, gmask, bmask;
} swblit;


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

static int FIRST_BIT( uint mask )
{
	uint i;

	for( i = 0; !(BIT(i) & mask); i++ );

	return i;
}

static int COUNT_BITS( uint mask )
{
	uint i;

	for( i = 0; mask; mask = mask >> 1 )
		i += mask & 1;

	return i;
}

void R_BuildScreenMap( void )
{
	int i;
	uint rshift = FIRST_BIT(swblit.rmask), gshift = FIRST_BIT(swblit.gmask), bshift = FIRST_BIT(swblit.bmask);
	uint rbits = COUNT_BITS(swblit.rmask), gbits = COUNT_BITS(swblit.gmask), bbits = COUNT_BITS(swblit.bmask);
	uint rmult = BIT(rbits), gmult = BIT(gbits), bmult = BIT(bbits);
	uint rdiv = MASK(5), gdiv = MASK(6), bdiv = MASK(5);

	gEngfuncs.Con_Printf("Blit table: %d %d %d %d %d %d\n", rmult, gmult, bmult, rdiv, gdiv, bdiv );

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
		//major = r << (6 + 5) | (g << 5) | b;
		major = (r * rmult / rdiv) << rshift | (g * gmult / gdiv) << gshift | (b * bmult / bdiv) << bshift;


		for( j = 0; j < 256; j++ )
		{
			uint minor;
			// restore minor GBRGBRGB
			r = MOVE_BIT(j, 5, 1) | MOVE_BIT(j, 2, 0);
			g = MOVE_BIT(j, 7, 2) | MOVE_BIT(j, 4, 1) | MOVE_BIT(j, 1, 0);
			b = MOVE_BIT(j, 6, 2) | MOVE_BIT(j, 3, 1) | MOVE_BIT(j, 0, 0);
			//vid.screen[(i<<8)|j] = r << (6 + 5) | (g << 5) | b | major;
			minor = (r * rmult / rdiv) << rshift | (g * gmult / gdiv) << gshift | (b * bmult / bdiv) << bshift;

			if( swblit.bpp == 2 )
				vid.screen[(i<<8)|j] = major | minor;
			else
				vid.screen32[(i<<8)|j] = major | minor;

		}

	}
#endif
}

#define FOR_EACH_COLOR(x) 	for( r##x = 0; r##x < BIT(3); r##x++ ) for( g##x = 0; g##x < BIT(3); g##x++ ) for( b##x = 0; b##x < BIT(2); b##x++ )

void R_BuildBlendMaps( void )
{
	unsigned int r1, g1, b1;
	unsigned int r2, g2, b2;
	unsigned int i, j;

	FOR_EACH_COLOR(1)FOR_EACH_COLOR(2)
	{
		unsigned int r, g, b;
		unsigned short index1 = r1 << (2 + 3) | g1 << 2 | b1;
		unsigned short index2 = (r2 << (2 + 3) | g2 << 2 | b2) << 8;
		unsigned int a;

		r = r1 + r2;
		g = g1 + g2;
		b = b1 + b2;
		if( r > MASK(3) )
			r = MASK(3);
		if( g > MASK(3) )
			g = MASK(3);
		if( b > MASK(2) )
			b = MASK(2);
		ASSERT(!vid.addmap[index2|index1]);

		vid.addmap[index2|index1] =  r << (2 + 3) | g << 2 | b;
		r = r1 * r2 / MASK(3);
		g = g1 * g2 / MASK(3);
		b = b1 * b2 / MASK(2);

		vid.modmap[index2|index1] =  r << (2 + 3) | g << 2 | b;
#if 0
		for( a = 0; a < 8; a++ )
		{
			r = r1 * (7 - a) / 7 + r2 * a / 7;
			g = g1 * (7 - a) / 7 + g2 * a / 7;
			b = b1 * (7 - a) / 7 + b2 * a / 7;
			//if( b == 1 ) b = 0;
			vid.alphamap[a << 16|index2|index1] =  r << (2 + 3) | g << 2 | b;
		}
#endif
	}
	for( i = 0; i < 8192; i++ )
	{
		unsigned int r, g, b;
		uint color = i << 3;
		uint m = color >> 8;
		uint j = color & 0xff;
		unsigned short index1 = i;

		r1 = ((m >> (8 - 3) )<< 2 ) & MASK(5);
		g1 = ((m >> (8 - 3 - 3)) << 3) & MASK(6);
		b1 = ((m >> (8 - 3 - 3 - 2)) << 3) & MASK(5);
		r1 |= MOVE_BIT(j, 5, 1) | MOVE_BIT(j, 2, 0);
		g1 |= MOVE_BIT(j, 7, 2) | MOVE_BIT(j, 4, 1) | MOVE_BIT(j, 1, 0);
		b1 |= MOVE_BIT(j, 6, 2) | MOVE_BIT(j, 3, 1) | MOVE_BIT(j, 0, 0);


		for( j = 0; j < 32; j++)
		{
			unsigned int index2 = j << 13;
			unsigned int major, minor;
			r = r1 * j / 32;
			g = g1 * j / 32;
			b = b1 * j / 32;
			major = (((r >> 2) & MASK(3)) << 5) |( (( (g >> 3) & MASK(3)) << 2 )  )| (((b >> 3) & MASK(2)));

			// save minor GBRGBRGB
			minor = MOVE_BIT(r,1,5) | MOVE_BIT(r,0,2) | MOVE_BIT(g,2,7) | MOVE_BIT(g,1,4) | MOVE_BIT(g,0,1) | MOVE_BIT(b,2,6)| MOVE_BIT(b,1,3)|MOVE_BIT(b,0,0);

			vid.colormap[index2|index1] =  major << 8 | (minor & 0xFF);
		}
	}
#if 1
	for( i = 0; i < 1024; i++ )
	{
		unsigned int r, g, b;
		uint color = i << 6 | BIT(5) | BIT(4) | BIT(3);
		uint m = color >> 8;
		uint j = color & 0xff;
		unsigned short index1 = i;

		r1 = ((m >> (8 - 3) )<< 2 ) & MASK(5);
		g1 = ((m >> (8 - 3 - 3)) << 3) & MASK(6);
		b1 = ((m >> (8 - 3 - 3 - 2)) << 3) & MASK(5);
		r1 |= MOVE_BIT(j, 5, 1) | MOVE_BIT(j, 2, 0);
		g1 |= MOVE_BIT(j, 7, 2) | MOVE_BIT(j, 4, 1) | MOVE_BIT(j, 1, 0);
		b1 |= MOVE_BIT(j, 6, 2) | MOVE_BIT(j, 3, 1) | MOVE_BIT(j, 0, 0);


		FOR_EACH_COLOR(2)
		{
			unsigned int index2 = (r2 << (2 + 3) | g2 << 2 | b2) << 10;
			unsigned int k;
			for( k = 0; k < 3; k++ )
			{
				unsigned int major, minor;
				unsigned int a = k + 2;


				r = r1 * (7 - a) / 7 + (r2 << 2 | BIT(2)) * a / 7;
				g = g1 * (7 - a) / 7 + (g2 << 3 | MASK(2)) * a / 7;
				b = b1 * (7 - a) / 7 + (b2 << 3 | MASK(2)) * a / 7;
				if( r > MASK(5) )
					r = MASK(5);
				if( g > MASK(6) )
					g = MASK(6);
				if( b > MASK(5) )
					b = MASK(5);


				ASSERT( b < 32 );
				major = (((r >> 2) & MASK(3)) << 5) |( (( (g >> 3) & MASK(3)) << 2 )  )| (((b >> 3) & MASK(2)));

				// save minor GBRGBRGB
				minor = MOVE_BIT(r,1,5) | MOVE_BIT(r,0,2) | MOVE_BIT(g,2,7) | MOVE_BIT(g,1,4) | MOVE_BIT(g,0,1) | MOVE_BIT(b,2,6)| MOVE_BIT(b,1,3)|MOVE_BIT(b,0,0);
				minor = minor & ~0x3f;


				vid.alphamap[k << 18|index2|index1] =  major << 8 | (minor & 0xFF);
			}
		}
	}
#endif
}

void R_AllocScreen( void );

void R_InitBlit(void)
{

	/*LOAD(glBegin);
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
	LOAD(glTexParameteri);*/
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

	//buffer = Mem_Malloc( r_temppool, 1920*1080*2 );

	R_BuildBlendMaps();
	R_AllocScreen();
}

void R_AllocScreen( void )
{
	if( gpGlobals->width < 320 )
		gpGlobals->width = 320;
	if( gpGlobals->height < 200 )
		gpGlobals->height = 200;

	R_InitCaches();
	gEngfuncs.SW_CreateBuffer( gpGlobals->width, gpGlobals->height, &swblit.stride, &swblit.bpp,
							   &swblit.rmask, &swblit.gmask, &swblit.bmask);
	R_BuildScreenMap();
	vid.width = gpGlobals->width;
	vid.height = gpGlobals->height;
	vid.rowbytes = gpGlobals->width; // rowpixels
	if( d_pzbuffer )
		Mem_Free( d_pzbuffer );
	d_pzbuffer = Mem_Calloc( r_temppool, vid.width*vid.height*2 + 64 );
	if( vid.buffer )
		Mem_Free( vid.buffer );

	vid.buffer = Mem_Malloc( r_temppool, vid.width * vid.height*sizeof( pixel_t ) );
}

void R_BlitScreen( void )
{
	//memset( vid.buffer, 10, vid.width * vid.height );
	int u, v;
	void *buffer = gEngfuncs.SW_LockBuffer();
	if( !buffer || gpGlobals->width != vid.width || gpGlobals->height != vid.height )
	{
		R_AllocScreen();
		return;
	}
	//byte *buf = vid.buffer;

	//#pragma omp parallel for schedule(static)
	if( swblit.bpp == 2 )
	{
		unsigned short *pbuf = buffer;
		for( v = 0; v < vid.height;v++)
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

		for( v = 0; v < vid.height;v++)
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
		for( v = 0; v < vid.height;v++)
		{
			uint start = vid.rowbytes * v;
			uint dstart = swblit.stride * v;

			for( u = 0; u < vid.width; u++ )
			{
				unsigned int s = vid.screen32[vid.buffer[start + u]];
				pbuf[(dstart+u)*3] = s;
				s = s >> 8;
				pbuf[(dstart+u)*3+1] = s;
				s = s >> 8;
				pbuf[(dstart+u)*3+2] = s;
			}
		}
	}

#if 0
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
#else
	gEngfuncs.SW_UnlockBuffer();
#endif
}
