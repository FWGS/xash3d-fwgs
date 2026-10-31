/*
net_ogc.h - Network stub for libogc

Copyright (C) 2026 mintferret

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/
#ifndef NET_OGC_H
#define NET_OGC_H

#include <network.h>

#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>

#include <errno.h>
#include <unistd.h>

// return the wii's ip address
static inline int ogc_net_gethostname( char *name, size_t len )
{
	struct in_addr addr;
	addr.s_addr = net_gethostip();
	strncpy( name, inet_ntoa(addr), len );

	if( len > 0 )
		name[len - 1] = '\0';

	return 0;
}

//net_socket is already defined
static inline int ogc_net_socket( int d, int t, int p )
{

    int ret = net_socket( d, t, 0 );
	if ( ret < 0 )
	{
		return -1;
	}
	return ret;
}


 //Note: below here is stuff adapted from the quake3 port

//build 8-byte POSIX addr for IOS (no sin_len prefix, tolen=8)
static inline int ogc_net_sendto( int s, const void *b, int l, int f, const struct sockaddr *a, int al )
{
	uint8_t ios_addr[8];
	const struct sockaddr *send_a  = a;

	if ( a && a->sa_family == AF_INET ) {
		const struct sockaddr_in *sin4 = ( const struct sockaddr_in * ) a;
		uint16_t fam = ( uint16_t )AF_INET;
		memcpy( ios_addr + 0, &fam,            2 );
		memcpy( ios_addr + 2, &sin4->sin_port, 2 );
		memcpy( ios_addr + 4, &sin4->sin_addr, 4 );
		send_a  = ( const struct sockaddr * )ios_addr;
	}

    return net_sendto( s, (void *)b, l, f, ( struct sockaddr * )send_a, 8 );
}

//recvfrom: normalize IOS error codes to -1
static inline int ogc_net_recvfrom( int s, void *b, int l, int f, struct sockaddr *a, socklen_t *al )
{
    int ret = net_recvfrom( s, b, l, f, a, al );
    if ( ret >= 0 )
		return ret;

	errno = EAGAIN;
    return -1;
}

#define ioctlsocket net_ioctl
#define closesocket net_close

#define select(n,r,w,e,t)           net_select((n),(r),(w),(e),(t))

#define gethostname(n,l)            ogc_net_gethostname((n),(l))
#define socket(d,t,p)               ogc_net_socket((d),(t),(p))
#define sendto(s,b,l,f,a,al)        ogc_net_sendto((s),(b),(l),(f),(a),(al))
#define recvfrom(s,b,l,f,a,al)      ogc_net_recvfrom((s),(b),(l),(f),(a),(al))

#define bind(s,a,l)                 net_bind((s),(a),(l))
#define fcntl(s,cmd,arg)            net_fcntl((s),(cmd),(arg))

#define connect(s,a,l)              net_connect((s),(a),(l))
#define listen(s,b)                 net_listen((s),(b))
#define accept(s,a,l)               net_accept((s),(a),(l))
#define send(s,b,l,f)               net_send((s),(b),(l),(f))
#define recv(s,b,l,f)               net_recv((s),(b),(l),(f))
#define setsockopt(s,lv,o,v,l)      net_setsockopt((s),(lv),(o),(v),(l))
#define getsockopt(s,lv,o,v,l)      net_getsockopt((s),(lv),(o),(v),(l))
#define getsockname(s,a,l)          net_getsockname((s),(a),(l))
#define gethostbyname(n)            net_gethostbyname(n)

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

#undef INVALID_SOCKET
#undef SOCKET_ERROR
#define INVALID_SOCKET 	-1
#define SOCKET_ERROR 	-1

#define SOCKET int
typedef int WSAsize_t;

#endif // NET_OGC_H
