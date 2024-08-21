/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef NET_ADR_H
#define NET_ADR_H

#include STDINT_H

// net.h -- quake's interface to the networking layer
// a1ba: copied from Quake-2/qcommon/qcommon.h and modified to support IPv6

#define	PORT_ANY	-1

typedef enum {NA_LOOPBACK = 1, NA_BROADCAST, NA_IP, NA_IPX, NA_BROADCAST_IPX, NA_IP6, NA_MULTICAST_IP6} netadrtype_t;

/*
Original Quake-2 structure:
typedef struct
{
	netadrtype_t	type;

	byte	ip[4];
	byte	ipx[10];

	unsigned short	port;
} netadr_t;
*/

#pragma pack( push, 1 )
typedef struct netadr_s
{
	union
	{
		// IPv6 struct
		struct
		{
			uint16_t	type6;
			uint8_t	ip6[16];
		};
		struct
		{
			uint32_t	type; // must be netadrtype_t but will break with short enums
			union
			{
				uint8_t	ip[4];
				uint32_t	ip4; // for easier conversions
			};
			uint8_t	ipx[10];
		};
	};
	uint16_t	port;
} netadr_t;
#pragma pack( pop )

STATIC_CHECK_SIZEOF( netadr_t, 20, 20 );

#endif // NET_ADR_H
