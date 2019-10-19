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
#include <ctype.h>

sentence_t	g_Sentences[MAX_SENTENCES];
static uint	g_numSentences;
static char	*rgpparseword[CVOXWORDMAX];	// array of pointers to parsed words
static char	voxperiod[] = "_period";	// vocal pause
static char	voxcomma[] = "_comma";	// vocal pause

static int IsNextWord( const char c )
{
	if( c == '.' || c == ',' || c == ' ' || c == '(' )
		return 1;
	return 0;
}

static int IsSkipSpace( const char c )
{
	if( c == ',' || c == '.' || c == ' ' )
		return 1;
	return 0;
}

static int IsWhiteSpace( const char space )
{
	if( space == ' ' || space == '\t' || space == '\r' || space == '\n' )
		return 1;
	return 0;
}

static int IsCommandChar( const char c )
{
	if( c == 'v' || c == 'p' || c == 's' || c == 'e' || c == 't' )
		return 1;
	return 0;
}

static int IsDelimitChar( const char c )
{
	if( c == '(' || c == ')' )
		return 1;
	return 0;
}

static char *ScanForwardUntil( char *string, const char scan )
{
	while( string[0] )
	{
		if( string[0] == scan )
			return string;
		string++;
	}
	return string;
}

// backwards scan psz for last '/'
// return substring in szpath null terminated
// if '/' not found, return 'vox/'
static char *VOX_GetDirectory( char *szpath, char *psz )
{
	char	c;
	int	cb = 0;
	char	*p = psz + Q_strlen( psz ) - 1;

	// scan backwards until first '/' or start of string
	c = *p;
	while( p > psz && c != '/' )
	{
		c = *( --p );
		cb++;
	}

	if( c != '/' )
	{
		// didn't find '/', return default directory
		Q_strcpy( szpath, "vox/" );
		return psz;
	}

	cb = Q_strlen( psz ) - cb;
	memcpy( szpath, psz, cb );
	szpath[cb] = 0;

	return p + 1;
}

// scan g_Sentences, looking for pszin sentence name
// return pointer to sentence data if found, null if not
// CONSIDER: if we have a large number of sentences, should
// CONSIDER: sort strings in g_Sentences and do binary search.
char *VOX_LookupString( const char *pSentenceName, int *psentencenum )
{
	int	i;

	if( Q_isdigit( pSentenceName ) && (i = Q_atoi( pSentenceName )) < g_numSentences )
	{
		if( psentencenum ) *psentencenum = i;
		return (g_Sentences[i].pName + Q_strlen( g_Sentences[i].pName ) + 1 );		
	}

	for( i = 0; i < g_numSentences; i++ )
	{
		if( !Q_stricmp( pSentenceName, g_Sentences[i].pName ))
		{
			if( psentencenum ) *psentencenum = i;
			return (g_Sentences[i].pName + Q_strlen( g_Sentences[i].pName ) + 1 );
		}
	}

	return NULL;
}

// parse a null terminated string of text into component words, with
// pointers to each word stored in rgpparseword
// note: this code actually alters the passed in string!
char **VOX_ParseString( char *psz ) 
{
	int	i, fdone = 0;
	char	c, *p = psz;

	memset( rgpparseword, 0, sizeof( char* ) * CVOXWORDMAX );

	if( !psz ) return NULL;

	i = 0;
	rgpparseword[i++] = psz;

	while( !fdone && i < CVOXWORDMAX )
	{
		// scan up to next word
		c = *p;
		while( c && !IsNextWord( c ))
			c = *(++p);
			
		// if '(' then scan for matching ')'
		if( c == '(' )
		{
			p = ScanForwardUntil( p, ')' );
			c = *(++p);
			if( !c ) fdone = 1;
		}

		if( fdone || !c )
		{
			fdone = 1;
		}
		else
		{	
			// if . or , insert pause into rgpparseword,
			// unless this is the last character
			if(( c == '.' || c == ',' ) && *(p+1) != '\n' && *(p+1) != '\r' && *(p+1) != 0 )
			{
				if( c == '.' ) rgpparseword[i++] = voxperiod;
				else rgpparseword[i++] = voxcomma;

				if( i >= CVOXWORDMAX )
					break;
			}

			// null terminate substring
			*p++ = 0;

			// skip whitespace
			c = *p;
			while( c && IsSkipSpace( c ))
				c = *(++p);

			if( !c ) fdone = 1;
			else rgpparseword[i++] = p;
		}
	}

	return rgpparseword;
}

float VOX_GetVolumeScale( channel_t *pchan )
{
	if( pchan->currentWord )
	{
		if ( pchan->words[pchan->wordIndex].volume )
		{
			float	volume = pchan->words[pchan->wordIndex].volume * 0.01f;
			if( volume < 1.0f ) return volume;
		}
	}

	return 1.0f;
}

void VOX_SetChanVol( channel_t *ch )
{
	float	scale;

	if( !ch->currentWord )
		return;

	scale = VOX_GetVolumeScale( ch );
	if( scale == 1.0f ) return;

	ch->rightvol = (int)(ch->rightvol * scale);
	ch->leftvol = (int)(ch->leftvol * scale);
}

float VOX_ModifyPitch( channel_t *ch, float pitch )
{
	if( ch->currentWord )
	{
		if( ch->words[ch->wordIndex].pitch > 0 )
		{
			pitch += ( ch->words[ch->wordIndex].pitch - PITCH_NORM ) * 0.01f;
		}
	}

	return pitch;
}

//===============================================================================
//  Get any pitch, volume, start, end params into voxword
//  and null out trailing format characters
//  Format: 
//		someword(v100 p110 s10 e20)
//		
//		v is volume, 0% to n%
//		p is pitch shift up 0% to n%
//		s is start wave offset %
//		e is end wave offset %
//		t is timecompression %
//
//  pass fFirst == 1 if this is the first string in sentence
//  returns 1 if valid string, 0 if parameter block only.
//
//  If a ( xxx ) parameter block does not directly follow a word, 
//  then that 'default' parameter block will be used as the default value
//  for all following words.  Default parameter values are reset
//  by another 'default' parameter block.  Default parameter values
//  for a single word are overridden for that word if it has a parameter block.
// 
//===============================================================================
int VOX_ParseWordParams( char *psz, voxword_t *pvoxword, int fFirst ) 
{
	char		*pszsave = psz;
	char		c, ct, sznum[8];
	static voxword_t	voxwordDefault;
	int		i;
			
	// init to defaults if this is the first word in string.
	if( fFirst )
	{
		voxwordDefault.pitch = -1;
		voxwordDefault.volume = 100;
		voxwordDefault.start = 0;
		voxwordDefault.end = 100;
		voxwordDefault.fKeepCached = 0;
		voxwordDefault.timecompress = 0;
	}

	*pvoxword = voxwordDefault;

	// look at next to last char to see if we have a 
	// valid format:
	c = *( psz + Q_strlen( psz ) - 1 );

	// no formatting, return
	if( c != ')' ) return 1; 

	// scan forward to first '('
	c = *psz;
	while( !IsDelimitChar( c ))
		c = *(++psz);

	// bogus formatting
	if( c == ')' ) return 0;
	
	// null terminate
	*psz = 0;
	ct = *(++psz);

	while( 1 )
	{
		// scan until we hit a character in the commandSet
		while( ct && !IsCommandChar( ct ))
			ct = *(++psz);
		
		if( ct == ')' )
			break;

		memset( sznum, 0, sizeof( sznum ));
		i = 0;

		c = *(++psz);
		
		if( !isdigit( c ))
			break;

		// read number
		while( isdigit( c ) && i < sizeof( sznum ) - 1 )
		{
			sznum[i++] = c;
			c = *(++psz);
		}

		// get value of number
		i = Q_atoi( sznum );

		switch( ct )
		{
		case 'v': pvoxword->volume = i; break;
		case 'p': pvoxword->pitch = i; break;
		case 's': pvoxword->start = i; break;
		case 'e': pvoxword->end = i; break;
		case 't': pvoxword->timecompress = i; break;
		}

		ct = c;
	}

	// if the string has zero length, this was an isolated
	// parameter block.  Set default voxword to these
	// values
	if( Q_strlen( pszsave ) == 0 )
	{
		voxwordDefault = *pvoxword;
		return 0;
	}

	return 1;
}

void VOX_LoadWord( channel_t *pchan )
{
	if( pchan->words[pchan->wordIndex].sfx )
	{
		wavdata_t	*pSource = S_LoadSound( pchan->words[pchan->wordIndex].sfx );

		if( pSource )
		{
			int start = pchan->words[pchan->wordIndex].start;
			int end = pchan->words[pchan->wordIndex].end;

			// apply mixer
			pchan->currentWord = &pchan->pMixer;
			pchan->currentWord->pData = pSource; 
				
			// don't allow overlapped ranges
			if( end <= start ) end = 0;

			if( start || end )
			{
				int	sampleCount = pSource->samples;

				if( start )
				{
					S_SetSampleStart( pchan, pSource, (int)(sampleCount * 0.01f * start));
				}

				if( end )
				{
					S_SetSampleEnd( pchan, pSource, (int)(sampleCount * 0.01f * end));
				}
			}
		}
	}
}

void VOX_FreeWord( channel_t *pchan )
{
	pchan->currentWord = NULL; // sentence is finished
	memset( &pchan->pMixer, 0, sizeof( pchan->pMixer ));

	// release unused sounds
	if( pchan->words[pchan->wordIndex].sfx )
	{
		// If this wave wasn't precached by the game code
		if( !pchan->words[pchan->wordIndex].fKeepCached )
		{
			FS_FreeSound( pchan->words[pchan->wordIndex].sfx->cache );
			pchan->words[pchan->wordIndex].sfx->cache = NULL;
			pchan->words[pchan->wordIndex].sfx = NULL;
		}
	}
}

void VOX_LoadFirstWord( channel_t *pchan, voxword_t *pwords )
{
	int	i = 0;

	// copy each pointer in the sfx temp array into the
	// sentence array, and set the channel to point to the
	// sentence array
	while( pwords[i].sfx != NULL )
	{
		pchan->words[i] = pwords[i];
		i++;
	}		
	pchan->words[i].sfx = NULL;

	pchan->wordIndex = 0;
	VOX_LoadWord( pchan );
}

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

// link all sounds in sentence, start playing first word.
void VOX_LoadSound( channel_t *pchan, const char *pszin )
{
	char	buffer[512];
	int	i, cword;
	char	pathbuffer[64];
	char	szpath[32];
	voxword_t	rgvoxword[CVOXWORDMAX];
	char	*psz;

	if( !pszin || !*pszin )
		return;

	memset( rgvoxword, 0, sizeof( voxword_t ) * CVOXWORDMAX );
	memset( buffer, 0, sizeof( buffer ));

	// lookup actual string in g_Sentences, 
	// set pointer to string data
	psz = VOX_LookupString( pszin, NULL );

	if( !psz )
	{
		Con_DPrintf( S_ERROR "VOX_LoadSound: no such sentence %s\n", pszin );
		return;
	}

	// get directory from string, advance psz
	psz = VOX_GetDirectory( szpath, psz );

	if( Q_strlen( psz ) > sizeof( buffer ) - 1 )
	{
		Con_Printf( S_ERROR "VOX_LoadSound: sentence is too long %s\n", psz );
		return;
	}

	// copy into buffer
	Q_strcpy( buffer, psz );
	psz = buffer;

	// parse sentence (also inserts null terminators between words)
	VOX_ParseString( psz );

	// for each word in the sentence, construct the filename,
	// lookup the sfx and save each pointer in a temp array	

	i = 0;
	cword = 0;
	while( rgpparseword[i] )
	{
		// Get any pitch, volume, start, end params into voxword
		if( VOX_ParseWordParams( rgpparseword[i], &rgvoxword[cword], i == 0 ))
		{
			// this is a valid word (as opposed to a parameter block)
			Q_strcpy( pathbuffer, szpath );
			Q_strncat( pathbuffer, rgpparseword[i], sizeof( pathbuffer ));
			Q_strncat( pathbuffer, ".wav", sizeof( pathbuffer ));

			// find name, if already in cache, mark voxword
			// so we don't discard when word is done playing
			rgvoxword[cword].sfx = S_FindName( pathbuffer, &( rgvoxword[cword].fKeepCached ));
			cword++;
		}
		i++;
	}

	VOX_LoadFirstWord( pchan, rgvoxword );

	pchan->isSentence = true;
	pchan->sfx = rgvoxword[0].sfx;
}

//-----------------------------------------------------------------------------
// Purpose: Take a NULL terminated sentence, and parse any commands contained in
//			{}.  The string is rewritten in place with those commands removed.
//
// Input  : *pSentenceData - sentence data to be modified in place
//			sentenceIndex - global sentence table index for any data that is 
//							parsed out
//-----------------------------------------------------------------------------
void VOX_ParseLineCommands( char *pSentenceData, int sentenceIndex )
{
	char	tempBuffer[512];
	char	*pNext, *pStart;
	int	length, tempBufferPos = 0;

	if( !pSentenceData )
		return;

	pStart = pSentenceData;

	while( *pSentenceData )
	{
		pNext = ScanForwardUntil( pSentenceData, '{' );

		// find length of "good" portion of the string (not a {} command)
		length = pNext - pSentenceData;
		if( tempBufferPos + length > sizeof( tempBuffer ))
		{
			Con_Printf( S_ERROR "Sentence too long (max length %lu characters)\n", sizeof(tempBuffer) - 1 );
			return;
		}

		// Copy good string to temp buffer
		memcpy( tempBuffer + tempBufferPos, pSentenceData, length );
		
		// move the copy position
		tempBufferPos += length;

		pSentenceData = pNext;
		
		// skip ahead of the opening brace
		if( *pSentenceData ) pSentenceData++;
		
		// skip whitespace
		while( *pSentenceData && *pSentenceData <= 32 )
			pSentenceData++;

		// simple comparison of string commands:
		switch( Q_tolower( *pSentenceData ))
		{
		case 'l':
			// all commands starting with the letter 'l' here
			if( !Q_strnicmp( pSentenceData, "len", 3 ))
			{
				g_Sentences[sentenceIndex].length = Q_atof( pSentenceData + 3 );
			}
			break;
		case 0:
		default:
			break;
		}

		pSentenceData = ScanForwardUntil( pSentenceData, '}' );
		
		// skip the closing brace
		if( *pSentenceData ) pSentenceData++;

		// skip trailing whitespace
		while( *pSentenceData && *pSentenceData <= 32 )
			pSentenceData++;
	}

	if( tempBufferPos < sizeof( tempBuffer ))
	{
		// terminate cleaned up copy
		tempBuffer[tempBufferPos] = 0;
		
		// copy it over the original data
		Q_strcpy( pStart, tempBuffer );
	}
}

// Load sentence file into memory, insert null terminators to
// delimit sentence name/sentence pairs.  Keep pointer to each
// sentence name so we can search later.
void VOX_ReadSentenceFile( const char *psentenceFileName )
{
	char	c, *pch, *pFileData;
	char	*pchlast, *pSentenceData;
	fs_offset_t	fileSize;

	// load file
	pFileData = (char *)FS_LoadFile( psentenceFileName, &fileSize, false );
	if( !pFileData ) return; // this game just doesn't used vox sound system

	pch = pFileData;
	pchlast = pch + fileSize;

	while( pch < pchlast )
	{
		if( g_numSentences >= MAX_SENTENCES )
		{
			Con_Printf( S_ERROR "VOX_Init: too many sentences specified, max is %d\n", MAX_SENTENCES );
			break;
		}

		// only process this pass on sentences
		pSentenceData = NULL;

		// skip newline, cr, tab, space

		c = *pch;
		while( pch < pchlast && IsWhiteSpace( c ))
			c = *(++pch);

		// skip entire line if first char is /
		if( *pch != '/' )
		{
			sentence_t *pSentence = &g_Sentences[g_numSentences++];

			pSentence->pName = pch;
			pSentence->length = 0;

			// scan forward to first space, insert null terminator
			// after sentence name

			c = *pch;
			while( pch < pchlast && c != ' ' )
				c = *(++pch);

			if( pch < pchlast )
				*pch++ = 0;

			// a sentence may have some line commands, make an extra pass
			pSentenceData = pch;
		}

		// scan forward to end of sentence or eof
		while( pch < pchlast && pch[0] != '\n' && pch[0] != '\r' )
			pch++;
	
		// insert null terminator
		if( pch < pchlast ) *pch++ = 0;

		// If we have some sentence data, parse out any line commands
		if( pSentenceData && pSentenceData < pchlast )
		{
			int	index = g_numSentences - 1;

			// the current sentence has an index of count-1
			VOX_ParseLineCommands( pSentenceData, index );
		}
	}
}

void VOX_Init( void )
{
	memset( g_Sentences, 0, sizeof( g_Sentences ));
	g_numSentences = 0;

	VOX_ReadSentenceFile( DEFAULT_SOUNDPATH "sentences.txt" );
}


void VOX_Shutdown( void )
{
	g_numSentences = 0;
}
