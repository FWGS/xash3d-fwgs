/*
ps4_sockaddr.h - fix struct sockaddr_storage of OpenOrbis toolchain
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

/*
OpenOrbis headers declare BSD style sockaddr and sockaddr_in (length byte
followed by family byte), but sockaddr_storage keeps Linux layout with the
family at offset 0. Code that sets or checks ss_family then actually uses
the length byte, so the kernel sees family 0: bind() fails with
EAFNOSUPPORT and socket( addr.ss_family, ... ) with EPROTONOSUPPORT.

This header is force-included into every source file of PS4 build
(see wscript), replacing the structure with correct FreeBSD layout.
*/
#pragma once
#ifndef PS4_SOCKADDR_H
#define PS4_SOCKADDR_H

#include <sys/socket.h>
#include <stdint.h>

struct xash_ps4_sockaddr_storage
{
	uint8_t     ss_len;
	sa_family_t ss_family;
	char        __ss_pad1[6];
	int64_t     __ss_align;
	char        __ss_pad2[112];
};

#define sockaddr_storage xash_ps4_sockaddr_storage

#endif // PS4_SOCKADDR_H
