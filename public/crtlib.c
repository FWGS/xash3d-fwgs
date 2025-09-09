/*
crtlib.c - internal stdlib
Copyright (C) 2011 Uncle Mike
Copyright (c) QuakeSpasm contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "port.h"
#include "xash3d_types.h"
#include "const.h"
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include "stdio.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"

void Q_strnlwr( const char *in, char *out, size_t size_out )
{
	size_t len, i;

	len = Q_strncpy( out, in, size_out );

	for( i = 0; i < len; i++ )
		out[i] = Q_tolower( out[i] );
}

int Q_atoi_hex( int sign, const char *str )
{
	int c, val = 0;

	if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ))
		str += 2;

	while( 1 )
	{
		c = *str++;
		if( c >= '0' && c <= '9' ) val = (val<<4) + c - '0';
		else if( c >= 'a' && c <= 'f' ) val = (val<<4) + c - 'a' + 10;
		else if( c >= 'A' && c <= 'F' ) val = (val<<4) + c - 'A' + 10;
		else return val * sign;
	}
}

static int Q_atoi_character( int sign, const char *str )
{
	return sign * str[1];
}

static const char *Q_atoi_strip_whitespace( const char *str )
{
	while( str && *str == ' ' )
		str++;

	return str;
}

int Q_atoi( const char *str )
{
	int val = 0;
	int c, sign;

	if( !COM_CheckString( str ))
		return 0;

	str = Q_atoi_strip_whitespace( str );

	if( !COM_CheckString( str ))
		return 0;

	if( *str == '-' )
	{
		sign = -1;
		str++;
	}
	else sign = 1;

	// check for hex
	if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ))
		return Q_atoi_hex( sign, str );

	// check for character
	if( str[0] == '\'' )
		return Q_atoi_character( sign, str );

	// assume decimal
	while( 1 )
	{
		c = *str++;
		if( c < '0' || c > '9' )
			return val * sign;
		val = val * 10 + c - '0';
	}
	return 0;
}

float Q_atof( const char *str )
{
	double	val = 0;
	int	c, sign, decimal, total;

	if( !COM_CheckString( str ))
		return 0;

	str = Q_atoi_strip_whitespace( str );

	if( !COM_CheckString( str ))
		return 0;

	if( *str == '-' )
	{
		sign = -1;
		str++;
	}
	else sign = 1;

	// check for hex
	if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ))
		return Q_atoi_hex( sign, str );

	// check for character
	if( str[0] == '\'' )
		return Q_atoi_character( sign, str );

	// assume decimal
	decimal = -1;
	total = 0;

	while( 1 )
	{
		c = *str++;
		if( c == '.' )
		{
			decimal = total;
			continue;
		}

		if( c < '0' || c > '9' )
			break;
		val = val * 10 + c - '0';
		total++;
	}

	if( decimal == -1 )
		return val * sign;

	while( total > decimal )
	{
		val /= 10;
		total--;
	}

	return val * sign;
}

void Q_atov( float *vec, const char *str, size_t siz )
{
	const char *pstr, *pfront;
	int	j;

	memset( vec, 0, sizeof( *vec ) * siz );
	pstr = pfront = str;

	for( j = 0; j < siz; j++ )
	{
		vec[j] = Q_atof( pfront );

		// valid separator is space
		while( *pstr && *pstr != ' ' )
			pstr++;

		if( !*pstr ) break;
		pstr++;
		pfront = pstr;
	}
}

static qboolean Q_starcmp( const char *pattern, const char *text )
{
	char		c, c1;
	const char	*p = pattern, *t = text;

	while(( c = *p++ ) == '?' || c == '*' )
	{
		if( c == '?' && *t++ == '\0' )
			return false;
	}

	if( c == '\0' ) return true;

	for( c1 = (( c == '\\' ) ? *p : c ); ; )
	{
		if( Q_tolower( *t ) == c1 && Q_stricmpext( p - 1, t ))
			return true;
		if( *t++ == '\0' ) return false;
	}
}

qboolean Q_strnicmpext( const char *pattern, const char *text, size_t minimumlength )
{
	size_t  i = 0;
	char	c;

	while(( c = *pattern++ ) != '\0' )
	{
		i++;

		switch( c )
		{
		case '?':
			if( *text++ == '\0' )
				return false;
			break;
		case '\\':
			if( Q_tolower( *pattern++ ) != Q_tolower( *text++ ))
				return false;
			break;
		case '*':
			return Q_starcmp( pattern, text );
		default:
			if( Q_tolower( c ) != Q_tolower( *text++ ))
				return false;
		}
	}
	return ( *text == '\0' ) || i == minimumlength;
}

qboolean Q_stricmpext( const char *pattern, const char *text )
{
	return Q_strnicmpext( pattern, text, ~((size_t)0) );
}

const byte *Q_memmem( const byte *haystack, size_t haystacklen, const byte *needle, size_t needlelen )
{
	const byte *i;

	// quickly find first matching symbol
	while( haystacklen && ( i = memchr( haystack, needle[0], haystacklen )))
	{
		if( !memcmp( i, needle, needlelen ))
			return i;

		// skip one byte
		i++;

		haystacklen -= i - haystack;
		haystack = i;
	}

	return NULL;
}

void Q_memor( byte *XASH_RESTRICT dst, const byte *XASH_RESTRICT src, size_t len )
{
	size_t i;
	for( i = 0; i < len; i++ ) // msvc likes to optimize this loop form
		dst[i] |= src[i];
}

const char* Q_timestamp( int format )
{
	static string	timestamp;
	time_t		crt_time;
	const struct tm	*crt_tm;

	time( &crt_time );
	crt_tm = localtime( &crt_time );

	switch( format )
	{
	case TIME_FULL:
		// Build the full timestamp (ex: "Apr03 2007 [23:31.55]");
		strftime( timestamp, sizeof( timestamp ), "%b%d %Y [%H:%M.%S]", crt_tm );
		break;
	case TIME_DATE_ONLY:
		// Build the date stamp only (ex: "Apr03 2007");
		strftime( timestamp, sizeof( timestamp ), "%b%d %Y", crt_tm );
		break;
	case TIME_TIME_ONLY:
		// Build the time stamp only (ex: "23:31.55");
		strftime( timestamp, sizeof( timestamp ), "%H:%M.%S", crt_tm );
		break;
	case TIME_NO_SECONDS:
		// Build the time stamp exclude seconds (ex: "13:46");
		strftime( timestamp, sizeof( timestamp ), "%H:%M", crt_tm );
		break;
	case TIME_YEAR_ONLY:
		// Build the date stamp year only (ex: "2006");
		strftime( timestamp, sizeof( timestamp ), "%Y", crt_tm );
		break;
	case TIME_FILENAME:
		// Build a timestamp that can use for filename (ex: "Nov2006-26 (19.14.28)");
		strftime( timestamp, sizeof( timestamp ), "%b%Y-%d_%H.%M.%S", crt_tm );
		break;
	default:
		Q_snprintf( timestamp, sizeof( timestamp ), "%s: unknown format %d", __func__, format );
		break;
	}

	return timestamp;
}

#if !HAVE_STRCASESTR
char *Q_stristr( const char *string, const char *string2 )
{
	int	c;
	size_t	len;

	if( !string || !string2 ) return NULL;

	c = Q_tolower( *string2 );
	len = Q_strlen( string2 );

	while( string )
	{
		for( ; *string && Q_tolower( *string ) != c; string++ );

		if( *string )
		{
			if( !Q_strnicmp( string, string2, len ))
				break;
			string++;
		}
		else return NULL;
	}
	return (char *)string;
}
#endif // !defined( HAVE_STRCASESTR )

int Q_vsnprintf( char *buffer, size_t buffersize, const char *format, va_list args )
{
	int	result;

	if( unlikely( buffersize == 0 ))
		return -1; // report as overflow

#ifndef _MSC_VER
	result = vsnprintf( buffer, buffersize, format, args );
#else
	__try
	{
		result = _vsnprintf( buffer, buffersize, format, args );
	}

	// to prevent crash while output
	__except( EXCEPTION_EXECUTE_HANDLER )
	{
		Q_strncpy( buffer, "^1sprintf throw exception^7\n", buffersize );
		result = buffersize;
	}
#endif

	if( result >= buffersize )
	{
		buffer[buffersize - 1] = '\0';
		return -1;
	}

	return result;
}

int Q_snprintf( char *buffer, size_t buffersize, const char *format, ... )
{
	va_list	args;
	int	result;

	va_start( args, format );
	result = Q_vsnprintf( buffer, buffersize, format, args );
	va_end( args );

	return result;
}

void COM_StripColors( const char *in, char *out )
{
	while( *in )
	{
		if( IsColorString( in ))
			in += 2;
		else *out++ = *in++;
	}
	*out = '\0';
}

char *Q_pretifymem( float value, int digitsafterdecimal )
{
	static char	output[8][32];
	static int	current;
	const float onekb = 1024.0f;
	const float onemb = onekb * onekb;
	const char *suffix;
	char		*out = output[current];
	char		val[32], *i, *o, *dot;
	int		pos;

	current = ( current + 1 ) & ( 8 - 1 );

	// first figure out which bin to use
	if( value > onemb )
	{
		value /= onemb;
		suffix = "Mb";
	}
	else if( value > onekb )
	{
		value /= onekb;
		suffix = "Kb";
	}
	else
	{
		suffix = "bytes";
	}

	// if it's basically integral, don't do any decimals
	if( fabs( value - (int)value ) < 0.00001f || digitsafterdecimal <= 0 )
		Q_snprintf( val, sizeof( val ), "%i %s", (int)Q_rint( value ), suffix );
	else if( digitsafterdecimal >= 1 )
		Q_snprintf( val, sizeof( val ), "%.*f %s", digitsafterdecimal, (double)value, suffix );

	// copy from in to out
	i = val;
	o = out;

	// search for decimal or if it was integral, find the space after the raw number
	dot = Q_strchr( i, '.' );
	if( !dot ) dot = Q_strchr( i, ' ' );

	pos = dot - i;	// compute position of dot
	pos -= 3;		// don't put a comma if it's <= 3 long

	while( *i )
	{
		// if pos is still valid then insert a comma every third digit, except if we would be
		// putting one in the first spot
		if( pos >= 0 && !( pos % 3 ))
		{
			// never in first spot
			if( o != out ) *o++ = ',';
		}

		pos--;		// count down comma position
		*o++ = *i++;	// copy rest of data as normal
	}
	*o = 0; // terminate

	return out;
}

/*
============
COM_FileBase

Extracts the base name of a file (no path, no extension, assumes '/' as path separator)
a1ba: adapted and simplified version from QuakeSpasm
============
*/
void COM_FileBase( const char *in, char *out, size_t size )
{
	const char *dot, *slash, *s;
	size_t len;

	if( unlikely( !COM_CheckString( in ) || size <= 1 ))
	{
		out[0] = 0;
		return;
	}

	slash = in;
	dot = NULL;
	for( s = in; *s; s++ )
	{
		if( *s == '/' || *s == '\\' )
			slash = s + 1;

		if( *s == '.' )
			dot = s;
	}

	if( dot == NULL || dot < slash )
		dot = s;

	len = Q_min( size - 1, dot - slash );

	memcpy( out, slash, len );
	out[len] = 0;
}

/*
============
COM_FileExtension
============
*/
const char *COM_FileExtension( const char *in )
{
	const char *dot;

	dot = Q_strrchr( in, '.' );

	// quickly exit if there is no dot at all
	if( dot == NULL )
		return "";

	// if there are any of these special symbols after the dot, the file has no extension
	if( Q_strpbrk( dot + 1, "\\/:" ))
		return "";

	return dot + 1;
}

/*
============
COM_FileWithoutPath
============
*/
const char *COM_FileWithoutPath( const char *in )
{
	const char *separator, *backslash, *colon;

	separator = Q_strrchr( in, '/' );
	backslash = Q_strrchr( in, '\\' );

	if( !separator || separator < backslash )
		separator = backslash;

	colon = Q_strrchr( in, ':' );

	if( !separator || separator < colon )
		separator = colon;

	return separator ? separator + 1 : in;
}

/*
============
COM_ExtractFilePath
============
*/
void COM_ExtractFilePath( const char *path, char *dest )
{
	const char *src = path + Q_strlen( path ) - 1;

	// back up until a \ or the start
	while( src > path && !(*(src - 1) == '\\' || *(src - 1) == '/' ))
		src--;

	if( src > path )
	{
		memcpy( dest, path, src - path );
		dest[src - path - 1] = 0; // cutoff backslash
	}
	else dest[0] = 0; // file without path
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension( char *path )
{
	size_t	length;

	length = Q_strlen( path );

	if( length > 0 )
		length--;

	while( length > 0 && path[length] != '.' )
	{
		length--;
		if( path[length] == '/' || path[length] == '\\' || path[length] == ':' )
			return; // no extension
	}

	if( length ) path[length] = 0;
}

/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension( char *path, const char *extension, size_t size )
{
	const char	*src;
	size_t		 len;

	// if path doesn't have a .EXT, append extension
	// (extension should include the .)
	len = Q_strlen( path );
	src = path + len - 1;

	while( *src != '/' && src != path )
	{
		// it has an extension
		if( *src == '.' ) return;
		src--;
	}

	Q_strncpy( &path[len], extension, size - len );
}

/*
==================
COM_ReplaceExtension
==================
*/
void COM_ReplaceExtension( char *path, const char *extension, size_t size )
{
	COM_StripExtension( path );
	COM_DefaultExtension( path, extension, size );
}

/*
============
COM_RemoveLineFeed
============
*/
void COM_RemoveLineFeed( char *str, size_t bufsize )
{
	size_t i;

	for( i = 0; i < bufsize && *str != '\0'; i++, str++ )
	{
		if( *str == '\r' || *str == '\n' )
			*str = '\0';
	}
}

/*
============
COM_PathSlashFix

ensure directory path always ends on forward slash
============
*/
void COM_PathSlashFix( char *path )
{
	size_t len = Q_strlen( path );

	if( path[len - 1] == '\\' )
	{
		path[len - 1] = '/';
	}
	else if( path[len - 1] != '/' )
	{
		path[len] = '/';
		path[len + 1] = '\0';
	}
}

/*
==============
COM_IsSingleChar

interpert this character as single
==============
*/
static int COM_IsSingleChar( unsigned int flags, char c )
{
	if( c == '{' || c == '}' || c == '\'' || c == ',' )
		return true;

	if( !FBitSet( flags, PFILE_IGNOREBRACKET ) && ( c == ')' || c == '(' ))
		return true;

	if( FBitSet( flags, PFILE_HANDLECOLON ) && c == ':' )
		return true;

	return false;
}

/*
==============
COM_ParseFile

text parser
==============
*/
char *COM_ParseFileSafe( char *data, char *token, const int size, unsigned int flags, int *plen, qboolean *quoted )
{
	int	c, len = 0;
	qboolean overflow = false;

	if( quoted )
		*quoted = false;

	if( !token || !size )
	{
		if( plen ) *plen = 0;
		return NULL;
	}

	token[0] = 0;

	if( !data )
		return NULL;
// skip whitespace
skipwhite:
	while(( c = ((byte)*data)) <= ' ' )
	{
		if( c == 0 )
		{
			if( plen ) *plen = overflow ? -1 : len;
			return NULL;	// end of file;
		}
		data++;
	}

	// skip // or #, if requested, comments
	if(( c == '/' && data[1] == '/' ) || ( c == '#' && FBitSet( flags, PFILE_IGNOREHASHCMT )))
	{
		while( *data && *data != '\n' )
			data++;
		goto skipwhite;
	}

	// handle quoted strings specially
	if( c == '\"' )
	{
		if( quoted )
			*quoted = true;

		data++;
		while( 1 )
		{
			c = (byte)*data;

			// unexpected line end
			if( !c )
			{
				token[len] = 0;
				if( plen ) *plen = overflow ? -1 : len;
				return data;
			}
			data++;

			if( c == '\\' && *data == '"' )
			{
				if( len + 1 < size )
				{
					token[len] = (byte)*data;
					len++;
				}
				else overflow = true;

				data++;
				continue;
			}

			if( c == '\"' )
			{
				token[len] = 0;
				if( plen ) *plen = overflow ? -1 : len;
				return data;
			}

			if( len + 1 < size )
			{
				token[len] = c;
				len++;
			}
			else overflow = true;
		}
	}

	// parse single characters
	if( COM_IsSingleChar( flags, c ))
	{
		if( size >= 2 ) // char and \0
		{
			token[len] = c;
			len++;
			token[len] = 0;
			if( plen ) *plen = overflow ? -1 : len;
			return data + 1;
		}
		else
		{
			// couldn't pass anything
			token[0] = 0;
			if( plen ) *plen = overflow ? -1 : len;
			return data;
		}
	}

	// parse a regular word
	do
	{
		if( len + 1 < size )
		{
			token[len] = c;
			len++;
		}
		else overflow = true;

		data++;
		c = ((byte)*data);

		if( COM_IsSingleChar( flags, c ))
			break;
	} while( c > 32 );

	token[len] = 0;

	if( plen ) *plen = overflow ? -1 : len;

	return data;
}

int matchpattern( const char *in, const char *pattern, qboolean caseinsensitive )
{
	const char *separators = "/\\:";

	if( !Q_strcmp( pattern, "*" ))
		separators = "";

	return matchpattern_with_separator( in, pattern, caseinsensitive, separators, false );
}

// wildcard_least_one: if true * matches 1 or more characters
//                     if false * matches 0 or more characters
int matchpattern_with_separator( const char *in, const char *pattern, qboolean caseinsensitive, const char *separators, qboolean wildcard_least_one )
{
	int c1, c2;

	while( *pattern )
	{
		switch( *pattern )
		{
		case 0:
			return 1; // end of pattern
		case '?': // match any single character
			if( *in == 0 || Q_strchr( separators, *in ))
				return 0; // no match
			in++;
			pattern++;
			break;
		case '*': // match anything until following string
			if( wildcard_least_one )
			{
				if( *in == 0 || Q_strchr( separators, *in ))
					return 0; // no match
				in++;
			}
			pattern++;
			while( *in )
			{
				if( Q_strchr(separators, *in ))
					break;
				// see if pattern matches at this offset
				if( matchpattern_with_separator(in, pattern, caseinsensitive, separators, wildcard_least_one ))
					return 1;
				// nope, advance to next offset
				in++;
			}
			break;
		default:
			if( *in != *pattern )
			{
				if( !caseinsensitive )
					return 0; // no match
				c1 = *in;
				if( c1 >= 'A' && c1 <= 'Z' )
					c1 += 'a' - 'A';
				c2 = *pattern;
				if( c2 >= 'A' && c2 <= 'Z' )
					c2 += 'a' - 'A';
				if( c1 != c2 )
					return 0; // no match
			}
			in++;
			pattern++;
			break;
		}
	}
	if( *in )
		return 0; // reached end of pattern but not end of input
	return 1; // success
}

