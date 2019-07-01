/*
lib_common.c - common dynamic library code
Copyright (C) 2018 Flying With Gauss

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
#include "library.h"
#include "filesystem.h"
#include "server.h"

static char s_szLastError[1024] = "";

const char *COM_GetLibraryError()
{
	return s_szLastError;
}

void COM_ResetLibraryError()
{
	s_szLastError[0] = 0;
}

void COM_PushLibraryError( const char *error )
{
	Q_strncat( s_szLastError, error, sizeof( s_szLastError ) );
	Q_strncat( s_szLastError, "\n", sizeof( s_szLastError ) );
}

void *COM_FunctionFromName_SR( void *hInstance, const char *pName )
{
#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	if( !Q_memcmp( pName, "ofs:",4 ) )
		return svgame.dllFuncs.pfnGameInit + Q_atoi(pName + 4);
#endif
	return COM_FunctionFromName( hInstance, pName );
}

const char *COM_OffsetNameForFunction( void *function )
{
	static string sname;
	Q_snprintf( sname, MAX_STRING, "ofs:%d", (size_t)((byte*)function - (byte*)svgame.dllFuncs.pfnGameInit) );
	Con_Reportf( "COM_OffsetNameForFunction %s\n", sname );
	return sname;
}

/*
=============================================================================

	LIBRARY NAMING(see Documentation/library-naming.md for more info)

=============================================================================
*/

/*
==============
COM_GenerateClientLibraryName

Generates platform-unique and compatible name for client libraries
==============
*/
static void COM_GenerateClientLibraryName( const char *name, char *out, size_t size )
{
#ifdef XASH_INTERNAL_GAMELIBS // assuming library loader knows where to get libraries

	Q_strncpy( out, name, size );

#elif ( XASH_WIN32 || XASH_LINUX || XASH_APPLE ) && XASH_X86

	Q_snprintf( out, size, "%s/%s." OS_LIB_EXT,
		GI->dll_path,
		name );

#elif ( XASH_WIN32 || XASH_LINUX || XASH_APPLE )

	Q_snprintf( out, size, "%s/%s_%s." OS_LIB_EXT,
		GI->dll_path,
		name,
		Q_buildarch() );

#else

	Q_snprintf( out, size, "%s/%s_%s_%s." OS_LIB_EXT,
		GI->dll_path,
		name,
		Q_buildos(),
		Q_buildarch() );

#endif
}

/*
==============
COM_GenerateClientLibraryName

Generates platform-unique and compatible name for server library
==============
*/
static void COM_GenerateServerLibraryName( char *out, size_t size )
{
#ifdef XASH_INTERNAL_GAMELIBS // assuming library loader knows where to get libraries

	Q_strncpy( out, "server", size );

#elif ( XASH_WIN32 || XASH_LINUX || XASH_APPLE ) && XASH_X86

#if XASH_WIN32
	Q_strncpy( out, GI->game_dll, size );
#elif XASH_APPLE
	Q_strncpy( out, GI->game_dll_osx, size );
#else // XASH_LINUX
	Q_strncpy( out, GI->game_dll_linux, size );
#endif

#else
	string dllpath;
	char *ext;

#if XASH_WIN32
	Q_strncpy( dllname, GI->game_dll, sizeof( dllname ) );
#elif XASH_APPLE
	Q_strncpy( dllname, GI->game_dll_osx, sizeof( dllname ) );
#else // XASH_APPLE
	Q_strncpy( dllname, GI->game_dll_linux, sizeof( dllname ) );
#endif

	ext = COM_FileExtension( dllpath );
	COM_StripExtension( dllpath );

#if ( XASH_WIN32 || XASH_LINUX || XASH_APPLE )
	Q_snprintf( out, size, "%s_%s.%s", dllpath, Q_buildarch(), ext );
#else
	Q_snprintf( out, size, "%s_%s_%s.%s", dllpath, Q_buildos(), Q_buildarch(), ext );
#endif

#endif
}


/*
==============
COM_GetCommonLibraryName

Generates platform-unique and compatible name for server library
==============
*/
void COM_GetCommonLibraryName( ECommonLibraryType eLibType, char *out, size_t size )
{
	switch( eLibType )
	{
	case LIBRARY_GAMEUI:
		COM_GenerateClientLibraryName( "menu", out, size );
		break;
	case LIBRARY_CLIENT:
		if( SI.clientlib[0] )
		{
			Q_strncpy( out, SI.clientlib, size );
		}
		else
		{
			COM_GenerateClientLibraryName( "client", out, size );
		}
		break;
	case LIBRARY_SERVER:
		if( SI.gamedll[0] )
		{
			Q_strncpy( out, SI.gamedll, size );
		}
		else
		{
			COM_GenerateServerLibraryName( out, size );
		}
		break;
	default:
		ASSERT( true );
		out[0] = 0;
		break;
	}
}
