/*
custom.c - customization routines
Copyright (C) 2018 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "custom.h"
#include "ref_common.h"
#include "hpak.h" // be aware of HPK limits
#include "wadfile.h"

static rgbdata_t *CustomDecal_LoadHLWADSpray( const void *raw, int size )
{
	rgbdata_t *out;
	int i, j;
	const wad3header_t *hdr;
	const wad3lump_t *lumps;
	const unsigned char *mipdata;
	mip_t mip;
	uint32_t width, height, offset0, m0size, m1size, m2size, m3size;
	const unsigned char *pixels;
	const unsigned char *palette;
	unsigned char grad_palette[256 * 3];
	const unsigned char *use_palette;
	int alpha_mode;
	unsigned char frontR = 0, frontG = 0, frontB = 0;
	float t;
	byte idx;

	if( !raw || size < 64 )
		return NULL;

	hdr = (const wad3header_t *)raw;

	if( hdr->numlumps <= 0 || hdr->infotableofs <= 0 || hdr->infotableofs >= size )
		return NULL;

	lumps = (const wad3lump_t *)((const char *)raw + hdr->infotableofs );

	for( i = 0; i < hdr->numlumps; ++i )
	{
		if( lumps[i].type == TYP_PALETTE || lumps[i].type == TYP_MIPTEX )
		{
			mipdata = (const unsigned char *)raw + lumps[i].filepos;

			if( lumps[i].disksize < sizeof( mip_t ) + 2 + 768 )
				continue;

			memcpy( &mip, mipdata, sizeof( mip ));
			width = mip.width;
			height = mip.height;

			if( width <= 0 || height <= 0 || width > 256 || height > 256 )
				continue;

			offset0 = mip.offsets[0];
			if( offset0 == 0 || offset0 + width * height > lumps[i].disksize )
				continue;

			pixels = mipdata + offset0;
			m0size = width * height;
			m1size = m0size / 4;
			m2size = m0size / 16;
			m3size = m0size / 64;
			palette = mipdata + 0x28 + m0size + m1size + m2size + m3size + 2;

			out = Mem_Calloc( host.imagepool, sizeof( rgbdata_t ));
			out->buffer = Mem_Malloc( host.imagepool, width * height * 4 );
			out->width = width;
			out->height = height;
			out->flags = IMAGE_HAS_COLOR | IMAGE_HAS_ALPHA;
			out->type = PF_RGBA_32;
			out->size = width * height * 4;
			out->palette = NULL;

			use_palette = palette;
			alpha_mode = 0;

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
				alpha_mode = 1; // gradient
			}

			for( j = 0; j < (int)(width * height); ++j )
			{
				idx = pixels[j];
				out->buffer[j * 4 + 0] = use_palette[idx * 3 + 0];
				out->buffer[j * 4 + 1] = use_palette[idx * 3 + 1];
				out->buffer[j * 4 + 2] = use_palette[idx * 3 + 2];
				if( alpha_mode == 1 )
				{
					out->buffer[j * 4 + 3] = idx; // soft transparency (0x40)
				}
				else
				{
					out->buffer[j * 4 + 3] = ( idx == 255 ) ? 0 : 255; // classic (0x43)
				}
			}
			return out;
		}
	}
	return NULL;
}

static rgbdata_t *CustomDecal_LoadImage( const char *path, void *raw, int size )
{
	const char *testname;
	rgbdata_t *wadimg;

	if( raw && size > 16 && memcmp( raw, "WAD3", 4 ) == 0 )
	{
		wadimg = CustomDecal_LoadHLWADSpray( raw, size );
		if( wadimg )
			return wadimg;
	}

	// this way we limit file types
	if( !Q_stricmp( COM_FileExtension( path ), "png" ))
		testname = "#logo.png";
	else testname = "#logo.bmp";

	Image_SetForceFlags( IL_LOAD_PLAYER_DECAL );

	return FS_LoadImage( testname, raw, size );
}

static qboolean CustomDecal_Validate( const char *path, void *raw, int nFileSize )
{
	rgbdata_t *test = CustomDecal_LoadImage( path, raw, nFileSize );

	if( test )
	{
		// all's ok, logo is valid
		FS_FreeImage( test );
		return true;
	}

	return false;
}

void COM_ClearCustomizationList( customization_t *pHead, qboolean bCleanDecals )
{
	customization_t	*pCurrent;
	customization_t	*pNext;

	for( pCurrent = pHead->pNext; pCurrent != NULL; pCurrent = pNext )
	{
		pNext = pCurrent->pNext;

		if( pCurrent->bInUse && pCurrent->pBuffer )
			Mem_Free( pCurrent->pBuffer );

		if( pCurrent->bInUse && pCurrent->pInfo )
		{
#if !XASH_DEDICATED
			if( pCurrent->resource.type == t_decal )
			{
				if( bCleanDecals && CL_Active( ))
					ref.dllFuncs.R_DecalRemoveAll( pCurrent->nUserData1 );
			}
#endif

			FS_FreeImage( pCurrent->pInfo );
		}
		Mem_Free( pCurrent );
	}

	pHead->pNext = NULL;
}

qboolean COM_CreateCustomization( customization_t *pListHead, resource_t *pResource, int playernumber, int flags, customization_t **pOut, int *nLumps )
{
	qboolean		bError = false;
	fs_offset_t		checksize = 0;
	customization_t	*pCust;

	if( pOut ) *pOut = NULL;

	pCust = Z_Calloc( sizeof( customization_t ));
	pCust->resource = *pResource;

	if( pResource->nDownloadSize <= 0 )
		goto CustomizationError;

	pCust->bInUse = true;

	if( FBitSet( flags, FCUST_FROMHPAK ))
	{
		if( !HPAK_GetDataPointer( hpk_custom_file.string, pResource, (byte **)&pCust->pBuffer, NULL ))
			bError = true;
	}
	else
	{

		pCust->pBuffer = FS_LoadFile( pResource->szFileName, &checksize, true );
		if( (int)checksize != pCust->resource.nDownloadSize )
			bError = true;
	}

	if( bError )
		goto CustomizationError;

	if( FBitSet( pCust->resource.ucFlags, RES_CUSTOM ) && pCust->resource.type == t_decal )
	{
		pCust->resource.playernum = playernumber;

		if( CustomDecal_Validate( pResource->szFileName, pCust->pBuffer, pResource->nDownloadSize ))
		{
			if( !FBitSet( flags, FCUST_IGNOREINIT ))
			{
				if( pResource->nDownloadSize >= HPAK_ENTRY_MIN_SIZE && pResource->nDownloadSize <= HPAK_ENTRY_MAX_SIZE )
				{
					pCust->bTranslated = true;
					pCust->nUserData1 = 0;
					pCust->nUserData2 = 7;

					if( !FBitSet( flags, FCUST_WIPEDATA ))
						pCust->pInfo = CustomDecal_LoadImage( pResource->szFileName, pCust->pBuffer, pCust->resource.nDownloadSize );
					else pCust->pInfo = NULL;
					if( nLumps ) *nLumps = 1;
				}
				else
				{
					Con_Printf( S_WARN "Ignoring custom decal \"%s\": wrong size (%i bytes)\n", pResource->szFileName, pResource->nDownloadSize );
				}
			}
		}
	}

	if( pOut ) *pOut = pCust;
	pCust->pNext = pListHead->pNext;
	pListHead->pNext = pCust;

	return true;

CustomizationError:
	if( pCust->pBuffer )
		Mem_Free( pCust->pBuffer );

	if( pCust->pInfo )
		Mem_Free( pCust->pInfo );
	Mem_Free( pCust );

	return false;
}

int COM_SizeofResourceList( resource_t *pList, resourceinfo_t *ri )
{
	int		nSize = 0;
	resource_t	*p;

	memset( ri, 0, sizeof( *ri ));

	for( p = pList->pNext; p != pList; p = p->pNext )
	{
		nSize += p->nDownloadSize;

		if( p->type == t_model && p->nIndex == 1 )
			ri->info[t_world].size += p->nDownloadSize;
		else ri->info[p->type].size += p->nDownloadSize;
	}

	return nSize;
}
