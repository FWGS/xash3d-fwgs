/*
snd_mp3_stub_gc.c - GameCube MP3 stub (no mpg123)
Copyright (C) 2026 Xash3D FWGS GameCube port

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

#include "soundlib.h"

qboolean Sound_LoadMPG( const char *name, const byte *buffer, fs_offset_t filesize )
{
	(void)buffer;
	(void)filesize;
	Con_Reportf( S_WARN "Xash3D GameCube: MP3 load skipped (no mpg123) name=%s\n", name ? name : "?" );
	return false;
}

stream_t *Stream_OpenMPG( const char *filename )
{
	(void)filename;
	return NULL;
}

int Stream_ReadMPG( stream_t *stream, int bytes, void *buffer )
{
	(void)stream;
	(void)bytes;
	(void)buffer;
	return 0;
}

int Stream_SetPosMPG( stream_t *stream, int newpos )
{
	(void)stream;
	(void)newpos;
	return -1;
}

int Stream_GetPosMPG( stream_t *stream )
{
	(void)stream;
	return 0;
}

void Stream_FreeMPG( stream_t *stream )
{
	(void)stream;
}
