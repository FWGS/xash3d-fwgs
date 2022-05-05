/*
gl_image.c - texture uploading and processing
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

#include "r_local.h"

#define IsLightMap( tex )	( FBitSet(( tex )->flags, TF_ATLAS_PAGE ))

static image_t r_images[MAX_TEXTURES];

image_t *R_GetTexture( unsigned int texnum )
{
	ASSERT( texnum >= 0 && texnum < MAX_TEXTURES );
	return &r_images[texnum];
}

void GAME_EXPORT GL_Bind( int tmu, unsigned int texnum )
{
	image_t	*image;

	extern void	(*d_pdrawspans)(void *);
	extern void R_PolysetFillSpans8 ( void * );
	extern void R_PolysetDrawSpansConstant8_33( void *pspanpackage);
	extern void R_PolysetDrawSpansTextureBlended( void *pspanpackage);
	extern void R_PolysetDrawSpansBlended( void *pspanpackage);
	extern void R_PolysetDrawSpansAdditive( void *pspanpackage);
	extern void R_PolysetDrawSpansGlow( void *pspanpackage);

	image = &r_images[texnum];
	//vid.rendermode = kRenderNormal;

	if( vid.rendermode == kRenderNormal )
	{
		r_affinetridesc.pskin = image->pixels[0];
		d_pdrawspans = R_PolysetFillSpans8 ;
	}
	else if( vid.rendermode == kRenderTransAdd)
	{
		r_affinetridesc.pskin = image->pixels[0];
		d_pdrawspans = R_PolysetDrawSpansAdditive;
	}
	else if( vid.rendermode == kRenderGlow )
	{
		r_affinetridesc.pskin = image->pixels[0];
		d_pdrawspans = R_PolysetDrawSpansGlow;
	}
	else if( image->alpha_pixels )
	{
		r_affinetridesc.pskin = image->alpha_pixels;
		d_pdrawspans = R_PolysetDrawSpansTextureBlended;
	}
	else
	{
		r_affinetridesc.pskin = image->pixels[0];
		d_pdrawspans = R_PolysetDrawSpansBlended;
	}

	r_affinetridesc.skinwidth = image->width;
	r_affinetridesc.skinheight = image->height;
}

void GL_ApplyTextureParams( int texnum )
{
	// Do nothing
}

static void GL_UpdateTextureParams( int iTexture )
{
	image_t	*tex = &r_images[iTexture];

	Assert( tex != NULL );

	if( !tex->pixels) return; // free slot

	GL_Bind( XASH_TEXTURE0, iTexture );
}

void R_SetTextureParameters( void )
{
	int	i;

	for( i = 1; i < (sizeof(r_images) / sizeof(image_t)); i++)
	{
		if( r_images[i].used )
		{
			GL_UpdateTextureParams( i );
		}
	}
}

static size_t GL_CalcImageSize( pixformat_t format, int width, int height, int depth )
{
	size_t	size = 0;

	// check the depth error
	depth = Q_max( 1, depth );

	switch( format )
	{
	case PF_RGB_24:
	case PF_BGR_24:
		size = width * height * depth * 3;
		break;
	case PF_BGRA_32:
	case PF_RGBA_32:
		size = width * height * depth * 4;
		break;
	case PF_DXT1:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 8) * depth;
		break;
	case PF_DXT3:
	case PF_DXT5:
	case PF_ATI2:
		size = (((width + 3) >> 2) * ((height + 3) >> 2) * 16) * depth;
		break;
	}

	return size;
}

static size_t GL_CalcTextureSize( int width, int height, int depth )
{
	return width * height * 2;
}

static int GL_CalcMipmapCount( image_t *tex, qboolean haveBuffer )
{
	int	width, height;
	int	mipcount;

	Assert( tex != NULL );

	if( !haveBuffer )
		return 1;

	// generate mip-levels by user request
	if( FBitSet( tex->flags, TF_NOMIPMAP ))
		return 1;

	// mip-maps can't exceeds 4
	for( mipcount = 0; mipcount < 4; mipcount++ )
	{
		width = Q_max( 1, ( tex->width >> mipcount ));
		height = Q_max( 1, ( tex->height >> mipcount ));
		if( width == 1 && height == 1 )
			break;
	}

	return mipcount + 1;
}

static void GL_SetTextureDimensions( image_t *tex, int width, int height, int depth )
{
	int	maxTextureSize = 1024;
	int	maxDepthSize = 1;

	Assert( tex != NULL );

	// store original sizes
	tex->srcWidth = width;
	tex->srcHeight = height;

	if( width > maxTextureSize || height > maxTextureSize || depth > maxDepthSize )
	{
		while( width > maxTextureSize || height > maxTextureSize )
		{
			width >>= 1;
			height >>= 1;
		}
	}

	// set the texture dimensions
	tex->width = Q_max( 1, width );
	tex->height = Q_max( 1, height );
	tex->depth = Q_max( 1, depth );
}

static void GL_SetTextureTarget( image_t *tex, rgbdata_t *pic )
{
	Assert( pic != NULL );
	Assert( tex != NULL );

	// correct depth size
	pic->depth = Q_max( 1, pic->depth );
	tex->numMips = 0; // begin counting

	// correct mip count
	pic->numMips = Q_max( 1, pic->numMips );
}

static void GL_SetTextureFormat( image_t *tex, pixformat_t format, int channelMask )
{
	qboolean	haveColor = ( channelMask & IMAGE_HAS_COLOR );
	qboolean	haveAlpha = ( channelMask & IMAGE_HAS_ALPHA );

	Assert( tex != NULL );
	//tex->transparent = !!( channelMask & IMAGE_HAS_ALPHA );
}

byte *GL_ResampleTexture( const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap )
{
	uint		frac, fracStep;
	uint		*in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte		*pix1, *pix2, *pix3, *pix4;
	uint		*out, *inRow1, *inRow2;
	static byte	*scaledImage = NULL;	// pointer to a scaled image
	vec3_t		normal;
	int		i, x, y;

	if( !source ) return NULL;

	scaledImage = Mem_Realloc( r_temppool, scaledImage, outWidth * outHeight * 4 );
	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)scaledImage;

	frac = fracStep >> 2;
	for( i = 0; i < outWidth; i++ )
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for( i = 0; i < outWidth; i++ )
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if( isNormalMap )
	{
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

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
		for( y = 0; y < outHeight; y++, out += outWidth )
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for( x = 0; x < outWidth; x++ )
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out+x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out+x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out+x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out+x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}

void GL_BoxFilter3x3( byte *out, const byte *in, int w, int h, int x, int y )
{
	int		r = 0, g = 0, b = 0, a = 0;
	int		count = 0, acount = 0;
	int		i, j, u, v;
	const byte	*pixel;

	for( i = 0; i < 3; i++ )
	{
		u = ( i - 1 ) + x;

		for( j = 0; j < 3; j++ )
		{
			v = ( j - 1 ) + y;

			if( u >= 0 && u < w && v >= 0 && v < h )
			{
				pixel = &in[( u + v * w ) * 4];

				if( pixel[3] != 0 )
				{
					r += pixel[0];
					g += pixel[1];
					b += pixel[2];
					a += pixel[3];
					acount++;
				}
			}
		}
	}

	if(  acount == 0 )
		acount = 1;

	out[0] = r / acount;
	out[1] = g / acount;
	out[2] = b / acount;
//	out[3] = (int)( SimpleSpline( ( a / 12.0f ) / 255.0f ) * 255 );
}

/*
=================
GL_ApplyFilter

Apply box-filter to 1-bit alpha
=================
*/
byte *GL_ApplyFilter( const byte *source, int width, int height )
{
	byte	*in = (byte *)source;
	byte	*out = (byte *)source;
	int	i;

	if( ENGINE_GET_PARM( PARM_QUAKE_COMPATIBLE ) )
		return in;

	for( i = 0; source && i < width * height; i++, in += 4 )
	{
		if( in[0] == 0 && in[1] == 0 && in[2] == 0 && in[3] == 0 )
			GL_BoxFilter3x3( in, source, width, height, i % width, i / width );
	}

	return out;
}

/*
=================
GL_BuildMipMap

Operates in place, quartering the size of the texture
=================
*/
static void GL_BuildMipMap( byte *in, int srcWidth, int srcHeight, int srcDepth, int flags )
{
	byte	*out = in;
	int	instride = ALIGN( srcWidth * 4, 1 );
	int	mipWidth, mipHeight, outpadding;
	int	row, x, y, z;
	vec3_t	normal;

	if( !in ) return;

	mipWidth = Q_max( 1, ( srcWidth >> 1 ));
	mipHeight = Q_max( 1, ( srcHeight >> 1 ));
	outpadding = ALIGN( mipWidth * 4, 1 ) - mipWidth * 4;
	row = srcWidth << 2;

	if( FBitSet( flags, TF_ALPHACONTRAST ))
	{
		memset( in, mipWidth, mipWidth * mipHeight * 4 );
		return;
	}

	// move through all layers
	for( z = 0; z < srcDepth; z++ )
	{
		if( FBitSet( flags, TF_NORMALMAP ))
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( in[row+4] )
						+ MAKE_SIGNED( next[row+0] ) + MAKE_SIGNED( next[row+4] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( in[row+5] )
						+ MAKE_SIGNED( next[row+1] ) + MAKE_SIGNED( next[row+5] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( in[row+6] )
						+ MAKE_SIGNED( next[row+2] ) + MAKE_SIGNED( next[row+6] );
					}
					else
					{
						normal[0] = MAKE_SIGNED( in[row+0] ) + MAKE_SIGNED( next[row+0] );
						normal[1] = MAKE_SIGNED( in[row+1] ) + MAKE_SIGNED( next[row+1] );
						normal[2] = MAKE_SIGNED( in[row+2] ) + MAKE_SIGNED( next[row+2] );
					}

					if( !VectorNormalizeLength( normal ))
						VectorSet( normal, 0.5f, 0.5f, 1.0f );

					out[0] = 128 + (byte)(127.0f * normal[0]);
					out[1] = 128 + (byte)(127.0f * normal[1]);
					out[2] = 128 + (byte)(127.0f * normal[2]);
					out[3] = 255;
				}
			}
		}
		else
		{
			for( y = 0; y < mipHeight; y++, in += instride * 2, out += outpadding )
			{
				byte *next = ((( y << 1 ) + 1 ) < srcHeight ) ? ( in + instride ) : in;
				for( x = 0, row = 0; x < mipWidth; x++, row += 8, out += 4 )
				{
					if((( x << 1 ) + 1 ) < srcWidth )
					{
						out[0] = (in[row+0] + in[row+4] + next[row+0] + next[row+4]) >> 2;
						out[1] = (in[row+1] + in[row+5] + next[row+1] + next[row+5]) >> 2;
						out[2] = (in[row+2] + in[row+6] + next[row+2] + next[row+6]) >> 2;
						out[3] = (in[row+3] + in[row+7] + next[row+3] + next[row+7]) >> 2;
					}
					else
					{
						out[0] = (in[row+0] + next[row+0]) >> 1;
						out[1] = (in[row+1] + next[row+1]) >> 1;
						out[2] = (in[row+2] + next[row+2]) >> 1;
						out[3] = (in[row+3] + next[row+3]) >> 1;
					}
				}
			}
		}
	}
}

/*
===============
GL_UploadTexture

upload texture into video memory
===============
*/
static qboolean GL_UploadTexture( int texnum, rgbdata_t *pic )
{
	byte		*buf, *data;
	size_t		texsize, size;
	uint		width, height;
	uint		i, j, numSides;
	uint		offset = 0;
	qboolean		normalMap = false;
	const byte	*bufend;
	int mipCount;
	image_t	*tex = &r_images[texnum];

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];
	GL_SetTextureDimensions( tex, pic->width, pic->height, pic->depth );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	//gEngfuncs.Con_Printf("%s %d %d\n", tex->name, tex->width, tex->height );

	Assert( pic != NULL );
	Assert( tex != NULL );

	if( !pic->buffer )
		return true;

	buf = pic->buffer;

	mipCount = 4;//GL_CalcMipmapCount( tex, ( buf != NULL ));

	// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
	if((( pic->width != tex->width ) || ( pic->height != tex->height )))
		data = GL_ResampleTexture( buf, pic->width, pic->height, tex->width, tex->height, normalMap );
	else data = buf;

	//if( !ImageDXT( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
	//	data = GL_ApplyFilter( data, tex->width, tex->height );

	// mips will be auto-generated if desired
	for( j = 0; j < mipCount; j++ )
	{
	int x, y;
		width = Q_max( 1, ( tex->width >> j ));
		height = Q_max( 1, ( tex->height >> j ));
		texsize = GL_CalcTextureSize( width, height, tex->depth );
		size = GL_CalcImageSize( pic->type, width, height, tex->depth );
		//GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, data );
		// increase size to workaround triangle renderer bugs
		// it seems to assume memory readable. maybe it was pointed to WAD?
		//tex->pixels[j] = (byte*)Mem_Calloc( r_temppool, width * height * sizeof(pixel_t) + 1024 ) + 512;
		tex->pixels[j] = (pixel_t*)Mem_Calloc( r_temppool, width * height * sizeof(pixel_t) );


		//memset( (byte*)tex->pixels[j] - 512, 0xFF, 512 );
		//memset( (byte*)tex->pixels[j] + width * height * sizeof(pixel_t), 0xFF, 512 );

		if( j == 0 &&  tex->flags & TF_HAS_ALPHA )
			tex->alpha_pixels = (pixel_t*)Mem_Calloc( r_temppool, width * height * sizeof(pixel_t) );

		for(i = 0; i < height * width; i++ )
		{
				unsigned int r, g, b, major, minor;
		#if 0
				r = data[i * 4 + 0] * MASK(5-1) / 255;
				g = data[i * 4 + 1] * MASK(6-1) / 255;
				b = data[i * 4 + 2] * MASK(5-1) / 255;
		#else
				// seems to look better
				r = data[i * 4 + 0] * BIT(5) / 256;
				g = data[i * 4 + 1] * BIT(6) / 256;
				b = data[i * 4 + 2] * BIT(5) / 256;
		#endif
				// 565 to 332
				major = (((r >> 2) & MASK(3)) << 5) |( (( (g >> 3) & MASK(3)) << 2 )  )| (((b >> 3) & MASK(2)));

				// save minor GBRGBRGB
				minor = MOVE_BIT(r,1,5) | MOVE_BIT(r,0,2) | MOVE_BIT(g,2,7) | MOVE_BIT(g,1,4) | MOVE_BIT(g,0,1) | MOVE_BIT(b,2,6)| MOVE_BIT(b,1,3)|MOVE_BIT(b,0,0);

				tex->pixels[j][i] = major << 8 | (minor & 0xFF);
				if( j == 0 && tex->alpha_pixels )
				{
					unsigned int alpha = (data[i * 4 + 3] * 8 / 256) << (16 - 3);
					tex->alpha_pixels[i] = (tex->pixels[j][i] >> 3) | alpha;
					if( !sw_noalphabrushes->value && data[i * 4 + 3] < 128 && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ) )
						tex->pixels[j][i] = TRANSPARENT_COLOR; //0000 0011 0100 1001;
				}

		}

		if( mipCount > 1 )
			GL_BuildMipMap( data, width, height, tex->depth, tex->flags );

		tex->size += texsize;
		tex->numMips++;

		//GL_CheckTexImageError( tex );
	}

#if 0


	GL_SetTextureTarget( tex, pic ); // must be first

	// make sure what target is correct
	if( tex->target == GL_NONE )
	{
		gEngfuncs.Con_DPrintf( S_ERROR "GL_UploadTexture: %s is not supported by your hardware\n", tex->name );
		return false;
	}

	GL_SetTextureDimensions( tex, pic->width, pic->height, pic->depth );
	GL_SetTextureFormat( tex, pic->type, pic->flags );

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	if(( pic->width * pic->height ) & 3 )
	{
		// will be resampled, just tell me for debug targets
		gEngfuncs.Con_Reportf( "GL_UploadTexture: %s s&3 [%d x %d]\n", tex->name, pic->width, pic->height );
	}

	buf = pic->buffer;
	bufend = pic->buffer + pic->size; // total image size include all the layers, cube sides, mipmaps
	offset = GL_CalcImageSize( pic->type, pic->width, pic->height, pic->depth );
	texsize = GL_CalcTextureSize( tex->format, tex->width, tex->height, tex->depth );
	normalMap = FBitSet( tex->flags, TF_NORMALMAP ) ? true : false;
	numSides = FBitSet( pic->flags, IMAGE_CUBEMAP ) ? 6 : 1;

	// uploading texture into video memory, change the binding
	glState.currentTextures[glState.activeTMU] = tex->texnum;
	pglBindTexture( tex->target, tex->texnum );

	for( i = 0; i < numSides; i++ )
	{
		// track the buffer bounds
		if( buf != NULL && buf >= bufend )
			gEngfuncs.Host_Error( "GL_UploadTexture: %s image buffer overflow\n", tex->name );

		if( ImageDXT( pic->type ))
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageDXT( tex, i, j, width, height, tex->depth, size, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}
		}
		else if( Q_max( 1, pic->numMips ) > 1 )	// not-compressed DDS
		{
			for( j = 0; j < Q_max( 1, pic->numMips ); j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, buf );
				tex->size += texsize;
				buf += size; // move pointer
				tex->numMips++;

				GL_CheckTexImageError( tex );

			}
		}
		else // RGBA32
		{
			int mipCount = GL_CalcMipmapCount( tex, ( buf != NULL ));

			// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
			if(( tex->depth == 1 ) && ( pic->width != tex->width ) || ( pic->height != tex->height ))
				data = GL_ResampleTexture( buf, pic->width, pic->height, tex->width, tex->height, normalMap );
			else data = buf;

			if( !ImageDXT( pic->type ) && !FBitSet( tex->flags, TF_NOMIPMAP ) && FBitSet( pic->flags, IMAGE_ONEBIT_ALPHA ))
				data = GL_ApplyFilter( data, tex->width, tex->height );

			// mips will be auto-generated if desired
			for( j = 0; j < mipCount; j++ )
			{
				width = Q_max( 1, ( tex->width >> j ));
				height = Q_max( 1, ( tex->height >> j ));
				texsize = GL_CalcTextureSize( tex->format, width, height, tex->depth );
				size = GL_CalcImageSize( pic->type, width, height, tex->depth );
				GL_TextureImageRAW( tex, i, j, width, height, tex->depth, pic->type, data );
				if( mipCount > 1 )
					GL_BuildMipMap( data, width, height, tex->depth, tex->flags );
				tex->size += texsize;
				tex->numMips++;

				GL_CheckTexImageError( tex );
			}

			// move to next side
			if( numSides > 1 && ( buf != NULL ))
				buf += GL_CalcImageSize( pic->type, pic->width, pic->height, 1 );
		}
	}

	SetBits( tex->flags, TF_IMG_UPLOADED ); // done
	tex->numMips /= numSides;

	return true;
#endif
	return true;
}

/*
===============
GL_ProcessImage

do specified actions on pixels
===============
*/
static void GL_ProcessImage( int texnum, rgbdata_t *pic )
{
	uint	img_flags = 0;

	image_t	*tex = &r_images[texnum];

	// force upload texture as RGB or RGBA (detail textures requires this)
	if( tex->flags & TF_FORCE_COLOR ) pic->flags |= IMAGE_HAS_COLOR;
	if( pic->flags & IMAGE_HAS_ALPHA ) tex->flags |= TF_HAS_ALPHA;

	if( ImageDXT( pic->type ))
	{
		if( !pic->numMips )
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if( pic->flags & IMAGE_HAS_LUMA )
			tex->flags |= TF_HAS_LUMA;

		if( pic->flags & IMAGE_QUAKEPAL )
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if( tex->flags & TF_MAKELUMA )
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if( !FBitSet( tex->flags, TF_IMG_UPLOADED ) && FBitSet( tex->flags, TF_KEEP_SOURCE ))
			tex->original = gEngfuncs.FS_CopyImage( pic ); // because current pic will be expanded to rgba

		// we need to expand image into RGBA buffer
		if( pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32 )
			img_flags |= IMAGE_FORCE_RGBA;

		if( FBitSet( tex->flags, TF_LUMINANCE ))
			ClearBits( pic->flags, IMAGE_HAS_COLOR );
	}
}

/*
================
GL_UpdateTexSize

recalc image room
================
*/
void GAME_EXPORT GL_UpdateTexSize( int texnum, int width, int height, int depth )
{
	int		i, j, texsize;
	int		numSides;
	image_t	*tex;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return;

	tex = &r_images[texnum];
	numSides = FBitSet( tex->flags, TF_CUBEMAP ) ? 6 : 1;
	GL_SetTextureDimensions( tex, width, height, depth );
	tex->size = 0; // recompute now

	for( i = 0; i < numSides; i++ )
	{
		for( j = 0; j < Q_max( 1, tex->numMips ); j++ )
		{
			width = Q_max( 1, ( tex->width >> j ));
			height = Q_max( 1, ( tex->height >> j ));
			texsize = GL_CalcTextureSize( width, height, tex->depth );
			tex->size += texsize;
		}
	}
}

qboolean GAME_EXPORT GL_LoadTextureFromBuffer( int texnum, rgbdata_t *pic, texFlags_t flags, qboolean update )
{
	// See if already loaded
	if( r_images[texnum].used && !update )
		return true;

	// Invalid picture pointer
	if( !pic )
		return false;

	if( update )
	{
		if( r_images[texnum].used == false )
		{
			gEngfuncs.Host_Error( "GL_LoadTextureFromBuffer: couldn't find texture with num %d for update\n", texnum );
		}

		SetBits( r_images[texnum].flags, flags );
	}
	else
	{
		// Initialize the new one
		memset( &(r_images[texnum]), 0, sizeof(image_t) );

		r_images[texnum].used   = true;
		//r_images[texnum].texnum = texnum;
		r_images[texnum].flags  = flags;
	}

	GL_ProcessImage( texnum, pic );
	GL_UploadTexture( texnum, pic );  // FIXME: How to handle error?
	GL_ApplyTextureParams( texnum );  // Update texture filter, wrap etc

	return true;
}

void GAME_EXPORT GL_DeleteTexture( unsigned int texnum )
{
	int i;
	
	if( r_images[texnum].used )
	{
		memset( &(r_images[texnum]), 0, sizeof(image_t) );

		// Release source
		if( r_images[texnum].original )
		{
			gEngfuncs.FS_FreeImage( r_images[texnum].original );
		}

		for( i = 0; i < 4; i++ )
		{
			if( r_images[texnum].pixels[i] )
			{
				Mem_Free( r_images[texnum].pixels[i] );
			}
		}
		
		if( r_images[texnum].alpha_pixels )
		{
			Mem_Free( r_images[texnum].alpha_pixels );
		}
	}
}

void GAME_EXPORT GL_ProcessTexture( int texnum, float gamma, int topColor, int bottomColor )
{
	image_t	*image;
	rgbdata_t		*pic;
	int		flags = 0;

	if( texnum <= 0 || texnum >= MAX_TEXTURES )
		return; // missed image
	image = &r_images[texnum];

	// select mode
	if( gamma != -1.0f )
	{
		flags = IMAGE_LIGHTGAMMA;
	}
	else if( topColor != -1 && bottomColor != -1 )
	{
		flags = IMAGE_REMAP;
	}
	else
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: bad operation for %s\n", image->name );
		return;
	}

	if( !image->original )
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: no input data for %s\n", image->name );
		return;
	}

	if( ImageDXT( image->original->type ))
	{
		gEngfuncs.Con_Printf( S_ERROR "GL_ProcessTexture: can't process compressed texture %s\n", image->name );
		return;
	}

	// all the operations makes over the image copy not an original
	pic = gEngfuncs.FS_CopyImage( image->original );
	gEngfuncs.Image_Process( &pic, topColor, bottomColor, flags, 0.0f );

	GL_UploadTexture( texnum, pic );
	GL_ApplyTextureParams( texnum ); // update texture filter, wrap etc

	gEngfuncs.FS_FreeImage( pic );
}

void R_InitImages( void )
{
	memset( r_images, 0, sizeof( r_images ));

	R_SetTextureParameters();
}

void R_ShutdownImages( void )
{
	int	i;

	for( i = 1; i < (sizeof(r_images) / sizeof(image_t)); i++)
	{
		if( r_images[i].used )
		{
			GL_DeleteTexture( i );
		}
	}

	memset( tr.lightmapTextures, 0, sizeof( tr.lightmapTextures ));
	memset( r_images, 0, sizeof( r_images ));
}
