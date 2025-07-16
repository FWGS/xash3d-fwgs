/*
cl_spray.h - spray conversion for GoldSrc protocol
Copyright (C) 2025 Xash3D FWGS contributors
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef CL_SPRAY_H
#define CL_SPRAY_H

// constants
#define SPRAY_MAX_SURFACE   12228
#define SPRAY_PALETTE_SIZE  256
#define SPRAY_PALETTE_BYTES ( SPRAY_PALETTE_SIZE * 3 )
#define SPRAY_FILENAME      "tempdecal.wad"

qboolean CL_ConvertImageToWAD3( const char *filename );

#endif // CL_SPRAY_H