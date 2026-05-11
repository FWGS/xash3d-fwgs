/*
utils.h - Useful helper functions
Copyright (C) 2020 Andrey Akhmichin

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifndef	UTILS_H
#define UTILS_H

qboolean	 MakeDirectory( const char *path );
qboolean	 MakeFullPath( const char *path );
void		 ExtractFileName( char *name, size_t size );
off_t		 GetSizeOfFile( FILE *fp );
byte		*LoadFile( const char *filename, off_t *size );
void		 LogPutS( const char *str );
void		 LogPrintf( const char *szFmt, ... );

#endif // UTILS_H

