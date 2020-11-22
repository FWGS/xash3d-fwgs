#include "common.h"
#include "eiface.h"
#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "img_tga.h"

void GeneratePixel( byte *pix, int i, int j, int w, int h, qboolean genAlpha )
{
	float x = (j/(float)w)-0.5f;
	float y = (i/(float)h)-0.5f;
	float d = sqrt(x*x+y*y);
	pix[0] = (sin(d*30.0f)+1.0f)*126;
	pix[1] = (sin(d*27.723f)+1.0f)*126;
	pix[2] = (sin(d*42.41f)+1.0f)*126;
	pix[3] = genAlpha ? (cos(d*2.0f)+1.0f)*126 : 255;
}

void Test_CheckImage( const char *name, rgbdata_t *rgb )
{
	int i, j;
	rgbdata_t *load;
	byte *buf;

        // test reading
	load = FS_LoadImage( name, NULL, 0 );
	ASSERT( load->width == rgb->width );
	ASSERT( load->height == rgb->height );
	ASSERT( load->type == rgb->type );
	ASSERT( (load->flags & rgb->flags) != 0 );
	ASSERT( load->size == rgb->size );
	ASSERT( memcmp(load->buffer, rgb->buffer, rgb->size ) == 0 );

	Con_Printf("Loaded %s -- OK\n", name );
	Mem_Free( load );
}

int main( int argc, char **argv )
{
	int i, j;
	rgbdata_t rgb = { 0 };
	byte *buf;
	const char *extensions[] = { "tga", "png", "bmp" };

	// generate image
	rgb.width = 256;
	rgb.height = 512;
	rgb.type = PF_RGBA_32;
	rgb.flags = IMAGE_HAS_ALPHA;
	rgb.size = rgb.width * rgb.height * 4;
	buf = rgb.buffer = malloc(rgb.size);

	for( i = 0; i < rgb.height; i++ )
	{
		for( j = 0; j < rgb.width; j++ )
		{
			GeneratePixel( buf, i, j, rgb.width, rgb.height, true );
			buf += 4;
		}
	}

	// init engine
	Host_InitCommon( argc, argv, "tests", false );

	// initialize normal imagelib
	Image_Setup();

	for( i = 0; i < ARRAYSIZE(extensions); i++ )
	{
		const char *name = va( "test_gen.%s", extensions[i] );

		// test saving
		qboolean ret = FS_SaveImage( name, &rgb );
		Con_Printf("Save %s -- %s\n", name, ret ? "OK" : "FAIL");
		ASSERT(ret == true);

		// test reading
		Test_CheckImage( name, &rgb );
	}

	free( rgb.buffer );

	Host_Shutdown();
	return 0;
}
