/*
crtlib.c - internal stdlib
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
#include "port.h"
#include "xash3d_types.h"
#include "const.h"
#include <math.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "stdio.h"
#include "crtlib.h"
#include "xash3d_mathlib.h"

void Q_strnupr( const char *in, char *out, size_t size_out )
{
	if( size_out == 0 ) return;

	while( *in && size_out > 1 )
	{
		if( *in >= 'a' && *in <= 'z' )
			*out++ = *in++ + 'A' - 'a';
		else *out++ = *in++;
		size_out--;
	}
	*out = '\0';
}

void Q_strnlwr( const char *in, char *out, size_t size_out )
{
	if( size_out == 0 ) return;

	while( *in && size_out > 1 )
	{
		if( *in >= 'A' && *in <= 'Z' )
			*out++ = *in++ + 'a' - 'A';
		else *out++ = *in++;
		size_out--;
	}
	*out = '\0';
}

qboolean Q_isdigit( const char *str )
{
	if( str && *str )
	{
		while( isdigit( *str )) str++;
		if( !*str ) return true;
	}
	return false;
}

qboolean Q_isspace( const char *str )
{
	if( str && *str )
	{
		while( isspace( *str ) ) str++;
		if( !*str ) return true;
	}
	return false;
}

size_t Q_colorstr( const char *string )
{
	size_t		len;
	const char	*p;

	if( !string ) return 0;

	len = 0;
	p = string;
	while( *p )
	{
		if( IsColorString( p ))
		{
			len += 2;
			p += 2;
			continue;
		}
		p++;
	}

	return len;
}

char Q_toupper( const char in )
{
	char	out;

	if( in >= 'a' && in <= 'z' )
		out = in + 'A' - 'a';
	else out = in;

	return out;
}

char Q_tolower( const char in )
{
	char	out;

	if( in >= 'A' && in <= 'Z' )
		out = in + 'a' - 'A';
	else out = in;

	return out;
}

size_t Q_strncat( char *dst, const char *src, size_t size )
{
	register char	*d = dst;
	register const char	*s = src;
	register size_t	n = size;
	size_t		dlen;

	if( !dst || !src || !size )
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
}

size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	register char	*d = dst;
	register const char	*s = src;
	register size_t	n = size;

	if( !dst || !src || !size )
		return 0;

	// copy as many bytes as will fit
	if( n != 0 && --n != 0 )
	{
		do
		{
			if(( *d++ = *s++ ) == 0 )
				break;
		} while( --n != 0 );
	}

	// not enough room in dst, add NULL and traverse rest of src
	if( n == 0 )
	{
		if( size != 0 )
			*d = '\0'; // NULL-terminate dst
		while( *s++ );
	}
	return ( s - src - 1 ); // count does not include NULL
}

int Q_atoi( const char *str )
{
	int val = 0;
	int c, sign;

	if( !str ) return 0;

	// check for empty charachters in string
	while( str && *str == ' ' )
		str++;

	if( !str ) return 0;

	if( *str == '-' )
	{
		sign = -1;
		str++;
	}
	else sign = 1;

	// check for hex
	if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ))
	{
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

	// check for character
	if( str[0] == '\'' )
		return sign * str[1];

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

	if( !str ) return 0.0f;

	// check for empty charachters in string
	while( str && *str == ' ' )
		str++;

	if( !str ) return 0.0f;

	if( *str == '-' )
	{
		sign = -1;
		str++;
	}
	else sign = 1;

	// check for hex
	if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ))
	{
		str += 2;
		while( 1 )
		{
			c = *str++;
			if( c >= '0' && c <= '9' ) val = (val * 16) + c - '0';
			else if( c >= 'a' && c <= 'f' ) val = (val * 16) + c - 'a' + 10;
			else if( c >= 'A' && c <= 'F' ) val = (val * 16) + c - 'A' + 10;
			else return val * sign;
		}
	}

	// check for character
	if( str[0] == '\'' ) return sign * str[1];

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
	string	buffer;
	char	*pstr, *pfront;
	int	j;

	Q_strncpy( buffer, str, sizeof( buffer ));
	memset( vec, 0, sizeof( vec_t ) * siz );
	pstr = pfront = buffer;

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

const char* Q_timestamp( int format )
{
	static string	timestamp;
	time_t		crt_time;
	const struct tm	*crt_tm;
	string		timestring;

	time( &crt_time );
	crt_tm = localtime( &crt_time );

	switch( format )
	{
	case TIME_FULL:
		// Build the full timestamp (ex: "Apr03 2007 [23:31.55]");
		strftime( timestring, sizeof( timestring ), "%b%d %Y [%H:%M.%S]", crt_tm );
		break;
	case TIME_DATE_ONLY:
		// Build the date stamp only (ex: "Apr03 2007");
		strftime( timestring, sizeof( timestring ), "%b%d %Y", crt_tm );
		break;
	case TIME_TIME_ONLY:
		// Build the time stamp only (ex: "23:31.55");
		strftime( timestring, sizeof( timestring ), "%H:%M.%S", crt_tm );
		break;
	case TIME_NO_SECONDS:
		// Build the time stamp exclude seconds (ex: "13:46");
		strftime( timestring, sizeof( timestring ), "%H:%M", crt_tm );
		break;
	case TIME_YEAR_ONLY:
		// Build the date stamp year only (ex: "2006");
		strftime( timestring, sizeof( timestring ), "%Y", crt_tm );
		break;
	case TIME_FILENAME:
		// Build a timestamp that can use for filename (ex: "Nov2006-26 (19.14.28)");
		strftime( timestring, sizeof( timestring ), "%b%Y-%d_%H.%M.%S", crt_tm );
		break;
	default: return NULL;
	}

	Q_strncpy( timestamp, timestring, sizeof( timestamp ));

	return timestamp;
}

#if !defined( HAVE_STRCASESTR )
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

int Q_sprintf( char *buffer, const char *format, ... )
{
	va_list	args;
	int	result;

	va_start( args, format );
	result = Q_vsnprintf( buffer, 99999, format, args );
	va_end( args );

	return result;
}

void COM_StripColors( const char *in, char *out )
{
	while ( *in )
	{
		if ( IsColorString( in ) )
			in += 2;
		else *out++ = *in++;
	}
	*out = '\0';
}

uint Q_hashkey( const char *string, uint hashSize, qboolean caseinsensitive )
{
	uint	i, hashKey = 0;

	if( caseinsensitive )
	{
		for( i = 0; string[i]; i++)
			hashKey += (i * 119) * Q_tolower( string[i] );
	}
	else
	{
		for( i = 0; string[i]; i++ )
			hashKey += (i + 119) * (int)string[i];
	}

	hashKey = ((hashKey ^ (hashKey >> 10)) ^ (hashKey >> 20)) & (hashSize - 1);

	return hashKey;
}

char *Q_pretifymem( float value, int digitsafterdecimal )
{
	static char	output[8][32];
	static int	current;
	float		onekb = 1024.0f;
	float		onemb = onekb * onekb;
	char		suffix[8];
	char		*out = output[current];
	char		val[32], *i, *o, *dot;
	int		pos;

	current = ( current + 1 ) & ( 8 - 1 );

	// first figure out which bin to use
	if( value > onemb )
	{
		value /= onemb;
		Q_strcpy( suffix, " Mb" );
	}
	else if( value > onekb )
	{
		value /= onekb;
		Q_strcpy( suffix, " Kb" );
	}
	else Q_strcpy( suffix, " bytes" );

	// clamp to >= 0
	digitsafterdecimal = Q_max( digitsafterdecimal, 0 );

	// if it's basically integral, don't do any decimals
	if( fabs( value - (int)value ) < 0.00001f )
	{
		Q_sprintf( val, "%i%s", (int)value, suffix );
	}
	else
	{
		char fmt[32];

		// otherwise, create a format string for the decimals
		Q_sprintf( fmt, "%%.%if%s", digitsafterdecimal, suffix );
		Q_sprintf( val, fmt, (double)value );
	}

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
va

does a varargs printf into a temp buffer,
so I don't need to have varargs versions
of all text functions.
============
*/
char *va( const char *format, ... )
{
	va_list		argptr;
	static char	string[16][1024], *s;
	static int	stringindex = 0;

	s = string[stringindex];
	stringindex = (stringindex + 1) & 15;
	va_start( argptr, format );
	Q_vsnprintf( s, sizeof( string[0] ), format, argptr );
	va_end( argptr );

	return s;
}

/*
============
COM_FileBase

Extracts the base name of a file (no path, no extension, assumes '/' as path separator)
============
*/
void COM_FileBase( const char *in, char *out )
{
	int	len, start, end;

	len = Q_strlen( in );
	if( !len ) return;

	// scan backward for '.'
	end = len - 1;

	while( end && in[end] != '.' && in[end] != '/' && in[end] != '\\' )
		end--;

	if( in[end] != '.' )
		end = len-1; // no '.', copy to end
	else end--; // found ',', copy to left of '.'

	// scan backward for '/'
	start = len - 1;

	while( start >= 0 && in[start] != '/' && in[start] != '\\' )
		start--;

	if( start < 0 || ( in[start] != '/' && in[start] != '\\' ))
		start = 0;
	else start++;

	// length of new sting
	len = end - start + 1;

	// Copy partial string
	Q_strncpy( out, &in[start], len + 1 );
	out[len] = 0;
}

/*
============
COM_FileExtension
============
*/
const char *COM_FileExtension( const char *in )
{
	const char *separator, *backslash, *colon, *dot;

	separator = Q_strrchr( in, '/' );
	backslash = Q_strrchr( in, '\\' );

	if( !separator || separator < backslash )
		separator = backslash;

	colon = Q_strrchr( in, ':' );

	if( !separator || separator < colon )
		separator = colon;

	dot = Q_strrchr( in, '.' );

	if( dot == NULL || ( separator && ( dot < separator )))
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
	while( src != path && !(*(src - 1) == '\\' || *(src - 1) == '/' ))
		src--;

	if( src != path )
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
void COM_DefaultExtension( char *path, const char *extension )
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

	Q_strcpy( &path[len], extension );
}

/*
==================
COM_ReplaceExtension
==================
*/
void COM_ReplaceExtension( char *path, const char *extension )
{
	COM_StripExtension( path );
	COM_DefaultExtension( path, extension );
}

/*
============
COM_RemoveLineFeed
============
*/
void COM_RemoveLineFeed( char *str )
{
	while( *str != '\0' )
	{
		if( *str == '\r' || *str == '\n' )
			*str = '\0';

		++str;
	}
}

/*
============
COM_FixSlashes

Changes all '\' characters into '/' characters, in place.
============
*/
void COM_FixSlashes( char *pname )
{
	for( ; *pname; pname++ )
	{
		if( *pname == '\\' )
			*pname = '/';
	}
}

/*
============
COM_PathSlashFix
============
*/
void COM_PathSlashFix( char *path )
{
	size_t	len;

	len = Q_strlen( path );

	if( path[len - 1] != '\\' && path[len - 1] != '/' )
		Q_strcpy( &path[len], "/" );
}

/*
============
COM_Hex2Char
============
*/
char COM_Hex2Char( uint8_t hex )
{
	if( hex >= 0x0 && hex <= 0x9 )
		hex += '0';
	else if( hex >= 0xA && hex <= 0xF )
		hex += '7';

	return (char)hex;
}

/*
============
COM_Hex2String
============
*/
void COM_Hex2String( uint8_t hex, char *str )
{
	*str++ = COM_Hex2Char( hex >> 4 );
	*str++ = COM_Hex2Char( hex & 0x0F );
	*str = '\0';
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

	// skip // comments
	if( c == '/' && data[1] == '/' )
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
	return matchpattern_with_separator( in, pattern, caseinsensitive, "/\\:", false );
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

