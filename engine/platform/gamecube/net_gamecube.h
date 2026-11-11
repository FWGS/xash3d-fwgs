/*
 * net_gamecube.h - Map Xash BSD-socket calls onto libogc/libogc2 (BBA / eth2sp1).
 *
 * libogc2 probes the retail BBA and native W5500/ENC28J60 drivers.
 * Classic libogc supports the retail BBA only; Swiss SDK emulation is not
 * a substitute for native homebrew drivers.
 * Soft-fail: missing adapter must not crash; SP remains loopback-capable.
 */
#ifndef NET_GAMECUBE_H
#define NET_GAMECUBE_H

#include <errno.h>
#include <string.h>
#include <network.h>
#if !XASH_GAMECUBE_LIBOGC2
#include <sys/filio.h>
#endif

#define XASH_NO_IPV6_RESOLVE 1

#if XASH_GAMECUBE_LIBOGC2
/* IPv6 placeholders allow common engine code to compile; no IPv6 socket is
 * ever opened. Keep the BSD length/family prefix used by libogc2 IPv4. */
#define AF_INET6 28
#define PF_INET6 AF_INET6
#define INADDR_NONE 0xffffffffU
struct in6_addr { union { unsigned char bytes[16]; u32 words[4]; } data; };
extern const struct in6_addr in6addr_any;
#define s6_addr data.bytes
struct sockaddr_in6 {
	u8 sin6_len, sin6_family;
	u16 sin6_port;
	u32 sin6_flowinfo;
	struct in6_addr sin6_addr;
	u32 sin6_scope_id;
};
struct sockaddr_storage {
	u8 ss_len, ss_family;
	u8 padding[126];
};
#endif


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

#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif

#ifndef IP_TOS
#define IP_TOS 1
#endif

#ifndef IP_MULTICAST_LOOP
#define IP_MULTICAST_LOOP 34
#endif

/* Winsock-style aliases used by net_ws.c */
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

#define SOCKET             int
typedef int                WSAsize_t;

/* libogc exposes net_* ; Xash expects BSD names.
 * Use distinct wrappers for socket/bind/… so locals named `net_socket`
 * (common in net_ws.c) do not collide with a `#define socket net_socket`. */
/* Track bind ports — libbba getsockname is a stub without sin_port. */
enum { GC_NET_PORT_SLOTS = 24 };
void GC_NetRememberBoundPort( int s, u16 port_be );
u16  GC_NetLookupBoundPort( int s );

/* libogc2 returns -errno without setting newlib errno. Translate at the
 * boundary so BSD callers handle would-block, bind errors and retries. */
static inline int GC_NetResult( int result )
{
#if XASH_GAMECUBE_LIBOGC2
	if( result < 0 )
	{
		errno = -result;
		return -1;
	}
#endif
	return result;
}

static inline int GC_Socket( int domain, int type, int protocol )
{
	if( domain != AF_INET )
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	return GC_NetResult( net_socket( domain, type, protocol ));
}
static inline int GC_Bind( int s, const struct sockaddr *name, socklen_t namelen )
{
	int ret = GC_NetResult( net_bind( s, (struct sockaddr *)name, namelen ));
	if( ret >= 0 && name && namelen >= (socklen_t)sizeof( struct sockaddr_in )
		&& name->sa_family == AF_INET )
		GC_NetRememberBoundPort( s, ((const struct sockaddr_in *)name)->sin_port );
	return ret;
}
static inline int GC_Connect( int s, const struct sockaddr *name, socklen_t namelen )
{
	return GC_NetResult( net_connect( s, (struct sockaddr *)name, namelen ));
}
s32 GC_SendTo( int s, const void *data, s32 len, u32 flags,
	const struct sockaddr *to, socklen_t tolen );
static inline int GC_RecvFrom( int s, void *mem, size_t len, u32 flags,
	struct sockaddr *from, socklen_t *fromlen )
{
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
	return GC_NetResult( net_recvfrom( s, mem, len, flags | MSG_DONTWAIT, from, fromlen ));
}
static inline int GC_Close( int s )
{
	int ret = GC_NetResult( net_close( s ));
	if( ret >= 0 ) GC_NetRememberBoundPort( s, 0 );
	return ret;
}
static inline int GC_SetSockOpt( int s, int level, int opt, const void *value, socklen_t len )
{
	return GC_NetResult( net_setsockopt( s, level, opt, value, len ));
}
static inline int GC_GetSockOpt( int s, int level, int opt, void *value, socklen_t *len )
{
	return GC_NetResult( net_getsockopt( s, level, opt, value, len ));
}
static inline int GC_Select( int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout )
{
	return GC_NetResult( net_select( n, readfds, writefds, exceptfds, timeout ));
}
static inline int GC_Ioctl( int s, unsigned long request, void *arg )
{
	return GC_NetResult( net_ioctl( s, request, arg ));
}
static inline int GC_GetSockName( int s, struct sockaddr *addr, socklen_t *len )
{
	return GC_NetResult( net_getsockname( s, addr, len ));
}
static inline int GC_Send( int s, const void *data, size_t len, int flags )
{
	return GC_NetResult( net_send( s, data, len, flags ));
}
static inline int GC_Recv( int s, void *data, size_t len, int flags )
{
	return GC_NetResult( net_recv( s, data, len, flags | MSG_DONTWAIT ));
}
static inline int GC_Listen( int s, int backlog )
{
	return GC_NetResult( net_listen( s, backlog ));
}
static inline int GC_Accept( int s, struct sockaddr *addr, socklen_t *len )
{
	return GC_NetResult( net_accept( s, addr, len ));
}

#define socket             GC_Socket
#define bind               GC_Bind
#define listen             GC_Listen
#define accept             GC_Accept
#define connect            GC_Connect
#define send               GC_Send
#define sendto             GC_SendTo
#define recv               GC_Recv
#define recvfrom           GC_RecvFrom
#define closesocket        GC_Close
#define setsockopt         GC_SetSockOpt
#define getsockopt         GC_GetSockOpt
#define getsockname        GC_GetSockName
#define select             GC_Select
#define ioctlsocket        GC_Ioctl
#define gethostbyname      net_gethostbyname

/* Minimal getaddrinfo for AF_INET hostname resolve (IPv6 disabled). */
struct gc_addrinfo {
	int              ai_flags;
	int              ai_family;
	int              ai_socktype;
	int              ai_protocol;
	socklen_t        ai_addrlen;
	struct sockaddr *ai_addr;
	char            *ai_canonname;
	struct gc_addrinfo *ai_next;
};

#ifndef AI_PASSIVE
#define AI_PASSIVE 0x0001
#endif

#define addrinfo gc_addrinfo

int  GC_GetAddrInfo( const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res );
void GC_FreeAddrInfo( struct addrinfo *res );

#define getaddrinfo  GC_GetAddrInfo
#define freeaddrinfo GC_FreeAddrInfo

/* newlib already declares gethostname(); provide our implementation in
 * net_gamecube.c and route the Xash call through a distinct symbol so we
 * do not collide with the libc prototype. */
int GC_GetHostname( char *name, size_t len );
#define gethostname GC_GetHostname

/* Bring-up / teardown for BBA and Swiss eth2sp1 (W5500) emulation.
 * Returns 1 on success, 0 on soft-fail (no adapter / timeout). */
int GC_NetIsReady( void );
const char *GC_NetLocalIP( void );
int GC_NetBringUp( int max_retries );
void GC_NetShutdown( void );
int GC_NetEnsureReady( void );

#endif /* NET_GAMECUBE_H */
