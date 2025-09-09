/*
common.c - misc functions used by dlls'
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if defined( ALLOCA_H )
#include ALLOCA_H
#endif
#include "common.h"
#include "studio.h"
#include "xash3d_mathlib.h"
#include "const.h"
#include "client.h"
#include "library.h"

static const char *const file_exts[] =
{
	// ban text files that don't make sense as resource
	"cfg", "lst", "ini", "log",

	// ban Windows code
	"exe", "vbs", "com", "bat",
	"dll", "sys", "ps1",

	// ban common unix code
	// NOTE: in unix anything can be executed as long it has access flag
	"so", "sh", "dylib",

	// ban mobile archives
	"apk", "ipa",
};

#ifdef _DEBUG
void DBG_AssertFunction( qboolean fExpr, const char* szExpr, const char* szFile, int szLine, const char* szMessage )
{
	if( fExpr ) return;

	if( szMessage != NULL )
		Con_DPrintf( S_ERROR "ASSERT FAILED:\n %s \n(%s@%d)\n%s\n", szExpr, szFile, szLine, szMessage );
	else Con_DPrintf( S_ERROR "ASSERT FAILED:\n %s \n(%s@%d)\n", szExpr, szFile, szLine );
}
#endif	// DEBUG

static int idum = 0;

#define MAX_RANDOM_RANGE	0x7FFFFFFFUL
#define IA		16807
#define IM		2147483647
#define IQ		127773
#define IR		2836
#define NTAB		32
#define EPS		1.2e-7
#define NDIV		(1 + (IM - 1) / NTAB)
#define AM		(1.0 / IM)
#define RNMX		(1.0 - EPS)

static int lran1( void )
{
	static int	iy = 0;
	static int	iv[NTAB];
	int		j;
	int		k;

	if( idum <= 0 || !iy )
	{
		if( -(idum) < 1 ) idum = 1;
		else idum = -(idum);

		for( j = NTAB + 7; j >= 0; j-- )
		{
			k = (idum) / IQ;
			idum = IA * (idum - k * IQ) - IR * k;
			if( idum < 0 ) idum += IM;
			if( j < NTAB ) iv[j] = idum;
		}

		iy = iv[0];
	}

	k = (idum) / IQ;
	idum = IA * (idum - k * IQ) - IR * k;
	if( idum < 0 ) idum += IM;
	j = iy / NDIV;
	iy = iv[j];
	iv[j] = idum;

	return iy;
}

// fran1 -- return a random floating-point number on the interval [0,1]
static float fran1( void )
{
	float temp = (float)AM * lran1();
	if( temp > RNMX )
		return (float)RNMX;
	return temp;
}

void GAME_EXPORT COM_SetRandomSeed( int lSeed )
{
	if( lSeed ) idum = lSeed;
	else idum = -time( NULL );

	if( 1000 < idum )
		idum = -idum;
	else if( -1000 < idum )
		idum -= 22261048;
}

float GAME_EXPORT COM_RandomFloat( float flLow, float flHigh )
{
	float	fl;

	if( idum == 0 ) COM_SetRandomSeed( 0 );

	fl = fran1(); // float in [0,1]
	return (fl * (flHigh - flLow)) + flLow; // float in [low, high)
}

int GAME_EXPORT COM_RandomLong( int lLow, int lHigh )
{
	dword	maxAcceptable;
	dword	n, x = lHigh - lLow + 1;

	if( idum == 0 ) COM_SetRandomSeed( 0 );

	if( x <= 0 || MAX_RANDOM_RANGE < x - 1 )
		return lLow;

	// The following maps a uniform distribution on the interval [0, MAX_RANDOM_RANGE]
	// to a smaller, client-specified range of [0,x-1] in a way that doesn't bias
	// the uniform distribution unfavorably. Even for a worst case x, the loop is
	// guaranteed to be taken no more than half the time, so for that worst case x,
	// the average number of times through the loop is 2. For cases where x is
	// much smaller than MAX_RANDOM_RANGE, the average number of times through the
	// loop is very close to 1.
	maxAcceptable = MAX_RANDOM_RANGE - ((MAX_RANDOM_RANGE + 1) % x );
	do
	{
		n = lran1();
	} while( n > maxAcceptable );

	return lLow + (n % x);
}

/*
============
va

does a varargs printf into a temp buffer,
so I don't need to have varargs versions
of all text functions.
============
*/
char *va( const char *format, ... )
{
	va_list		argptr;
	static char	string[16][MAX_VA_STRING], *s;
	static int	stringindex = 0;

	s = string[stringindex];
	stringindex = (stringindex + 1) & 15;
	va_start( argptr, format );
	Q_vsnprintf( s, sizeof( string[0] ), format, argptr );
	va_end( argptr );

	return s;
}

/*
===============================================================================

	LZSS Compression

===============================================================================
*/
#define LZSS_ID		(('S'<<24)|('S'<<16)|('Z'<<8)|('L'))
#define LZSS_LOOKSHIFT	4
#define LZSS_WINDOW_SIZE	4096
#define LZSS_LOOKAHEAD	BIT( LZSS_LOOKSHIFT )


typedef struct
{
	unsigned int	id;
	unsigned int	size;
} lzss_header_t;

// expected to be sixteen bytes
typedef struct lzss_node_s
{
	const byte	*data;
	struct lzss_node_s	*prev;
	struct lzss_node_s	*next;
	char		pad[4];
} lzss_node_t;

typedef struct
{
	lzss_node_t	*start;
	lzss_node_t	*end;
} lzss_list_t;

typedef struct
{
	lzss_list_t	*hash_table;
	lzss_node_t	*hash_node;
	int		window_size;
} lzss_state_t;

qboolean LZSS_IsCompressed( const byte *source, size_t input_len )
{
	const lzss_header_t *phdr;

	if( input_len <= sizeof( lzss_header_t ))
		return 0;

	phdr = (const lzss_header_t *)source;

	if( phdr && phdr->id == LZSS_ID )
		return true;
	return false;
}

uint LZSS_GetActualSize( const byte *source, size_t input_len )
{
	const lzss_header_t *phdr;

	if( input_len <= sizeof( lzss_header_t ))
		return 0;

	phdr = (const lzss_header_t *)source;

	if( phdr && phdr->id == LZSS_ID )
		return phdr->size;

	return 0;
}

static void LZSS_BuildHash( lzss_state_t *state, const byte *source )
{
	lzss_list_t	*list;
	lzss_node_t	*node;
	unsigned int	targetindex = (uintptr_t)source & ( state->window_size - 1 );

	node = &state->hash_node[targetindex];

	if( node->data )
	{
		list = &state->hash_table[*node->data];
		if( node->prev )
		{
			list->end = node->prev;
			node->prev->next = NULL;
		}
		else
		{
			list->start = NULL;
			list->end = NULL;
		}
	}

	list = &state->hash_table[*source];
	node->data = source;
	node->prev = NULL;
	node->next = list->start;
	if( list->start )
		list->start->prev = node;
	else list->end = node;
	list->start = node;
}

static byte *LZSS_CompressNoAlloc( lzss_state_t *state, byte *pInput, int input_length, byte *pOutputBuf, uint *pOutputSize )
{
	byte		*pStart = pOutputBuf; // allocate the output buffer, compressed buffer is expected to be less, caller will free
	byte		*pEnd = pStart + input_length - sizeof( lzss_header_t ) - 8; // prevent compression failure
	lzss_header_t	*header = (lzss_header_t *)pStart;
	byte		*pOutput = pStart + sizeof( lzss_header_t );
	const byte	*pEncodedPosition = NULL;
	byte		*pLookAhead = pInput;
	byte		*pWindow = pInput;
	int		i, putCmdByte = 0;
	byte		*pCmdByte = NULL;

	if( input_length <= sizeof( lzss_header_t ) + 8 )
		return NULL;

	// set LZSS header
	header->id = LZSS_ID;
	header->size = input_length;

	// create the compression work buffers, small enough (~64K) for stack
	state->hash_table = (lzss_list_t *)alloca( 256 * sizeof( lzss_list_t ));
	memset( state->hash_table, 0, 256 * sizeof( lzss_list_t ));
	state->hash_node = (lzss_node_t *)alloca( state->window_size * sizeof( lzss_node_t ));
	memset( state->hash_node, 0, state->window_size * sizeof( lzss_node_t ));

	while( input_length > 0 )
	{
		int		lookAheadLength = input_length < LZSS_LOOKAHEAD ? input_length : LZSS_LOOKAHEAD;
		lzss_node_t	*hash = state->hash_table[pLookAhead[0]].start;
		int		encoded_length = 0;

		pWindow = pLookAhead - state->window_size;

		if( pWindow < pInput )
			pWindow = pInput;

		if( !putCmdByte )
		{
			pCmdByte = pOutput++;
			*pCmdByte = 0;
		}

		putCmdByte = ( putCmdByte + 1 ) & 0x07;

		while( hash != NULL )
		{
			int	length = lookAheadLength;
			int	match_length = 0;

			while( length-- && hash->data[match_length] == pLookAhead[match_length] )
				match_length++;

			if( match_length > encoded_length )
			{
				encoded_length = match_length;
				pEncodedPosition = hash->data;
			}

			if( match_length == lookAheadLength )
				break;

			hash = hash->next;
		}

		if ( encoded_length >= 3 )
		{
			*pCmdByte = (*pCmdByte >> 1) | 0x80;
			*pOutput++ = (( pLookAhead - pEncodedPosition - 1 ) >> LZSS_LOOKSHIFT );
			*pOutput++ = (( pLookAhead - pEncodedPosition - 1 ) << LZSS_LOOKSHIFT ) | ( encoded_length - 1 );
		}
		else
		{
			*pCmdByte = ( *pCmdByte >> 1 );
			*pOutput++ = *pLookAhead;
			encoded_length = 1;
		}

		for( i = 0; i < encoded_length; i++ )
		{
			LZSS_BuildHash( state, pLookAhead++ );
		}

		input_length -= encoded_length;

		if( pOutput >= pEnd )
		{
			// compression is worse, abandon
			state->hash_table = NULL;
			state->hash_node = NULL;
			return NULL;
		}
	}

	if( input_length != 0 )
	{
		// unexpected failure
		Assert( 0 );
		state->hash_table = NULL;
		state->hash_node = NULL;
		return NULL;
	}

	if( !putCmdByte )
	{
		pCmdByte = pOutput++;
		*pCmdByte = 0x01;
	}
	else
	{
		*pCmdByte = (( *pCmdByte >> 1 ) | 0x80 ) >> ( 7 - putCmdByte );
	}

	// put two ints at end of buffer
	*pOutput++ = 0;
	*pOutput++ = 0;

	if( pOutputSize )
		*pOutputSize = pOutput - pStart;

	return pStart;
}

byte *LZSS_Compress( byte *pInput, int inputLength, uint *pOutputSize )
{
	byte *pStart = (byte *)malloc( inputLength );
	byte *pFinal = NULL;
	lzss_state_t state = { .window_size = LZSS_WINDOW_SIZE };

	if( !pStart )
		return NULL;

	pFinal = LZSS_CompressNoAlloc( &state, pInput, inputLength, pStart, pOutputSize );

	if( !pFinal )
	{
		free( pStart );
		return NULL;
	}

	return pStart;
}

uint LZSS_Decompress( const byte *pInput, byte *pOutput, size_t input_len, size_t output_len )
{
	uint	totalBytes = 0;
	int	getCmdByte = 0;
	int	cmdByte = 0;
	uint	actualSize;
	const byte *pInputEnd = pInput + input_len - 1; // thanks to nillerusr for the fix!
	byte *pOrigOutput = pOutput;

	if( input_len <= sizeof( lzss_header_t ))
		return 0;

	actualSize = LZSS_GetActualSize( pInput, input_len );

	if( !actualSize || actualSize > output_len )
		return 0;

	pInput += sizeof( lzss_header_t );

	while( 1 )
	{
		if( !getCmdByte )
		{
			if( pInput > pInputEnd )
				return 0;

			cmdByte = *pInput++;
		}
		getCmdByte = ( getCmdByte + 1 ) & 0x07;

		if( cmdByte & 0x01 )
		{
			int	position;
			int	i, count;
			byte	*pSource;

			if( pInput > pInputEnd )
				return 0;

			position = *pInput++ << LZSS_LOOKSHIFT;
			position |= ( *pInput >> LZSS_LOOKSHIFT );
			count = ( *pInput++ & 0x0F ) + 1;

			if( count == 1 )
				break;

			pSource = pOutput - position - 1;

			if( totalBytes + count > output_len || pSource < pOrigOutput )
				return 0;

			for( i = 0; i < count; i++ )
				*pOutput++ = *pSource++;
			totalBytes += count;
		}
		else
		{
			if( totalBytes + 1 > output_len || pInput > pInputEnd )
				return 0;

			*pOutput++ = *pInput++;
			totalBytes++;
		}
		cmdByte = cmdByte >> 1;
	}

	if( totalBytes != actualSize )
	{
		Assert( 0 );
		return 0;
	}
	return totalBytes;
}

/*
==============
COM_IsWhiteSpace

interpret symbol as whitespace
==============
*/

static int COM_IsWhiteSpace( char space )
{
	if( space == ' ' || space == '\t' || space == '\r' || space == '\n' )
		return 1;
	return 0;
}

/*
================
COM_ParseVector

================
*/
qboolean COM_ParseVector( char **pfile, float *v, size_t size )
{
	string	token;
	qboolean	bracket = false;
	char	*saved;
	uint	i;

	if( v == NULL || size == 0 )
		return false;

	memset( v, 0, sizeof( *v ) * size );

	if( size == 1 )
	{
		*pfile = COM_ParseFile( *pfile, token, sizeof( token ));
		v[0] = Q_atof( token );
		return true;
	}

	saved = *pfile;

	if(( *pfile = COM_ParseFile( *pfile, token, sizeof( token ))) == NULL )
		return false;

	if( token[0] == '(' )
		bracket = true;
	else *pfile = saved; // restore token to right get it again

	for( i = 0; i < size; i++ )
	{
		*pfile = COM_ParseFile( *pfile, token, sizeof( token ));
		v[i] = Q_atof( token );
	}

	if( !bracket ) return true;	// done

	if(( *pfile = COM_ParseFile( *pfile, token, sizeof( token ))) == NULL )
		return false;

	if( token[0] == ')' )
		return true;
	return false;
}

/*
=============
COM_FileSize

=============
*/
int GAME_EXPORT COM_FileSize( const char *filename )
{
	return FS_FileSize( filename, false );
}

/*
=============
COM_TrimSpace

trims all whitespace from the front
and end of a string
=============
*/
void COM_TrimSpace( const char *source, char *dest )
{
	int	start, end, length;

	start = 0;
	end = Q_strlen( source );

	while( source[start] && COM_IsWhiteSpace( source[start] ))
		start++;
	end--;

	while( end > 0 && COM_IsWhiteSpace( source[end] ))
		end--;
	end++;

	length = end - start;

	if( length > 0 )
		memcpy( dest, source + start, length );
	else length = 0;

	// terminate the dest string
	dest[length] = 0;
}

/*
==================
COM_Nibble

Returns the 4 bit nibble for a hex character
==================
*/
byte COM_Nibble( char c )
{
	if(( c >= '0' ) && ( c <= '9' ))
	{
		 return (byte)(c - '0');
	}

	if(( c >= 'A' ) && ( c <= 'F' ))
	{
		 return (byte)(c - 'A' + 0x0a);
	}

	if(( c >= 'a' ) && ( c <= 'f' ))
	{
		 return (byte)(c - 'a' + 0x0a);
	}

	return '0';
}

/*
==================
COM_HexConvert

Converts pszInput Hex string to nInputLength/2 binary
==================
*/
void COM_HexConvert( const char *pszInput, int nInputLength, byte *pOutput )
{
	const char	*pIn;
	byte		*p = pOutput;
	int		i;


	for( i = 0; i < nInputLength; i += 2 )
	{
		pIn = &pszInput[i];
		*p = COM_Nibble( pIn[0] ) << 4 | COM_Nibble( pIn[1] );
		p++;
	}
}

/*
=============
COM_MemFgets

=============
*/
char *GAME_EXPORT COM_MemFgets( byte *pMemFile, int fileSize, int *filePos, char *pBuffer, int bufferSize )
{
	int	i, last, stop;

	if( !pMemFile || !pBuffer || !filePos )
		return NULL;

	if( *filePos >= fileSize )
		return NULL;

	i = *filePos;
	last = fileSize;

	// fgets always NULL terminates, so only read bufferSize-1 characters
	if( last - *filePos > ( bufferSize - 1 ))
		last = *filePos + ( bufferSize - 1);

	stop = 0;

	// stop at the next newline (inclusive) or end of buffer
	while( i < last && !stop )
	{
		if( pMemFile[i] == '\n' )
			stop = 1;
		i++;
	}

	// if we actually advanced the pointer, copy it over
	if( i != *filePos )
	{
		// we read in size bytes
		int	size = i - *filePos;

		// copy it out
		memcpy( pBuffer, pMemFile + *filePos, size );

		// If the buffer isn't full, terminate (this is always true)
		if( size < bufferSize ) pBuffer[size] = 0;

		// update file pointer
		*filePos = i;
		return pBuffer;
	}

	return NULL;
}

/*
====================
Cache_Check

consistency check
====================
*/
void *GAME_EXPORT Cache_Check( poolhandle_t mempool, cache_user_t *c )
{
	if( !c->data )
		return NULL;

	if( !Mem_IsAllocatedExt( mempool, c->data ))
		return NULL;

	return c->data;
}

/*
=============
COM_LoadFileForMe

=============
*/
byte *GAME_EXPORT COM_LoadFileForMe( const char *filename, int *pLength )
{
	string	name;
	byte	*pfile;
	fs_offset_t	iLength;

	if( !COM_CheckString( filename ))
	{
		if( pLength )
			*pLength = 0;
		return NULL;
	}

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	pfile = g_fsapi.LoadFileMalloc( name, &iLength, false );
	if( pLength ) *pLength = (int)iLength;

	return pfile;
}

/*
=============
COM_LoadFile

=============
*/
byte *GAME_EXPORT COM_LoadFile( const char *filename, int usehunk, int *pLength )
{
	return COM_LoadFileForMe( filename, pLength );
}

/*
=============
COM_SaveFile

=============
*/
int GAME_EXPORT COM_SaveFile( const char *filename, const void *data, int len )
{
	// check for empty filename
	if( !COM_CheckString( filename ))
		return false;

	// check for null data
	if( !data || len <= 0 )
		return false;

	return FS_WriteFile( filename, data, len );
}

/*
=============
COM_FreeFile

=============
*/
void GAME_EXPORT COM_FreeFile( void *buffer )
{
	free( buffer );
}

/*
=============
pfnGetModelType

=============
*/
int GAME_EXPORT pfnGetModelType( model_t *mod )
{
	if( !mod ) return mod_bad;
	return mod->type;
}

/*
=============
pfnGetModelBounds

=============
*/
void GAME_EXPORT pfnGetModelBounds( model_t *mod, float *mins, float *maxs )
{
	if( mod )
	{
		if( mins ) VectorCopy( mod->mins, mins );
		if( maxs ) VectorCopy( mod->maxs, maxs );
	}
	else
	{
		if( mins ) VectorClear( mins );
		if( maxs ) VectorClear( maxs );
	}
}

/*
=============
pfnCVarGetPointer

can return NULL
=============
*/
cvar_t *GAME_EXPORT pfnCVarGetPointer( const char *szVarName )
{
	return (cvar_t *)Cvar_FindVar( szVarName );
}

/*
=============
pfnCompareFileTime

=============
*/
int GAME_EXPORT pfnCompareFileTime( const char *path1, const char *path2, int *retval )
{
	int t1, t2;
	*retval = 0;

	if( !path1 || !path2 )
		return 0;

	if(( t1 = g_fsapi.FileTime( path1, false )) == -1 )
		return 0;

	if(( t2 = g_fsapi.FileTime( path2, false )) == -1 )
		return 0;

	if( t1 < t2 )
		*retval = -1;
	else if( t1 > t2 )
		*retval = 1;

	return 1;
}

/*
=============
COM_CheckParm

=============
*/
int GAME_EXPORT COM_CheckParm( char *parm, char **ppnext )
{
	int	i = Sys_CheckParm( parm );

	if( ppnext )
	{
		if( i != 0 && i < host.argc - 1 )
			*ppnext = (char *)host.argv[i + 1];
		else *ppnext = NULL;
	}

	return i;
}

/*
=============
pfnTime

=============
*/
float GAME_EXPORT pfnTime( void )
{
	return (float)Sys_DoubleTime();
}

qboolean COM_IsSafeFileToDownload( const char *filename )
{
	char		lwrfilename[4096];
	const char	*last;
	const char	*ext;
	size_t	len;
	int		i;

	if( !COM_CheckString( filename ))
		return false;

	ext = COM_FileExtension( filename );
	len = Q_strlen( filename );

	// only allow extensionless files that start with !MD5
	if( !Q_strncmp( filename, "!MD5", 4 ))
	{
		if( COM_CheckStringEmpty( ext ))
			return false;

		len = Q_strlen( filename );

		if( len != 36 )
			return false;

		for( i = 4; i < len; i++ )
		{
			if(( filename[i] >= '0' && filename[i] <= '9' ) ||
				( filename[i] >= 'A' && filename[i] <= 'F' ))
				continue;

			return false;
		}

		return true;
	}

	for( i = 0; i < len; i++ )
	{
		if( !isprint( filename[i] ))
			return false;
	}

	Q_strnlwr( filename, lwrfilename, sizeof( lwrfilename ));
	ext = COM_FileExtension( lwrfilename );

	if( Q_strpbrk( lwrfilename, "\\:~" ) || Q_strstr( lwrfilename, ".." ))
		return false;

	if( lwrfilename[0] == '/' )
		return false;

	last = Q_strrchr( lwrfilename, '.' );

	if( last == NULL )
		return false;

	if( Q_strlen( last ) != 4 )
		return false;

	for( i = 0; i < ARRAYSIZE( file_exts ); i++ )
	{
		if( !Q_stricmp( ext, file_exts[i] ))
			return false;
	}

	return true;
}

char *_copystring( poolhandle_t mempool, const char *s, const char *filename, int fileline )
{
	size_t	size;
	char	*b;

	if( !s ) return NULL;
	if( !mempool ) mempool = host.mempool;

	size = Q_strlen( s ) + 1;
	b = _Mem_Alloc( mempool, size, false, filename, fileline );
	Q_strncpy( b, s, size );

	return b;
}

/*
======================

COMMON EXPORT STUBS

======================
*/


/*
=============
pfnSequenceGet

used by CS:CZ
=============
*/
void *GAME_EXPORT pfnSequenceGet( const char *fileName, const char *entryName )
{
	Msg( "%s: file %s, entry %s\n", __func__, fileName, entryName );

	return NULL;
}

/*
=============
pfnSequencePickSentence

used by CS:CZ
=============
*/
void *GAME_EXPORT pfnSequencePickSentence( const char *groupName, int pickMethod, int *picked )
{
	Msg( "%s: group %s, pickMethod %i\n", __func__, groupName, pickMethod );

	return NULL;

}

/*
=============
pfnIsCareerMatch

used by CS:CZ (client stub)
=============
*/
int GAME_EXPORT pfnIsCareerMatch( void )
{
	return 0;
}

/*
=============
pfnProcessTutorMessageDecayBuffer

only exists in PlayStation version
=============
*/
void GAME_EXPORT pfnProcessTutorMessageDecayBuffer( int *buffer, int bufferLength )
{
}

/*
=============
pfnConstructTutorMessageDecayBuffer

only exists in PlayStation version
=============
*/
void GAME_EXPORT pfnConstructTutorMessageDecayBuffer( int *buffer, int bufferLength )
{
}

/*
=============
pfnResetTutorMessageDecayData

only exists in PlayStation version
=============
*/
void GAME_EXPORT pfnResetTutorMessageDecayData( void )
{
}

#if XASH_ENGINE_TESTS

#include "tests.h"

#ifdef USE_ASAN
#include <sanitizer/asan_interface.h>
#endif

static void Test_LZSS( void )
{
	char poison1[8192];
	byte in[256];
	char poison2[8192];
	byte out[256];
	char poison3[8192];

	lzss_header_t *hdr = (lzss_header_t *)in;
	uint result;

	const byte compressed[] =
	{
		0x4c, 0x5a, 0x53, 0x53, 0x1a, 0x00, 0x00, 0x00, 0x00,
		0x44, 0x6f, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x6c, 0x00,
		0x69, 0x6b, 0x65, 0x20, 0x77, 0x68, 0x61, 0x74, 0x41,
		0x00, 0xd4, 0x73, 0x65, 0x65, 0x3f, 0x00, 0x00, 0x00,
	};
	const char decompressed[] = "Do you like what you see?";

#ifdef USING_ASAN
	ASAN_POISON_MEMORY_REGION( poison1, sizeof( poison1 ));
	ASAN_POISON_MEMORY_REGION( poison2, sizeof( poison2 ));
	ASAN_POISON_MEMORY_REGION( poison3, sizeof( poison3 ));
#endif

	hdr->size = sizeof( in ) - sizeof( *hdr );
	hdr->id = LZSS_ID;

	memset( in + sizeof( *hdr ), 0xff, sizeof( in ) - sizeof( *hdr ));
	result = LZSS_Decompress( in, out, sizeof( in ), sizeof( out ));
	TASSERT_EQi( result, 0 );

	memset( in + sizeof( *hdr ), 0x00, sizeof( in ) - sizeof( *hdr ));
	result = LZSS_Decompress( in, out, sizeof( in ), sizeof( out ));
	TASSERT_EQi( result, 0 );

	hdr->size = 1;
	hdr->id = LZSS_ID;
	result = LZSS_Decompress( in, out, sizeof( in ), sizeof( out ));
	TASSERT_EQi( result, 0 );

	hdr->size = 999;
	hdr->id = LZSS_ID;
	result = LZSS_Decompress( in, out, sizeof( in ), sizeof( out ));
	TASSERT_EQi( result, 0 );

	hdr->size = sizeof( in ) - sizeof( *hdr );
	hdr->id = 0xa1ba;
	result = LZSS_Decompress( in, out, sizeof( in ), sizeof( out ));
	TASSERT_EQi( result, 0 );

	result = LZSS_Decompress( compressed, out, sizeof( compressed ), sizeof( out ));
	TASSERT_EQi( result, 26 );
	TASSERT_STR( out, decompressed );
}

void Test_RunCommon( void )
{
	Msg( "Checking COM_IsSafeFileToDownload...\n" );

	TASSERT_EQi( COM_IsSafeFileToDownload( "models/bsg_props/[hl-lab.ru]bush.mdl" ), true );
	TASSERT_EQi( COM_IsSafeFileToDownload( "!MD5AAB5E8B307672DA86FBD10AC302BC732" ), true );
	TASSERT_EQi( COM_IsSafeFileToDownload( "!MD56f1ffd8c96bd64c9c27955309f6ecfe6" ), false );
	TASSERT_EQi( COM_IsSafeFileToDownload( "!MD5AAB5E8B307672DA86FBD10AC302B.exe" ), false );
	TASSERT_EQi( COM_IsSafeFileToDownload( "!MD5/../../valve/resource/GameMenu.res" ), false );
	TASSERT_EQi( COM_IsSafeFileToDownload( "not-a-virus-trust-me.bat" ), false );
	TASSERT_EQi( COM_IsSafeFileToDownload( "a-texture.png" ), true );

	Msg( "Checking LZSS_Decompress...\n" );
	Test_LZSS();
}
#endif
