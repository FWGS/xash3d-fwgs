/*
r_context.c -- dx2 renderer image
Copyright (C) 2025 lewa_j

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
#include "com_strings.h"
#include "xash3d_mathlib.h"
#include "crtlib.h"
#include "crclib.h"

#define Assert(x) if(!( x )) gEngfuncs.Host_Error( "assert failed at %s:%i\n", __FILE__, __LINE__ )


static dx_texture_t		dx_textures[MAX_TEXTURES];
static dx_texture_t *dx_texturesHashTable[TEXTURES_HASH_SIZE];
static uint		dx_numTextures;

static rgbdata_t *GL_FakeImage(int width, int height, int depth, int flags)
{
	static byte	data2D[1024]; // 16x16x4
	static rgbdata_t	r_image;

	// also use this for bad textures, but without alpha
	r_image.width = Q_max(1, width);
	r_image.height = Q_max(1, height);
	r_image.depth = Q_max(1, depth);
	r_image.flags = flags;
	r_image.type = PF_BGRA_32;
	r_image.size = r_image.width * r_image.height * r_image.depth * 4;
	r_image.buffer = (r_image.size > sizeof(data2D)) ? NULL : data2D;
	r_image.palette = NULL;
	r_image.numMips = 1;
	r_image.encode = 0;

	if (FBitSet(r_image.flags, IMAGE_CUBEMAP))
		r_image.size *= 6;
	memset(data2D, 0xFF, sizeof(data2D));

	return &r_image;
}

static byte    dottexture[8][8] =
{
	  {0,1,1,0,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {1,1,1,1,0,0,0,0},
	  {0,1,1,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
	  {0,0,0,0,0,0,0,0},
};

static void GL_CreateInternalTextures(void)
{
	int	dx2, dy, d;
	int	x, y;
	rgbdata_t *pic;

	// emo-texture from quake1
	pic = GL_FakeImage(16, 16, 1, IMAGE_HAS_COLOR);

	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			if ((y < 8) ^ (x < 8))
				((uint *)pic->buffer)[y * 16 + x] = 0xFFFF00FF;
			else ((uint *)pic->buffer)[y * 16 + x] = 0xFF000000;
		}
	}

	tr.defaultTexture = GL_LoadTextureFromBuffer(REF_DEFAULT_TEXTURE, pic, TF_COLORMAP, false);

	// particle texture from quake1
	pic = GL_FakeImage(8, 8, 1, IMAGE_HAS_COLOR | IMAGE_HAS_ALPHA);

	for (x = 0; x < 8; x++)
	{
		for (y = 0; y < 8; y++)
		{
			if (dottexture[x][y])
				pic->buffer[(y * 8 + x) * 4 + 3] = 255;
			else pic->buffer[(y * 8 + x) * 4 + 3] = 0;
		}
	}

	tr.particleTexture = GL_LoadTextureFromBuffer(REF_PARTICLE_TEXTURE, pic, TF_CLAMP, false);

	// white texture
	pic = GL_FakeImage(4, 4, 1, IMAGE_HAS_COLOR);
	for (x = 0; x < 16; x++)
		((uint *)pic->buffer)[x] = 0xFFFFFFFF;
	tr.whiteTexture = GL_LoadTextureFromBuffer(REF_WHITE_TEXTURE, pic, TF_COLORMAP, false);

	// gray texture
	pic = GL_FakeImage(4, 4, 1, IMAGE_HAS_COLOR);
	for (x = 0; x < 16; x++)
		((uint *)pic->buffer)[x] = 0xFF7F7F7F;
	tr.grayTexture = GL_LoadTextureFromBuffer(REF_GRAY_TEXTURE, pic, TF_COLORMAP, false);

	// black texture
	pic = GL_FakeImage(4, 4, 1, IMAGE_HAS_COLOR);
	for (x = 0; x < 16; x++)
		((uint *)pic->buffer)[x] = 0xFF000000;
	tr.blackTexture = GL_LoadTextureFromBuffer(REF_BLACK_TEXTURE, pic, TF_COLORMAP, false);

	// cinematic dummy
	pic = GL_FakeImage(640, 100, 1, IMAGE_HAS_COLOR);
	tr.cinTexture = GL_LoadTextureFromBuffer("*cintexture", pic, TF_NOMIPMAP | TF_CLAMP, false);
}

void R_InitImages(void)
{
	memset(dx_textures, 0, sizeof(dx_textures));
	memset(dx_texturesHashTable, 0, sizeof(dx_texturesHashTable));
	dx_numTextures = 0;

	// create unused 0-entry
	Q_strncpy(dx_textures->name, "*unused*", sizeof(dx_textures->name));
	dx_textures->hashValue = COM_HashKey(dx_textures->name, TEXTURES_HASH_SIZE);
	dx_textures->nextHash = dx_texturesHashTable[dx_textures->hashValue];
	dx_texturesHashTable[dx_textures->hashValue] = dx_textures;
	dx_numTextures = 1;

	GL_CreateInternalTextures();
}

dx_texture_t *R_GetTexture(unsigned int texnum)
{
	Assert(texnum < MAX_TEXTURES);
	return &dx_textures[texnum];
}

void R_ShowTextures( void )
{

}

const byte *R_GetTextureOriginalBuffer(unsigned int idx)
{
	return NULL;
}

static qboolean GL_CheckTexName(const char *name)
{
	int len;

	if( COM_StringEmptyOrNULL( name ))
		return false;

	len = Q_strlen(name);

	// because multi-layered textures can exceed name string
	if (len >= sizeof(dx_textures->name))
	{
		gEngfuncs.Con_Printf(S_ERROR "LoadTexture: too long name %s (%d)\n", name, len);
		return false;
	}

	return true;
}

static dx_texture_t *GL_TextureForName(const char *name)
{
	dx_texture_t *tex;
	uint		hash;

	// find the texture in array
	hash = COM_HashKey(name, TEXTURES_HASH_SIZE);

	for (tex = dx_texturesHashTable[hash]; tex != NULL; tex = tex->nextHash)
	{
		if (!Q_stricmp(tex->name, name))
			return tex;
	}

	return NULL;
}

static dx_texture_t *GL_AllocTexture(const char *name, texFlags_t flags)
{
	dx_texture_t *tex = NULL;

	// find a free texture_t slot
	uint i;

	//skip 0
	for (i = 1; i < MAX_TEXTURES; i++)
	{
		if (dx_textures[i].used)
			continue;

		tex = &dx_textures[i];
		break;
	}

	if (tex == NULL)
	{
		gEngfuncs.Host_Error("%s: MAX_TEXTURES limit exceeds\n", __func__);
		return NULL;
	}

	// copy initial params
	Q_strncpy(tex->name, name, sizeof(tex->name));
	tex->used = 1;
	tex->flags = flags;

	// increase counter
	dx_numTextures = Q_max((tex - dx_textures) + 1, dx_numTextures);

	// add to hash table
	tex->hashValue = COM_HashKey(name, TEXTURES_HASH_SIZE);
	tex->nextHash = dx_texturesHashTable[tex->hashValue];
	dx_texturesHashTable[tex->hashValue] = tex;

	return tex;
}

static void GL_ProcessImage(dx_texture_t *tex, rgbdata_t *pic)
{
	uint	img_flags = 0;

	// force upload texture as RGB or RGBA (detail textures requires this)
	if (tex->flags & TF_FORCE_COLOR) pic->flags |= IMAGE_HAS_COLOR;
	if (pic->flags & IMAGE_HAS_ALPHA) tex->flags |= TF_HAS_ALPHA;

	if (ImageCompressed(pic->type))
	{
		if (!pic->numMips)
			tex->flags |= TF_NOMIPMAP; // disable mipmapping by user request

		// clear all the unsupported flags
		tex->flags &= ~TF_KEEP_SOURCE;
	}
	else
	{
		// copy flag about luma pixels
		if (pic->flags & IMAGE_HAS_LUMA)
			tex->flags |= TF_HAS_LUMA;

		if (pic->flags & IMAGE_QUAKEPAL)
			tex->flags |= TF_QUAKEPAL;

		// create luma texture from quake texture
		if (tex->flags & TF_MAKELUMA)
		{
			img_flags |= IMAGE_MAKE_LUMA;
			tex->flags &= ~TF_MAKELUMA;
		}

		if (!FBitSet(tex->flags, TF_IMG_UPLOADED) && FBitSet(tex->flags, TF_KEEP_SOURCE))
			tex->original = gEngfuncs.FS_CopyImage(pic); // because current pic will be expanded to rgba

		// we need to expand image into RGBA buffer
		if (pic->type == PF_INDEXED_24 || pic->type == PF_INDEXED_32)
			img_flags |= IMAGE_FORCE_RGBA;

		// processing image before uploading (force to rgba, make luma etc)
		if (pic->buffer) gEngfuncs.Image_Process(&pic, 0, 0, img_flags, 0);

		if (FBitSet(tex->flags, TF_LUMINANCE))
			ClearBits(pic->flags, IMAGE_HAS_COLOR);
	}
}

static int GL_CalcMipmapCount(dx_texture_t *tex, qboolean haveBuffer)
{
	int	width, height;
	int	mipcount;

	Assert(tex != NULL);

	//TEMP
	return 1;

	if (!haveBuffer)
		return 1;

	// generate mip-levels by user request
	if (FBitSet(tex->flags, TF_NOMIPMAP))
		return 1;

	// mip-maps can't exceeds 16
	for (mipcount = 0; mipcount < 16; mipcount++)
	{
		width = Q_max(1, (tex->width >> mipcount));
		height = Q_max(1, (tex->height >> mipcount));
		if (width == 1 && height == 1)
			break;
	}

	return mipcount + 1;
}

static byte *GL_ResampleTexture(const byte *source, int inWidth, int inHeight, int outWidth, int outHeight, qboolean isNormalMap)
{
	uint		frac, fracStep;
	uint *in = (uint *)source;
	uint		p1[0x1000], p2[0x1000];
	byte *pix1, *pix2, *pix3, *pix4;
	uint *out, *inRow1, *inRow2;
	static byte *scaledImage = NULL;	// pointer to a scaled image
	vec3_t		normal;
	int		i, x, y;

	if (!source) return NULL;

	scaledImage = Mem_Realloc(r_temppool, scaledImage, outWidth * outHeight * 4);
	fracStep = inWidth * 0x10000 / outWidth;
	out = (uint *)scaledImage;

	frac = fracStep >> 2;
	for (i = 0; i < outWidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	frac = (fracStep >> 2) * 3;
	for (i = 0; i < outWidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracStep;
	}

	if (isNormalMap)
	{
		for (y = 0; y < outHeight; y++, out += outWidth)
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for (x = 0; x < outWidth; x++)
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				normal[0] = MAKE_SIGNED(pix1[0]) + MAKE_SIGNED(pix2[0]) + MAKE_SIGNED(pix3[0]) + MAKE_SIGNED(pix4[0]);
				normal[1] = MAKE_SIGNED(pix1[1]) + MAKE_SIGNED(pix2[1]) + MAKE_SIGNED(pix3[1]) + MAKE_SIGNED(pix4[1]);
				normal[2] = MAKE_SIGNED(pix1[2]) + MAKE_SIGNED(pix2[2]) + MAKE_SIGNED(pix3[2]) + MAKE_SIGNED(pix4[2]);

				if (!VectorNormalizeLength(normal))
					VectorSet(normal, 0.5f, 0.5f, 1.0f);

				((byte *)(out + x))[0] = 128 + (byte)(127.0f * normal[0]);
				((byte *)(out + x))[1] = 128 + (byte)(127.0f * normal[1]);
				((byte *)(out + x))[2] = 128 + (byte)(127.0f * normal[2]);
				((byte *)(out + x))[3] = 255;
			}
		}
	}
	else
	{
		for (y = 0; y < outHeight; y++, out += outWidth)
		{
			inRow1 = in + inWidth * (int)(((float)y + 0.25f) * inHeight / outHeight);
			inRow2 = in + inWidth * (int)(((float)y + 0.75f) * inHeight / outHeight);

			for (x = 0; x < outWidth; x++)
			{
				pix1 = (byte *)inRow1 + p1[x];
				pix2 = (byte *)inRow1 + p2[x];
				pix3 = (byte *)inRow2 + p1[x];
				pix4 = (byte *)inRow2 + p2[x];

				((byte *)(out + x))[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
				((byte *)(out + x))[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
				((byte *)(out + x))[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
				((byte *)(out + x))[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
			}
		}
	}

	return scaledImage;
}

static void GL_TextureImageRAW(dx_texture_t *tex, int width, int height, int type, const char *data)
{
	if (!data)
		return;

	DDSURFACEDESC ddsd = {};
	ZeroMemory(&ddsd, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_LPSURFACE;
	DXCheck(IDirectDrawSurface_Lock(tex->dds, NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR, NULL));

	int pitch = ddsd.lPitch;
	if (!pitch)
		pitch = ddsd.dwWidth * 4;
	char *dst = (char *)ddsd.lpSurface;

	for (int j = 0; j < (int)ddsd.dwHeight; j++)
	{
		//RGBA to BGRA
		if (type == PF_RGBA_32)
		{
			for (int i = 0; i < ddsd.dwWidth; i++)
			{
				dst[i * 4 + 0] = data[i * 4 + 2];
				dst[i * 4 + 1] = data[i * 4 + 1];
				dst[i * 4 + 2] = data[i * 4 + 0];
				dst[i * 4 + 3] = data[i * 4 + 3];
			}
		}
		else if (type == PF_BGRA_32)
		{
			memcpy(dst, data, ddsd.dwWidth * 4);
		}

		dst += pitch;
		data += ddsd.dwWidth * 4;
	}

	DXCheck(IDirectDrawSurface_Unlock(tex->dds, ddsd.lpSurface));
}

static qboolean GL_UploadTexture(dx_texture_t *tex, rgbdata_t *pic)
{
	byte *buf, *data;
	size_t		texsize, size;
	uint		width, height;
	uint		i, j, numSides;
	qboolean		normalMap;
	const byte *bufend;

	// dedicated server
	if (!dxc.window)
		return true;

	Assert(pic != NULL);
	Assert(tex != NULL);

	qboolean supported = true;

	if (FBitSet(pic->flags, IMAGE_CUBEMAP))
		supported = false;
	else if (FBitSet(pic->flags, IMAGE_MULTILAYER) && pic->depth >= 1)
		supported = false;
	else if (pic->width > 1 && pic->height > 1 && pic->depth > 1)
		supported = false;
	else if (FBitSet(tex->flags, TF_RECTANGLE))
		supported = false;
	else if (FBitSet(tex->flags, TF_MULTISAMPLE))
		supported = false;

	// make sure what target is correct
	if (!supported)
	{
		gEngfuncs.Con_DPrintf(S_ERROR "%s: %s is not supported\n", __func__, tex->name);
		return false;
	}

	//if (pic->type == PF_BC6H_SIGNED || pic->type == PF_BC6H_UNSIGNED || pic->type == PF_BC7_UNORM || pic->type == PF_BC7_SRGB)
	if (ImageCompressed(pic->type))
	{
		gEngfuncs.Con_DPrintf(S_ERROR "%s: compressed textures are not supported\n", __func__);
		return false;
	}

	// store original sizes
	tex->srcWidth = pic->width;
	tex->srcHeight = pic->height;

	// set the texture dimensions
	{
		int	step = 0;// (int)gl_round_down.value;
		int	scaled_width, scaled_height;

		for (scaled_width = 1; scaled_width < pic->width; scaled_width <<= 1);

		if (step > 0 && pic->width < scaled_width && (step == 1 || (scaled_width - pic->width) > (scaled_width >> step)))
			scaled_width >>= 1;

		for (scaled_height = 1; scaled_height < pic->height; scaled_height <<= 1);

		if (step > 0 && pic->height < scaled_height && (step == 1 || (scaled_height - pic->height) > (scaled_height >> step)))
			scaled_height >>= 1;

		tex->width = Q_max(1, scaled_width);
		tex->height = Q_max(1, scaled_height);
	}

	DDSURFACEDESC ddsd = {};
	ZeroMemory(&ddsd, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
	//if (pic->numMips > 1)
	//	ddsd.ddsCaps.dwCaps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
	ddsd.dwWidth = tex->width;
	ddsd.dwHeight = tex->height;

#if 1
	//BGRA8
	ddsd.dwFlags |= DDSD_PIXELFORMAT;
	ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
	ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB;
	if(FBitSet(pic->flags, IMAGE_HAS_ALPHA))
		ddsd.ddpfPixelFormat.dwFlags |= DDPF_ALPHAPIXELS;
	ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
	ddsd.ddpfPixelFormat.dwBBitMask = 0xFF;
	ddsd.ddpfPixelFormat.dwGBitMask = 0xFF00;
	ddsd.ddpfPixelFormat.dwRBitMask = 0xFF0000;
	ddsd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
#endif

	DXCheck(IDirectDraw_CreateSurface(dxc.pdd, &ddsd, &tex->dds, NULL));
	DXCheck(IDirectDrawSurface_QueryInterface(tex->dds, &IID_IDirect3DTexture, (void**)&tex->d3dTex));
	DXCheck(IDirect3DTexture_GetHandle(tex->d3dTex, dxc.pd3dd, &tex->d3dHandle));

	tex->fogParams[0] = pic->fogParams[0];
	tex->fogParams[1] = pic->fogParams[1];
	tex->fogParams[2] = pic->fogParams[2];
	tex->fogParams[3] = pic->fogParams[3];

	buf = pic->buffer;
	bufend = pic->buffer + pic->size; // total image size include all the layers, cube sides, mipmaps
	normalMap = FBitSet(tex->flags, TF_NORMALMAP) ? true : false;

	if (Q_max(1, pic->numMips) > 1)	// not-compressed DDS
	{
		//TEMP
		j = 0;
		//for (j = 0; j < Q_max(1, pic->numMips); j++)
		{
			// track the buffer bounds
			if (buf != NULL && buf >= bufend)
				gEngfuncs.Host_Error("%s: %s image buffer overflow\n", __func__, tex->name);

			width = Q_max(1, (tex->width >> j));
			height = Q_max(1, (tex->height >> j));
			size = gEngfuncs.Image_CalcImageSize(pic->type, width, height, 1);
			GL_TextureImageRAW(tex, width, height, pic->type, buf);
			buf += size; // move pointer
			tex->numMips++;
		}
	}
	else // RGBA32
	{
		// track the buffer bounds
		if (buf != NULL && buf >= bufend)
			gEngfuncs.Host_Error("%s: %s image buffer overflow\n", __func__, tex->name);

		int mipCount = GL_CalcMipmapCount(tex, (buf != NULL));

		// NOTE: only single uncompressed textures can be resamples, no mips, no layers, no sides
		if (((pic->width != tex->width) || (pic->height != tex->height)))
			data = GL_ResampleTexture(buf, pic->width, pic->height, tex->width, tex->height, normalMap);
		else data = buf;

		//if (!ImageCompressed(pic->type) && !FBitSet(tex->flags, TF_NOMIPMAP) && FBitSet(pic->flags, IMAGE_ONEBIT_ALPHA))
		//	data = GL_ApplyFilter(data, tex->width, tex->height);

		// mips will be auto-generated if desired
		for (j = 0; j < mipCount; j++)
		{
			width = Q_max(1, (tex->width >> j));
			height = Q_max(1, (tex->height >> j));
			size = gEngfuncs.Image_CalcImageSize(pic->type, width, height, 1);
			GL_TextureImageRAW(tex, width, height, pic->type, data);
			//if (mipCount > 1)
			//	GL_BuildMipMap(data, width, height, tex->depth, tex->flags);
			tex->numMips++;
		}
	}

	SetBits(tex->flags, TF_IMG_UPLOADED); // done

	return true;
}

int GL_LoadTextureFromBuffer(const char *name, rgbdata_t *pic, texFlags_t flags, qboolean update)
{
	dx_texture_t *tex;

	if (!GL_CheckTexName(name))
		return 0;

	// see if already loaded
	if ((tex = GL_TextureForName(name)) && !update)
		return (tex - dx_textures);

	// couldn't loading image
	if (!pic) return 0;

	if (update)
	{
		if (tex == NULL)
			gEngfuncs.Host_Error("%s: couldn't find texture %s for update\n", __func__, name);
		SetBits(tex->flags, flags);
	}
	else
	{
		// allocate the new one
		tex = GL_AllocTexture(name, flags);
	}

	GL_ProcessImage(tex, pic);

	if (!GL_UploadTexture(tex, pic))
	{
		memset(tex, 0, sizeof(dx_texture_t));
		return 0;
	}

	return (tex - dx_textures);
}

void GL_ProcessTexture(int texnum, float gamma, int topColor, int bottomColor)
{
	;
}

void GetDetailScaleForTexture(int texture, float *xScale, float *yScale)
{
	*xScale = *yScale = 1.0f;
}

void GetExtraParmsForTexture(int texture, byte *red, byte *green, byte *blue, byte *alpha)
{
	*red = *green = *blue = *alpha = 0;
}

int GL_FindTexture(const char *name)
{
	gEngfuncs.Con_Printf("GL_FindTexture(%s)\n", name);
	return 0;
}

const char *GL_TextureName(unsigned int texnum)
{
	return NULL;
}

const byte *GL_TextureData(unsigned int texnum)
{
	return NULL;
}

int GL_LoadTexture( const char *name, const byte *buf, size_t size, int flags )
{
	dx_texture_t *tex;
	rgbdata_t *pic;
	uint		picFlags = 0;

	if (!GL_CheckTexName(name))
		return 0;

	// see if already loaded
	if ((tex = GL_TextureForName(name)))
		return (tex - dx_textures);

	gEngfuncs.Con_Printf("GL_LoadTexture(%s, %p, %zu, %X)\n", name, buf, size, flags);

	if (FBitSet(flags, TF_NOFLIP_TGA))
		SetBits(picFlags, IL_DONTFLIP_TGA);

	if (FBitSet(flags, TF_KEEP_SOURCE) && !FBitSet(flags, TF_EXPAND_SOURCE))
		SetBits(picFlags, IL_KEEP_8BIT);

	// set some image flags
	gEngfuncs.Image_SetForceFlags(picFlags);

	pic = gEngfuncs.FS_LoadImage(name, buf, size);
	if (!pic) return 0; // couldn't loading image

	// allocate the new one
	tex = GL_AllocTexture(name, flags);
	GL_ProcessImage(tex, pic);

	if (!GL_UploadTexture(tex, pic))
	{
		memset(tex, 0, sizeof(dx_texture_t));
		gEngfuncs.FS_FreeImage(pic); // release source texture
		return 0;
	}

	//GL_ApplyTextureParams(tex); // update texture filter, wrap etc
	gEngfuncs.FS_FreeImage(pic); // release source texture

	// NOTE: always return texnum as index in array or engine will stop work !!!
	return tex - dx_textures;
}

int GL_CreateTexture(const char *name, int width, int height, const void *buffer, texFlags_t flags)
{
	return 0;
}

int GL_LoadTextureArray(const char **names, int flags)
{
	return 0;
}

int GL_CreateTextureArray(const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags)
{
	return 0;
}

void GL_FreeTexture(unsigned int texnum)
{
	if (texnum <= 0 || texnum >= MAX_TEXTURES)
		return;

	gEngfuncs.Con_Printf("GL_FreeTexture(%d) %s\n", texnum, dx_textures[texnum].name);

	dx_texture_t* tex = R_GetTexture(texnum);
	if (!tex->dds)
		return;

	// debug
	if (!tex->name[0])
	{
		gEngfuncs.Con_Printf(S_ERROR "%s: trying to free unnamed texture with handle %X\n", __func__, tex->d3dHandle);
		return;
	}

	// remove from hash table
	dx_texture_t **prev = &dx_texturesHashTable[tex->hashValue];

	while (1)
	{
		dx_texture_t *cur = *prev;
		if (!cur) break;

		if (cur == tex)
		{
			*prev = cur->nextHash;
			break;
		}
		prev = &cur->nextHash;
	}

	// TODO invalidate texture state

	// release source
	if (tex->original)
		gEngfuncs.FS_FreeImage(tex->original);

	if (tex->d3dTex)
		IDirect3DTexture_Release(tex->d3dTex);
	if (tex->dds)
		IDirectDrawSurface_Release(tex->dds);
	memset(tex, 0, sizeof(*tex));
}

void R_OverrideTextureSourceSize(unsigned int textnum, unsigned int srcWidth, unsigned int srcHeight)
{

}
