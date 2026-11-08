/*
lib_apple.c - Mach-O parsing for Apple platforms
Copyright (C) 2026 Alibek Omarov

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
#if XASH_APPLE && !XASH_IOS
#include "common.h"
#include <mach-o/dyld.h>

/*
=============================================================================

	DIRECT DEPENDENCY CHECK

We only parse enough of the binary to walk its list of direct dependencies.
The parser is byte order and word size agnostic, so a 64-bit engine can inspect a 32-bit game library and vice versa

=============================================================================
*/
// magics as they're laid out in the file, always read as big endian, so we
// don't have to care about the host byte order
#define MACHO_MAGIC_BE32     0xfeedfaceu
#define MACHO_MAGIC_LE32     0xcefaedfeu
#define MACHO_MAGIC_BE64     0xfeedfacfu
#define MACHO_MAGIC_LE64     0xcffaedfeu
#define MACHO_FAT_MAGIC      0xcafebabeu
#define MACHO_FAT_MAGIC64    0xcafebabfu

static uint32_t MachO_ReadU32( const byte *p, qboolean be )
{
	if( be )
		return ((uint32_t)p[0] << 24 ) | ((uint32_t)p[1] << 16 ) | ((uint32_t)p[2] << 8 ) | (uint32_t)p[3];

	return ((uint32_t)p[3] << 24 ) | ((uint32_t)p[2] << 16 ) | ((uint32_t)p[1] << 8 ) | (uint32_t)p[0];
}

static uint64_t MachO_ReadU64( const byte *p, qboolean be )
{
	if( be )
		return ((uint64_t)MachO_ReadU32( p, true ) << 32 ) | MachO_ReadU32( p + 4, true );

	return ((uint64_t)MachO_ReadU32( p + 4, false ) << 32 ) | MachO_ReadU32( p, false );
}

static qboolean MachO_CheckThinDependency( const byte *data, uint64_t size, const char *depname )
{
	if( size < 8 )
		return false;

	uint32_t hdrsize;
	qboolean be;
	switch( MachO_ReadU32( data, true ))
	{
	case MACHO_MAGIC_BE32:
		be = true;
		hdrsize = 28;
		break;
	case MACHO_MAGIC_LE32:
		be = false;
		hdrsize = 28;
		break;
	case MACHO_MAGIC_BE64:
		be = true;
		hdrsize = 32;
		break;
	case MACHO_MAGIC_LE64:
		be = false;
		hdrsize = 32;
		break;
	default:
		return false;
	}

	if( size < hdrsize )
		return false;

	uint32_t ncmds = MachO_ReadU32( data + 16, be );
	const uint32_t sizeofcmds = MachO_ReadU32( data + 20, be );

	if( sizeofcmds > size - hdrsize )
		return false;

	const uint64_t end = hdrsize + sizeofcmds;

	for( uint64_t off = hdrsize; ncmds > 0; ncmds-- )
	{
		if( end - off < 8 )
			break;

		const uint32_t cmd     = MachO_ReadU32( data + off, be );
		const uint32_t cmdsize = MachO_ReadU32( data + off + 4, be );

		if( cmdsize < 8 || cmdsize > end - off )
			break;

		switch( cmd )
		{
		case LC_LOAD_DYLIB:
		case LC_LOAD_WEAK_DYLIB:
		case LC_REEXPORT_DYLIB:
		case LC_LOAD_UPWARD_DYLIB:
			// a dylib_command is a load_command followed by four more dwords,
			// the first of which is an offset to the install name, counted
			// from the beginning of the load command
			if( cmdsize >= 24 )
			{
				const uint32_t nameoff = MachO_ReadU32( data + off + 8, be );

				if( nameoff >= 24 && nameoff < cmdsize )
				{
					const char *dep = (const char *)( data + off + nameoff );

					// install names are paths, only the file name interests us
					if( memchr( dep, '\0', cmdsize - nameoff ) && !Q_stricmp( COM_FileWithoutPath( dep ), depname ))
						return true;
				}
			}
			break;
		}

		off += cmdsize;
	}

	return false;
}

qboolean Platform_CheckLibraryDirectDependency( const byte *data, size_t size, const char *depname )
{
	if( size < 8 )
		return false;

	const uint32_t sig = MachO_ReadU32( data, true );

	if( sig != MACHO_FAT_MAGIC && sig != MACHO_FAT_MAGIC64 )
		return MachO_CheckThinDependency( data, size, depname );

	// Use the process architecture, including when running under Rosetta.
	const struct mach_header *image = _dyld_get_image_header( 0 );
	if( !image )
		return false;

	// fat headers are always stored big endian
	const qboolean fat64   = sig == MACHO_FAT_MAGIC64;
	const uint32_t entsize = fat64 ? 32 : 20;
	const uint32_t narch   = MachO_ReadU32( data + 4, true );

	for( uint32_t i = 0; i < narch; i++ )
	{
		const uint64_t hdr = 8 + (uint64_t)i * entsize;

		if( hdr > size || size - hdr < entsize )
			break;

		if( MachO_ReadU32( data + hdr, true ) != (uint32_t)image->cputype )
			continue;

		// fat_arch stores 32-bit offsets and sizes, fat_arch_64 stores 64-bit ones
		// and pads the size field out to an eight byte boundary
		const uint64_t sliceoff = fat64 ? MachO_ReadU64( data + hdr + 8, true ) : MachO_ReadU32( data + hdr + 8, true );
		const uint64_t slicesize = fat64 ? MachO_ReadU64( data + hdr + 16, true ) : MachO_ReadU32( data + hdr + 12, true );

		if( sliceoff > size || size - sliceoff < slicesize )
			continue;

		return MachO_CheckThinDependency( data + sliceoff, slicesize, depname );
	}

	return false;
}

#endif // XASH_APPLE && !XASH_IOS
