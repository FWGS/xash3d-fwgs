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

#include "common.h"
#include "studio.h"
#include "xash3d_mathlib.h"
#include "const.h"
#include "client.h"
#include "library.h"
#include "sequence.h"

static const char *file_exts[] =
{
	"cfg",
	"lst",
	"exe",
	"vbs",
	"com",
	"bat",
	"dll",
	"ini",
	"log",
	"sys",
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

void COM_SetRandomSeed( int lSeed )
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

qboolean LZSS_IsCompressed( const byte *source )
{
	lzss_header_t	*phdr = (lzss_header_t *)source;

	if( phdr && phdr->id == LZSS_ID )
		return true;
	return false;
}

uint LZSS_GetActualSize( const byte *source )
{
	lzss_header_t	*phdr = (lzss_header_t *)source;

	if( phdr && phdr->id == LZSS_ID )
		return phdr->size;
	return 0;
}

static void LZSS_BuildHash( lzss_state_t *state, const byte *source )
{
	lzss_list_t	*list;
	lzss_node_t	*node;
	unsigned int	targetindex = (uint)source & ( state->window_size - 1 );

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

byte *LZSS_CompressNoAlloc( lzss_state_t *state, byte *pInput, int input_length, byte *pOutputBuf, uint *pOutputSize )
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
			return NULL;
		}
	}

	if( input_length != 0 )
	{
		// unexpected failure
		Assert( 0 );
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
	byte		*pStart = (byte *)malloc( inputLength );
	byte		*pFinal = NULL;
	lzss_state_t	state;

	memset( &state, 0, sizeof( state ));
	state.window_size = LZSS_WINDOW_SIZE;

	pFinal = LZSS_CompressNoAlloc( &state, pInput, inputLength, pStart, pOutputSize );

	if( !pFinal )
	{
		free( pStart );
		return NULL;
	}

	return pStart;
}

uint LZSS_Decompress( const byte *pInput, byte *pOutput )
{
	uint	totalBytes = 0;
	int	getCmdByte = 0;
	int	cmdByte = 0;
	uint	actualSize = LZSS_GetActualSize( pInput );

	if( !actualSize )
		return 0;

	pInput += sizeof( lzss_header_t );

	while( 1 )
	{
		if( !getCmdByte ) 
			cmdByte = *pInput++;
		getCmdByte = ( getCmdByte + 1 ) & 0x07;

		if( cmdByte & 0x01 )
		{
			int	position = *pInput++ << LZSS_LOOKSHIFT;
			int	i, count;
			byte	*pSource;

			position |= ( *pInput >> LZSS_LOOKSHIFT );
			count = ( *pInput++ & 0x0F ) + 1;

			if( count == 1 ) 
				break;

			pSource = pOutput - position - 1;
			for( i = 0; i < count; i++ )
				*pOutput++ = *pSource++;
			totalBytes += count;
		} 
		else 
		{
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
COM_IsSingleChar

interpert this character as single
==============
*/
static int COM_IsSingleChar( char c )
{
	if( c == '{' || c == '}' || c == '\'' || c == ',' )
		return true;

	if( !host.com_ignorebracket && ( c == ')' || c == '(' ))
		return true;

	if( host.com_handlecolon && c == ':' )
		return true;

	return false;
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
==============
COM_ParseFile

text parser
==============
*/
char *COM_ParseFile( char *data, char *token )
{
	int	c, len;

	if( !token )
		return NULL;
	
	len = 0;
	token[0] = 0;
	
	if( !data )
		return NULL;
// skip whitespace
skipwhite:
	while(( c = ((byte)*data)) <= ' ' )
	{
		if( c == 0 )
			return NULL;	// end of file;
		data++;
	}
	
	// skip // comments
	if( c=='/' && data[1] == '/' )
	{
		while( *data && *data != '\n' )
			data++;
		goto skipwhite;
	}

	// handle quoted strings specially
	if( c == '\"' )
	{
		data++;
		while( 1 )
		{
			c = (byte)*data;

			// unexpected line end
			if( !c )
			{
				token[len] = 0;
				return data;
			}
			data++;

			if( c == '\"' )
			{
				token[len] = 0;
				return data;
			}
			token[len] = c;
			len++;
		}
	}

	// parse single characters
	if( COM_IsSingleChar( c ))
	{
		token[len] = c;
		len++;
		token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		token[len] = c;
		data++;
		len++;
		c = ((byte)*data);

		if( COM_IsSingleChar( c ))
			break;
	} while( c > 32 );
	
	token[len] = 0;

	return data;
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
		*pfile = COM_ParseFile( *pfile, token );
		v[0] = Q_atof( token );
		return true;
	}

	saved = *pfile;

	if(( *pfile = COM_ParseFile( *pfile, token )) == NULL )
		return false;

	if( token[0] == '(' )
		bracket = true;
	else *pfile = saved; // restore token to right get it again

	for( i = 0; i < size; i++ )
	{
		*pfile = COM_ParseFile( *pfile, token );
		v[i] = Q_atof( token );
	}

	if( !bracket ) return true;	// done

	if(( *pfile = COM_ParseFile( *pfile, token )) == NULL )
		return false;

	if( token[0] == ')' )
		return true;
	return false;
}

/*
=============
COM_CheckString

=============
*/
#if 0
int COM_CheckString( const char *string )
{
	if( !string || !*string )
		return 0;
	return 1;
}
#endif

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
COM_AddAppDirectoryToSearchPath

=============
*/
void GAME_EXPORT COM_AddAppDirectoryToSearchPath( const char *pszBaseDir, const char *appName )
{
	FS_AddGameHierarchy( pszBaseDir, FS_NOWRITE_PATH );
}

/*
===========
COM_ExpandFilename

Finds the file in the search path, copies over the name with the full path name.
This doesn't search in the pak file.
===========
*/
int GAME_EXPORT COM_ExpandFilename( const char *fileName, char *nameOutBuffer, int nameOutBufferSize )
{
	const char	*path;
	char		result[MAX_SYSPATH];

	if( !COM_CheckString( fileName ) || !nameOutBuffer || nameOutBufferSize <= 0 )
		return 0;

	// filename examples:
	// media\sierra.avi - D:\Xash3D\valve\media\sierra.avi
	// models\barney.mdl - D:\Xash3D\bshift\models\barney.mdl
	if(( path = FS_GetDiskPath( fileName, false )) != NULL )
	{
		Q_sprintf( result, "%s/%s", host.rootdir, path );		

		// check for enough room
		if( Q_strlen( result ) > nameOutBufferSize )
			return 0;

		Q_strncpy( nameOutBuffer, result, nameOutBufferSize );
		return 1;
	}
	return 0;
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
============
COM_FixSlashes

Changes all '/' characters into '\' characters, in place.
============
*/
void COM_FixSlashes( char *pname )
{
	while( *pname )
	{
		if( *pname == '\\' )
			*pname = '/';
		pname++;
	}
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
char *COM_MemFgets( byte *pMemFile, int fileSize, int *filePos, char *pBuffer, int bufferSize )
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
void *Cache_Check( byte *mempool, cache_user_t *c )
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
byte* GAME_EXPORT COM_LoadFileForMe( const char *filename, int *pLength )
{
	string	name;
	byte	*file, *pfile;
	fs_offset_t	iLength;

	if( !COM_CheckString( filename ))
	{
		if( pLength )
			*pLength = 0;
		return NULL;
	}

	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );

	pfile = FS_LoadFile( name, &iLength, false );
	if( pLength ) *pLength = (int)iLength;

	if( pfile )
	{
		file = malloc( iLength + 1 );
		if( file != NULL )
		{
			memcpy( file, pfile, iLength );
			file[iLength] = '\0';
		}
		Mem_Free( pfile );
		pfile = file;
	}

	return pfile;
}

/*
=============
COM_LoadFile

=============
*/
byte *COM_LoadFile( const char *filename, int usehunk, int *pLength )
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
COM_NormalizeAngles

=============
*/
void COM_NormalizeAngles( vec3_t angles )
{
	int i;

	for( i = 0; i < 3; i++ )
	{
		if( angles[i] > 180.0f )
			angles[i] -= 360.0f;
		else if( angles[i] < -180.0f )
			angles[i] += 360.0f;
	}
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
pfnCvar_RegisterServerVariable

standard path to register game variable
=============
*/
void GAME_EXPORT pfnCvar_RegisterServerVariable( cvar_t *variable )
{
	if( variable != NULL )
		SetBits( variable->flags, FCVAR_EXTDLL );
	Cvar_RegisterVariable( (convar_t *)variable );
}

/*
=============
pfnCvar_RegisterEngineVariable

use with precaution: this cvar will NOT unlinked
after game.dll is unloaded
=============
*/
void GAME_EXPORT pfnCvar_RegisterEngineVariable( cvar_t *variable )
{
	Cvar_RegisterVariable( (convar_t *)variable );
}

/*
=============
pfnCvar_RegisterVariable

=============
*/
cvar_t *pfnCvar_RegisterClientVariable( const char *szName, const char *szValue, int flags )
{
	if( FBitSet( flags, FCVAR_GLCONFIG ))
		return (cvar_t *)Cvar_Get( szName, szValue, flags, va( CVAR_GLCONFIG_DESCRIPTION, szName ));
	return (cvar_t *)Cvar_Get( szName, szValue, flags|FCVAR_CLIENTDLL, Cvar_BuildAutoDescription( flags|FCVAR_CLIENTDLL ));
}

/*
=============
pfnCvar_RegisterVariable

=============
*/
cvar_t *pfnCvar_RegisterGameUIVariable( const char *szName, const char *szValue, int flags )
{
	if( FBitSet( flags, FCVAR_GLCONFIG ))
		return (cvar_t *)Cvar_Get( szName, szValue, flags, va( CVAR_GLCONFIG_DESCRIPTION, szName ));
	return (cvar_t *)Cvar_Get( szName, szValue, flags|FCVAR_GAMEUIDLL, Cvar_BuildAutoDescription( flags|FCVAR_GAMEUIDLL ));
}

/*
=============
pfnCVarGetPointer

can return NULL
=============
*/
cvar_t *pfnCVarGetPointer( const char *szVarName )
{
	return (cvar_t *)Cvar_FindVar( szVarName );
}

/*
=============
pfnCVarDirectSet

allow to set cvar directly
=============
*/
void GAME_EXPORT pfnCVarDirectSet( cvar_t *var, const char *szValue )
{
	Cvar_DirectSet( (convar_t *)var, szValue );
}

/*
=============
COM_CompareFileTime

=============
*/
int GAME_EXPORT COM_CompareFileTime( const char *filename1, const char *filename2, int *iCompare )
{
	int	bRet = 0;

	*iCompare = 0;

	if( filename1 && filename2 )
	{
		int ft1 = FS_FileTime( filename1, false );
		int ft2 = FS_FileTime( filename2, false );

		// one of files is missing
		if( ft1 == -1 || ft2 == -1 )
			return bRet;

		*iCompare = Host_CompareFileTime( ft1,  ft2 );
		bRet = 1;
	}

	return bRet;
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

/*
=============
pfnGetGameDir

=============
*/
void GAME_EXPORT pfnGetGameDir( char *szGetGameDir )
{
	if( !szGetGameDir ) return;
	Q_strcpy( szGetGameDir, GI->gamefolder );
}

qboolean COM_IsSafeFileToDownload( const char *filename )
{
	char		lwrfilename[4096];
	const char	*first, *last;
	const char	*ext;
	int		i;

	if( !COM_CheckString( filename ))
		return false;

	if( !Q_strncmp( filename, "!MD5", 4 ))
		return true;

	Q_strnlwr( filename, lwrfilename, sizeof( lwrfilename ));

	if( Q_strstr( lwrfilename, "\\" ) || Q_strstr( lwrfilename, ":" ) || Q_strstr( lwrfilename, ".." ) || Q_strstr( lwrfilename, "~" ))
		return false;

	if( lwrfilename[0] == '/' )
		return false;

	first = Q_strchr( lwrfilename, '.' );
	last = Q_strrchr( lwrfilename, '.' );

	if( first == NULL || last == NULL )
		return false;

	if( first != last )
		return false;

	if( Q_strlen( first ) != 4 )
		return false;

	ext = COM_FileExtension( lwrfilename );

	for( i = 0; i < ARRAYSIZE( file_exts ); i++ )
	{
		if( !Q_stricmp( ext, file_exts[i] ))
			return false;
	}

	return true;
}

char *_copystring( byte *mempool, const char *s, const char *filename, int fileline )
{
	char	*b;

	if( !s ) return NULL;
	if( !mempool ) mempool = host.mempool;

	b = _Mem_Alloc( mempool, Q_strlen( s ) + 1, false, filename, fileline );
	Q_strcpy( b, s );

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
	Msg( "Sequence_Get: file %s, entry %s\n", fileName, entryName );


	return Sequence_Get( fileName, entryName );
}

/*
=============
pfnSequencePickSentence

used by CS:CZ
=============
*/
void *GAME_EXPORT pfnSequencePickSentence( const char *groupName, int pickMethod, int *picked )
{
	Msg( "Sequence_PickSentence: group %s, pickMethod %i\n", groupName, pickMethod );

	return  Sequence_PickSentence( groupName, pickMethod, picked );

}

/*
=============
pfnIsCareerMatch

used by CS:CZ (client stub)
=============
*/
int GAME_EXPORT GAME_EXPORT pfnIsCareerMatch( void )
{
	return 0;
}

/*
=============
pfnRegisterTutorMessageShown

only exists in PlayStation version
=============
*/
void GAME_EXPORT pfnRegisterTutorMessageShown( int mid )
{
}

/*
=============
pfnGetTimesTutorMessageShown

only exists in PlayStation version
=============
*/
int GAME_EXPORT pfnGetTimesTutorMessageShown( int mid )
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
