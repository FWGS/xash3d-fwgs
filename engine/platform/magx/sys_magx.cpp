/*
sys_magx.cpp - MotoMAGX system utils
Copyright (C) 2019 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "platform/platform.h"
#include <ZApplication.h>

typedef void (*pfnChangeGame)( const char *progname );
extern "C" int EXPORT Host_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );

extern "C" int EXPORT Platform_Main( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func )
{
	ZApplication app( argc, argv );
	return Host_Main( argc, argv, progname, bChangeGame, func );
}
