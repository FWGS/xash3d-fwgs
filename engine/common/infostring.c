/*
infostring.c - network info strings
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

#define MAX_KV_SIZE		128

/*
=======================================================================

			INFOSTRING STUFF

=======================================================================
*/
/*
===============
Info_Print

printing current key-value pair
===============
*/
void Info_Print( const char *s )
{
	char	key[MAX_KV_SIZE];
	char	value[MAX_KV_SIZE];
	int	l, count;
	char	*o;

	if( *s == '\\' ) s++;

	while( *s )
	{
		count = 0;
		o = key;

		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}

		l = o - key;
		if( l < 20 )
		{
			memset( o, ' ', 20 - l );
			key[20] = 0;
		}
		else *o = 0;

		Con_Printf( "%s", key );

		if( !*s )
		{
			Con_Printf( "(null)\n" );
			return;
		}

		count = 0;
		o = value;
		s++;
		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( *s ) s++;
		Con_Printf( "%s\n", value );
	}
}

/*
==============
Info_IsValid

check infostring for potential problems
==============
*/
qboolean Info_IsValid( const char *s )
{
	char	key[MAX_KV_SIZE];
	char	value[MAX_KV_SIZE];
	int	count;
	char	*o;

	if( *s == '\\' ) s++;

	while( *s )
	{
		count = 0;
		o = key;

		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( !*s ) return false;

		count = 0;
		o = value;
		s++;
		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( !COM_CheckStringEmpty( value ) )
			return false;

		if( *s ) s++;
	}

	return true;
}

#if !XASH_DEDICATED
/*
==============
Info_WriteVars

==============
*/
void Info_WriteVars( file_t *f )
{
	char	*s = CL_Userinfo();
	char	pkey[MAX_SERVERINFO_STRING];
	static	char value[4][MAX_SERVERINFO_STRING]; // use two buffers so compares work without stomping on each other
	static	int valueindex;
	convar_t	*pcvar;
	char	*o;

	valueindex = (valueindex + 1) % 4;
	if( *s == '\\' ) s++;

	while( 1 )
	{
		o = pkey;
		while( *s != '\\' )
		{
			if( !*s ) return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while( *s != '\\' && *s )
		{
			if( !*s ) return;
			*o++ = *s++;
		}
		*o = 0;

		pcvar = Cvar_FindVar( pkey );

		if( !pcvar && pkey[0] != '*' )  // don't store out star keys
			FS_Printf( f, "setinfo \"%s\" \"%s\"\n", pkey, value[valueindex] );

		if( !*s ) return;
		s++;
	}
}
#endif // XASH_DEDICATED

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
const char *GAME_EXPORT Info_ValueForKey( const char *s, const char *key )
{
	char	pkey[MAX_KV_SIZE];
	static	char value[4][MAX_KV_SIZE]; // use two buffers so compares work without stomping on each other
	static	int valueindex;
	int	count;
	char	*o;

	valueindex = (valueindex + 1) % 4;
	if( *s == '\\' ) s++;

	while( 1 )
	{
		count = 0;
		o = pkey;

		while( count < (MAX_KV_SIZE - 1) && *s != '\\' )
		{
			if( !*s ) return "";
			*o++ = *s++;
			count++;
		}

		*o = 0;
		s++;

		o = value[valueindex];
		count = 0;

		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			if( !*s ) return "";
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( !Q_strcmp( key, pkey ))
			return value[valueindex];
		if( !*s ) return "";
		s++;
	}
}

qboolean GAME_EXPORT Info_RemoveKey( char *s, const char *key )
{
	char	*start;
	char	pkey[MAX_KV_SIZE];
	char	value[MAX_KV_SIZE];
	int	cmpsize = Q_strlen( key );
	int	count;
	char	*o;

	if( cmpsize > ( MAX_KV_SIZE - 1 ))
		cmpsize = MAX_KV_SIZE - 1;

	if( Q_strchr( key, '\\' ))
		return false;

	while( 1 )
	{
		start = s;
		if( *s == '\\' ) s++;
		count = 0;
		o = pkey;

		while( count < (MAX_KV_SIZE - 1) && *s != '\\' )
		{
			if( !*s ) return false;
			*o++ = *s++;
			count++;
		}
		*o = 0;
		s++;

		count = 0;
		o = value;

		while( count < (MAX_KV_SIZE - 1) && *s != '\\' && *s )
		{
			if( !*s ) return false;
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( !Q_strncmp( key, pkey, cmpsize ))
		{
			size_t size = Q_strlen( s ) + 1;

			memmove( start, s, size ); // remove this part
			return true;
		}

		if( !*s ) return false;
	}
}

void Info_RemovePrefixedKeys( char *start, char prefix )
{
	char	*s, *o;
	char	pkey[MAX_KV_SIZE];
	char	value[MAX_KV_SIZE];
	int	count;

	s = start;

	while( 1 )
	{
		if( *s == '\\' ) s++;

		count = 0;
		o = pkey;

		while( count < (MAX_KV_SIZE - 1) && *s != '\\' )
		{
			if( !*s ) return;
			*o++ = *s++;
			count++;
		}
		*o = 0;
		s++;

		count = 0;
		o = value;

		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			if( !*s ) return;
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( pkey[0] == prefix )
		{
			Info_RemoveKey( start, pkey );
			s = start;
		}

		if( !*s ) return;
	}
}

static qboolean Info_IsKeyImportant( const char *key )
{
	if( key[0] == '*' )
		return true;
	if( !Q_strcmp( key, "name" ))
		return true;
	if( !Q_strcmp( key, "model" ))
		return true;
	if( !Q_strcmp( key, "rate" ))
		return true;
	if( !Q_strcmp( key, "topcolor" ))
		return true;
	if( !Q_strcmp( key, "bottomcolor" ))
		return true;
	if( !Q_strcmp( key, "cl_updaterate" ))
		return true;
	if( !Q_strcmp( key, "cl_lw" ))
		return true;
	if( !Q_strcmp( key, "cl_lc" ))
		return true;
	if( !Q_strcmp( key, "cl_nopred" ))
		return true;
	return false;
}

static char *Info_FindLargestKey( char *s )
{
	char	key[MAX_KV_SIZE];
	char	value[MAX_KV_SIZE];
	static	char largest_key[128];
	int	largest_size = 0;
	int	l, count;
	char	*o;

	*largest_key = 0;

	if( *s == '\\' ) s++;

	while( *s )
	{
		int	size = 0;

		count = 0;
		o = key;

		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}

		l = o - key;
		*o = 0;
		size = Q_strlen( key );

		if( !*s ) return largest_key;

		count = 0;
		o = value;
		s++;
		while( count < (MAX_KV_SIZE - 1) && *s && *s != '\\' )
		{
			*o++ = *s++;
			count++;
		}
		*o = 0;

		if( *s ) s++;

		size += Q_strlen( value );

		if(( size > largest_size ) && !Info_IsKeyImportant( key ))
		{
			Q_strncpy( largest_key, key, sizeof( largest_key ));
			largest_size = size;
		}
	}

	return largest_key;
}

qboolean Info_SetValueForStarKey( char *s, const char *key, const char *value, int maxsize )
{
	char	new[1024], *v;
	int	c, team;

	if( Q_strchr( key, '\\' ) || Q_strchr( value, '\\' ))
	{
		Con_Printf( S_ERROR "SetValueForKey: can't use keys or values with a \\\n" );
		return false;
	}

	if( Q_strstr( key, ".." ) || Q_strstr( value, ".." ))
		return false;

	if( Q_strchr( key, '\"' ) || Q_strchr( value, '\"' ))
	{
		Con_Printf( S_ERROR "SetValueForKey: can't use keys or values with a \"\n" );
		return false;
	}

	if( Q_strlen( key ) > ( MAX_KV_SIZE - 1 ) || Q_strlen( value ) > ( MAX_KV_SIZE - 1 ))
		return false;

	Info_RemoveKey( s, key );

	if( !COM_CheckString( value ) )
		return true; // just clear variable

	Q_snprintf( new, sizeof( new ), "\\%s\\%s", key, value );
	if( Q_strlen( new ) + Q_strlen( s ) > maxsize )
	{
		// no more room in buffer to add key/value
		if( Info_IsKeyImportant( key ))
		{
			// keep removing the largest key/values until we have room
			char	*largekey;

			do
			{
				largekey = Info_FindLargestKey( s );
				Info_RemoveKey( s, largekey );
			} while((( Q_strlen( new ) + Q_strlen( s )) >= maxsize ) && *largekey != 0 );

			if( largekey[0] == 0 )
			{
				// no room to add setting
				return true; // info changed, new value can't saved
			}
		}
		else
		{
			// no room to add setting
			return true; // info changed, new value can't saved
		}
	}

	// only copy ascii values
	s += Q_strlen( s );
	v = new;

	team = ( Q_stricmp( key, "team" ) == 0 ) ? true : false;

	while( *v )
	{
		c = (byte)*v++;
		if( team ) c = Q_tolower( c );
		if( c > 13 ) *s++ = c;
	}
	*s = 0;

	// all done
	return true;
}

qboolean Info_SetValueForKey( char *s, const char *key, const char *value, int maxsize )
{
	if( key[0] == '*' )
	{
		Con_Printf( S_ERROR "Can't set *keys\n" );
		return false;
	}

	return Info_SetValueForStarKey( s, key, value, maxsize );
}

qboolean Info_SetValueForKeyf( char *s, const char *key, int maxsize, const char *format, ... )
{
	char value[MAX_VA_STRING];
	va_list args;

	va_start( args, format );
	Q_vsnprintf( value, sizeof( value ), format, args );
	va_end( args );

	return Info_SetValueForKey( s, key, value, maxsize );
}

