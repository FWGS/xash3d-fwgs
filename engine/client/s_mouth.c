/*
s_mouth.c - animate mouth
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

#include "common.h"
#include "sound.h"
#include "client.h"
#include "const.h"

#define CAVGSAMPLES		10

static void SND_CommitMouthValue( mouth_t *mouth, int savg, int scount )
{
	mouth->sndavg += savg;
	mouth->sndcount = (byte)scount;

	if( mouth->sndcount >= CAVGSAMPLES )
	{
		mouth->mouthopen = mouth->sndavg / CAVGSAMPLES;
		mouth->sndavg = 0;
		mouth->sndcount = 0;
	}
}

void SND_MoveMouth8( mouth_t *mouth, int pos, const wavdata_t *sc, int count, qboolean use_loop )
{
	const int8_t *pdata = NULL;
	int		scount;
	int		savg, data;
	uint 		i;

	count = S_RetrieveAudioSamples( sc, (const void **)&pdata, pos, count, use_loop );
	if( pdata == NULL ) return;

	i = 0;
	scount = mouth->sndcount;
	savg = 0;

	while( i < count && scount < CAVGSAMPLES )
	{
		data = pdata[i];
		savg += abs( data );

		i += 80 + ((byte)data & 0x1F);
		scount++;
	}

	SND_CommitMouthValue( mouth, savg, scount );
}

void SND_MoveMouth16( mouth_t *mouth, int pos, const wavdata_t *sc, int count, qboolean use_loop )
{
	const int16_t *pdata = NULL;
	int		savg, data;
	int		scount;
	uint 		i;

	count = S_RetrieveAudioSamples( sc, (const void **)&pdata, pos, count, use_loop );
	if( pdata == NULL ) return;

	i = 0;
	scount = mouth->sndcount;
	savg = 0;

	while( i < count && scount < CAVGSAMPLES )
	{
		data = pdata[i];
		data = (bound( -32767, data, 0x7ffe ) >> 8);
		savg += abs( data );

		i += 80 + ((byte)data & 0x1F);
		scount++;
	}

	SND_CommitMouthValue( mouth, savg, scount );
}

void SND_ForceInitMouth( int entnum )
{
	cl_entity_t *clientEntity;

	clientEntity = CL_GetEntityByIndex( entnum );

	if( clientEntity )
	{
		clientEntity->mouth.mouthopen = 0;
		clientEntity->mouth.sndavg = 0;
		clientEntity->mouth.sndcount = 0;
	}
}

void SND_ForceCloseMouth( int entnum )
{
	cl_entity_t *clientEntity;

	clientEntity = CL_GetEntityByIndex( entnum );

	if( clientEntity )
		clientEntity->mouth.mouthopen = 0;
}

void SND_MoveMouthRaw( mouth_t *mouth, const portable_samplepair_t *buf, int count )
{
	int		savg, data;
	int		scount = 0;
	uint 		i;

	i = 0;
	scount = mouth->sndcount;
	savg = 0;

	while ( i < count && scount < CAVGSAMPLES )
	{
		data = buf[i].left; // mono sound anyway
		data = ( bound( -32767, data, 0x7ffe ) >> 8 );
		savg += abs( data );

		i += 80 + ( (byte)data & 0x1F );
		scount++;
	}

	SND_CommitMouthValue( mouth, savg, scount );
}
