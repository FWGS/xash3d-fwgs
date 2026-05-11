/*
net_psvita.h - psvita network stubs
Copyright (C) 2021-2023 fgsfds

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
#ifndef NET_PSVITA_H
#define NET_PSVITA_H

#include <vitasdk.h>
#include "platform/posix/net.h"

/* we're missing IPv6 support; define some trash */
#define XASH_NO_IPV6_RESOLVE 1

#ifndef IN6_IS_ADDR_V4MAPPED
#define IN6_IS_ADDR_V4MAPPED( p ) ( 0 )
#endif

#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifndef IPV6_MULTICAST_LOOP
#define IPV6_MULTICAST_LOOP 19
#endif

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 26
#endif

#ifndef FIONBIO
#define FIONBIO SO_NONBLOCK
#endif

/* ioctlsocket() is only used to set non-blocking on sockets */

static inline int ioctl_psvita( int fd, int req, unsigned int *arg )
{
	if ( req == FIONBIO )
	{
		return setsockopt( fd, SOL_SOCKET, SO_NONBLOCK, arg, sizeof( *arg ) );
	}
	return -ENOSYS;
}

#define ioctlsocket ioctl_psvita

#endif // NET_PSVITA_H
