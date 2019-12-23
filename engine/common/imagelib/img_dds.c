/*
img_dds.c - dds format load
Copyright (C) 2015 Uncle Mike

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
#include "mathlib.h"

qboolean Image_CheckDXT3Alpha( dds_t *hdr, byte *fin )
{
	word	sAlpha;
	byte	*alpha; 
	int	x, y, i, j; 

	for( y = 0; y < hdr->dwHeight; y += 4 )
	{
		for( x = 0; x < hdr->dwWidth; x += 4 )
		{
			alpha = fin + 8;
			fin += 16;

			for( j = 0; j < 4; j++ )
			{
				sAlpha = alpha[2*j] + 256 * alpha[2*j+1];

				for( i = 0; i < 4; i++ )
				{
					if((( x + i ) < hdr->dwWidth ) && (( y + j ) < hdr->dwHeight ))
					{
						if( sAlpha == 0 )
							return true;
					}
					sAlpha >>= 4;
				}
			}
		}
	}

	return false;
}

qboolean Image_CheckDXT5Alpha( dds_t *hdr, byte *fin )
{
	uint	bits, bitmask;
	byte	*alphamask; 
	int	x, y, i, j; 

	for( y = 0; y < hdr->dwHeight; y += 4 )
	{
		for( x = 0; x < hdr->dwWidth; x += 4 )
		{
			if( y >= hdr->dwHeight || x >= hdr->dwWidth )
				break;

			alphamask = fin + 2;
			fin += 8;

			bitmask = ((uint *)fin)[1];
			fin += 8;

			// last three bytes
			bits = (alphamask[3]) | (alphamask[4] << 8) | (alphamask[5] << 16);

			for( j = 2; j < 4; j++ )
			{
				for( i = 0; i < 4; i++ )
				{
					// only put pixels out < width or height
					if((( x + i ) < hdr->dwWidth ) && (( y + j ) < hdr->dwHeight ))
					{
						if( bits & 0x07 )
							return true;
					}
					bits >>= 3;
				}
			}
		}
	}

	return false;
}
		
void Image_DXTGetPixelFormat( dds_t *hdr )
{
	uint bits = hdr->dsPixelFormat.dwRGBBitCount;

	if( !FBitSet( hdr->dsCaps.dwCaps2, DDS_VOLUME ))
		hdr->dwDepth = 1;

	if( FBitSet( hdr->dsPixelFormat.dwFlags, DDS_FOURCC ))
	{
		switch( hdr->dsPixelFormat.dwFourCC )
		{
		case TYPE_DXT1: 
			image.type = PF_DXT1;
			break;
		case TYPE_DXT2:
			image.flags &= ~IMAGE_HAS_ALPHA; // alpha is already premultiplied by color
			// intentionally fallthrough
		case TYPE_DXT3:
			image.type = PF_DXT3;
			break;
		case TYPE_DXT4:
			image.flags &= ~IMAGE_HAS_ALPHA; // alpha is already premultiplied by color
			// intentionally fallthrough
		case TYPE_DXT5:
			image.type = PF_DXT5;
			break;
		case TYPE_ATI2:
			image.type = PF_ATI2;
			break;
		default:
			image.type = PF_UNKNOWN; // assume error
			break;
		}
	}
	else
	{
		// this dds texture isn't compressed so write out ARGB or luminance format
		if( hdr->dsPixelFormat.dwFlags & DDS_DUDV )
		{
			image.type = PF_UNKNOWN; // assume error
		}
		else if( hdr->dsPixelFormat.dwFlags & DDS_LUMINANCE )
		{
			image.type = PF_UNKNOWN; // assume error
		}
		else 
		{
			switch( bits )
			{
			case 32:
				image.type = PF_BGRA_32;
				break;
			case 24:
				image.type = PF_BGR_24;
				break;
			case 8:
				image.type = PF_LUMINANCE;
				break;
			default:
				image.type = PF_UNKNOWN;
				break;
			}
		}
	}

	// setup additional flags
	if( hdr->dsCaps.dwCaps1 & DDS_COMPLEX && hdr->dsCaps.dwCaps2 & DDS_CUBEMAP )
		image.flags |= IMAGE_CUBEMAP;

	if( hdr->dwFlags & DDS_MIPMAPCOUNT )
		image.num_mips = hdr->dwMipMapCount; // get actual mip count
}

size_t Image_DXTGetLinearSize( int type, int width, int height, int depth )
{
	switch( type )
	{
	case PF_DXT1: return ((( width + 3 ) / 4 ) * (( height + 3 ) / 4 ) * depth * 8 );
	case PF_DXT3:
	case PF_DXT5:
	case PF_ATI2: return ((( width + 3 ) / 4 ) * (( height + 3 ) / 4 ) * depth * 16 );
	case PF_LUMINANCE: return (width * height * depth);
	case PF_BGR_24:
	case PF_RGB_24: return (width * height * depth * 3);
	case PF_BGRA_32:
	case PF_RGBA_32: return (width * height * depth * 4);
	}	

	return 0;
}

size_t Image_DXTCalcMipmapSize( dds_t *hdr ) 
{
	size_t	buffsize = 0;
	int	i, width, height;
		
	// now correct buffer size
	for( i = 0; i < Q_max( 1, ( hdr->dwMipMapCount )); i++ )
	{
		width = Q_max( 1, ( hdr->dwWidth >> i ));
		height = Q_max( 1, ( hdr->dwHeight >> i ));
		buffsize += Image_DXTGetLinearSize( image.type, width, height, image.depth );
	}

	return buffsize;
}

uint Image_DXTCalcSize( const char *name, dds_t *hdr, size_t filesize ) 
{
	size_t buffsize = 0;
	int w = image.width;
	int h = image.height;
	int d = image.depth;

	if( hdr->dsCaps.dwCaps2 & DDS_CUBEMAP ) 
	{
		// cubemap w*h always match for all sides
		buffsize = Image_DXTCalcMipmapSize( hdr ) * 6;
	}
	else if( hdr->dwFlags & DDS_MIPMAPCOUNT )
	{
		// if mipcount > 1
		buffsize = Image_DXTCalcMipmapSize( hdr );
	}
	else if( hdr->dwFlags & ( DDS_LINEARSIZE|DDS_PITCH ))
	{
		// just in case (no need, really)
		buffsize = hdr->dwLinearSize;
	}
	else 
	{
		// pretty solution for microsoft bug
		buffsize = Image_DXTCalcMipmapSize( hdr );
	}

	if( filesize != buffsize ) // main check
	{
		Con_DPrintf( S_WARN "Image_LoadDDS: (%s) probably corrupted (%i should be %lu)\n", name, buffsize, filesize );
		if( buffsize > filesize )
			return false;
	}

	return buffsize;
}

void Image_DXTAdjustVolume( dds_t *hdr )
{
	if( hdr->dwDepth <= 1 )
		return;

	hdr->dwLinearSize = Image_DXTGetLinearSize( image.type, hdr->dwWidth, hdr->dwHeight, hdr->dwDepth );
	hdr->dwFlags |= DDS_LINEARSIZE;
}

/*
=============
Image_LoadDDS
=============
*/
qboolean Image_LoadDDS( const char *name, const byte *buffer, fs_offset_t filesize )
{
	dds_t	header;
	byte	*fin;

	if( filesize < sizeof( dds_t ))
		return false;

	memcpy( &header, buffer, sizeof( dds_t ));

	if( header.dwIdent != DDSHEADER )
		return false; // it's not a dds file, just skip it

	if( header.dwSize != sizeof( dds_t ) - sizeof( uint )) // size of the structure (minus MagicNum)
	{
		Con_DPrintf( S_ERROR "Image_LoadDDS: (%s) have corrupted header\n", name );
		return false;
	}

	if( header.dsPixelFormat.dwSize != sizeof( dds_pixf_t )) // size of the structure
	{
		Con_DPrintf( S_ERROR "Image_LoadDDS: (%s) have corrupt pixelformat header\n", name );
		return false;
	}

	image.width = header.dwWidth;
	image.height = header.dwHeight;

	if( header.dwFlags & DDS_DEPTH )
		image.depth = header.dwDepth;
	else image.depth = 1;

	if( !Image_ValidSize( name )) return false;

	Image_DXTGetPixelFormat( &header ); // and image type too :)
	Image_DXTAdjustVolume( &header );

	if( !Image_CheckFlag( IL_DDS_HARDWARE ) && ImageDXT( image.type ))
		return false; // silently rejected

	if( image.type == PF_UNKNOWN ) 
	{
		Con_DPrintf( S_ERROR "Image_LoadDDS: (%s) has unrecognized type\n", name );
		return false;
	}

	image.size = Image_DXTCalcSize( name, &header, filesize - 128 ); 
	if( image.size == 0 ) return false; // just in case
	fin = (byte *)(buffer + sizeof( dds_t ));

	// copy an encode method
	image.encode = (word)header.dwReserved1[0];

	switch( image.encode )
	{
	case DXT_ENCODE_COLOR_YCoCg:
		SetBits( image.flags, IMAGE_HAS_COLOR );
		break;
	case DXT_ENCODE_NORMAL_AG_ORTHO:
	case DXT_ENCODE_NORMAL_AG_STEREO:
	case DXT_ENCODE_NORMAL_AG_PARABOLOID:
	case DXT_ENCODE_NORMAL_AG_QUARTIC:
	case DXT_ENCODE_NORMAL_AG_AZIMUTHAL:
		SetBits( image.flags, IMAGE_HAS_COLOR );
		break;
	default:	// check for real alpha-pixels
		if( image.type == PF_DXT3 && Image_CheckDXT3Alpha( &header, fin ))
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		else if( image.type == PF_DXT5 && Image_CheckDXT5Alpha( &header, fin ))
			SetBits( image.flags, IMAGE_HAS_ALPHA );
		if( !FBitSet( header.dsPixelFormat.dwFlags, DDS_LUMINANCE ))
			SetBits( image.flags, IMAGE_HAS_COLOR );
		break;
	}

	if( image.type == PF_LUMINANCE )
		ClearBits( image.flags, IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA );

	if( header.dwReserved1[1] != 0 )
	{
		// store texture reflectivity
		image.fogParams[0] = ((header.dwReserved1[1] & 0x000000FF) >> 0 );
		image.fogParams[1] = ((header.dwReserved1[1] & 0x0000FF00) >> 8 );
		image.fogParams[2] = ((header.dwReserved1[1] & 0x00FF0000) >> 16);
		image.fogParams[3] = ((header.dwReserved1[1] & 0xFF000000) >> 24);
	}

	// dds files will be uncompressed on a render. requires minimal of info for set this
	image.rgba = Mem_Malloc( host.imagepool, image.size ); 
	memcpy( image.rgba, fin, image.size );
	SetBits( image.flags, IMAGE_DDS_FORMAT );

	return true;
}
