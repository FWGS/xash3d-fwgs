/*
crtlib.h - internal stdlib
Copyright (C) 2011 Uncle Mike

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
// exported APIs headers and will get nice warning in case of changing values
#define PFILE_IGNOREBRACKET (1<<0)
#define PFILE_HANDLECOLON   (1<<1)
#define PFILE_TOKEN_MAX_LENGTH 1024
#define PFILE_FS_TOKEN_MAX_LENGTH 512

#ifdef __cplusplus
#define restrict
#endif // __cplusplus

//
// build.c
//
int Q_buildnum( void );
int Q_buildnum_date( const char *date );
int Q_buildnum_compat( void );
const char *Q_PlatformStringByID( const int platform );
const char *Q_buildos( void );
const char *Q_ArchitectureStringByID( const int arch, const uint abi, const int endianness, const qboolean is64 );
const char *Q_buildarch( void );
const char *Q_buildcommit( void );
const char *Q_buildbranch( void );

//
// crtlib.c
//
void Q_strnlwr( const char *in, char *out, size_t size_out );
#define Q_strlen( str ) (( str ) ? strlen(( str )) : 0 )
size_t Q_colorstr( const char *string );
int Q_atoi( const char *str );
float Q_atof( const char *str );
void Q_atov( float *vec, const char *str, size_t siz );
#define Q_strchr  strchr
#define Q_strrchr strrchr
qboolean Q_stricmpext( const char *pattern, const char *text );
qboolean Q_strnicmpext( const char *pattern, const char *text, size_t minimumlen );
const byte *Q_memmem( const byte *haystack, size_t haystacklen, const byte *needle, size_t needlelen );
void Q_memor( byte *XASH_RESTRICT dst, const byte *XASH_RESTRICT src, size_t len );
const char *Q_timestamp( int format );
int Q_vsnprintf( char *buffer, size_t buffersize, const char *format, va_list args );
int Q_snprintf( char *buffer, size_t buffersize, const char *format, ... ) _format( 3 );
#define Q_strpbrk strpbrk
void COM_StripColors( const char *in, char *out );
#define Q_memprint( val ) Q_pretifymem( val, 2 )
char *Q_pretifymem( float value, int digitsafterdecimal );
void COM_FileBase( const char *in, char *out, size_t size );
const char *COM_FileExtension( const char *in );
void COM_DefaultExtension( char *path, const char *extension, size_t size );
void COM_ReplaceExtension( char *path, const char *extension, size_t size );
void COM_ExtractFilePath( const char *path, char *dest );
const char *COM_FileWithoutPath( const char *in );
void COM_StripExtension( char *path );
void COM_RemoveLineFeed( char *str, size_t bufsize );
void COM_FixSlashes( char *pname );
void COM_PathSlashFix( char *path );
char COM_Hex2Char( uint8_t hex );
void COM_Hex2String( uint8_t hex, char *str );
// return 0 on empty or null string, 1 otherwise
#define COM_CheckString( string ) ( ( !string || !*string ) ? 0 : 1 )
#define COM_CheckStringEmpty( string ) ( ( !*string ) ? 0 : 1 )
char *COM_ParseFileSafe( char *data, char *token, const int size, unsigned int flags, int *len, qboolean *quoted );
#define COM_ParseFile( data, token, size ) COM_ParseFileSafe( data, token, size, 0, NULL, NULL )
int matchpattern( const char *in, const char *pattern, qboolean caseinsensitive );
int matchpattern_with_separator( const char *in, const char *pattern, qboolean caseinsensitive, const char *separators, qboolean wildcard_least_one );

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

static inline qboolean Q_isdigit( const char *str )
{
	if( likely( str && *str ))
	{
		while( isdigit( *str )) str++;
		if( !*str ) return true;
	}
	return false;
}

static inline qboolean Q_isspace( const char *str )
{
	if( likely( str && *str ))
	{
		while( isspace( *str ) ) str++;
		if( !*str ) return true;
	}
	return false;
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
static inline size_t Q_strncpy( char *dst, const char *src, size_t size )
{
#if HAVE_STRLCPY
	if( unlikely( !dst || !src || !size ))
		return 0;
	return strlcpy( dst, src, size );
#else
	size_t len;
	if( unlikely( !dst || !src || !size ))
		return 0;

	len = strlen( src );
	if( len + 1 > size ) // check if truncate
	{
		memcpy( dst, src, size - 1 );
		dst[size - 1] = 0;
	}
	else memcpy( dst, src, len + 1 );

	return len; // count does not include NULL
#endif
}

static inline size_t Q_strncat( char *dst, const char *src, size_t size )
{
#if HAVE_STRLCAT
	if( unlikely( !dst || !src || !size ))
		return 0;
	return strlcat( dst, src, size );
#else
	char *d = dst;
	const char *s = src;
	size_t n = size;
	size_t dlen;

	if( unlikely( !dst || !src || !size ))
		return 0;

	// find the end of dst and adjust bytes left but don't go past end
	while( n-- != 0 && *d != '\0' ) d++;
	dlen = d - dst;
	n = size - dlen;

	if( n == 0 ) return( dlen + Q_strlen( s ));

	while( *s != '\0' )
	{
		if( n != 1 )
		{
			*d++ = *s;
			n--;
		}
		s++;
	}

	*d = '\0';
	return( dlen + ( s - src )); // count does not include NULL
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
static inline const char *Q_strchrnul( const char *s, int c )
{
	const char *p = Q_strchr( s, c );
	if( p ) return p;
	return s + Q_strlen( s );
}
#endif // !HAVE_STRCHRNUL

#ifdef __cplusplus
}
#endif

#endif//STDLIB_H
