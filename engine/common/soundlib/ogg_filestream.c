/*
ogg_filestream.c - helper struct for working with Ogg files
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

#include "ogg_filestream.h"
#include "soundlib.h"
#include <string.h>

size_t OggFilestream_Read( void *ptr, size_t blockSize, size_t nmemb, void *datasource )
{
	ogg_filestream_t *filestream = (ogg_filestream_t *)datasource;
	size_t remain = filestream->filesize - filestream->position;
	size_t dataSize = blockSize * nmemb;

	// reads as many blocks as fits in remaining memory
	if( dataSize > remain )
		dataSize = remain - remain % blockSize;

	memcpy( ptr, filestream->buffer + filestream->position, dataSize );
	filestream->position += dataSize;
	return dataSize / blockSize;
}

int OggFilestream_Seek( void *datasource, int64_t offset, int whence )
{
	int64_t position;
	ogg_filestream_t *filestream = (ogg_filestream_t *)datasource;

	if( whence == SEEK_SET )
		position = offset;
	else if( whence == SEEK_CUR )
		position = offset + filestream->position;
	else if( whence == SEEK_END )
		position = offset + filestream->filesize;
	else
		return -1;

	if( position < 0 || position > filestream->filesize )
		return -1;

	filestream->position = position;
	return 0;
}

long OggFilestream_Tell( void *datasource )
{
	ogg_filestream_t *filestream = (ogg_filestream_t *)datasource;
	return filestream->position;
}
