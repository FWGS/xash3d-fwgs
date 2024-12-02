/*
ogg_filestream.h - helper struct for working with Ogg files
Copyright (C) 2024 SNMetamorph

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef OGG_FILESTREAM_H
#define OGG_FILESTREAM_H

#include "xash3d_types.h"

typedef struct ogg_filestream_s
{
	const char *name;
	const byte *buffer;
	size_t     filesize;
	size_t     position;
} ogg_filestream_t;

size_t OggFilestream_Read( void *ptr, size_t blockSize, size_t nmemb, void *datasource );
int OggFilestream_Seek( void *datasource, int64_t offset, int whence );
long OggFilestream_Tell( void *datasource );

static inline void OggFilestream_Init( ogg_filestream_t *filestream, const char *name, const byte *buffer, size_t filesize )
{
	filestream->name = name;
	filestream->buffer = buffer;
	filestream->filesize = filesize;
	filestream->position = 0;
}

#endif // OGG_FILESTREAM_H
