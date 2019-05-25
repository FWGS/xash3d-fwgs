/*
filesystem.h - engine FS
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

/*
========================================================================
PAK FILES

The .pak files are just a linear collapse of a directory tree
========================================================================
*/
// header
#define IDPACKV1HEADER	(('K'<<24)+('C'<<16)+('A'<<8)+'P')	// little-endian "PACK"

#define MAX_FILES_IN_PACK	65536 // pak

typedef struct
{
	int		ident;
	int		dirofs;
	int		dirlen;
} dpackheader_t;

typedef struct
{
	char		name[56];		// total 64 bytes
	int		filepos;
	int		filelen;
} dpackfile_t;

/*
========================================================================
.WAD archive format	(WhereAllData - WAD)

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
#define WAD3_NAMELEN	16
#define HINT_NAMELEN	5	// e.g. _mask, _norm
#define MAX_FILES_IN_WAD	65535	// real limit as above <2Gb size not a lumpcount

#include "const.h"

typedef struct
{
	int		ident;		// should be WAD3
	int		numlumps;		// num files
	int		infotableofs;	// LUT offset
} dwadinfo_t;

typedef struct
{
	int		filepos;		// file offset in WAD
	int		disksize;		// compressed or uncompressed
	int		size;		// uncompressed
	signed char	type;		// TYP_*
	signed char	attribs;		// file attribs
	char		pad0;
	char		pad1;
	char		name[WAD3_NAMELEN];	// must be null terminated
} dlumpinfo_t;

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

#define ZIP_HEADER_LF (('K'<<8)+('P')+(0x03<<16)+(0x04<<24))
#define ZIP_HEADER_SPANNED ((0x08<<24)+(0x07<<16)+('K'<<8)+'P')

#define ZIP_HEADER_CDF ((0x02<<24)+(0x01<<16)+('K'<<8)+'P')
#define ZIP_HEADER_EOCD ((0x06<<24)+(0x05<<16)+('K'<<8)+'P')

#define ZIP_COMPRESSION_NO_COMPRESSION      0
#define ZIP_COMPRESSION_DEFLATED            8

#define ZIP_ZIP64 0xffffffff

#pragma pack( 1 )
typedef struct zip_header_s
{
	uint signature; // little endian ZIP_HEADER
	u_int16_t version; // version of pkzip need to unpack
        u_int16_t flags; // flags (16 bits == 16 flags)
        u_int16_t compression_flags; // compression flags (bits)
        uint dos_date; // file modification time and file modification date
        uint crc32; //crc32
	uint compressed_size;
	uint uncompressed_size;
	u_int16_t filename_len;
	u_int16_t extrafield_len;
} zip_header_t;

#pragma pack( )

/*
  in zip64 comp and uncompr size == 0xffffffff remeber this
  compressed and uncompress filesize stored in extra field
*/

#pragma pack( 1 )
typedef struct zip_header_extra_s
{
	uint signature; // ZIP_HEADER_SPANNED
	uint crc32;
	uint compressed_size;
	uint uncompressed_size;
} zip_header_extra_t;
#pragma pack(  )

#pragma pack( 1 )
typedef struct zip_cdf_header_s
{
	uint signature;
	u_int16_t version;
	u_int16_t version_need;
	u_int16_t generalPurposeBitFlag;
	u_int16_t flags;
	u_int16_t modification_time;
	u_int16_t modification_date;
	uint crc32;
	uint compressed_size;
	uint uncompressed_size;
	u_int16_t filename_len;
	u_int16_t extrafield_len;
	u_int16_t file_commentary_len;
	u_int16_t disk_start;
	u_int16_t internal_attr;
	uint external_attr;
	uint local_header_offset;
} zip_cdf_header_t;
#pragma pack (  )

#pragma pack( 1 )
typedef struct zip_header_eocd_s
{
	u_int16_t disk_number;
	u_int16_t start_disk_number;
	u_int16_t number_central_directory_record;
	u_int16_t total_central_directory_record;
	uint size_of_central_directory;
	uint central_directory_offset;
        u_int16_t commentary_len;
} zip_header_eocd_t;
#pragma pack( )

#endif//FILESYSTEM_H
