/*
 * net_gamecube.h - Map Xash BSD-socket calls onto libogc/libogc2 (BBA / eth2sp1).
 *
 * Official BBA and Swiss-emulated ETH2SP1 (W5500/ENC28J60 on SP1) both surface
 * as the libogc network stack once Swiss (or Dolphin BBA) has initialized EXI.
 * Soft-fail: missing adapter must not crash; SP remains loopback-capable.
 */
#ifndef NET_GAMECUBE_H
#define NET_GAMECUBE_H

#include <errno.h>
#include <string.h>
#include <network.h>
#include <sys/filio.h>

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

static inline int GC_Socket( int domain, int type, int protocol )
{
	return (int)net_socket( (u32)domain, (u32)type, (u32)protocol );
}
static inline int GC_Bind( int s, const struct sockaddr *name, socklen_t namelen )
{
	s32 ret = net_bind( (s32)s, (struct sockaddr *)name, namelen );
	if( ret >= 0 && name && namelen >= (socklen_t)sizeof( struct sockaddr_in )
		&& name->sa_family == AF_INET )
	{
		const struct sockaddr_in *sa = (const struct sockaddr_in *)name;
		GC_NetRememberBoundPort( s, sa->sin_port );
	}
	return (int)ret;
}
static inline int GC_Connect( int s, const struct sockaddr *name, socklen_t namelen )
{
	return (int)net_connect( (s32)s, (struct sockaddr *)name, namelen );
}
s32 GC_SendTo( int s, const void *data, s32 len, u32 flags,
	const struct sockaddr *to, socklen_t tolen );
static inline s32 GC_RecvFrom( int s, void *mem, s32 len, u32 flags,
	struct sockaddr *from, socklen_t *fromlen )
{
	/* Force non-blocking: FIONBIO is flaky on libogc BBA, and the net_ws
	 * SO_RCVTIMEO=0 fallback means "block forever" on lwIP — that softlocks
	 * CL_ReadPackets right after the first getchallenge sendto. */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
	return net_recvfrom( (s32)s, mem, len, flags | MSG_DONTWAIT, from, fromlen );
}

#define socket             GC_Socket
#define bind               GC_Bind
#define listen             net_listen
#define accept             net_accept
#define connect            GC_Connect
#define send               net_send
#define sendto             GC_SendTo
#define recv               net_recv
#define recvfrom           GC_RecvFrom
#define closesocket        net_close
#define setsockopt         net_setsockopt
#define getsockopt         net_getsockopt
#define getsockname        net_getsockname
#define select             net_select
#define ioctlsocket        net_ioctl
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
