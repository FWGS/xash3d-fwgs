/*
ref_image.c - shared image code
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "ref_common.h"
#include "xash3d_mathlib.h"

/*
================
GL_ResampleTexture

Assume input buffer is RGBA
=================
*/
byte *GL_ResampleTexture( const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap )
{
	static byte	*scaledImage = NULL;	// pointer to a scaled image
	uint		p1[0x1000], p2[0x1000];

	if( !source ) return NULL;

	scaledImage = Mem_Realloc( r_temppool, scaledImage, outWidth * outHeight * 4 );
	const uint fracStep = inWidth * 0x10000 / outWidth;
	uint *in = (uint *)source;
	uint *out = (uint *)scaledImage;

	uint frac = fracStep >> 2;
	for( int i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( int i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( int y = 0; y < outHeight; y++, out += outWidth )
		{
			uint *inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			uint *inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( int x = 0; x < outWidth; x++ )
			{
				byte *pix1 = (byte *)inRow1 + p1[x];
				byte *pix2 = (byte *)inRow1 + p2[x];
				byte *pix3 = (byte *)inRow2 + p1[x];
				byte *pix4 = (byte *)inRow2 + p2[x];
				vec3_t normal;

				normal[0] = MAKE_SIGNED( pix1[0] ) + MAKE_SIGNED( pix2[0] ) + MAKE_SIGNED( pix3[0] ) + MAKE_SIGNED( pix4[0] );
				normal[1] = MAKE_SIGNED( pix1[1] ) + MAKE_SIGNED( pix2[1] ) + MAKE_SIGNED( pix3[1] ) + MAKE_SIGNED( pix4[1] );
				normal[2] = MAKE_SIGNED( pix1[2] ) + MAKE_SIGNED( pix2[2] ) + MAKE_SIGNED( pix3[2] ) + MAKE_SIGNED( pix4[2] );

				if( !VectorNormalizeLength( normal ))
					VectorSet( normal, 0.5f, 0.5f, 1.0f );

				((byte *)(out+x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out+x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out+x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out+x))[3] = 255;
			}
		}
	}
	else
	{
		for( int y = 0; y < outHeight; y++, out += outWidth )
		{
			uint *inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			uint *inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( int x = 0; x < outWidth; x++ )
			{
				byte *pix1 = (byte *)inRow1 + p1[x];
				byte *pix2 = (byte *)inRow1 + p2[x];
				byte *pix3 = (byte *)inRow2 + p1[x];
				byte *pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}
