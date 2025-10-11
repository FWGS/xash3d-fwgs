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
#include <ctype.h>

static char s_szLastError[1024] = "";

const char *COM_GetLibraryError( void )
{
	return s_szLastError;
}

void COM_ResetLibraryError( void )
{
	s_szLastError[0] = 0;
}

void COM_PushLibraryError( const char *error )
{
	if( s_szLastError[0] )
		Q_strncat( s_szLastError, "\n", sizeof( s_szLastError ) );
	Q_strncat( s_szLastError, error, sizeof( s_szLastError ) );
}

void *COM_FunctionFromName_SR( void *hInstance, const char *pName )
{
	char **funcs = NULL;
	size_t numfuncs, i;
	void *f = NULL;
	const char *func = NULL;

#ifdef XASH_ALLOW_SAVERESTORE_OFFSETS
	if( !memcmp( pName, "ofs:", 4 ))
		return (byte*)svgame.dllFuncs.pfnGameInit + Q_atoi( pName + 4 );
#endif

#if XASH_POSIX
	funcs = COM_ConvertToLocalPlatform( MANGLE_ITANIUM, pName, &numfuncs );

	if( funcs )
	{
		for( i = 0; i < numfuncs; i++ )
		{
			if( !f )
				f = COM_FunctionFromName( hInstance, funcs[i] );
			Z_Free( funcs[i] );
		}
		Z_Free( funcs );

		if( f ) return f;
	}
#elif _MSC_VER
	// TODO: COM_ConvertToLocalPlatform doesn't support MSVC yet
	// also custom loader strips always MSVC mangling, so Win32
	// platforms already use platform-neutral names
	func = COM_GetPlatformNeutralName( pName );

	if( func )
		return COM_FunctionFromName( hInstance, func );
#endif

	return COM_FunctionFromName( hInstance, pName );
}

const char *COM_OffsetNameForFunction( void *function )
{
	static string sname;
	Q_snprintf( sname, MAX_STRING, "ofs:%zu", ((byte*)function - (byte*)svgame.dllFuncs.pfnGameInit) );
	Con_Reportf( "%s: %s\n", __func__, sname );
	return sname;
}

dll_user_t *FS_FindLibrary( const char *dllname, qboolean directpath )
{
	dll_user_t *p;
	fs_dllinfo_t dllInfo;

	// no fs loaded yet, but let engine find fs
	if( !g_fsapi.FindLibrary )
	{
		p = Mem_Calloc( host.mempool, sizeof( dll_user_t ));
		Q_strncpy( p->shortPath, dllname, sizeof( p->shortPath ));
		Q_strncpy( p->fullPath, dllname, sizeof( p->fullPath ));
		Q_strncpy( p->dllName, dllname, sizeof( p->dllName ));

		return p;
	}

	// fs can't find library
	if( !g_fsapi.FindLibrary( dllname, directpath, &dllInfo ))
		return NULL;

	// NOTE: for libraries we not fail even if search is NULL
	// let the OS find library himself
	p = Mem_Calloc( host.mempool, sizeof( dll_user_t ));
	Q_strncpy( p->shortPath, dllInfo.shortPath, sizeof( p->shortPath ));
	Q_strncpy( p->fullPath, dllInfo.fullPath, sizeof( p->fullPath ));
	Q_strncpy( p->dllName, dllname, sizeof( p->dllName ));
	p->custom_loader = dllInfo.custom_loader;
	p->encrypted = dllInfo.encrypted;

	return p;
}

/*
=============================================================================

	LIBRARY NAMING(see Documentation/library-naming.md for more info)

=============================================================================
*/

static void COM_GenerateCommonLibraryName( const char *name, const char *ext, char *out, size_t size )
{
#if ( XASH_WIN32 || ( XASH_LINUX && !XASH_ANDROID ) || XASH_APPLE ) && XASH_X86
	Q_snprintf( out, size, "%s.%s", name, ext );
#elif XASH_WIN32 || ( XASH_LINUX && !XASH_ANDROID ) || XASH_APPLE
	Q_snprintf( out, size, "%s_%s.%s", name, Q_buildarch(), ext );
#else
	Q_snprintf( out, size, "%s_%s_%s.%s", name, Q_buildos(), Q_buildarch(), ext );
#endif
}

/*
==============
COM_GenerateClientLibraryPath

Generates platform-unique and compatible name for client libraries
==============
*/
static void COM_GenerateClientLibraryPath( const char *name, char *out, size_t size )
{
#ifdef XASH_INTERNAL_GAMELIBS // assuming library loader knows where to get libraries
	Q_strncpy( out, name, size );
#else
	string dllpath;

#if XASH_ANDROID
	Q_snprintf( dllpath, sizeof( dllpath ), "%s/lib%s", GI->dll_path, name );
#else
	Q_snprintf( dllpath, sizeof( dllpath ), "%s/%s", GI->dll_path, name );
#endif

	COM_GenerateCommonLibraryName( dllpath, OS_LIB_EXT, out, size );
#endif
}

/*
==============
COM_StripIntelSuffix

Some modders use _i?86 suffix in game library name
So strip it to follow library naming for non-Intel CPUs
==============
*/
static inline void COM_StripIntelSuffix( char *out )
{
	char *suffix = Q_strrchr( out, '_' );

	if( suffix && Q_stricmpext( "_i?86", suffix ))
		*suffix = 0;
}

/*
==============
COM_GenerateServerLibraryPath

Generates platform-unique and compatible name for server library
==============
*/
static void COM_GenerateServerLibraryPath( const char *alt_dllname, char *out, size_t size )
{
#ifdef XASH_INTERNAL_GAMELIBS // assuming library loader knows where to get libraries
	Q_strncpy( out, "server", size );
#elif XASH_X86 && XASH_WIN32
	Q_strncpy( out, GI->game_dll, size );
#elif XASH_X86 && XASH_APPLE
	Q_strncpy( out, GI->game_dll_osx, size );
#elif XASH_X86 && XASH_LINUX && !XASH_ANDROID
	Q_strncpy( out, GI->game_dll_linux, size );
	COM_StripExtension( out );

	// GoldSrc actually strips everything after '_', causing issues for mods that have '_' in the DLL name on Linux
	// e.g. delta_particles.so becomes delta.so. We're gonna be smarter and just drop the _i?86 if it matches...
	// ... until somebody complains :)
	COM_StripIntelSuffix( out );
	COM_DefaultExtension( out, "." OS_LIB_EXT, size );
#else
	string temp, dir, dllpath, ext;
	const char *dllname;

#if XASH_WIN32
	Q_strncpy( temp, GI->game_dll, sizeof( temp ));
#elif XASH_APPLE
	Q_strncpy( temp, GI->game_dll_osx, sizeof( temp ));
#else
	Q_strncpy( temp, GI->game_dll_linux, sizeof( temp ));
#endif

	// path to the dll directory
	COM_ExtractFilePath( temp, dir );

	if( alt_dllname )
	{
		dllname = alt_dllname;
		Q_strncpy( ext, OS_LIB_EXT, sizeof( ext ));
	}
	else
	{
		// cleaned up dll name
		Q_strncpy( ext, COM_FileExtension( temp ), sizeof( ext ));
		COM_StripExtension( temp );
		COM_StripIntelSuffix( temp );
		dllname = COM_FileWithoutPath( temp );
	}

	// add `lib` prefix if required by platform
#if XASH_ANDROID
	Q_snprintf( dllpath, sizeof( dllpath ), "%s/lib%s", dir, dllname );
#else
	Q_snprintf( dllpath, sizeof( dllpath ), "%s/%s", dir, dllname );
#endif

	// and finally add platform suffix
	COM_GenerateCommonLibraryName( dllpath, ext, out, size );
#endif
}


/*
==============
COM_GetCommonLibraryPath

Generates platform-unique and compatible name for server library
==============
*/
void COM_GetCommonLibraryPath( ECommonLibraryType eLibType, char *out, size_t size )
{
	switch( eLibType )
	{
	case LIBRARY_GAMEUI:
		if( COM_CheckStringEmpty( host.menulib ))
		{
			if( host.menulib[0] == '@' )
				COM_GenerateClientLibraryPath( host.menulib + 1, out, size );
			else Q_strncpy( out, host.menulib, size );
		}
		else COM_GenerateClientLibraryPath( "menu", out, size );
		break;
	case LIBRARY_CLIENT:
		if( COM_CheckStringEmpty( host.clientlib ))
		{
			if( host.clientlib[0] == '@' )
				COM_GenerateClientLibraryPath( host.clientlib + 1, out, size );
			else Q_strncpy( out, host.clientlib, size );
		}
		else COM_GenerateClientLibraryPath( "client", out, size );
		break;
	case LIBRARY_SERVER:
		if( COM_CheckStringEmpty( host.gamedll ))
		{
			if( host.gamedll[0] == '@' )
				COM_GenerateServerLibraryPath( host.gamedll + 1, out, size );
			else Q_strncpy( out, host.gamedll, size );
		}
		else COM_GenerateServerLibraryPath( NULL, out, size );
		break;
	default:
		ASSERT( 0 );
		out[0] = 0;
		break;
	}
}

/*
=============================================================================

	C++ MANGLE CONVERSION

=============================================================================
*/
#define MAX_NESTED_NAMESPACES 16 /* MSVC limit */

static EFunctionMangleType COM_DetectMangleType( const char *str )
{
	// Itanium C++ ABI mangling always start with _Z
	// namespaces start with N, therefore _ZN
	if( !Q_strncmp( str, "_ZN", 3 ) )
		return MANGLE_ITANIUM;

	// MSVC C++ mangling always start with ? and have
	if( str[0] == '?' && Q_strstr( str, "@@" ))
		return MANGLE_MSVC;

	// allow offsets, we just silently ignore them on conversion
	if( !Q_strncmp( str, "ofs:", 4 ))
		return MANGLE_OFFSET;

	// don't get confused by MSVC C mangling
	if( str[0] != '@' && Q_strchr( str, '@' ))
		return MANGLE_VALVE;

	// not technically an error
	return MANGLE_UNKNOWN;
}

char *COM_GetMSVCName( const char *in_name )
{
	static string   out_name;
	char            *pos;

	if( in_name[0] == '?' )  // is this a MSVC C++ mangled name?
	{
		if(( pos = Q_strstr( in_name, "@@" )) != NULL )
		{
			ptrdiff_t len = pos - in_name;

			// strip off the leading '?'
			Q_strncpy( out_name, in_name + 1, sizeof( out_name ));
			out_name[len-1] = 0; // terminate string at the "@@"
			return out_name;
		}
	}

	Q_strncpy( out_name, in_name, sizeof( out_name ));

	return out_name;
}

static char *COM_GetItaniumName( const char * const in_name )
{
	static string out_name;
	const char *f = in_name;
	string symbols[16];
	uint len = 0;
	int i;
	int remaining;

	remaining = Q_strlen( f );

	if( remaining < 3 )
		goto invalid_format;

	out_name[0] = 0;

	// skip _ZN
	f += 3;
	remaining -= 3;

	for( i = 0; i < MAX_NESTED_NAMESPACES; i++ )
	{
		// parse symbol length marker
		len = 0;
		for( ; isdigit( *f ) && remaining > 0; f++, remaining-- )
			len = len * 10 + ( *f - '0' );

		// sane value
		len = Q_min( remaining, len );

		if( len == 0 )
			goto invalid_format;

		Q_strncpy( symbols[i], f, Q_min( len + 1, sizeof( out_name )));
		f += len;
		remaining -= len;

		// end marker
		if( *f == 'E' )
			break;

		if( !isdigit( *f ) || remaining <= 0 )
			goto invalid_format;
	}

	if( i == MAX_NESTED_NAMESPACES )
	{
		Con_DPrintf( "%s: too much nested namespaces: %s\n", __func__, in_name );
		return NULL;
	}

	for( ; i >= 0; i-- )
	{
		Q_strncat( out_name, symbols[i], sizeof( out_name ));
		if( i > 0 )
			Q_strncat( out_name, "@", sizeof( out_name ));
	}

	return out_name;

invalid_format:
	Con_DPrintf( "%s: invalid format: %s\n", __func__, in_name );
	return NULL;
}

char **COM_ConvertToLocalPlatform( EFunctionMangleType to, const char *from, size_t *numfuncs )
{
	string symbols[MAX_NESTED_NAMESPACES], temp, temp2;
	const char *prev;
	const char *postfix[3];
	int i = 0;
	char **ret;

	// TODO:
	if( to == MANGLE_MSVC )
		return NULL;

	switch( to )
	{
	case MANGLE_ITANIUM:
		postfix[0] = "Ev";
		postfix[1] = "EP11CBaseEntity";
		postfix[2] = "EP11CBaseEntityS1_8USE_TYPEf";
		break;
	default:
		ASSERT( 0 );
		return NULL;
	}

	prev = from;

	for( i = 0; i < MAX_NESTED_NAMESPACES; i++ )
	{
		const char *at = Q_strchr( prev, '@' );
		uint len;

		if( at ) len = (uint)( at - prev );
		else len = (uint)Q_strlen( prev );

		Q_strncpy( symbols[i], prev, Q_min( len + 1, sizeof( symbols[i] )));

		if( !at )
			break;

		prev = at + 1;
	}

	if( i == MAX_NESTED_NAMESPACES )
	{
		Con_DPrintf( "%s: too much nested namespaces: %s\n", __func__, from );
		return NULL;
	}

	// only three possible variations
	*numfuncs = ARRAYSIZE( postfix );
	ret = Z_Malloc( sizeof( char * ) * ARRAYSIZE( postfix ) );

	Q_strncpy( temp, "_ZN", sizeof( temp ));

	for( ; i >= 0; i-- )
	{
		Q_snprintf( temp2, sizeof( temp2 ), "%u%s", (uint)Q_strlen( symbols[i] ), symbols[i] );
		Q_strncat( temp, temp2, sizeof( temp ));
	}

	for( i = 0; i < ARRAYSIZE( postfix ); i++ )
	{
		Q_snprintf( temp2, sizeof( temp2 ), "%s%s", temp, postfix[i] );
		ret[i] = copystring( temp2 );
	}

	return ret;
}

const char *COM_GetPlatformNeutralName( const char *in_name )
{
	EFunctionMangleType type = COM_DetectMangleType( in_name );

	switch( type )
	{
	case MANGLE_ITANIUM: return COM_GetItaniumName( in_name );
	case MANGLE_MSVC: return COM_GetMSVCName( in_name );
	default: return in_name;
	}
}

#if XASH_ENGINE_TESTS
#include "tests.h"

static void Test_DetectMangleType( void )
{
	TASSERT(COM_DetectMangleType( "asdf" ) == MANGLE_UNKNOWN );
	TASSERT(COM_DetectMangleType( "012345" ) == MANGLE_UNKNOWN );
	TASSERT(COM_DetectMangleType( "?asdf" ) == MANGLE_UNKNOWN );
	TASSERT(COM_DetectMangleType( "_Zasdf" ) == MANGLE_UNKNOWN );

	TASSERT(COM_DetectMangleType( "ofs:1234" ) == MANGLE_OFFSET );
	TASSERT(COM_DetectMangleType( "ofs:asdf" ) == MANGLE_OFFSET );
	TASSERT(COM_DetectMangleType( "ofs:" ) == MANGLE_OFFSET );

	TASSERT(COM_DetectMangleType( "_ZN1f1fEv" ) == MANGLE_ITANIUM );
	TASSERT(COM_DetectMangleType( "_ZN3foo3barEv" ) == MANGLE_ITANIUM );

	TASSERT(COM_DetectMangleType( "?f@f@@msvcsucks" ) == MANGLE_MSVC );
	TASSERT(COM_DetectMangleType( "?foo@bar@@IHATEMSVC" ) == MANGLE_MSVC );

	TASSERT(COM_DetectMangleType( "f@f" ) == MANGLE_VALVE );
	TASSERT(COM_DetectMangleType( "foo@bar" ) == MANGLE_VALVE );

	// Xash3D FWGS extensions test
	TASSERT(COM_DetectMangleType( "_ZN1f1f1fEv" ) == MANGLE_ITANIUM );
	TASSERT(COM_DetectMangleType( "_ZN3foo3bar3bazEv" ) == MANGLE_ITANIUM );

	TASSERT(COM_DetectMangleType( "?f@f@f@@msvcsucks" ) == MANGLE_MSVC );
	TASSERT(COM_DetectMangleType( "?foo@bar@@IHATEMSVC" ) == MANGLE_MSVC );

	TASSERT(COM_DetectMangleType( "f@f@f" ) == MANGLE_VALVE );
	TASSERT(COM_DetectMangleType( "foo@bar@baz" ) == MANGLE_VALVE );
}

static void Test_GetMSVCName( void )
{
	const char *symbols[] =
	{
		"", "",
		"?f@f@@XYZA", "f@f",
		"?foo@bar@@QAEXXZ", "foo@bar",
		"foo", "foo",
		"?foo", "?foo",
		"?foo@@", "foo", // not an error?
		"?foo@bar@baz@@gotstrippedanyway","foo@bar@baz"
	};
	int i;

	for( i = 0; i < ARRAYSIZE( symbols ); i += 2 )
	{
		Msg( "Checking if MSVC '%s' converts to '%s'...\n", symbols[i], symbols[i+1] );

		TASSERT( !Q_strcmp( COM_GetMSVCName( symbols[i] ), symbols[i+1] ));
	}
}

static void Test_GetItaniumName( void )
{
	const char *symbols[] =
	{
		"", NULL,
		"_", NULL,
		"_Z", NULL,
		"_ZN", NULL,
		"_ZNv", NULL,
		"_ZN4barr3foo", NULL,
		"_ZN3bar3foov", NULL,
		"_ZN4bar3fooEv", NULL,
		"_ZN3bar3fooEv", "foo@bar",
		"_Z3foov", NULL,
		"_ZN3fooEv", "foo", // not possible?
		"_ZN3baz3bar3fooEdontcare", "foo@bar@baz",
	};
	int i;

	for( i = 0; i < ARRAYSIZE( symbols ); i += 2 )
	{
		Msg( "Checking if Itanium '%s' converts to '%s'...\n", symbols[i], symbols[i+1] );

		TASSERT( !Q_strcmp( COM_GetItaniumName( symbols[i] ), symbols[i+1] ));
	}
}

static void Test_ConvertFromValveToLocal( void )
{
	const char *symbols[] =
	{
		"", "_ZN",
		"foo", "_ZN3foo",
		"xash3d@fwgs", "_ZN4fwgs6xash3d",
		"foo@bar@bazz", "_ZN4bazz3bar3foo"
	};
	int i;

	for( i = 0; i < ARRAYSIZE( symbols ); i += 2 )
	{
		char **ret;
		size_t numfuncs;
		size_t symlen = Q_strlen( symbols[i + 1] );

		Msg( "Checking if Valve '%s' converts to Itanium '%s'...\n", symbols[i], symbols[i+1] );

		ret = COM_ConvertToLocalPlatform( MANGLE_ITANIUM, symbols[i], &numfuncs );

		TASSERT( numfuncs == 3 );
		TASSERT( !Q_strncmp( ret[0], symbols[i+1], symlen ));
		TASSERT( !Q_strncmp( ret[1], symbols[i+1], symlen ));
		TASSERT( !Q_strncmp( ret[2], symbols[i+1], symlen ));
	}
}

void Test_RunLibCommon( void )
{
	TRUN( Test_DetectMangleType() );
	TRUN( Test_GetMSVCName() );
	TRUN( Test_GetItaniumName() );
	TRUN( Test_ConvertFromValveToLocal() );
}
#endif /* XASH_ENGINE_TESTS */
