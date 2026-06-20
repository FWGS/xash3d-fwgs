/*
net_stub.h - stub BSD sockets
Copyright (C) 2020 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef NET_STUB_H
#define NET_STUB_H

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SOCKET int
typedef int WSAsize_t;
typedef unsigned int socklen_t;

struct in_addr { unsigned long s_addr; };
struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in { short sin_family; unsigned short sin_port; struct in_addr sin_addr; };
struct sockaddr_in6 { short sin6_family; unsigned short sin6_port; unsigned int sin6_flowinfo; struct in6_addr sin6_addr; unsigned int sin6_scope_id; };
struct sockaddr { short sa_family; int stub[32]; };
struct sockaddr_storage { short ss_family; char __ss_pad[126]; };
struct hostent { int h_addr_list[1]; };
struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

static const struct in6_addr in6addr_any;

#define AF_INET 2
#define AF_INET6 10
#define AF_UNSPEC 0
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define INADDR_BROADCAST 0xffffffff
#define INADDR_ANY 0

#define ntohs(n) ( (((n) & 0xFF00) >> 8) | (((n) & 0x00FF) << 8) )
#define htons(n) ( (((n) & 0xFF00) >> 8) | (((n) & 0x00FF) << 8) )
#define ntohl(n) ( (((n) & 0xFF000000) >> 24) | (((n) & 0x00FF0000) >> 8) \
	 | (((n) & 0x0000FF00) << 8) | (((n) & 0x000000FF) << 24) )
#define htonl(n) ( (((n) & 0xFF000000) >> 24) | (((n) & 0x00FF0000) >> 8) \
	 | (((n) & 0x0000FF00) << 8) | (((n) & 0x000000FF) << 24) )

#define gethostbyname(...) NULL
#define inet_addr(...) ((unsigned long)-1)
#define recvfrom(...) (-1)
#define sendto(...) (-1)
#define socket(...) (-1)
#define ioctlsocket(...) (-1)
#define setsockopt(...) (-1)
#define getsockopt(...) (-1)
#define gethostname(...) (-1)
#define getsockname(...) (-1)
#define connect(...) (-1)
#define send(...) (-1)
#define recv(...) (-1)
#define listen(...) (-1)
#define accept(...) (-1)
#define bind(...) (-1)
#define closesocket(...) (-1)
#define select(...) (-1)
#define getaddrinfo(...) (-1)
#define freeaddrinfo(...) ((void)0)

#define WSAGetLastError() (22)
#define WSAEINTR           1
#define WSAEBADF           2
#define WSAEACCES          3
#define WSAEFAULT          4
#define WSAEINVAL          5
#define WSAEMFILE          6
#define WSAEWOULDBLOCK     7
#define WSAEINPROGRESS     8
#define WSAEALREADY        9
#define WSAENOTSOCK        10
#define WSAEDESTADDRREQ    11
#define WSAEMSGSIZE        12
#define WSAEPROTOTYPE      13
#define WSAENOPROTOOPT     14
#define WSAEPROTONOSUPPORT 15
#define WSAESOCKTNOSUPPORT 16
#define WSAEOPNOTSUPP      17
#define WSAEPFNOSUPPORT    18
#define WSAEAFNOSUPPORT    19
#define WSAEADDRINUSE      20
#define WSAEADDRNOTAVAIL   21
#define WSAENETDOWN        22
#define WSAENETUNREACH     23
#define WSAENETRESET       24
#define WSAECONNABORTED    25
#define WSAECONNRESET      26
#define WSAENOBUFS         27
#define WSAEISCONN         28
#define WSAENOTCONN        29
#define WSAESHUTDOWN       30
#define WSAETOOMANYREFS    31
#define WSAETIMEDOUT       32
#define WSAECONNREFUSED    33
#define WSAELOOP           34
#define WSAENAMETOOLONG    35
#define WSAEHOSTDOWN       36

#endif /* NET_STUB_H */
