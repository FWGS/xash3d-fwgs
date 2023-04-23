/*
s_vox.c - npc sentences
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
#include "const.h"
#include "sequence.h"
#include <ctype.h>

static int cszrawsentences = 0;
static char *rgpszrawsentence[CVOXFILESENTENCEMAX];
static const char *voxperiod = "_period", *voxcomma = "_comma";

// return number of samples mixed
int VOX_MixDataToDevice( channel_t *pchan, int sampleCount, int outputRate, int outputOffset )
{
	// save this to compute total output
	int	startingOffset = outputOffset;

	if( !pchan->currentWord )
		return 0;

	while( sampleCount > 0 && pchan->currentWord )
	{
		int	timeCompress = pchan->words[pchan->wordIndex].timecompress;
		int	outputCount = S_MixDataToDevice( pchan, sampleCount, outputRate, outputOffset, timeCompress );

		outputOffset += outputCount;
		sampleCount -= outputCount;

		// if we finished load a next word
		if( pchan->currentWord->finished )
		{
			VOX_FreeWord( pchan );
			pchan->wordIndex++;
			VOX_LoadWord( pchan );

			if( pchan->currentWord )
			{
				pchan->sfx = pchan->words[pchan->wordIndex].sfx;
			}
		}
	}
	return outputOffset - startingOffset;
}

void VOX_LoadWord( channel_t *ch )
{
	const voxword_t *word = &ch->words[ch->wordIndex];
	wavdata_t *data;
	int start, end, samples;

	if( !word->sfx )
		return;

	data = S_LoadSound( word->sfx );

	if( !data )
		return;

	ch->currentWord = &ch->pMixer;
	ch->currentWord->pData = data;

	samples = data->samples;
	start   = word->start;
	end     = word->end;

	if( end <= start ) end = 0;

	if( start )
		S_SetSampleStart( ch, data, start * 0.01f * samples );

	if( end )
		S_SetSampleEnd( ch, data, end * 0.01f * samples );
}

void VOX_FreeWord( channel_t *ch )
{
	voxword_t *word = &ch->words[ch->wordIndex];

	ch->currentWord = NULL;
	memset( &ch->pMixer, 0, sizeof( ch->pMixer ));

	if( !word->sfx && !word->fKeepCached )
		return;

	FS_FreeSound( word->sfx->cache );
	word->sfx->cache = NULL;
	word->sfx = NULL;
}

void VOX_SetChanVol( channel_t *ch )
{
	voxword_t *word;
	if( !ch->currentWord )
		return;

	word = &ch->words[ch->wordIndex];

	if( word->volume == 100 )
		return;

	ch->leftvol = ch->leftvol * word->volume * 0.01f;
	ch->rightvol = ch->rightvol * word->volume * 0.01f;
}

float VOX_ModifyPitch( channel_t *ch, float pitch )
{
	voxword_t *word;
	if( !ch->currentWord )
		return pitch;

	word = &ch->words[ch->wordIndex];

	if( word->pitch < 0 )
		return pitch;

	pitch += ( word->pitch - PITCH_NORM ) * 0.01f;

	return pitch;
}

static const char *VOX_GetDirectory( char *szpath, const char *psz, int nsize )
{
	const char *p;
	int len;

	// search / backwards
	p = Q_strrchr( psz, '/' );

	if( !p )
	{
		Q_strncpy( szpath, "vox/", nsize );
		return psz;
	}

	len = p - psz + 1;

	if( len > nsize )
	{
		Con_Printf( "VOX_GetDirectory: invalid directory in: %s\n", psz );
		return NULL;
	}

	memcpy( szpath, psz, len );
	szpath[len] = 0;

	return p + 1;
}

static const char *VOX_LookupString( const char *pszin )
{
	int i = -1, len;
	const char *c;

	// check if we are a CSCZ or immediate sentence
	if( *pszin == '#' )
	{
		// Q_atoi is too smart and allows negative values
		// so check with Q_isdigit beforehand
		if( Q_isdigit( pszin + 1 ))
		{
			sentenceEntry_s *sentenceEntry;
			i = Q_atoi( pszin + 1 );
			if(( sentenceEntry = Sequence_GetSentenceByIndex( i )))
				return sentenceEntry->data;
		}
		else
		{
			// immediate sentence, probably coming from "speak" command
			return pszin + 1;
		}
	}

	// check if we received an index
	if( Q_isdigit( pszin ))
	{
		i = Q_atoi( pszin );

		if( i >= cszrawsentences )
			i = -1;
	}

	// last hope: find it in sentences array
	if( i == -1 )
	{
		for( i = 0; i < cszrawsentences; i++ )
		{
			if( !Q_stricmp( pszin, rgpszrawsentence[i] ))
				break;
		}
	}

	// not found, exit
	if( i == cszrawsentences )
		return NULL;

	len = Q_strlen( rgpszrawsentence[i] );

	c = &rgpszrawsentence[i][len + 1];
	for( ; *c == ' ' || *c == '\t'; c++ );

	return c;
}

static int VOX_ParseString( char *psz, char *rgpparseword[CVOXWORDMAX] )
{
	int i = 0;

	if( !psz )
		return i;

	rgpparseword[i++] = psz;

	while( i < CVOXWORDMAX )
	{
		// skip to next word
		for( ; *psz &&
			*psz != ' ' &&
			*psz != '.' &&
			*psz != ',' &&
			*psz != '('; psz++ );

		// skip anything in between ( and )
		if( *psz == '(' )
		{
			for( ; *psz && *psz != ')'; psz++ );
			psz++;
		}

		if( !*psz )
			return i;

		// . and , are special but if not end of string
		if(( *psz == '.' || *psz == ',' ) &&
			psz[1] != '\n' && psz[1] != '\r' && psz[1] != '\0' )
		{
			if( *psz == '.' )
				rgpparseword[i++] = (char *)voxperiod;
			else rgpparseword[i++] = (char *)voxcomma;

			if( i >= CVOXWORDMAX )
				return i;
		}

		*psz++ = 0;

		for( ; *psz && ( *psz == '.' || *psz == ' ' || *psz == ',' );
		     psz++ );

		if( !*psz )
			return i;

		rgpparseword[i++] = psz;
	}

	return i;
}

static qboolean VOX_ParseWordParams( char *psz, voxword_t *pvoxword, qboolean fFirst )
{
	int len, i;
	char sznum[8], *pszsave = psz;
	static voxword_t voxwordDefault;

	if( fFirst )
	{
		voxwordDefault.fKeepCached = 0;
		voxwordDefault.pitch = -1;
		voxwordDefault.volume = 100;
		voxwordDefault.start = 0;
		voxwordDefault.end = 100;
		voxwordDefault.timecompress = 0;
	}

	*pvoxword = voxwordDefault;

	len = Q_strlen( psz );

	if( len == 0 )
		return false;

	// no special params
	if( psz[len-1] != ')' )
		return true;

	for( ; *psz != '(' && *psz != ')'; psz++ );

	// invalid syntax
	if( *psz == ')' )
		return false;

	// split filename and params
	*psz++ = '\0';

	for( ;; )
	{
		char command;

		// find command
		for( ; *psz &&
			*psz != 'v' &&
			*psz != 'p' &&
			*psz != 's' &&
			*psz != 'e' &&
			*psz != 't'; psz++ )
		{
			if( *psz == ')' )
				break;
		}

		command = *psz++;

		if( !isdigit( *psz ))
			break;

		memset( sznum, 0, sizeof( sznum ));
		for( i = 0; i < sizeof( sznum ) - 1 && isdigit( *psz ); i++, psz++ )
			sznum[i] = *psz;

		i = Q_atoi( sznum );
		switch( command )
		{
		case 'e': pvoxword->end = i; break;
		case 'p': pvoxword->pitch = i; break;
		case 's': pvoxword->start = i; break;
		case 't': pvoxword->timecompress = i; break;
		case 'v': pvoxword->volume = i; break;
		}
	}

	// no actual word but new defaults
	if( Q_strlen( pszsave ) == 0 )
	{
		voxwordDefault = *pvoxword;
		return false;
	}

	return true;
}

void VOX_LoadSound( channel_t *ch, const char *pszin )
{
	char buffer[512], szpath[32], pathbuffer[64];
	char *rgpparseword[CVOXWORDMAX];
	const char *psz;
	int i, j;

	if( !pszin )
		return;

	memset( buffer, 0, sizeof( buffer ));
	memset( rgpparseword, 0, sizeof( rgpparseword ));

	psz = VOX_LookupString( pszin );

	if( !psz )
	{
		Con_Printf( "VOX_LoadSound: no sentence named %s\n", pszin );
		return;
	}

	psz = VOX_GetDirectory( szpath, psz, sizeof( szpath ));

	if( !psz )
	{
		Con_Printf( "VOX_LoadSound: failed getting directory for %s\n", pszin );
		return;
	}

	if( Q_strlen( psz ) >= sizeof( buffer ) )
	{
		Con_Printf( "VOX_LoadSound: sentence is too long %s", psz );
		return;
	}

	Q_strncpy( buffer, psz, sizeof( buffer ));
	VOX_ParseString( buffer, rgpparseword );

	j = 0;
	for( i = 0; rgpparseword[i]; i++ )
	{
		if( !VOX_ParseWordParams( rgpparseword[i], &ch->words[j], i == 0 ))
			continue;

		Q_snprintf( pathbuffer, sizeof( pathbuffer ), "%s%s.wav", szpath, rgpparseword[i] );

		ch->words[j].sfx = S_FindName( pathbuffer, &ch->words[j].fKeepCached );

		j++;
	}

	ch->words[j].sfx = NULL;
	ch->sfx = ch->words[0].sfx;
	ch->wordIndex = 0;
	ch->isSentence = true;

	VOX_LoadWord( ch );
}

static void VOX_ReadSentenceFile_( byte *buf, fs_offset_t size )
{
	char *p, *last;

	p = (char *)buf;
	last = p + size;

	while( p < last )
	{
		char *name = NULL, *value = NULL;

		if( cszrawsentences >= CVOXFILESENTENCEMAX )
			break;

		for( ; p < last && ( *p == '\n' || *p == '\r' || *p == '\t' || *p == ' ' );
		     p++ );

		if( *p != '/' )
		{
			name = p;

			for( ; p < last && *p != ' ' && *p != '\t' ; p++ );

			if( p < last )
				*p++ = 0;

			value = p;
		}

		for( ; p < last && *p != '\n' && *p != '\r'; p++ );

		if( p < last )
			*p++ = 0;

		if( name )
		{
			int index = cszrawsentences;
			int size = strlen( name ) + strlen( value ) + 2;

			rgpszrawsentence[index] = Mem_Malloc( host.mempool, size );
			memcpy( rgpszrawsentence[index], name, size );
			rgpszrawsentence[index][size - 1] = 0;
			cszrawsentences++;
		}
	}
}

static void VOX_ReadSentenceFile( const char *path )
{
	byte *buf;
	fs_offset_t size;

	VOX_Shutdown();

	buf = FS_LoadFile( path, &size, false );
	if( !buf ) return;

	VOX_ReadSentenceFile_( buf, size );

	Mem_Free( buf );
}

void VOX_Init( void )
{
	VOX_ReadSentenceFile( DEFAULT_SOUNDPATH "sentences.txt" );
}

void VOX_Shutdown( void )
{
	int i;

	for( i = 0; i < cszrawsentences; i++ )
		Mem_Free( rgpszrawsentence[i] );

	cszrawsentences = 0;
}

#if XASH_ENGINE_TESTS
#include "tests.h"

static void Test_VOX_GetDirectory( void )
{
	const char *data[] =
	{
		"", "", "vox/",
		"bark bark", "bark bark", "vox/",
		"barney/meow", "meow", "barney/"

	};
	int i;

	for( i = 0; i < sizeof( data ) / sizeof( data[0] ); i += 3 )
	{
		string szpath;
		const char *p = VOX_GetDirectory( szpath, data[i+0], sizeof( szpath ));

		TASSERT_STR( p, data[i+1] );
		TASSERT_STR( szpath, data[i+2] );
	}
}

static void Test_VOX_LookupString( void )
{
	int i;
	const char *p, *data[] =
	{
		"0", "123",
		"3", "SPAAACE",
		"-2", NULL,
		"404", NULL,
		"not found", NULL,
		"exactmatch", "123",
		"caseinsensitive", "456",
		"SentenceWithTabs", "789",
		"SentenceWithSpaces", "SPAAACE",
	};

	VOX_Shutdown();

	rgpszrawsentence[cszrawsentences++] = (char*)"exactmatch\000123";
	rgpszrawsentence[cszrawsentences++] = (char*)"CaseInsensitive\000456";
	rgpszrawsentence[cszrawsentences++] = (char*)"SentenceWithTabs\0\t\t\t789";
	rgpszrawsentence[cszrawsentences++] = (char*)"SentenceWithSpaces\0  SPAAACE";
	rgpszrawsentence[cszrawsentences++] = (char*)"SentenceWithTabsAndSpaces\0\t \t\t MEOW";

	for( i = 0; i < sizeof( data ) / sizeof( data[0] ); i += 2 )
	{
		p = VOX_LookupString( data[i] );

		TASSERT_STR( p, data[i+1] );
	}

	cszrawsentences = 0;
}

static void Test_VOX_ParseString( void )
{
	char *rgpparseword[CVOXWORDMAX];
	const char *data[] =
	{
		"(p100) my ass is, heavy!(p80 t20) clik.",
		"(p100)", "my", "ass", "is", "_comma", "heavy!(p80 t20)", "clik", NULL,
		"freeman...",
		"freeman", "_period", NULL,
	};
	int i = 0;

	while( i < sizeof( data ) / sizeof( data[0] ))
	{
		char buffer[4096];
		int wordcount, j = 0;
		Q_strncpy( buffer, data[i], sizeof( buffer ));
		wordcount = VOX_ParseString( buffer, rgpparseword );

		i++;

		while( data[i] )
		{
			TASSERT_STR( data[i], rgpparseword[j] );
			i++;
			j++;
		}

		TASSERT( j == wordcount );

		i++;
	}
}

static void Test_VOX_ParseWordParams( void )
{
	string buffer;
	qboolean ret;
	voxword_t word;

	Q_strncpy( buffer, "heavy!(p80)", sizeof( buffer ));
	ret = VOX_ParseWordParams( buffer, &word, true );
	TASSERT_STR( buffer, "heavy!" );
	TASSERT( word.pitch == 80 );
	TASSERT( ret );

	Q_strncpy( buffer, "(p105)", sizeof( buffer ));
	ret = VOX_ParseWordParams( buffer, &word, false );
	TASSERT_STR( buffer, "" );
	TASSERT( word.pitch == 105 );
	TASSERT( !ret );

	Q_strncpy( buffer, "quiet(v50)", sizeof( buffer ));
	ret = VOX_ParseWordParams( buffer, &word, false );
	TASSERT_STR( buffer, "quiet" );
	TASSERT( word.pitch == 105 ); // defaulted
	TASSERT( word.volume == 50 );
	TASSERT( ret );
}

void Test_RunVOX( void )
{
	TRUN( Test_VOX_GetDirectory() );
	TRUN( Test_VOX_LookupString() );
	TRUN( Test_VOX_ParseString() );
	TRUN( Test_VOX_ParseWordParams() );
}

#endif /* XASH_ENGINE_TESTS */
