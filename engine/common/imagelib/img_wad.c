/*
img_mip.c - hl1 and q1 image mips
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
#include "wadfile.h"
#include "studio.h"
#include "sprite.h"
#include "qfont.h"

/*
============
Image_LoadPAL
============
*/
qboolean Image_LoadPAL( const char *name, const byte *buffer, fs_offset_t filesize )
{
	int	rendermode = LUMP_NORMAL;

	if( filesize != 768 )
	{
		Con_DPrintf( S_ERROR "Image_LoadPAL: (%s) have invalid size (%ld should be %d)\n", name, filesize, 768 );
		return false;
	}

	if( name[0] == '#' )
	{
		// using palette name as rendermode
		if( Q_stristr( name, "normal" ))
			rendermode = LUMP_NORMAL;
		else if( Q_stristr( name, "masked" ))
			rendermode = LUMP_MASKED;
		else if( Q_stristr( name, "gradient" ))
			rendermode = LUMP_GRADIENT;
		else if( Q_stristr( name, "valve" ))
		{
			rendermode = LUMP_HALFLIFE;
			buffer = NULL; // force to get HL palette
		}
		else if( Q_stristr( name, "id" ))
		{
			rendermode = LUMP_QUAKE1;
			buffer = NULL; // force to get Q1 palette
		}
	}

	// NOTE: image.d_currentpal not cleared with Image_Reset()
	// and stay valid any time before new call of Image_SetPalette
	Image_GetPaletteLMP( buffer, rendermode );
	Image_CopyPalette32bit();

	image.rgba = NULL;	// only palette, not real image
	image.size = 1024;	// expanded palette
	image.width = image.height = 0;
	image.depth = 1;

	return true;
}

/*
============
Image_LoadFNT
============
*/
qboolean Image_LoadFNT( const char *name, const byte *buffer, fs_offset_t filesize )
{
	qfont_t		font;
	const byte	*pal, *fin;
	size_t		size;
	int		numcolors;

	if( image.hint == IL_HINT_Q1 )
		return false; // Quake1 doesn't have qfonts

	if( filesize < sizeof( font ))
		return false;

	memcpy( &font, buffer, sizeof( font ));

	// last sixty four bytes - what the hell ????
	size = sizeof( qfont_t ) - 4 + ( font.height * font.width * QCHAR_WIDTH ) + sizeof( short ) + 768 + 64;

	if( size != filesize )
	{
		// oldstyle font: "conchars" or "creditsfont"
		image.width = 256;		// hardcoded
		image.height = font.height;
	}
	else
	{
		// Half-Life 1.1.0.0 font style (qfont_t)
		image.width = font.width * QCHAR_WIDTH;
		image.height = font.height;
	}

	if( !Image_LumpValidSize( name ))
		return false;

	fin = buffer + sizeof( font ) - 4;
	pal = fin + (image.width * image.height);
	numcolors = *(short *)pal, pal += sizeof( short );

	if( numcolors == 768 || numcolors == 256 )
	{
		// g-cont. make sure that is didn't hit anything
		Image_GetPaletteLMP( pal, LUMP_MASKED );
		image.flags |= IMAGE_HAS_ALPHA; // fonts always have transparency
	}
	else
	{
		return false;
	}

	image.type = PF_INDEXED_32;	// 32-bit palette
	image.depth = 1;

	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}

/*
======================
Image_SetMDLPointer

Transfer buffer pointer before Image_LoadMDL
======================
*/
static void *g_mdltexdata;
void Image_SetMDLPointer( byte *p )
{
	g_mdltexdata = p;
}

/*
============
Image_LoadMDL
============
*/
qboolean Image_LoadMDL( const char *name, const byte *buffer, fs_offset_t filesize )
{
	byte		*fin;
	size_t		pixels;
	mstudiotexture_t	*pin;
	int		flags;

	pin = (mstudiotexture_t *)buffer;
	flags = pin->flags;

	image.width = pin->width;
	image.height = pin->height;
	pixels = image.width * image.height;
	fin = (byte *)g_mdltexdata;
	ASSERT(fin);
	g_mdltexdata = NULL;

	if( !Image_ValidSize( name ))
		return false;

	if( image.hint == IL_HINT_HL )
	{
		if( filesize < ( sizeof( *pin ) + pixels + 768 ))
			return false;

		if( FBitSet( flags, STUDIO_NF_MASKED ))
		{
			byte	*pal = fin + pixels;

			Image_GetPaletteLMP( pal, LUMP_MASKED );
			image.flags |= IMAGE_HAS_ALPHA|IMAGE_ONEBIT_ALPHA;
		}
		else Image_GetPaletteLMP( fin + pixels, LUMP_NORMAL );
	}
	else
	{
		return false; // unknown or unsupported mode rejected
	}

	image.type = PF_INDEXED_32;	// 32-bit palete
	image.depth = 1;

	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}

/*
============
Image_LoadSPR
============
*/
qboolean Image_LoadSPR( const char *name, const byte *buffer, fs_offset_t filesize )
{
	dspriteframe_t	pin;	// identical for q1\hl sprites
	qboolean		truecolor = false;
	byte *fin;

	if( image.hint == IL_HINT_HL )
	{
		if( !image.d_currentpal )
			return false;
	}
	else if( image.hint == IL_HINT_Q1 )
	{
		Image_GetPaletteQ1();
	}
	else
	{
		// unknown mode rejected
		return false;
	}

	memcpy( &pin, buffer, sizeof(dspriteframe_t) );
	image.width = pin.width;
	image.height = pin.height;

	if( filesize < image.width * image.height )
		return false;

	if( filesize == ( image.width * image.height * 4 ))
		truecolor = true;

	// sorry, can't validate palette rendermode
	if( !Image_LumpValidSize( name )) return false;
	image.type = (truecolor) ? PF_RGBA_32 : PF_INDEXED_32;	// 32-bit palete
	image.depth = 1;

	// detect alpha-channel by palette type
	switch( image.d_rendermode )
	{
	case LUMP_MASKED:
		SetBits( image.flags, IMAGE_ONEBIT_ALPHA );
		// intentionally fallthrough
	case LUMP_GRADIENT:
	case LUMP_QUAKE1:
		SetBits( image.flags, IMAGE_HAS_ALPHA );
		break;
	}

	fin =  (byte *)(buffer + sizeof(dspriteframe_t));

	if( truecolor )
	{
		// spr32 support
		image.size = image.width * image.height * 4;
		image.rgba = Mem_Malloc( host.imagepool, image.size );
		memcpy( image.rgba, fin, image.size );
		SetBits( image.flags, IMAGE_HAS_COLOR ); // Color. True Color!
		return true;
	}
	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}

/*
============
Image_LoadLMP
============
*/
qboolean Image_LoadLMP( const char *name, const byte *buffer, fs_offset_t filesize )
{
	lmp_t	lmp;
	byte	*fin, *pal;
	int	rendermode;
	int	i, pixels;

	if( filesize < sizeof( lmp ))
		return false;

	// valve software trick (particle palette)
	if( Q_stristr( name, "palette.lmp" ))
		return Image_LoadPAL( name, buffer, filesize );

	// id software trick (image without header)
	if( Q_stristr( name, "conchars" ) && filesize == 16384 )
	{
		image.width = image.height = 128;
		rendermode = LUMP_QUAKE1;
		filesize += sizeof( lmp );
		fin = (byte *)buffer;

		// need to remap transparent color from first to last entry
		for( i = 0; i < 16384; i++ ) if( !fin[i] ) fin[i] = 0xFF;
	}
	else
	{
		fin = (byte *)buffer;
		memcpy( &lmp, fin, sizeof( lmp ));
		image.width = lmp.width;
		image.height = lmp.height;
		rendermode = LUMP_NORMAL;
		fin += sizeof( lmp );
	}

	pixels = image.width * image.height;

	if( filesize < sizeof( lmp ) + pixels )
		return false;

	if( !Image_ValidSize( name ))
		return false;

	if( image.hint != IL_HINT_Q1 && filesize > (int)sizeof(lmp) + pixels )
	{
		int	numcolors;

		// HACKHACK: console background image shouldn't be transparent
		if( !Q_stristr( name, "conback" ))
		{
			for( i = 0; i < pixels; i++ )
			{
				if( fin[i] == 255 )
				{
					image.flags |= IMAGE_HAS_ALPHA;
					rendermode = LUMP_MASKED;
					break;
				}
			}
		}
		pal = fin + pixels;
		numcolors = *(short *)pal;
		if( numcolors != 256 ) pal = NULL; // corrupted lump ?
		else pal += sizeof( short );
	}
	else if( image.hint != IL_HINT_HL )
	{
		image.flags |= IMAGE_HAS_ALPHA;
		rendermode = LUMP_QUAKE1;
		pal = NULL;
	}
	else
	{
		// unknown mode rejected
		return false;
	}

	Image_GetPaletteLMP( pal, rendermode );
	image.type = PF_INDEXED_32; // 32-bit palete
	image.depth = 1;

	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}

/*
=============
Image_LoadMIP
=============
*/
qboolean Image_LoadMIP( const char *name, const byte *buffer, fs_offset_t filesize )
{
	mip_t	mip;
	qboolean	hl_texture;
	byte	*fin, *pal;
	int	ofs[4], rendermode;
	int	i, pixels, numcolors;
	int	reflectivity[3] = { 0, 0, 0 };

	if( filesize < sizeof( mip ))
		return false;

	memcpy( &mip, buffer, sizeof( mip ));
	image.width = mip.width;
	image.height = mip.height;

	if( !Image_ValidSize( name ))
		return false;

	memcpy( ofs, mip.offsets, sizeof( ofs ));
	pixels = image.width * image.height;

	if( image.hint != IL_HINT_Q1 && filesize >= (int)sizeof(mip) + ((pixels * 85)>>6) + sizeof(short) + 768)
	{
		// half-life 1.0.0.1 mip version with palette
		fin = (byte *)buffer + mip.offsets[0];
		pal = (byte *)buffer + mip.offsets[0] + (((image.width * image.height) * 85)>>6);
		numcolors = *(short *)pal;
		if( numcolors != 256 ) pal = NULL; // corrupted mip ?
		else pal += sizeof( short ); // skip colorsize

		hl_texture = true;

		// setup rendermode
		if( Q_strrchr( name, '{' ))
		{
			// NOTE: decals with 'blue base' can be interpret as colored decals
			if( !Image_CheckFlag( IL_LOAD_DECAL ) || ( pal[765] == 0 && pal[766] == 0 && pal[767] == 255 ))
			{
				SetBits( image.flags, IMAGE_ONEBIT_ALPHA );
				rendermode = LUMP_MASKED;
			}
			else
			{
				// classic gradient decals
				SetBits( image.flags, IMAGE_COLORINDEX );
				rendermode = LUMP_GRADIENT;
			}

			SetBits( image.flags, IMAGE_HAS_ALPHA );
		}
		else
		{
			int	pal_type;

			// NOTE: we can have luma-pixels if quake1 texture
			// converted into the hl texture but palette leave unchanged
			// this is a good reason for using fullbright pixels
			pal_type = Image_ComparePalette( pal );

			// check for luma pixels (but ignore liquid textures because they have no lightmap)
			if( mip.name[0] != '*' && mip.name[0] != '!' && pal_type == PAL_QUAKE1 )
			{
				for( i = 0; i < image.width * image.height; i++ )
				{
					if( fin[i] > 224 )
					{
						image.flags |= IMAGE_HAS_LUMA;
						break;
					}
				}
			}

			if( pal_type == PAL_QUAKE1 )
				SetBits( image.flags, IMAGE_QUAKEPAL );
			rendermode = LUMP_NORMAL;
		}

		Image_GetPaletteLMP( pal, rendermode );
		image.d_currentpal[255] &= 0xFFFFFF;
	}
	else if( image.hint != IL_HINT_HL && filesize >= (int)sizeof(mip) + ((pixels * 85)>>6))
	{
		// quake1 1.01 mip version without palette
		fin = (byte *)buffer + mip.offsets[0];
		pal = NULL; // clear palette
		rendermode = LUMP_NORMAL;

		hl_texture = false;

		// check for luma and alpha pixels
		if( !image.custom_palette )
		{
			for( i = 0; i < image.width * image.height; i++ )
			{
				if( fin[i] > 224 && fin[i] != 255 )
				{
					// don't apply luma to water surfaces because they have no lightmap
					if( mip.name[0] != '*' && mip.name[0] != '!' )
						image.flags |= IMAGE_HAS_LUMA;
					break;
				}
			}
		}

		// Arcane Dimensions has the transparent textures
		if( Q_strrchr( name, '{' ))
		{
			for( i = 0; i < image.width * image.height; i++ )
			{
				if( fin[i] == 255 )
				{
					// don't set ONEBIT_ALPHA flag for some reasons
					image.flags |= IMAGE_HAS_ALPHA;
					break;
				}
			}
		}

		SetBits( image.flags, IMAGE_QUAKEPAL );
		Image_GetPaletteQ1();
	}
	else
	{
		return false; // unknown or unsupported mode rejected
	}

	// check for quake-sky texture
	if( !Q_strncmp( mip.name, "sky", 3 ) && image.width == ( image.height * 2 ))
	{
		// g-cont: we need to run additional checks for palette type and colors ?
		image.flags |= IMAGE_QUAKESKY;
	}

	// check for half-life water texture
	if( hl_texture && ( mip.name[0] == '!' || !Q_strnicmp( mip.name, "water", 5 )))
	{
		// grab the fog color
		image.fogParams[0] = pal[3*3+0];
		image.fogParams[1] = pal[3*3+1];
		image.fogParams[2] = pal[3*3+2];

		// grab the fog density
		image.fogParams[3] = pal[4*3+0];
	}
	else if( hl_texture && ( rendermode == LUMP_GRADIENT ))
	{
		// grab the decal color
		image.fogParams[0] = pal[255*3+0];
		image.fogParams[1] = pal[255*3+1];
		image.fogParams[2] = pal[255*3+2];

		// calc the decal reflectivity
		image.fogParams[3] = VectorAvg( image.fogParams );
	}
	else if( pal != NULL )
	{
		// calc texture reflectivity
		for( i = 0; i < 256; i++ )
		{
			reflectivity[0] += pal[i*3+0];
			reflectivity[1] += pal[i*3+1];
			reflectivity[2] += pal[i*3+2];
		}

		VectorDivide( reflectivity, 256, image.fogParams );
	}

	image.type = PF_INDEXED_32;	// 32-bit palete
	image.depth = 1;

	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}
