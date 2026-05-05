/*
BMPUtils.cpp -- BMP helper implementation
Copyright (C) 2018 a1batross
*/
extern "C" {
#include "common.h"
#include "filesystem.h"
#include "client.h"
}
#include "BMPUtils.h"

CBMP *CBMP::LoadFile( const char *filename )
{
	fs_offset_t length = 0;
	bmp_t *bmp = (bmp_t *)g_fsapi.LoadFile( filename, &length, false );

	if( !bmp )
		return NULL;

	if( (size_t)length < sizeof( bmp_t ))
	{
		Mem_Free( bmp );
		return NULL;
	}

	if( bmp->id[0] != 'B' || bmp->id[1] != 'M' )
	{
		Mem_Free( bmp );
		return NULL;
	}

	CBMP::SwapBmpHdrToLE( bmp );

	if( !bmp->width || !bmp->height )
	{
		Mem_Free( bmp );
		return NULL;
	}

	if( (int)length < (int)bmp->fileSize ||
	    (int)length < (int)bmp->bitmapDataSize ||
	    (int)length < (int)bmp->bitmapDataOffset ||
	    (int)length < (int)bmp->bitmapHeaderSize ||
	    (int)length < (int)( bmp->bitmapDataOffset + bmp->bitmapDataSize ) ||
	    (int)length < (int)( bmp->bitmapHeaderSize + bmp->bitmapDataSize ))
	{
		Mem_Free( bmp );
		return NULL;
	}

	return new CBMP( bmp );
}

int CBMP::Upload( const char *name, int flags )
{
	uint fileSize = GetBitmapHdr()->fileSize;
	SwapHdrToLE();
	return ref.dllFuncs.GL_LoadTexture( name, GetBitmap(), fileSize, TF_IMAGE | TF_NOMIPMAP | flags );
}
