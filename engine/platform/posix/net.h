/*
net.h - WinSock to BSD sockets wrap
Copyright (C) 2022 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef NET_H
#define NET_H

#include <sys/types.h>
#include <sys/socket.h>
#if !XASH_PSVITA
#include <sys/ioctl.h>
#endif
#if XASH_SUNOS // TODO: figure out if we need this header on other systems
#include <sys/filio.h>
#endif
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#if XASH_IRIX
#include <sys/time.h>
#endif

#define WSAGetLastError()  errno
#define WSAEINTR           EINTR
#define WSAEBADF           EBADF
#define WSAEACCES          EACCES
#define WSAEFAULT          EFAULT
#define WSAEINVAL          EINVAL
#define WSAEMFILE          EMFILE
#define WSAEWOULDBLOCK     EWOULDBLOCK
#define WSAEINPROGRESS     EINPROGRESS
#define WSAEALREADY        EALREADY
#define WSAENOTSOCK        ENOTSOCK
#define WSAEDESTADDRREQ    EDESTADDRREQ
#define WSAEMSGSIZE        EMSGSIZE
#define WSAEPROTOTYPE      EPROTOTYPE
#define WSAENOPROTOOPT     ENOPROTOOPT
#define WSAEPROTONOSUPPORT EPROTONOSUPPORT
#define WSAESOCKTNOSUPPORT ESOCKTNOSUPPORT
#define WSAEOPNOTSUPP      EOPNOTSUPP
#define WSAEPFNOSUPPORT    EPFNOSUPPORT
#define WSAEAFNOSUPPORT    EAFNOSUPPORT
#define WSAEADDRINUSE      EADDRINUSE
#define WSAEADDRNOTAVAIL   EADDRNOTAVAIL
#define WSAENETDOWN        ENETDOWN
#define WSAENETUNREACH     ENETUNREACH
#define WSAENETRESET       ENETRESET
#define WSAECONNABORTED    ECONNABORTED
#define WSAECONNRESET      ECONNRESET
#define WSAENOBUFS         ENOBUFS
#define WSAEISCONN         EISCONN
#define WSAENOTCONN        ENOTCONN
#define WSAESHUTDOWN       ESHUTDOWN
#define WSAETOOMANYREFS    ETOOMANYREFS
#define WSAETIMEDOUT       ETIMEDOUT
#define WSAECONNREFUSED    ECONNREFUSED
#define WSAELOOP           ELOOP
#define WSAENAMETOOLONG    ENAMETOOLONG
#define WSAEHOSTDOWN       EHOSTDOWN


#ifndef XASH_DOS4GW
#define HAVE_GETADDRINFO
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

#if XASH_EMSCRIPTEN
/* All socket operations are non-blocking already */
static int ioctl_stub( int d, unsigned long r, ... )
{
	return 0;
}
#define ioctlsocket ioctl_stub
#elif !XASH_PSVITA // XASH_EMSCRIPTEN
#define ioctlsocket ioctl
#endif // XASH_EMSCRIPTEN
#define closesocket close
#endif
#define SOCKET int
typedef int WSAsize_t;

#endif // NET_H
