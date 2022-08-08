/*
hpak.c - custom user package to send other clients
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef HPAK_H
#define HPAK_H

#include "custom.h"

/*
========================================================================
.HPK archive format	(Hash PAK - HPK)

List of compressed files, that can be identify only by TYPE_*

<format>
header:	dwadinfo_t[dwadinfo_t]
file_1:	byte[dwadinfo_t[num]->disksize]
file_2:	byte[dwadinfo_t[num]->disksize]
file_3:	byte[dwadinfo_t[num]->disksize]
...
file_n:	byte[dwadinfo_t[num]->disksize]
infotable	dlumpinfo_t[dwadinfo_t->numlumps]
========================================================================
*/

#define IDHPAKHEADER	(('K'<<24)+('A'<<16)+('P'<<8)+'H') // little-endian "HPAK"
#define IDHPAK_VERSION	1

typedef struct
{
	int		ident;		// should be equal HPAK
	int		version;
	int		infotableofs;
} hpak_header_t;

typedef struct
{
	resource_t	resource;
	int		filepos;
	int		disksize;
} hpak_lump_t;

typedef struct
{
	int		count;
	hpak_lump_t	*entries;		// variable sized.
} hpak_info_t;

#endif // HPAK_H
