/*
texture.c - texture writer (Updated for 4K/Truecolor)
Copyright (C) 2020 Andrey Akhmichin
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "const.h"
#include "crtlib.h"
#include "studio.h"
#include "img_bmp.h"
#include "img_tga.h"
#include "mdldec.h"
#include "texture.h"
#include "utils.h"

/*
============
WriteBMP (24-bit Truecolor)
============
*/
static void WriteBMP(FILE* fp, mstudiotexture_t* texture)
{
	int          i, j, padding;
	const byte* pixel, * palette;
	byte* pic, row[4096 * 3]; // Supports up to 4K width
	bmp_t        bmp_hdr = { 0 };

	// BMP Header
	bmp_hdr.id[0] = 'B';
	bmp_hdr.id[1] = 'M';
	bmp_hdr.width = texture->width;
	bmp_hdr.height = texture->height;
	bmp_hdr.planes = 1;
	bmp_hdr.bitsPerPixel = 24;
	bmp_hdr.bitmapHeaderSize = BI_SIZE;
	bmp_hdr.bitmapDataOffset = sizeof(bmp_t);

	// Calculate row padding (BMP rows are 4-byte aligned)
	padding = (4 - (texture->width * 3) % 4) % 4;
	bmp_hdr.bitmapDataSize = (texture->width * 3 + padding) * texture->height;
	bmp_hdr.fileSize = sizeof(bmp_t) + bmp_hdr.bitmapDataSize;

	fwrite(&bmp_hdr, sizeof(bmp_t), 1, fp);

	pic = (byte*)texture_hdr + texture->index;
	palette = pic + texture->width * texture->height; // Palette follows pixel data

	// Write pixels (convert 8-bit indexed to 24-bit BGR)
	for (i = texture->height - 1; i >= 0; i--) // BMP is bottom-to-top
	{
		const byte* row_start = pic + i * texture->width;
		byte* row_ptr = row;

		for (j = 0; j < texture->width; j++)
		{
			int color_idx = row_start[j];
			const byte* color = palette + color_idx * 3;

			// BMP uses BGR order
			*row_ptr++ = color[2]; // B
			*row_ptr++ = color[1]; // G
			*row_ptr++ = color[0]; // R
		}

		fwrite(row, texture->width * 3, 1, fp);
		if (padding > 0) fwrite("\0\0\0", padding, 1, fp); // Pad row
	}
}

/*
============
WriteTGA (32-bit RGBA)
============
*/
static void WriteTGA(FILE* fp, mstudiotexture_t* texture)
{
	int          i, j;
	const byte* pixel, * palette;
	byte* pic, rgba[4096 * 4]; // Supports up to 4K width
	tga_t        tga_hdr = { 0 };

	// TGA Header
	tga_hdr.image_type = 2; // Uncompressed truecolor
	tga_hdr.width = texture->width;
	tga_hdr.height = texture->height;
	tga_hdr.pixel_size = 32; // RGBA
	tga_hdr.attributes = 8; // 8-bit alpha channel

	fwrite(&tga_hdr, sizeof(tga_t), 1, fp);

	pic = (byte*)texture_hdr + texture->index;
	palette = pic + texture->width * texture->height;

	// Write pixels (convert 8-bit indexed to 32-bit RGBA)
	for (i = 0; i < texture->height; i++)
	{
		const byte* row_start = pic + i * texture->width;
		byte* row_ptr = rgba;

		for (j = 0; j < texture->width; j++)
		{
			int color_idx = row_start[j];
			const byte* color = palette + color_idx * 3;

			*row_ptr++ = color[0]; // R
			*row_ptr++ = color[1]; // G
			*row_ptr++ = color[2]; // B
			*row_ptr++ = 255;       // A (opaque)
		}

		fwrite(rgba, texture->width * 4, 1, fp);
	}
}

/*
============
WriteTextures (Compatibility Layer)
============
*/
void WriteTextures(void)
{
	int          i, len, namelen, emptyplace;
	FILE* fp;
	mstudiotexture_t* texture = (mstudiotexture_t*)((byte*)texture_hdr + texture_hdr->textureindex);
	char         path[MAX_SYSPATH];

	len = Q_snprintf(path, MAX_SYSPATH, "%s" DEFAULT_TEXTUREPATH, destdir);

	if (len == -1 || !MakeDirectory(path))
	{
		fputs("ERROR: Couldn't create texture directory\n", stderr);
		return;
	}

	emptyplace = MAX_SYSPATH - len;

	for (i = 0; i < texture_hdr->numtextures; ++i, ++texture)
	{
		namelen = Q_strncpy(&path[len], texture->name, emptyplace);

		if (emptyplace - namelen < 0)
		{
			fprintf(stderr, "ERROR: Path too long for %s\n", texture->name);
			continue;
		}

		// Force .tga extension for alpha support
		Q_strncpy(strstr(path, ".bmp") ? path : strrchr(path, '.') + 1, "tga", 4);

		fp = fopen(path, "wb");
		if (!fp)
		{
			fprintf(stderr, "ERROR: Couldn't write %s\n", path);
			continue;
		}

		WriteTGA(fp, texture); // Always export as modern TGA
		fclose(fp);
		printf("Exported: %s\n", path);
	}
}
