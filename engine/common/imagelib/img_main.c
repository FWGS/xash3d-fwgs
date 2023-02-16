/*
img_main.c - load & save various image formats
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <math.h>
#include "imagelib.h"

// global image variables
imglib_t	image;

typedef struct suffix_s
{
	const char	*suf;
	uint		flags;
	side_hint_t	hint;
} suffix_t;

static const suffix_t skybox_qv1[6] =
{
{ "ft", IMAGE_FLIP_X, CB_HINT_POSX },
{ "bk", IMAGE_FLIP_Y, CB_HINT_NEGX },
{ "up", IMAGE_ROT_90, CB_HINT_POSZ },
{ "dn", IMAGE_ROT_90, CB_HINT_NEGZ },
{ "rt", IMAGE_ROT_90, CB_HINT_POSY },
{ "lf", IMAGE_ROT270, CB_HINT_NEGY },
};

static const suffix_t skybox_qv2[6] =
{
{ "_ft", IMAGE_FLIP_X, CB_HINT_POSX },
{ "_bk", IMAGE_FLIP_Y, CB_HINT_NEGX },
{ "_up", IMAGE_ROT_90, CB_HINT_POSZ },
{ "_dn", IMAGE_ROT_90, CB_HINT_NEGZ },
{ "_rt", IMAGE_ROT_90, CB_HINT_POSY },
{ "_lf", IMAGE_ROT270, CB_HINT_NEGY },
};

static const suffix_t cubemap_v1[6] =
{
{ "px", 0, CB_HINT_POSX },
{ "nx", 0, CB_HINT_NEGX },
{ "py", 0, CB_HINT_POSY },
{ "ny", 0, CB_HINT_NEGY },
{ "pz", 0, CB_HINT_POSZ },
{ "nz", 0, CB_HINT_NEGZ },
};

typedef struct cubepack_s
{
	const char	*name;	// just for debug
	const suffix_t	*type;
} cubepack_t;

static const cubepack_t load_cubemap[] =
{
{ "3Ds Sky1", skybox_qv1 },
{ "3Ds Sky2", skybox_qv2 },
{ "3Ds Cube", cubemap_v1 },
{ NULL, NULL },
};

// soul of ImageLib - table of image format constants
const bpc_desc_t PFDesc[] =
{
{ PF_UNKNOWN,	"raw",	0x1908, 0 },
{ PF_INDEXED_24,	"pal 24",	0x1908, 1 },
{ PF_INDEXED_32,	"pal 32",	0x1908, 1 },
{ PF_RGBA_32,	"RGBA 32",0x1908, 4 },
{ PF_BGRA_32,	"BGRA 32",0x80E1, 4 },
{ PF_RGB_24,	"RGB 24",	0x1908, 3 },
{ PF_BGR_24,	"BGR 24",	0x80E0, 3 },
{ PF_LUMINANCE,	"LUM 8",	0x1909, 1 },
{ PF_DXT1,	"DXT 1",	0x83F1, 4 },
{ PF_DXT3,	"DXT 3",	0x83F2, 4 },
{ PF_DXT5,	"DXT 5",	0x83F3, 4 },
{ PF_ATI2,	"ATI 2",	0x8837, 4 },
};

void Image_Reset( void )
{
	// reset global variables
	image.width = image.height = image.depth = 0;
	image.source_width = image.source_height = 0;
	image.source_type = image.num_mips = 0;
	image.num_sides = image.flags = 0;
	image.encode = DXT_ENCODE_DEFAULT;
	image.type = PF_UNKNOWN;
	image.fogParams[0] = 0;
	image.fogParams[1] = 0;
	image.fogParams[2] = 0;
	image.fogParams[3] = 0;

	// pointers will be saved with prevoius picture struct
	// don't care about it
	image.palette = NULL;
	image.cubemap = NULL;
	image.rgba = NULL;
	image.ptr = 0;
	image.size = 0;
}

static rgbdata_t *ImagePack( void )
{
	rgbdata_t	*pack;

	// clear any force flags
	image.force_flags = 0;

	if( image.cubemap && image.num_sides != 6 )
	{
		// this never can happen, just in case
		return NULL;
	}

	pack = Mem_Calloc( host.imagepool, sizeof( *pack ));

	if( image.cubemap )
	{
		image.flags |= IMAGE_CUBEMAP;
		pack->buffer = image.cubemap;
		pack->width = image.source_width;
		pack->height = image.source_height;
		pack->type = image.source_type;
		pack->size = image.size * image.num_sides;
	}
	else
	{
		pack->buffer = image.rgba;
		pack->width = image.width;
		pack->height = image.height;
		pack->depth = image.depth;
		pack->type = image.type;
		pack->size = image.size;
	}

	// copy fog params
	pack->fogParams[0] = image.fogParams[0];
	pack->fogParams[1] = image.fogParams[1];
	pack->fogParams[2] = image.fogParams[2];
	pack->fogParams[3] = image.fogParams[3];

	pack->flags = image.flags;
	pack->numMips = image.num_mips;
	pack->palette = image.palette;
	pack->encode = image.encode;

	return pack;
}

/*
================
FS_AddSideToPack

================
*/
static qboolean FS_AddSideToPack( int adjust_flags )
{
	byte	*out, *flipped;
	qboolean	resampled = false;

	// first side set average size for all cubemap sides!
	if( !image.cubemap )
	{
		image.source_width = image.width;
		image.source_height = image.height;
		image.source_type = image.type;
	}

	// keep constant size, render.dll expecting it
	image.size = image.source_width * image.source_height * 4;

	// mixing dds format with any existing ?
	if( image.type != image.source_type )
		return false;

	// flip image if needed
	flipped = Image_FlipInternal( image.rgba, &image.width, &image.height, image.source_type, adjust_flags );
	if( !flipped ) return false; // try to reasmple dxt?
	if( flipped != image.rgba ) image.rgba = Image_Copy( image.size );

	// resampling image if needed
	out = Image_ResampleInternal((uint *)image.rgba, image.width, image.height, image.source_width, image.source_height, image.source_type, &resampled );
	if( !out ) return false; // try to reasmple dxt?
	if( resampled ) image.rgba = Image_Copy( image.size );

	image.cubemap = Mem_Realloc( host.imagepool, image.cubemap, image.ptr + image.size );
	memcpy( image.cubemap + image.ptr, image.rgba, image.size ); // add new side

	Mem_Free( image.rgba );	// release source buffer
	image.ptr += image.size; 	// move to next
	image.num_sides++;		// bump sides count

	return true;
}

static const loadpixformat_t *Image_GetLoadFormatForExtension( const char *ext )
{
	const loadpixformat_t *format;

	if( !COM_CheckStringEmpty( ext ))
		return NULL;

	for( format = image.loadformats; format->formatstring; format++ )
	{
		if( !Q_stricmp( ext, format->ext ))
			return format;
	}

	return NULL;
}

static qboolean Image_ProbeLoadBuffer_( const loadpixformat_t *fmt, const char *name, const byte *buf, size_t size, int override_hint )
{
	if( override_hint > 0 )
		image.hint = override_hint;
	else image.hint = fmt->hint;

	return fmt->loadfunc( name, buf, size );
}

static qboolean Image_ProbeLoadBuffer( const loadpixformat_t *fmt, const char *name, const byte *buf, size_t size, int override_hint )
{
	if( size <= 0 )
		return false;

	// bruteforce all loaders
	if( !fmt )
	{
		for( fmt = image.loadformats; fmt->formatstring; fmt++ )
		{
			if( Image_ProbeLoadBuffer_( fmt, name, buf, size, override_hint ))
				 return true;
		}

		return false;
	}

	return Image_ProbeLoadBuffer_( fmt, name, buf, size, override_hint );
}

static qboolean Image_ProbeLoad_( const loadpixformat_t *fmt, const char *name, const char *suffix, int override_hint )
{
	qboolean success = false;
	fs_offset_t filesize;
	string path;
	byte *f;

	Q_snprintf( path, sizeof( path ), fmt->formatstring, name, suffix, fmt->ext );
	f = FS_LoadFile( path, &filesize, false );

	if( f )
	{
		success = Image_ProbeLoadBuffer( fmt, path, f, filesize, override_hint );

		Mem_Free( f );
	}

	return success;
}

static qboolean Image_ProbeLoad( const loadpixformat_t *fmt, const char *name, const char *suffix, int override_hint )
{
	if( !fmt )
	{
		// bruteforce all formats to allow implicit extension
		for( fmt = image.loadformats; fmt->formatstring; fmt++ )
		{
			if( Image_ProbeLoad_( fmt, name, suffix, override_hint ))
				return true;
		}

		return false;
	}

	return Image_ProbeLoad_( fmt, name, suffix, override_hint );
}

/*
================
FS_LoadImage

loading and unpack to rgba any known image
================
*/
rgbdata_t *FS_LoadImage( const char *filename, const byte *buffer, size_t size )
{
	const char	*ext = COM_FileExtension( filename );
	string		loadname;
	int		i;
	const loadpixformat_t *extfmt;
	const cubepack_t	*cmap;

	Q_strncpy( loadname, filename, sizeof( loadname ));
	Image_Reset(); // clear old image

	// we needs to compare file extension with list of supported formats
	// and be sure what is real extension, not a filename with dot
	if(( extfmt = Image_GetLoadFormatForExtension( ext )))
		COM_StripExtension( loadname );

	// special mode: skip any checks, load file from buffer
	if( filename[0] == '#' && buffer && size )
		goto load_internal;

	if( Image_ProbeLoad( extfmt, loadname, "", -1 ))
		return ImagePack();

	// check all cubemap sides with package suffix
	for( cmap = load_cubemap; cmap && cmap->type; cmap++ )
	{
		for( i = 0; i < 6; i++ )
		{
			if( Image_ProbeLoad( extfmt, loadname, cmap->type[i].suf, cmap->type[i].hint ) &&
				 FS_AddSideToPack( cmap->type[i].flags )) // process flags to flip some sides
			{
				break;
			}

			if( image.num_sides != i + 1 ) // check side
			{
				// first side not found, probably it's not cubemap
				// it contain info about image_type and dimensions, don't generate black cubemaps
				if( !image.cubemap ) break;
				// Mem_Alloc already filled memblock with 0x00, no need to do it again
				image.cubemap = Mem_Realloc( host.imagepool, image.cubemap, image.ptr + image.size );
				image.ptr += image.size; // move to next
				image.num_sides++; // merge counter
			}
		}

		// make sure what all sides is loaded
		if( image.num_sides != 6 )
		{
			// unexpected errors ?
			if( image.cubemap )
				Mem_Free( image.cubemap );
			Image_Reset();
		}
		else break;
	}

	if( image.cubemap )
		return ImagePack(); // all done

load_internal:
	if( buffer && size )
	{
		if( Image_ProbeLoadBuffer( extfmt, loadname, buffer, size, -1 ))
			return ImagePack();
	}

	if( loadname[0] != '#' )
		Con_Reportf( S_WARN "FS_LoadImage: couldn't load \"%s\"\n", loadname );

	// clear any force flags
	image.force_flags = 0;

	return NULL;
}



/*
================
Image_Save

writes image as any known format
================
*/
qboolean FS_SaveImage( const char *filename, rgbdata_t *pix )
{
	const char	*ext = COM_FileExtension( filename );
	qboolean		anyformat = !COM_CheckStringEmpty( ext );
	string		path, savename;
	const savepixformat_t *format;

	if( !pix || !pix->buffer || anyformat )
	{
		// clear any force flags
		image.force_flags = 0;
		return false;
	}

	Q_strncpy( savename, filename, sizeof( savename ));
	COM_StripExtension( savename ); // remove extension if needed

	if( pix->flags & (IMAGE_CUBEMAP|IMAGE_SKYBOX))
	{
		size_t		realSize = pix->size; // keep real pic size
		byte		*picBuffer; // to avoid corrupt memory on free data
		const suffix_t	*box;
		int		i;

		if( pix->flags & IMAGE_SKYBOX )
			box = skybox_qv1;
		else if( pix->flags & IMAGE_CUBEMAP )
			box = cubemap_v1;
		else
		{
			// clear any force flags
			image.force_flags = 0;
			return false;	// do not happens
		}

		pix->size /= 6; // now set as side size
		picBuffer = pix->buffer;

		// save all sides seperately
		for( format = image.saveformats; format && format->formatstring; format++ )
		{
			if( !Q_stricmp( ext, format->ext ))
			{
				for( i = 0; i < 6; i++ )
				{
					Q_sprintf( path, format->formatstring, savename, box[i].suf, format->ext );
					if( !format->savefunc( path, pix )) break; // there were errors
					pix->buffer += pix->size; // move pointer
				}

				// restore pointers
				pix->size = realSize;
				pix->buffer = picBuffer;

				// clear any force flags
				image.force_flags = 0;

				return ( i == 6 );
			}
		}
	}
	else
	{
		for( format = image.saveformats; format && format->formatstring; format++ )
		{
			if( !Q_stricmp( ext, format->ext ))
			{
				Q_sprintf( path, format->formatstring, savename, "", format->ext );
				if( format->savefunc( path, pix ))
				{
					// clear any force flags
					image.force_flags = 0;
					return true; // saved
				}
			}
		}
	}

	// clear any force flags
	image.force_flags = 0;

	return false;
}

/*
================
Image_FreeImage

free RGBA buffer
================
*/
void FS_FreeImage( rgbdata_t *pack )
{
	if( !pack ) return;
	if( pack->buffer ) Mem_Free( pack->buffer );
	if( pack->palette ) Mem_Free( pack->palette );
	Mem_Free( pack );
}

/*
================
FS_CopyImage

make an image copy
================
*/
rgbdata_t *FS_CopyImage( rgbdata_t *in )
{
	rgbdata_t	*out;
	int	palSize = 0;

	if( !in ) return NULL;

	out = Mem_Malloc( host.imagepool, sizeof( rgbdata_t ));
	*out = *in;

	switch( in->type )
	{
	case PF_INDEXED_24:
		palSize = 768;
		break;
	case PF_INDEXED_32:
		palSize = 1024;
		break;
	}

	if( palSize )
	{
		out->palette = Mem_Malloc( host.imagepool, palSize );
		memcpy( out->palette, in->palette, palSize );
	}

	if( in->size )
	{
		out->buffer = Mem_Malloc( host.imagepool, in->size );
		memcpy( out->buffer, in->buffer, in->size );
	}

	return out;
}

#if XASH_ENGINE_TESTS
#include "tests.h"

static void GeneratePixel( byte *pix, uint i, uint j, uint w, uint h, qboolean genAlpha )
{
	double x = ( j / (double)w ) - 0.5;
	double y = ( i / (double)h ) - 0.5;
	double d = sqrt( x * x + y * y );
	pix[0] = (byte)(( sin( d * 30.0 ) + 1.0 ) * 126 );
	pix[1] = (byte)(( sin( d * 27.723 ) + 1.0 ) * 126 );
	pix[2] = (byte)(( sin( d * 42.41 ) + 1.0 ) * 126 );
	pix[3] = genAlpha ? (byte)(( cos( d * 2.0 ) + 1.0 ) * 126 ) : 255;
}

static void Test_CheckImage( const char *name, rgbdata_t *rgb )
{
	rgbdata_t *load;

	// test reading
	load = FS_LoadImage( name, NULL, 0 );
	TASSERT( load->width == rgb->width )
	TASSERT( load->height == rgb->height )
	TASSERT( load->type == rgb->type )
	TASSERT( ( load->flags & rgb->flags ) != 0 )
	TASSERT( load->size == rgb->size )
	TASSERT( memcmp(load->buffer, rgb->buffer, rgb->size ) == 0 )

	Mem_Free( load );
}

void Test_RunImagelib( void )
{
	rgbdata_t rgb = { 0 };
	byte *buf;
	const char *extensions[] = { "tga", "png", "bmp" };
	uint i, j;

	Image_Setup();

	// generate image
	rgb.width = 256;
	rgb.height = 512;
	rgb.type = PF_RGBA_32;
	rgb.flags = IMAGE_HAS_ALPHA;
	rgb.size = rgb.width * rgb.height * 4;
	buf = rgb.buffer = Z_Malloc( rgb.size );

	for( i = 0; i < rgb.height; i++ )
	{
		for( j = 0; j < rgb.width; j++ )
		{
			GeneratePixel( buf, i, j, rgb.width, rgb.height, true );
			buf += 4;
		}
	}

	for( i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++ )
	{
		const char *name = va( "test_gen.%s", extensions[i] );

		// test saving
		qboolean ret = FS_SaveImage( name, &rgb );
		Con_Printf( "Checking if we can save images in '%s' format...\n", extensions[i] );
		ASSERT(ret == true);

		// test reading
		Con_Printf( "Checking if we can read images in '%s' format...\n", extensions[i] );
		Test_CheckImage( name, &rgb );
	}

	Z_Free( rgb.buffer );
}

#define IMPLEMENT_IMAGELIB_FUZZ_TARGET( export, target ) \
int EXPORT export( const uint8_t *Data, size_t Size ) \
{ \
	rgbdata_t *rgb; \
	host.type = HOST_NORMAL; \
	Memory_Init(); \
	Image_Init(); \
	if( target( "#internal", Data, Size )) \
	{ \
		rgb = ImagePack(); \
		FS_FreeImage( rgb ); \
	} \
	Image_Shutdown(); \
	return 0; \
} \

IMPLEMENT_IMAGELIB_FUZZ_TARGET( Fuzz_Image_LoadBMP, Image_LoadBMP )
IMPLEMENT_IMAGELIB_FUZZ_TARGET( Fuzz_Image_LoadPNG, Image_LoadPNG )
IMPLEMENT_IMAGELIB_FUZZ_TARGET( Fuzz_Image_LoadDDS, Image_LoadDDS )
IMPLEMENT_IMAGELIB_FUZZ_TARGET( Fuzz_Image_LoadTGA, Image_LoadTGA )

#endif /* XASH_ENGINE_TESTS */
