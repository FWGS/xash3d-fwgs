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

qboolean	 IsFileExists( const char *filename );
off_t		 GetFileSize( FILE *fp );
byte		*LoadFile( const char *filename );

#endif // UTILS_H

