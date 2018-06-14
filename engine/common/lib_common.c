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
	MsgDev( D_NOTE, "COM_OffsetNameForFunction %s\n", sname );
	return sname;
}
