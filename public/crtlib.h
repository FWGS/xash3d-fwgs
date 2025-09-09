/*
crtlib.h - internal stdlib
Copyright (C) 2011 Uncle Mike
Copyright (C) Free Software Foundation, Inc

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef STDLIB_H
#define STDLIB_H

#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "build.h"
#include "xash3d_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

// timestamp modes
enum
{
	TIME_FULL = 0,
	TIME_DATE_ONLY,
	TIME_TIME_ONLY,
	TIME_NO_SECONDS,
	TIME_YEAR_ONLY,
	TIME_FILENAME,
};

// a1ba: not using BIT macro, so flags can be copypasted into
// exported APIs headers and will not get warning in case of changing values
#define PFILE_IGNOREBRACKET (1<<0)
#define PFILE_HANDLECOLON   (1<<1)
#define PFILE_IGNOREHASHCMT (1<<2)

#define PFILE_TOKEN_MAX_LENGTH 1024
#define PFILE_FS_TOKEN_MAX_LENGTH 512

#ifdef __cplusplus
#define restrict
#endif // __cplusplus

//
// build.c
//
int Q_buildnum( void );
int Q_buildnum_iso( const char *date );
int Q_buildnum_compat( void );
const char *Q_PlatformStringByID( const int platform );
const char *Q_buildos( void );
const char *Q_ArchitectureStringByID( const int arch, const uint abi, const int endianness, const qboolean is64 );
const char *Q_buildarch( void );
extern const char *g_buildcommit;
extern const char *g_buildbranch;
extern const char *g_build_date;
extern const char *g_buildcommit_date;

//
// crtlib.c
//
void Q_strnlwr( const char *in, char *out, size_t size_out );
#define Q_strlen( str ) (( str ) ? strlen(( str )) : 0 )
int Q_atoi_hex( int sign, const char *str );
int Q_atoi( const char *str );
float Q_atof( const char *str );
void Q_atov( float *vec, const char *str, size_t siz );
#define Q_strchr  strchr
#define Q_strrchr strrchr
qboolean Q_stricmpext( const char *pattern, const char *text );
qboolean Q_strnicmpext( const char *pattern, const char *text, size_t minimumlen );
const byte *Q_memmem( const byte *haystack, size_t haystacklen, const byte *needle, size_t needlelen );
void Q_memor( byte *XASH_RESTRICT dst, const byte *XASH_RESTRICT src, size_t len );
const char *Q_timestamp( int format ) RETURNS_NONNULL;
int Q_vsnprintf( char *buffer, size_t buffersize, const char *format, va_list args );
int Q_snprintf( char *buffer, size_t buffersize, const char *format, ... ) FORMAT_CHECK( 3 );
#define Q_strpbrk strpbrk
void COM_StripColors( const char *in, char *out );
#define Q_memprint( val ) Q_pretifymem( val, 2 )
char *Q_pretifymem( float value, int digitsafterdecimal );
void COM_FileBase( const char *in, char *out, size_t size );
const char *COM_FileExtension( const char *in ) RETURNS_NONNULL;
void COM_DefaultExtension( char *path, const char *extension, size_t size );
void COM_ReplaceExtension( char *path, const char *extension, size_t size );
void COM_ExtractFilePath( const char *path, char *dest );
const char *COM_FileWithoutPath( const char *in );
void COM_StripExtension( char *path );
void COM_RemoveLineFeed( char *str, size_t bufsize );
void COM_PathSlashFix( char *path );
// return 0 on empty or null string, 1 otherwise
#define COM_CheckString( string ) ( ( !string || !*string ) ? 0 : 1 )
#define COM_CheckStringEmpty( string ) ( ( !*string ) ? 0 : 1 )
char *COM_ParseFileSafe( char *data, char *token, const int size, unsigned int flags, int *len, qboolean *quoted );
#define COM_ParseFile( data, token, size ) COM_ParseFileSafe( data, token, size, 0, NULL, NULL )
int matchpattern( const char *in, const char *pattern, qboolean caseinsensitive );
int matchpattern_with_separator( const char *in, const char *pattern, qboolean caseinsensitive, const char *separators, qboolean wildcard_least_one );

//
// dllhelpers.c
//
typedef struct dllfunc_s
{
	const char	*name;
	void		**func;
} dllfunc_t;

void ClearExports( const dllfunc_t *funcs, size_t num_funcs );
qboolean ValidateExports( const dllfunc_t *funcs, size_t num_funcs );

static inline char Q_toupper( const char in )
{
	char	out;

	if( in >= 'a' && in <= 'z' )
		out = in + 'A' - 'a';
	else out = in;

	return out;
}

static inline char Q_tolower( const char in )
{
	char	out;

	if( in >= 'A' && in <= 'Z' )
		out = in + 'a' - 'A';
	else out = in;

	return out;
}

static inline qboolean Q_istype( const char *str, int (*istype)( int c ))
{
	if( likely( str && *str ))
	{
		while( istype( *str )) str++;
		if( !*str ) return true;
	}
	return false;
}

static inline qboolean Q_isdigit( const char *str )
{
	return Q_istype( str, isdigit );
}

static inline qboolean Q_isalpha( const char *str )
{
	return Q_istype( str, isalpha );
}

static inline qboolean Q_isspace( const char *str )
{
	return Q_istype( str, isspace );
}

static inline int Q_strcmp( const char *s1, const char *s2 )
{
	if( likely( s1 && s2 ))
		return strcmp( s1, s2 );
	return ( s1 ? 1 : 0 ) - ( s2 ? 1 : 0 );
}

static inline int Q_strncmp( const char *s1, const char *s2, size_t n )
{
	if( likely( s1 && s2 ))
		return strncmp( s1, s2, n );
	return ( s1 ? 1 : 0 ) - ( s2 ? 1 : 0 );
}

static inline char *Q_strstr( const char *s1, const char *s2 )
{
	if( likely( s1 && s2 ))
		return (char *)strstr( s1, s2 );
	return NULL;
}

// libc extensions, be careful what to enable or what not
static inline size_t Q_strnlen( const char *str, size_t size )
{
#if HAVE_STRNLEN
	return strnlen( str, size );
#else
	const char *p = (const char *)memchr( str, 0, size );
	return p ? p - str : size;
#endif
}

static inline size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	if( unlikely( !dst || !src || !size ))
		return 0;
#if HAVE_STRLCPY
	return strlcpy( dst, src, size );
#else
	{
		size_t len = strlen( src );

		if( len >= size ) // check if truncate
		{
			memcpy( dst, src, size );
			dst[size - 1] = 0;
		}
		else memcpy( dst, src, len + 1 );

		return len; // count does not include NULL
	}
#endif
}

static inline size_t Q_strncat( char *dst, const char *src, size_t size )
{
	if( unlikely( !dst || !src || !size ))
		return 0;
#if HAVE_STRLCAT
	return strlcat( dst, src, size );
#else
	{
		size_t slen = strlen( src );
		size_t dlen = Q_strnlen( dst, size );

		if( dlen != size )
		{
			size_t copy = size - dlen - 1;

			if( copy > slen )
				copy = slen;

			memcpy( &dst[dlen], src, copy );
			dst[dlen + copy] = 0;
		}

		return dlen + slen;
	}
#endif
}

#if HAVE_STRICMP || HAVE_STRCASECMP
static inline int Q_stricmp( const char *s1, const char *s2 )
{
	if( likely( s1 && s2 ))
	{
#if HAVE_STRICMP
		return stricmp( s1, s2 );
#elif HAVE_STRCASECMP
		return strcasecmp( s1, s2 );
#endif
	}
	return ( s1 ? 1 : 0 ) - ( s2 ? 1 : 0 );
}
#else
int Q_stricmp( const char *s1, const char *s2 );
#endif

#if HAVE_STRICMP || HAVE_STRCASECMP
static inline int Q_strnicmp( const char *s1, const char *s2, size_t n )
{
	if( likely( s1 && s2 ))
	{
#if HAVE_STRICMP
		return strnicmp( s1, s2, n );
#elif HAVE_STRCASECMP
		return strncasecmp( s1, s2, n );
#endif
	}
	return ( s1 ? 1 : 0 ) - ( s2 ? 1 : 0 );
}
#else
int Q_strnicmp( const char *s1, const char *s2, size_t n );
#endif


#if HAVE_STRCASESTR
static inline char *Q_stristr( const char *s1, const char *s2 )
{
	if( likely( s1 && s2 ))
		return (char *)strcasestr( s1, s2 );
	return NULL;
}
#else // !HAVE_STRCASESTR
char *Q_stristr( const char *s1, const char *s2 );
#endif // !HAVE_STRCASESTR

#if HAVE_STRCHRNUL
#define Q_strchrnul strchrnul
#else // !HAVE_STRCHRNUL
static inline char *Q_strchrnul( const char *s, int c )
{
	char *p = (char *)Q_strchr( s, c );
	if( p ) return p;
	return (char *)s + Q_strlen( s );
}
#endif // !HAVE_STRCHRNUL

/*
===========
Q_splitstr

splits strings by a character
if handler returns nonzero value, exists with that value
===========
*/
static inline int Q_splitstr( char *str, int delim, void *userdata,
	int (*handler)( char *prev, char *next, void *userdata ))
{
	char *prev = str;
	char *next = Q_strchrnul( prev, delim );
	int ret = 0;

	for( ; ; prev = next + 1, next = Q_strchrnul( prev, delim ))
	{
		int ch = *next; // save next value if it's modified by handler

		ret = handler( prev, next, userdata );

		if( !ch || ret != 0 )
			break;
	}

	return ret;
}

/*
============
COM_FixSlashes

Changes all '\' characters into '/' characters, in place.
============
*/
static inline void COM_FixSlashes( char *pname )
{
	while(( pname = Q_strchr( pname, '\\' )))
		*pname = '/';
}

#ifdef __cplusplus
}
#endif

#endif//STDLIB_H
