/*
img_utils.c - image common tools
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

#include "imagelib.h"
#include "xash3d_mathlib.h"
#include "mod_local.h"

#define LERPBYTE( i )	r = resamplerow1[i]; out[i] = (byte)(((( resamplerow2[i] - r ) * lerp)>>16 ) + r )
#define FILTER_SIZE		5

uint d_8toQ1table[256];
uint d_8toHLtable[256];
uint d_8to24table[256];

qboolean q1palette_init = false;
qboolean hlpalette_init = false;

static byte palette_q1[768] =
{
0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,
171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,
47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,
27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,139,107,107,151,115,115,163,123,
123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,
7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,
0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,55,0,75,59,7,87,67,7,
95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,
87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,
243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,
95,183,135,107,195,147,123,211,163,139,227,179,151,171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,
83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,
143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,
19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,
107,87,71,95,75,59,83,63,51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,
95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,
243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,
0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,43,175,47,47,159,47,47,143,47,
47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,
7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,
59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,
243,147,255,247,199,255,255,255,159,91,83
};

// this is used only for particle colors
static byte palette_hl[768] =
{
0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,
171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,
47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,
27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,139,107,107,151,115,115,163,123,
123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,
7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,
0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,55,0,75,59,7,87,67,7,
95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,
87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,
243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,
95,183,135,107,195,147,123,211,163,139,227,179,151,171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,
83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,
143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,
19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,
107,87,71,95,75,59,83,63,51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,
95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,
243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,
0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,43,175,47,47,159,47,47,143,47,
47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,
7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,
59,183,155,55,199,195,55,231,227,87,0,255,0,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,
147,255,247,199,255,255,255,159,91,83
};

/*
=============================================================================

	XASH3D LOAD IMAGE FORMATS

=============================================================================
*/
// stub
static const loadpixformat_t load_null[] =
{
{ NULL, NULL, IL_HINT_NO }
};

static const loadpixformat_t load_game[] =
{
{ "dds", Image_LoadDDS, IL_HINT_NO },   // dds for world and studio models
{ "bmp", Image_LoadBMP, IL_HINT_NO },   // WON menu images
{ "tga", Image_LoadTGA, IL_HINT_NO },   // hl vgui menus
{ "png", Image_LoadPNG, IL_HINT_NO },   // NightFire 007 menus
{ "wad", Image_LoadWAD, IL_HINT_NO },   // hl wad files
{ "mip", Image_LoadMIP, IL_HINT_NO },   // hl textures from wad or buffer
{ "mdl", Image_LoadMDL, IL_HINT_HL },   // hl studio model skins
{ "spr", Image_LoadSPR, IL_HINT_HL },   // hl sprite frames
{ "lmp", Image_LoadLMP, IL_HINT_NO },   // hl menu images (cached.wad etc)
{ "fnt", Image_LoadFNT, IL_HINT_HL },   // hl console font (fonts.wad etc)
{ "pal", Image_LoadPAL, IL_HINT_NO },   // install studio\sprite palette
{ "ktx2", Image_LoadKTX2, IL_HINT_NO }, // ktx2 for world and studio models
{ NULL, NULL, IL_HINT_NO }
};

/*
=============================================================================

	XASH3D SAVE IMAGE FORMATS

=============================================================================
*/
// stub
static const savepixformat_t save_null[] =
{
{ NULL, NULL }
};

// Xash3D normal instance
static const savepixformat_t save_game[] =
{
{ "tga", Image_SaveTGA }, // tga screenshots
{ "bmp", Image_SaveBMP }, // bmp levelshots or screenshots
{ "png", Image_SavePNG }, // png screenshots
{ NULL, NULL }
};

void Image_Setup( void )
{
	image.cmd_flags = IL_USE_LERPING|IL_ALLOW_OVERWRITE;
	image.loadformats = load_game;
	image.saveformats = save_game;
}

void Image_Init( void )
{
	// init pools
	host.imagepool = Mem_AllocPool( "ImageLib Pool" );

	// install image formats (can be re-install later by Image_Setup)
	switch( host.type )
	{
	case HOST_NORMAL:
		Image_Setup( );
		break;
	case HOST_DEDICATED:
		image.cmd_flags = 0;
		image.loadformats = load_game;
		image.saveformats = save_null;
		break;
	default:	// all other instances not using imagelib
		image.cmd_flags = 0;
		image.loadformats = load_null;
		image.saveformats = save_null;
		break;
	}

	image.tempbuffer = NULL;
}

void Image_Shutdown( void )
{
	Mem_Check(); // check for leaks
	Mem_FreePool( &host.imagepool );
}

byte *Image_Copy( size_t size )
{
	byte	*out;

	out = Mem_Realloc( host.imagepool, image.tempbuffer, size );
	image.tempbuffer = NULL;

	return out;
}

/*
=================
Image_CustomPalette
=================
*/
qboolean Image_CustomPalette( void )
{
	return image.custom_palette;
}

/*
=================
Image_CheckFlag
=================
*/
qboolean Image_CheckFlag( int bit )
{
	if( FBitSet( image.force_flags, bit ))
		return true;

	if( FBitSet( image.cmd_flags, bit ))
		return true;

	return false;
}

/*
=================
Image_SetForceFlags
=================
*/
void Image_SetForceFlags( uint flags )
{
	SetBits( image.force_flags, flags );
}

/*
=================
Image_ClearForceFlags
=================
*/
void Image_ClearForceFlags( void )
{
	image.force_flags = 0;
}

/*
=================
Image_AddCmdFlags
=================
*/
void Image_AddCmdFlags( uint flags )
{
	SetBits( image.cmd_flags, flags );
}

qboolean Image_ValidSize( const char *name )
{
	int max_width = IMAGE_MAXWIDTH;
	int max_height = IMAGE_MAXHEIGHT;

	if( Image_CheckFlag( IL_LOAD_PLAYER_DECAL ))
	{
		max_width = PLDECAL_MAXWIDTH;
		max_height = PLDECAL_MAXHEIGHT;
	}

	if( image.width > max_width || image.height > max_height || image.width <= 0 || image.height <= 0 )
	{
		Con_DPrintf( S_ERROR "Image: (%s) dims out of range [%dx%d]\n", name, image.width, image.height );
		return false;
	}
	return true;
}

qboolean Image_LumpValidSize( const char *name )
{
	if( image.width > LUMP_MAXWIDTH || image.height > LUMP_MAXHEIGHT || image.width <= 0 || image.height <= 0 )
	{
		Con_DPrintf( S_ERROR "Image: (%s) dims out of range [%dx%d]\n", name, image.width,image.height );
		return false;
	}
	return true;
}

/*
=============
Image_ComparePalette
=============
*/
int Image_ComparePalette( const byte *pal )
{
	if( pal == NULL )
		return PAL_INVALID;
	else if( !memcmp( palette_q1, pal, 765 )) // last color was changed
		return PAL_QUAKE1;
	else if( !memcmp( palette_hl, pal, 765 ))
		return PAL_HALFLIFE;
	return PAL_CUSTOM;
}

static void Image_SetPalette( const byte *pal, uint *d_table )
{
	byte	rgba[4];
	uint uirgba; // TODO: palette looks byte-swapped on big-endian
	int	i;

	// setup palette
	switch( image.d_rendermode )
	{
	case LUMP_NORMAL:
		for( i = 0; i < 256; i++ )
		{
			memcpy( rgba, &pal[i * 3], 3 );
			rgba[3] = 0xFF;
			memcpy( &uirgba, rgba, sizeof( uirgba ));
			d_table[i] = uirgba;
		}
		break;
	case LUMP_TEXGAMMA:
		for( i = 0; i < 256; i++ )
		{
			rgba[0] = TextureToGamma( pal[i * 3 + 0] );
			rgba[1] = TextureToGamma( pal[i * 3 + 1] );
			rgba[2] = TextureToGamma( pal[i * 3 + 2] );
			rgba[3] = 0xFF;
			memcpy( &uirgba, rgba, sizeof( uirgba ));
			d_table[i] = uirgba;
		}
		break;
	case LUMP_GRADIENT:
		for( i = 0; i < 256; i++ )
		{
			rgba[0] = pal[765];
			rgba[1] = pal[766];
			rgba[2] = pal[767];
			rgba[3] = i;
			memcpy( &uirgba, rgba, sizeof( uirgba ));
			d_table[i] = uirgba;
		}
		break;
	case LUMP_MASKED:
		for( i = 0; i < 255; i++ )
		{
			rgba[0] = pal[i*3+0];
			rgba[1] = pal[i*3+1];
			rgba[2] = pal[i*3+2];
			rgba[3] = 0xFF;
			memcpy( &uirgba, rgba, sizeof( uirgba ));
			d_table[i] = uirgba;
		}
		d_table[255] = 0;
		break;
	case LUMP_EXTENDED:
		for( i = 0; i < 256; i++ )
		{
			rgba[0] = pal[i*4+0];
			rgba[1] = pal[i*4+1];
			rgba[2] = pal[i*4+2];
			rgba[3] = pal[i*4+3];
			memcpy( &uirgba, rgba, sizeof( uirgba ));
			d_table[i] = uirgba;
		}
		break;
	}
}

static void Image_ConvertPalTo24bit( rgbdata_t *pic )
{
	byte	*pal32, *pal24;
	byte	*converted;
	int	i;

	if( pic->type == PF_INDEXED_24 )
		return; // does nothing

	pal24 = converted = Mem_Malloc( host.imagepool, 768 );
	pal32 = pic->palette;

	for( i = 0; i < 256; i++, pal24 += 3, pal32 += 4 )
	{
		pal24[0] = pal32[0];
		pal24[1] = pal32[1];
		pal24[2] = pal32[2];
	}

	Mem_Free( pic->palette );
	pic->palette = converted;
	pic->type = PF_INDEXED_24;
}

void Image_CopyPalette32bit( void )
{
	if( image.palette ) return; // already created ?
	image.palette = Mem_Malloc( host.imagepool, 1024 );
	memcpy( image.palette, image.d_currentpal, 1024 );
}

void Image_CheckPaletteQ1( void )
{
	rgbdata_t	*pic = FS_LoadImage( DEFAULT_INTERNAL_PALETTE, NULL, 0 );

	if( pic && pic->size == 1024 )
	{
		Image_ConvertPalTo24bit( pic );
		if( Image_ComparePalette( pic->palette ) == PAL_CUSTOM )
		{
			image.d_rendermode = LUMP_NORMAL;
			Con_DPrintf( "custom quake palette detected\n" );
			Image_SetPalette( pic->palette, d_8toQ1table );
			d_8toQ1table[255] = 0; // 255 is transparent
			image.custom_palette = true;
			q1palette_init = true;
		}
	}

	if( pic ) FS_FreeImage( pic );
}

void Image_GetPaletteQ1( void )
{
	if( !q1palette_init )
	{
		image.d_rendermode = LUMP_NORMAL;
		Image_SetPalette( palette_q1, d_8toQ1table );
		d_8toQ1table[255] = 0; // 255 is transparent
		q1palette_init = true;
	}

	image.d_rendermode = LUMP_QUAKE1;
	image.d_currentpal = d_8toQ1table;
}

void Image_GetPaletteHL( void )
{
	if( !hlpalette_init )
	{
		image.d_rendermode = LUMP_NORMAL;
		Image_SetPalette( palette_hl, d_8toHLtable );
		hlpalette_init = true;
	}

	image.d_rendermode = LUMP_HALFLIFE;
	image.d_currentpal = d_8toHLtable;
}

void Image_GetPaletteBMP( const byte *pal )
{
	image.d_rendermode = LUMP_EXTENDED;

	if( pal )
	{
		Image_SetPalette( pal, d_8to24table );
		image.d_currentpal = d_8to24table;
	}
}

void Image_GetPaletteLMP( const byte *pal, int rendermode )
{
	image.d_rendermode = rendermode;

	if( pal )
	{
		Image_SetPalette( pal, d_8to24table );
		image.d_currentpal = d_8to24table;
	}
	else
	{
		switch( rendermode )
		{
		case LUMP_QUAKE1:
			Image_GetPaletteQ1();
			break;
		case LUMP_HALFLIFE:
			Image_GetPaletteHL();
			break;
		default:
			// defaulting to half-life palette
			Image_GetPaletteHL();
			break;
		}
	}
}

void Image_PaletteHueReplace( byte *palSrc, int newHue, int start, int end, int pal_size )
{
	float	r, g, b;
	float	maxcol, mincol;
	float	hue, val, sat;
	int	i;

	hue = (float)(newHue * ( 360.0f / 255 ));
	pal_size = bound( 3, pal_size, 4 );

	for( i = start; i <= end; i++ )
	{
		r = palSrc[i*pal_size+0];
		g = palSrc[i*pal_size+1];
		b = palSrc[i*pal_size+2];

		maxcol = Q_max( Q_max( r, g ), b ) / 255.0f;
		mincol = Q_min( Q_min( r, g ), b ) / 255.0f;

		if( maxcol == 0 ) continue;

		val = maxcol;
		sat = (maxcol - mincol) / maxcol;

		mincol = val * (1.0f - sat);

		if( hue <= 120.0f )
		{
			b = mincol;
			if( hue < 60 )
			{
				r = val;
				g = mincol + hue * (val - mincol) / (120.0f - hue);
			}
			else
			{
				g = val;
				r = mincol + (120.0f - hue) * (val - mincol) / hue;
			}
		}
		else if( hue <= 240.0f )
		{
			r = mincol;
			if( hue < 180.0f )
			{
				g = val;
				b = mincol + (hue - 120.0f) * (val - mincol) / (240.0f - hue);
			}
			else
			{
				b = val;
				g = mincol + (240.0f - hue) * (val - mincol) / (hue - 120.0f);
			}
		}
		else
		{
			g = mincol;
			if( hue < 300.0f )
			{
				b = val;
				r = mincol + (hue - 240.0f) * (val - mincol) / (360.0f - hue);
			}
			else
			{
				r = val;
				b = mincol + (360.0f - hue) * (val - mincol) / (hue - 240.0f);
			}
		}

		palSrc[i*pal_size+0] = (byte)(r * 255);
		palSrc[i*pal_size+1] = (byte)(g * 255);
		palSrc[i*pal_size+2] = (byte)(b * 255);
	}
}

static void Image_PaletteTranslate( byte *palSrc, int top, int bottom, int pal_size )
{
	byte	dst[256], src[256];
	int	i;

	pal_size = bound( 3, pal_size, 4 );
	for( i = 0; i < 256; i++ )
		src[i] = i;
	memcpy( dst, src, 256 );

	if( top < 128 )
	{
		// the artists made some backwards ranges. sigh.
		memcpy( dst + SHIRT_HUE_START, src + top, 16 );
	}
	else
	{
		for( i = 0; i < 16; i++ )
			dst[SHIRT_HUE_START+i] = src[top + 15 - i];
	}

	if( bottom < 128 )
	{
		memcpy( dst + PANTS_HUE_START, src + bottom, 16 );
	}
	else
	{
		for( i = 0; i < 16; i++ )
			dst[PANTS_HUE_START + i] = src[bottom + 15 - i];
	}

	// last color isn't changed
	for( i = 0; i < 255; i++ )
	{
		palSrc[i*pal_size+0] = palette_q1[dst[i]*3+0];
		palSrc[i*pal_size+1] = palette_q1[dst[i]*3+1];
		palSrc[i*pal_size+2] = palette_q1[dst[i]*3+2];
	}
}

void Image_CopyParms( rgbdata_t *src )
{
	Image_Reset();

	image.width = src->width;
	image.height = src->height;
	image.type = src->type;
	image.flags = src->flags;
	image.size = src->size;
	image.palette = src->palette;	// may be NULL

	memcpy( image.fogParams, src->fogParams, sizeof( image.fogParams ));
}

/*
============
Image_Copy8bitRGBA

NOTE: must call Image_GetPaletteXXX before used
============
*/
qboolean Image_Copy8bitRGBA( const byte *in, byte *out, int pixels )
{
	int	*iout = (int *)out;
	byte	*fin = (byte *)in;
	byte	*col;
	int	i;

	if( !in || !image.d_currentpal )
		return false;

	// this is a base image with luma - clear luma pixels
	if( image.flags & IMAGE_HAS_LUMA )
	{
		for( i = 0; i < image.width * image.height; i++ )
			fin[i] = fin[i] < 224 ? fin[i] : 0;
	}

	// check for color
	for( i = 0; i < 256; i++ )
	{
		col = (byte *)&image.d_currentpal[i];
		if( col[0] != col[1] || col[1] != col[2] )
		{
			image.flags |= IMAGE_HAS_COLOR;
			break;
		}
	}

	while( pixels >= 8 )
	{
		iout[0] = image.d_currentpal[in[0]];
		iout[1] = image.d_currentpal[in[1]];
		iout[2] = image.d_currentpal[in[2]];
		iout[3] = image.d_currentpal[in[3]];
		iout[4] = image.d_currentpal[in[4]];
		iout[5] = image.d_currentpal[in[5]];
		iout[6] = image.d_currentpal[in[6]];
		iout[7] = image.d_currentpal[in[7]];

		in += 8;
		iout += 8;
		pixels -= 8;
	}

	if( pixels & 4 )
	{
		iout[0] = image.d_currentpal[in[0]];
		iout[1] = image.d_currentpal[in[1]];
		iout[2] = image.d_currentpal[in[2]];
		iout[3] = image.d_currentpal[in[3]];
		in += 4;
		iout += 4;
	}

	if( pixels & 2 )
	{
		iout[0] = image.d_currentpal[in[0]];
		iout[1] = image.d_currentpal[in[1]];
		in += 2;
		iout += 2;
	}

	if( pixels & 1 ) // last byte
		iout[0] = image.d_currentpal[in[0]];
	image.type = PF_RGBA_32;	// update image type;

	return true;
}

static void Image_Resample32LerpLine( const byte *in, byte *out, int inwidth, int outwidth )
{
	int	j, xi, oldx = 0, f, fstep, endx, lerp;

	fstep = (int)(inwidth * 65536.0f / outwidth);
	endx = (inwidth-1);

	for( j = 0, f = 0; j < outwidth; j++, f += fstep )
	{
		xi = f>>16;
		if( xi != oldx )
		{
			in += (xi - oldx) * 4;
			oldx = xi;
		}
		if( xi < endx )
		{
			lerp = f & 0xFFFF;
			*out++ = (byte)((((in[4] - in[0]) * lerp)>>16) + in[0]);
			*out++ = (byte)((((in[5] - in[1]) * lerp)>>16) + in[1]);
			*out++ = (byte)((((in[6] - in[2]) * lerp)>>16) + in[2]);
			*out++ = (byte)((((in[7] - in[3]) * lerp)>>16) + in[3]);
		}
		else // last pixel of the line has no pixel to lerp to
		{
			*out++ = in[0];
			*out++ = in[1];
			*out++ = in[2];
			*out++ = in[3];
		}
	}
}

static void Image_Resample24LerpLine( const byte *in, byte *out, int inwidth, int outwidth )
{
	int	j, xi, oldx = 0, f, fstep, endx, lerp;

	fstep = (int)(inwidth * 65536.0f / outwidth);
	endx = (inwidth-1);

	for( j = 0, f = 0; j < outwidth; j++, f += fstep )
	{
		xi = f>>16;

		if( xi != oldx )
		{
			in += (xi - oldx) * 3;
			oldx = xi;
		}

		if( xi < endx )
		{
			lerp = f & 0xFFFF;
			*out++ = (byte)((((in[3] - in[0]) * lerp)>>16) + in[0]);
			*out++ = (byte)((((in[4] - in[1]) * lerp)>>16) + in[1]);
			*out++ = (byte)((((in[5] - in[2]) * lerp)>>16) + in[2]);
		}
		else // last pixel of the line has no pixel to lerp to
		{
			*out++ = in[0];
			*out++ = in[1];
			*out++ = in[2];
		}
	}
}

static void Image_Resample32Lerp( const void *indata, int inwidth, int inheight, void *outdata, int outwidth, int outheight )
{
	const byte *inrow;
	int	i, j, r, yi, oldy = 0, f, fstep, lerp, endy = (inheight - 1);
	int	inwidth4 = inwidth * 4;
	int	outwidth4 = outwidth * 4;
	byte	*out = (byte *)outdata;
	byte	*resamplerow1;
	byte	*resamplerow2;

	fstep = (int)(inheight * 65536.0f / outheight);

	resamplerow1 = (byte *)Mem_Malloc( host.imagepool, outwidth * 4 * 2);
	resamplerow2 = resamplerow1 + outwidth * 4;

	inrow = (const byte *)indata;

	Image_Resample32LerpLine( inrow, resamplerow1, inwidth, outwidth );
	Image_Resample32LerpLine( inrow + inwidth4, resamplerow2, inwidth, outwidth );

	for( i = 0, f = 0; i < outheight; i++, f += fstep )
	{
		yi = f>>16;

		if( yi < endy )
		{
			lerp = f & 0xFFFF;
			if( yi != oldy )
			{
				inrow = (byte *)indata + inwidth4 * yi;
				if( yi == oldy + 1 ) memcpy( resamplerow1, resamplerow2, outwidth4 );
				else Image_Resample32LerpLine( inrow, resamplerow1, inwidth, outwidth );
				Image_Resample32LerpLine( inrow + inwidth4, resamplerow2, inwidth, outwidth );
				oldy = yi;
			}

			j = outwidth - 4;

			while( j >= 0 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				LERPBYTE( 3);
				LERPBYTE( 4);
				LERPBYTE( 5);
				LERPBYTE( 6);
				LERPBYTE( 7);
				LERPBYTE( 8);
				LERPBYTE( 9);
				LERPBYTE(10);
				LERPBYTE(11);
				LERPBYTE(12);
				LERPBYTE(13);
				LERPBYTE(14);
				LERPBYTE(15);
				out += 16;
				resamplerow1 += 16;
				resamplerow2 += 16;
				j -= 4;
			}

			if( j & 2 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				LERPBYTE( 3);
				LERPBYTE( 4);
				LERPBYTE( 5);
				LERPBYTE( 6);
				LERPBYTE( 7);
				out += 8;
				resamplerow1 += 8;
				resamplerow2 += 8;
			}

			if( j & 1 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				LERPBYTE( 3);
				out += 4;
				resamplerow1 += 4;
				resamplerow2 += 4;
			}

			resamplerow1 -= outwidth4;
			resamplerow2 -= outwidth4;
		}
		else
		{
			if( yi != oldy )
			{
				inrow = (byte *)indata + inwidth4 * yi;
				if( yi == oldy + 1 ) memcpy( resamplerow1, resamplerow2, outwidth4 );
				else Image_Resample32LerpLine( inrow, resamplerow1, inwidth, outwidth);
				oldy = yi;
			}

			memcpy( out, resamplerow1, outwidth4 );
		}
	}

	Mem_Free( resamplerow1 );
}

static void Image_Resample32Nolerp( const void *indata, int inwidth, int inheight, void *outdata, int outwidth, int outheight )
{
	int	i, j;
	uint	frac, fracstep;
	int	*inrow, *out = (int *)outdata; // relies on int being 4 bytes

	fracstep = inwidth * 0x10000 / outwidth;

	for( i = 0; i < outheight; i++)
	{
		inrow = (int *)indata + inwidth * (i * inheight / outheight);
		frac = fracstep>>1;
		j = outwidth - 4;

		while( j >= 0 )
		{
			out[0] = inrow[frac >> 16];frac += fracstep;
			out[1] = inrow[frac >> 16];frac += fracstep;
			out[2] = inrow[frac >> 16];frac += fracstep;
			out[3] = inrow[frac >> 16];frac += fracstep;
			out += 4;
			j -= 4;
		}

		if( j & 2 )
		{
			out[0] = inrow[frac >> 16];frac += fracstep;
			out[1] = inrow[frac >> 16];frac += fracstep;
			out += 2;
		}

		if( j & 1 )
		{
			out[0] = inrow[frac >> 16];frac += fracstep;
			out += 1;
		}
	}
}

static void Image_Resample24Lerp( const void *indata, int inwidth, int inheight, void *outdata, int outwidth, int outheight )
{
	const byte *inrow;
	int	i, j, r, yi, oldy, f, fstep, lerp, endy = (inheight - 1);
	int	inwidth3 = inwidth * 3;
	int	outwidth3 = outwidth * 3;
	byte	*out = (byte *)outdata;
	byte	*resamplerow1;
	byte	*resamplerow2;

	fstep = (int)(inheight * 65536.0f / outheight);

	resamplerow1 = (byte *)Mem_Malloc( host.imagepool, outwidth * 3 * 2 );
	resamplerow2 = resamplerow1 + outwidth*3;

	inrow = (const byte *)indata;
	oldy = 0;
	Image_Resample24LerpLine( inrow, resamplerow1, inwidth, outwidth );
	Image_Resample24LerpLine( inrow + inwidth3, resamplerow2, inwidth, outwidth );

	for( i = 0, f = 0; i < outheight; i++, f += fstep )
	{
		yi = f>>16;

		if( yi < endy )
		{
			lerp = f & 0xFFFF;
			if( yi != oldy )
			{
				inrow = (byte *)indata + inwidth3 * yi;
				if( yi == oldy + 1) memcpy( resamplerow1, resamplerow2, outwidth3 );
				else Image_Resample24LerpLine( inrow, resamplerow1, inwidth, outwidth );
				Image_Resample24LerpLine( inrow + inwidth3, resamplerow2, inwidth, outwidth );
				oldy = yi;
			}

			j = outwidth - 4;

			while( j >= 0 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				LERPBYTE( 3);
				LERPBYTE( 4);
				LERPBYTE( 5);
				LERPBYTE( 6);
				LERPBYTE( 7);
				LERPBYTE( 8);
				LERPBYTE( 9);
				LERPBYTE(10);
				LERPBYTE(11);
				out += 12;
				resamplerow1 += 12;
				resamplerow2 += 12;
				j -= 4;
			}

			if( j & 2 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				LERPBYTE( 3);
				LERPBYTE( 4);
				LERPBYTE( 5);
				out += 6;
				resamplerow1 += 6;
				resamplerow2 += 6;
			}

			if( j & 1 )
			{
				LERPBYTE( 0);
				LERPBYTE( 1);
				LERPBYTE( 2);
				out += 3;
				resamplerow1 += 3;
				resamplerow2 += 3;
			}

			resamplerow1 -= outwidth3;
			resamplerow2 -= outwidth3;
		}
		else
		{
			if( yi != oldy )
			{
				inrow = (byte *)indata + inwidth3*yi;
				if( yi == oldy + 1) memcpy( resamplerow1, resamplerow2, outwidth3 );
				else Image_Resample24LerpLine( inrow, resamplerow1, inwidth, outwidth );
				oldy = yi;
			}

			memcpy( out, resamplerow1, outwidth3 );
		}
	}

	Mem_Free( resamplerow1 );
}

static void Image_Resample24Nolerp( const void *indata, int inwidth, int inheight, void *outdata, int outwidth, int outheight )
{
	uint	frac, fracstep;
	int	i, j, f, inwidth3 = inwidth * 3;
	byte	*inrow, *out = (byte *)outdata;

	fracstep = inwidth * 0x10000 / outwidth;

	for( i = 0; i < outheight; i++)
	{
		inrow = (byte *)indata + inwidth3 * (i * inheight / outheight);
		frac = fracstep>>1;
		j = outwidth - 4;

		while( j >= 0 )
		{
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			j -= 4;
		}

		if( j & 2 )
		{
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			out += 2;
		}

		if( j & 1 )
		{
			f = (frac >> 16)*3;
			*out++ = inrow[f+0];
			*out++ = inrow[f+1];
			*out++ = inrow[f+2];
			frac += fracstep;
			out += 1;
		}
	}
}

static void Image_Resample8Nolerp( const void *indata, int inwidth, int inheight, void *outdata, int outwidth, int outheight )
{
	int	i, j;
	byte	*in, *inrow;
	uint	frac, fracstep;
	byte	*out = (byte *)outdata;

	in = (byte *)indata;
	fracstep = inwidth * 0x10000 / outwidth;

	for( i = 0; i < outheight; i++, out += outwidth )
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep>>1;

		for( j = 0; j < outwidth; j++ )
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}

/*
================
Image_Resample
================
*/
byte *Image_ResampleInternal( const void *indata, int inwidth, int inheight, int outwidth, int outheight, int type, qboolean *resampled )
{
	qboolean	quality = Image_CheckFlag( IL_USE_LERPING );

	// nothing to resample ?
	if( inwidth == outwidth && inheight == outheight )
	{
		*resampled = false;
		return (byte *)indata;
	}

	// alloc new buffer
	switch( type )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
		image.tempbuffer = (byte *)Mem_Realloc( host.imagepool, image.tempbuffer, outwidth * outheight );
		Image_Resample8Nolerp( indata, inwidth, inheight, image.tempbuffer, outwidth, outheight );
		break;
	case PF_RGB_24:
	case PF_BGR_24:
		image.tempbuffer = (byte *)Mem_Realloc( host.imagepool, image.tempbuffer, outwidth * outheight * 3 );
		if( quality ) Image_Resample24Lerp( indata, inwidth, inheight, image.tempbuffer, outwidth, outheight );
		else Image_Resample24Nolerp( indata, inwidth, inheight, image.tempbuffer, outwidth, outheight );
		break;
	case PF_RGBA_32:
	case PF_BGRA_32:
		image.tempbuffer = (byte *)Mem_Realloc( host.imagepool, image.tempbuffer, outwidth * outheight * 4 );
		if( quality ) Image_Resample32Lerp( indata, inwidth, inheight, image.tempbuffer, outwidth, outheight );
		else Image_Resample32Nolerp( indata, inwidth, inheight, image.tempbuffer, outwidth, outheight );
		break;
	default:
		*resampled = false;
		return (byte *)indata;
	}

	*resampled = true;
	return image.tempbuffer;
}

/*
================
Image_Flip
================
*/
byte *Image_FlipInternal( const byte *in, word *srcwidth, word *srcheight, int type, int flags )
{
	int	i, x, y;
	word	width = *srcwidth;
	word	height = *srcheight;
	int	samples = PFDesc[type].bpp;
	qboolean	flip_x = FBitSet( flags, IMAGE_FLIP_X ) ? true : false;
	qboolean	flip_y = FBitSet( flags, IMAGE_FLIP_Y ) ? true : false;
	qboolean	flip_i = FBitSet( flags, IMAGE_ROT_90 ) ? true : false;
	int	row_inc = ( flip_y ? -samples : samples ) * width;
	int	col_inc = ( flip_x ? -samples : samples );
	int	row_ofs = ( flip_y ? ( height - 1 ) * width * samples : 0 );
	int	col_ofs = ( flip_x ? ( width - 1 ) * samples : 0 );
	const byte *p, *line;
	byte	*out;

	// nothing to process
	if( !FBitSet( flags, IMAGE_FLIP_X|IMAGE_FLIP_Y|IMAGE_ROT_90 ))
		return (byte *)in;

	switch( type )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
	case PF_RGB_24:
	case PF_BGR_24:
	case PF_RGBA_32:
	case PF_BGRA_32:
		image.tempbuffer = Mem_Realloc( host.imagepool, image.tempbuffer, width * height * samples );
		break;
	default:
		return (byte *)in;
	}

	out = image.tempbuffer;

	if( flip_i )
	{
		for( x = 0, line = in + col_ofs; x < width; x++, line += col_inc )
			for( y = 0, p = line + row_ofs; y < height; y++, p += row_inc, out += samples )
				for( i = 0; i < samples; i++ )
					out[i] = p[i];
	}
	else
	{
		for( y = 0, line = in + row_ofs; y < height; y++, line += row_inc )
			for( x = 0, p = line + col_ofs; x < width; x++, p += col_inc, out += samples )
				for( i = 0; i < samples; i++ )
					out[i] = p[i];
	}

	// update dims
	if( FBitSet( flags, IMAGE_ROT_90 ))
	{
		*srcwidth = height;
		*srcheight = width;
	}
	else
	{
		*srcwidth = width;
		*srcheight = height;
	}

	return image.tempbuffer;
}

static byte *Image_MakeLuma( byte *fin, int width, int height, int type, int flags )
{
	byte	*out;
	int	i;

	if( !FBitSet( flags, IMAGE_HAS_LUMA ))
		return (byte *)fin;

	switch( type )
	{
	case PF_INDEXED_24:
	case PF_INDEXED_32:
		out = image.tempbuffer = Mem_Realloc( host.imagepool, image.tempbuffer, width * height );
		for( i = 0; i < width * height; i++ )
			*out++ = fin[i] >= 224 ? fin[i] : 0;
		break;
	default:
		// another formats does ugly result :(
		Con_Printf( S_ERROR "%s: unsupported format %s\n", __func__, PFDesc[type].name );
		return (byte *)fin;
	}

	return image.tempbuffer;
}

qboolean Image_AddIndexedImageToPack( const byte *in, int width, int height )
{
	int	mipsize = width * height;
	qboolean	expand_to_rgba = true;

	if( Image_CheckFlag( IL_KEEP_8BIT ))
		expand_to_rgba = false;
	else if( FBitSet( image.flags, IMAGE_HAS_LUMA|IMAGE_QUAKESKY ))
		expand_to_rgba = false;

	image.size = mipsize;

	if( expand_to_rgba ) image.size *= 4;
	else Image_CopyPalette32bit();

	// reallocate image buffer
	image.rgba = Mem_Malloc( host.imagepool, image.size );
	if( !expand_to_rgba ) memcpy( image.rgba, in, image.size );
	else if( !Image_Copy8bitRGBA( in, image.rgba, mipsize ))
		return false; // probably pallette not installed

	return true;
}

/*
=============
Image_Decompress

force to unpack any image to 32-bit buffer
=============
*/
static qboolean Image_Decompress( const byte *data )
{
	byte	*fin, *fout;
	int	i, size;

	if( !data ) return false;
	fin = (byte *)data;

	size = image.width * image.height * 4;
	image.tempbuffer = Mem_Realloc( host.imagepool, image.tempbuffer, size );
	fout = image.tempbuffer;

	switch( PFDesc[image.type].format )
	{
	case PF_INDEXED_24:
		if( image.flags & IMAGE_HAS_ALPHA )
		{
			if( image.flags & IMAGE_COLORINDEX )
				Image_GetPaletteLMP( image.palette, LUMP_GRADIENT );
			else Image_GetPaletteLMP( image.palette, LUMP_MASKED );
		}
		else Image_GetPaletteLMP( image.palette, LUMP_NORMAL );
		// intentionally fallthrough
	case PF_INDEXED_32:
		if( !image.d_currentpal ) image.d_currentpal = (uint *)image.palette;
		if( !Image_Copy8bitRGBA( fin, fout, image.width * image.height ))
			return false;
		break;
	case PF_BGR_24:
		for (i = 0; i < image.width * image.height; i++ )
		{
			fout[(i<<2)+0] = fin[i*3+2];
			fout[(i<<2)+1] = fin[i*3+1];
			fout[(i<<2)+2] = fin[i*3+0];
			fout[(i<<2)+3] = 255;
		}
		break;
	case PF_RGB_24:
		for (i = 0; i < image.width * image.height; i++ )
		{
			fout[(i<<2)+0] = fin[i*3+0];
			fout[(i<<2)+1] = fin[i*3+1];
			fout[(i<<2)+2] = fin[i*3+2];
			fout[(i<<2)+3] = 255;
		}
		break;
	case PF_BGRA_32:
		for( i = 0; i < image.width * image.height; i++ )
		{
			fout[i*4+0] = fin[i*4+2];
			fout[i*4+1] = fin[i*4+1];
			fout[i*4+2] = fin[i*4+0];
			fout[i*4+3] = fin[i*4+3];
		}
		break;
	case PF_RGBA_32:
		// fast default case
		memcpy( fout, fin, size );
		break;
	default: return false;
	}

	// set new size
	image.size = size;

	return true;
}

static rgbdata_t *Image_DecompressInternal( rgbdata_t *pic )
{
	// quick case to reject unneeded conversions
	if( pic->type == PF_RGBA_32 )
		return pic;

	Image_CopyParms( pic );
	image.size = image.ptr = 0;

	Image_Decompress( pic->buffer );

	// now we can change type to RGBA
	pic->type = PF_RGBA_32;

	pic->buffer = Mem_Realloc( host.imagepool, pic->buffer, image.size );
	memcpy( pic->buffer, image.tempbuffer, image.size );
	if( pic->palette ) Mem_Free( pic->palette );
	pic->flags = image.flags;
	pic->palette = NULL;

	return pic;
}

static rgbdata_t *Image_LightGamma( rgbdata_t *pic )
{
	byte	*in = (byte *)pic->buffer;
	int	i;

	if( pic->type != PF_RGBA_32 )
		return pic;

	for( i = 0; i < pic->width * pic->height; i++, in += 4 )
	{
		in[0] = LightToTexGamma( in[0] );
		in[1] = LightToTexGamma( in[1] );
		in[2] = LightToTexGamma( in[2] );
	}

	return pic;
}

static qboolean Image_RemapInternal( rgbdata_t *pic, int topColor, int bottomColor )
{
	if( !pic->palette )
		return false;

	switch( pic->type )
	{
	case PF_INDEXED_24:
		break;
	case PF_INDEXED_32:
		Image_ConvertPalTo24bit( pic );
		break;
	default:
		return false;
	}

	if( Image_ComparePalette( pic->palette ) == PAL_QUAKE1 )
	{
		Image_PaletteTranslate( pic->palette, topColor * 16, bottomColor * 16, 3 );
	}
	else
	{
		// g-cont. preview images has a swapped top and bottom colors. I don't know why.
		Image_PaletteHueReplace( pic->palette, topColor, SUIT_HUE_START, SUIT_HUE_END, 3 );
		Image_PaletteHueReplace( pic->palette, bottomColor, PLATE_HUE_START, PLATE_HUE_END, 3 );
	}

	return true;
}

qboolean Image_Process( rgbdata_t **pix, int width, int height, uint flags, float reserved )
{
	rgbdata_t	*pic = *pix;
	qboolean	result = true;
	byte	*out;

	// check for buffers
	if( unlikely( !pic || !pic->buffer ))
	{
		image.force_flags = 0;
		return false;
	}

	if( unlikely( !flags ))
	{
		// clear any force flags
		image.force_flags = 0;
		return false; // no operation specfied
	}

	if( FBitSet( flags, IMAGE_MAKE_LUMA ))
	{
		out = Image_MakeLuma( pic->buffer, pic->width, pic->height, pic->type, pic->flags );
		if( pic->buffer != out ) memcpy( pic->buffer, image.tempbuffer, pic->size );
		ClearBits( pic->flags, IMAGE_HAS_LUMA );
	}

	if( FBitSet( flags, IMAGE_REMAP ))
	{
		// NOTE: user should keep copy of indexed image manually for new changes
		if( Image_RemapInternal( pic, width, height ))
			pic = Image_DecompressInternal( pic );
	}

	// update format to RGBA if any
	if( FBitSet( flags, IMAGE_FORCE_RGBA ))
		pic = Image_DecompressInternal( pic );

	if( FBitSet( flags, IMAGE_LIGHTGAMMA ))
		pic = Image_LightGamma( pic );

	out = Image_FlipInternal( pic->buffer, &pic->width, &pic->height, pic->type, flags );
	if( pic->buffer != out ) memcpy( pic->buffer, image.tempbuffer, pic->size );

	if( FBitSet( flags, IMAGE_RESAMPLE ) && width > 0 && height > 0 )
	{
		int	w = bound( 1, width, IMAGE_MAXWIDTH );	// 1 - 4096
		int	h = bound( 1, height, IMAGE_MAXHEIGHT);	// 1 - 4096
		qboolean	resampled = false;

		out = Image_ResampleInternal((uint *)pic->buffer, pic->width, pic->height, w, h, pic->type, &resampled );

		if( resampled ) // resampled or filled
		{
			Con_Reportf( "Image_Resample: from[%d x %d] to [%d x %d]\n", pic->width, pic->height, w, h );
			pic->width = w, pic->height = h;
			pic->size = w * h * PFDesc[pic->type].bpp;
			Mem_Free( pic->buffer );		// free original image buffer
			pic->buffer = Image_Copy( pic->size );	// unzone buffer
		}
		else
		{
			// not a resampled or filled
			result = false;
		}
	}

	// quantize image
	if( FBitSet( flags, IMAGE_QUANTIZE ))
		pic = Image_Quantize( pic );

	*pix = pic;

	// clear any force flags
	image.force_flags = 0;

	return result;
}

// This codebase has too many copies of this function:
// - ref_gl has one
// - ref_vk has one
// - ref_soft has one
// - many more places probably have one too
// TODO figure out how to make it available for ref_*
size_t Image_ComputeSize( int type, int width, int height, int depth )
{
	switch( type )
	{
	case PF_DXT1:
	case PF_BC4_SIGNED:
	case PF_BC4_UNSIGNED:
		return ((( width + 3 ) / 4 ) * (( height + 3 ) / 4 ) * depth * 8 );
	case PF_DXT3:
	case PF_DXT5:
	case PF_ATI2:
	case PF_BC5_UNSIGNED:
	case PF_BC5_SIGNED:
	case PF_BC6H_SIGNED:
	case PF_BC6H_UNSIGNED:
	case PF_BC7_UNORM:
	case PF_BC7_SRGB: return ((( width + 3 ) / 4 ) * (( height + 3 ) / 4 ) * depth * 16 );
	case PF_LUMINANCE: return ( width * height * depth );
	case PF_BGR_24:
	case PF_RGB_24: return ( width * height * depth * 3 );
	case PF_BGRA_32:
	case PF_RGBA_32: return ( width * height * depth * 4 );
	}

	return 0;
}

/*
============
Image_GenerateMipmaps
============
*/
void Image_GenerateMipmaps( const byte *source, int width, int height, byte *mip1, byte *mip2, byte *mip3 )
{
	const int sizes[3][2] = {
		{ width / 2, height / 2 },
		{ width / 4, height / 4 },
		{ width / 8, height / 8 }
	};
	byte      *mips[3] = { mip1, mip2, mip3 };
	int m, mw, mh, step, y, x;

	for( m = 0; m < 3; ++m )
	{
		if( !mips[m] )
			continue;
		mw = sizes[m][0];
		mh = sizes[m][1];
		step = 1 << ( m + 1 );
		for( y = 0; y < mh; ++y )
		{
			for( x = 0; x < mw; ++x )
			{
				mips[m][y * mw + x] = source[( y * step ) * width + ( x * step )];
			}
		}
	}
}
