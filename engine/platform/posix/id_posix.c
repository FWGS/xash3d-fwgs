/*
id_posix.c - network device enumeration for unique id generation
Copyright (C) 2026 Xash3D FWGS contributors

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

#if XASH_LINUX
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

static qboolean Posix_ValidateNetDevice( const char *dev )
{
	const char *prefix = "/sys/class/net";

	// These devices are fake, their mac address is generated each boot, while assign_type is 0
	if( !Q_strnicmp( dev, "ccmni", sizeof( "ccmni" ) - 1 ) || !Q_strnicmp( dev, "ifb", sizeof( "ifb" ) - 1 ))
		return false;

	byte *pfile = FS_LoadDirectFile( va( "%s/%s/addr_assign_type", prefix, dev ), NULL );

	// if NULL, it may be old kernel
	if( pfile )
	{
		int assignType = Q_atoi( (char*)pfile );

		Mem_Free( pfile );

		// check is MAC address is constant
		if( assignType != 0 )
			return false;
	}

	return true;
}

int Posix_GetNetDeviceAddresses( uint64_t *addresses, int max )
{
	const char *prefix = "/sys/class/net";
	DIR *dir = opendir( prefix );
	struct dirent *entry;
	int count = 0;

	if( !dir )
		return 0;

	while(( entry = readdir( dir )) && count < max )
	{
		if( !Q_strcmp( entry->d_name, "." ) || !Q_strcmp( entry->d_name, ".." ))
			continue;

		if( !Posix_ValidateNetDevice( entry->d_name ))
			continue;

		int fd = open( va( "%s/%s/address", prefix, entry->d_name ), O_RDONLY );

		if( fd < 0 )
			continue;

		char buffer[64];
		int ret = read( fd, buffer, sizeof( buffer ) - 1 );

		close( fd );

		if( ret <= 0 )
			continue;

		buffer[ret] = 0;

		uint mac[6];
		char term = 0;

		if( sscanf( buffer, "%02x:%02x:%02x:%02x:%02x:%02x%c", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &term ) < 6 )
			continue;

		// skip malformed or long address devices, we only need MAC addresses and they're always 48-bit
		if( term != '\n' && term != '\0' )
			continue;

		addresses[count] = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) | ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
		count++;
	}

	closedir( dir );
	return count;
}
#elif XASH_APPLE || XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>

int Posix_GetNetDeviceAddresses( uint64_t *addresses, int max )
{
	struct ifaddrs *ifaddr;
	int count = 0;

	if( getifaddrs( &ifaddr ) < 0 )
		return 0;

	for( struct ifaddrs *ifa = ifaddr; ifa != NULL && count < max; ifa = ifa->ifa_next )
	{
		if( !ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK )
			continue;

		if( FBitSet( ifa->ifa_flags, IFF_LOOPBACK | IFF_POINTOPOINT ))
			continue;

		const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)ifa->ifa_addr;

		if( sdl->sdl_alen != 6 )
			continue;

		const byte *mac = (const byte *)LLADDR( sdl );

		addresses[count] = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) | ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | (uint64_t)mac[5];
		count++;
	}

	freeifaddrs( ifaddr );
	return count;
}
#else
int Posix_GetNetDeviceAddresses( uint64_t *addresses, int max )
{
	Con_Reportf( S_WARN "%s: implement me!\n", __func__ );
	return 0;
}
#endif
