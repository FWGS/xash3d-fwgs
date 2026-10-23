/*
ps4_sockaddr.h - fix struct sockaddr_storage of OpenOrbis toolchain
Copyright (c) 2026 Xash3D FWGS contributors

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software. See LICENSE for the full text.
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
