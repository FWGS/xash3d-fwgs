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
	byte pal[768];

	if( filesize > sizeof( pal ))
	{
		Con_DPrintf( S_ERROR "%s: (%s) have invalid size (%li should be less or equal than %zu)\n", __func__, name, (long)filesize, sizeof( pal ));
		return false;
	}
	else if( filesize < sizeof( pal ) && buffer != NULL )
	{
		// palette might be truncated, fill it with zeros
		memset( pal, 0, sizeof( pal ));
		memcpy( pal, buffer, filesize );
		buffer = pal;
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
		else if( Q_stristr( name, "texgamma" ))
			rendermode = LUMP_TEXGAMMA;
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
		else Image_GetPaletteLMP( fin + pixels, LUMP_TEXGAMMA );
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

	memcpy( &pin, buffer, sizeof( dspriteframe_t ));
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
	uint	reflectivity[3] = { 0, 0, 0 };

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
			if( !Image_CheckFlag( IL_LOAD_DECAL ) || ( pal && pal[765] == 0 && pal[766] == 0 && pal[767] == 255 ))
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
			{
				SetBits( image.flags, IMAGE_QUAKEPAL );

				// if texture was converted from quake to half-life with no palette changes
				// then applying texgamma might make it too dark or even outright broken
				rendermode = LUMP_NORMAL;
			}
			else
			{
				// half-life mips need texgamma applied
				rendermode = LUMP_TEXGAMMA;
			}
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
	if( pal != NULL )
	{
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
		else
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
	}

	image.type = PF_INDEXED_32;	// 32-bit palete
	image.depth = 1;

	return Image_AddIndexedImageToPack( fin, image.width, image.height );
}

/*
============
Image_LoadWAD
============
*/
qboolean Image_LoadWAD( const char *name, const byte *buffer, fs_offset_t filesize )
{
	const dwadinfo_t    *header;
	const dlumpinfo_t   *lumps;
	const unsigned char *mipdata;
	int i, j;

	if( !buffer || filesize < sizeof( dwadinfo_t ))
		return false;

	header = (const dwadinfo_t *)buffer;
	if( header->numlumps <= 0 || header->infotableofs <= 0 || header->infotableofs >= (int)filesize )
		return false;

	lumps = (const dlumpinfo_t *)((const unsigned char *)buffer + header->infotableofs );

	for( i = 0; i < header->numlumps; ++i )
	{
		const unsigned char *pixels, *palette, *use_palette;
		unsigned char grad_palette[256 * 3];
		int mip_size;
		mip_t mip;
		uint32_t      width, height, offset0;
		uint32_t      m0size, m1size, m2size, m3size;
		qboolean      alpha_mode = false;
		unsigned char frontR = 0, frontG = 0, frontB = 0;
		float t;
		byte  idx;

		if( lumps[i].type != TYP_MIPTEX && lumps[i].type != TYP_PALETTE )
			continue;

		// get lump data and validate
		mipdata = (const unsigned char *)buffer + lumps[i].filepos;
		mip_size = lumps[i].disksize;
		if( lumps[i].filepos < 0 || lumps[i].filepos + mip_size > (int)filesize )
			continue;

		memcpy( &mip, mipdata, sizeof( mip ));
		width = mip.width;
		height = mip.height;

		if( width <= 0 || height <= 0 || width > 256 || height > 256 )
			continue;

		offset0 = mip.offsets[0];
		if( offset0 == 0 || offset0 + width * height > (uint32_t)mip_size )
			continue;

		pixels = mipdata + offset0;
		m0size = width * height;
		m1size = m0size / 4;
		m2size = m0size / 16;
		m3size = m0size / 64;
		palette = mipdata + 0x28 + m0size + m1size + m2size + m3size + 2;
		use_palette = palette;

		// handle gradient palette
		if( lumps[i].type == TYP_PALETTE )
		{
			// gradient palette
			const unsigned char *frontColorPtr = palette + 255 * 3;
			frontR = frontColorPtr[0];
			frontG = frontColorPtr[1];
			frontB = frontColorPtr[2];
			for( j = 0; j < 256; ++j )
			{
				t = j / 255.0f;
				grad_palette[j * 3 + 0] = (unsigned char)( frontR * t );
				grad_palette[j * 3 + 1] = (unsigned char)( frontG * t );
				grad_palette[j * 3 + 2] = (unsigned char)( frontB * t );
			}
			use_palette = grad_palette;
			alpha_mode = true; // gradient
		}

		// prepare image structure
		Image_Reset();
		image.width = width;
		image.height = height;
		image.type = PF_RGBA_32;
		image.flags = IMAGE_HAS_COLOR | IMAGE_HAS_ALPHA;
		image.size = width * height * 4;
		image.rgba = Mem_Malloc( host.imagepool, image.size );
		image.palette = NULL;

		// convert indexed pixels to RGBA
		for( j = 0; j < (int)( width * height ); ++j )
		{
			idx = pixels[j];
			image.rgba[j * 4 + 0] = use_palette[idx * 3 + 0];
			image.rgba[j * 4 + 1] = use_palette[idx * 3 + 1];
			image.rgba[j * 4 + 2] = use_palette[idx * 3 + 2];
			if( alpha_mode )
				image.rgba[j * 4 + 3] = idx; // soft transparency
			else
				image.rgba[j * 4 + 3] = ( idx == 255 ) ? 0 : 255; // classic
		}
		return true;
	}
	return false;
}

/*
============
Image_SaveWAD
============
*/
qboolean Image_SaveWAD( const char *name, rgbdata_t *pix )
{
	int         m0size, m1size, m2size, m3size;
	byte        *mip1_data = NULL, *mip2_data = NULL, *mip3_data = NULL;
	const byte  *palette;
	byte        grad_palette[256 * 3];
	file_t      *f;
	dwadinfo_t  header;
	mip_t       miptex;
	long        infotableofs;
	dlumpinfo_t lump;
	fs_offset_t pad;
	int         i;
	qboolean    result = false;
	int         lump_type = ( pix->flags & IMAGE_GRADIENT_DECAL ) ? TYP_PALETTE : TYP_MIPTEX;
	short       palette_size = 256;
	int         infotableofs32 = 0;

	if( !pix || !pix->buffer )
		return false;

	palette = pix->palette ? pix->palette : (const byte *)image.palette;

	m0size = pix->width * pix->height;
	m1size = m0size / 4;
	m2size = m0size / 16;
	m3size = m0size / 64;

	mip1_data = (byte *)Mem_Malloc( host.imagepool, m1size );
	mip2_data = (byte *)Mem_Malloc( host.imagepool, m2size );
	mip3_data = (byte *)Mem_Malloc( host.imagepool, m3size );
	if( !mip1_data || !mip2_data || !mip3_data )
		goto cleanup;

	Image_GenerateMipmaps( pix->buffer, pix->width, pix->height, mip1_data, mip2_data, mip3_data );

	memset( &miptex, 0, sizeof( mip_t ));
	Q_strncpy( miptex.name, "{LOGO", sizeof( miptex.name ));
	miptex.width = pix->width;
	miptex.height = pix->height;
	miptex.offsets[0] = sizeof( mip_t );
	miptex.offsets[1] = miptex.offsets[0] + m0size;
	miptex.offsets[2] = miptex.offsets[1] + m1size;
	miptex.offsets[3] = miptex.offsets[2] + m2size;

	f = FS_Open( name, "wb", false );
	if( !f )
		goto cleanup;

	memset( &header, 0, sizeof( header ));
	header.ident = IDWAD3HEADER;
	header.numlumps = 1;

	FS_Write( f, &header, sizeof( header ));
	FS_Write( f, &miptex, sizeof( mip_t ));
	FS_Write( f, pix->buffer, m0size );
	FS_Write( f, mip1_data, m1size );
	FS_Write( f, mip2_data, m2size );
	FS_Write( f, mip3_data, m3size );
	FS_Write( f, &palette_size, sizeof( short ));

	if( lump_type == TYP_PALETTE )
	{
		const byte *frontColorPtr = palette + 255 * 3;
		for( i = 0; i < 256; ++i )
		{
			float t = i / 255.0f;
			grad_palette[i * 3 + 0] = (byte)( frontColorPtr[0] * t );
			grad_palette[i * 3 + 1] = (byte)( frontColorPtr[1] * t );
			grad_palette[i * 3 + 2] = (byte)( frontColorPtr[2] * t );
		}
		FS_Write( f, grad_palette, 256 * 3 );
	}
	else
	{
		FS_Write( f, palette, 256 * 3 );
	}

	// padding up to a multiple of 4
	pad = (( FS_Tell( f ) + 3 ) & ~3 ) - FS_Tell( f );
	for( i = 0; i < pad; ++i )
		FS_Write( f, (const void *)&(char){0}, 1 );

	infotableofs = FS_Tell( f );
	memset( &lump, 0, sizeof( lump ));
	lump.filepos = sizeof( dwadinfo_t );
	lump.disksize = (int)( miptex.offsets[3] + m3size + sizeof( short ) + 256 * 3 );
	lump.size = lump.disksize;
	lump.type = (char)lump_type;
	lump.attribs = 0;
	Q_strncpy( lump.name, "tempdecal", sizeof( lump.name ));
	FS_Write( f, &lump, sizeof( lump ));

	FS_Seek( f, offsetof( dwadinfo_t, infotableofs ), SEEK_SET );
	infotableofs32 = (int)infotableofs;
	FS_Write( f, &infotableofs32, sizeof( int ));

	FS_Close( f );
	result = true;

cleanup:
	if( mip1_data )
		Mem_Free( mip1_data );
	if( mip2_data )
		Mem_Free( mip2_data );
	if( mip3_data )
		Mem_Free( mip3_data );
	return result;
}
