/*
texturelib.h - engine texture manager
Copyright (C) 2022 Valery Klachkov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef TEXTURELIB_H
#define TEXTURELIB_H

#include "common.h"

#include <ref_api.h>

void RM_Init();

void RM_SetRender( ref_interface_t* ref );

void RM_ReuploadTextures();

int  RM_LoadTexture( const char *name, const byte *buf, size_t size, int flags );
int	 RM_LoadTextureArray( const char **names, int flags );
int  RM_LoadTextureFromBuffer( const char *name, rgbdata_t *picture, int flags, qboolean update );
void RM_FreeTexture( unsigned int texnum );

const char*	RM_TextureName( unsigned int texnum );
const byte*	RM_TextureData( unsigned int texnum );

int  RM_FindTexture( const char *name );
void RM_GetTextureParams( int *w, int *h, int texnum );

int	RM_CreateTexture( const char *name, int width, int height, const void *buffer, texFlags_t flags );
int RM_CreateTextureArray( const char *name, int width, int height, int depth, const void *buffer, texFlags_t flags );

#endif// TEXTURELIB_H