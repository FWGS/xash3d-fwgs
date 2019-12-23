/*
net_ws.c - win network interface
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h" // ConnectionProgress
#include "netchan.h"
#include "mathlib.h"
#if XASH_WIN32
// Winsock
#include <WS2tcpip.h>
#else
// BSD sockets
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#endif

#define NET_USE_FRAGMENTS

#define PORT_ANY			-1
#define MAX_LOOPBACK		4
#define MASK_LOOPBACK		(MAX_LOOPBACK - 1)

#define MAX_ROUTEABLE_PACKET		1400
#define SPLITPACKET_MIN_SIZE			508		// RFC 791: 576(min ip packet) - 60 (ip header) - 8 (udp header)
#define SPLITPACKET_MAX_SIZE			64000
#define NET_MAX_FRAGMENTS		( NET_MAX_FRAGMENT / (SPLITPACKET_MIN_SIZE - sizeof( SPLITPACKET )) )

#if !XASH_WIN32 // not available in XP
#define HAVE_GETADDRINFO
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

#if XASH_EMSCRIPTEN
/* All socket operations are non-blocking already */
static int ioctl_stub( int d, unsigned long r, ... )
{
	return 0;
}
#define ioctlsocket ioctl_stub
#else // XASH_EMSCRIPTEN
#define ioctlsocket ioctl
#endif // XASH_EMSCRIPTEN
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
typedef socklen_t WSAsize_t;
#else // WIN32
typedef int WSAsize_t; // for some reason, MS has signed size. We won't exceed 2^32, probably, so just typedef it
#endif

typedef struct
{
	byte		data[NET_MAX_MESSAGE];
	int		datalen;
} net_loopmsg_t;

typedef struct
{
	net_loopmsg_t	msgs[MAX_LOOPBACK];
	int		get, send;
} net_loopback_t;

typedef struct packetlag_s
{
	byte		*data;	// Raw stream data is stored.
	int		size;
	netadr_t		from;
	float		receivedtime;
	struct packetlag_s	*next;
	struct packetlag_s	*prev;
} packetlag_t;

// split long packets. Anything over 1460 is failing on some routers.
typedef struct
{
	int		current_sequence;
	int		split_count;
	int		total_size;
	char		buffer[NET_MAX_FRAGMENT];
} LONGPACKET;

// use this to pick apart the network stream, must be packed
#pragma pack(push, 1)
typedef struct
{
	int		net_id;
	int		sequence_number;
	short		packet_id;
} SPLITPACKET;
#pragma pack(pop)

typedef struct
{
	net_loopback_t	loopbacks[NS_COUNT];
	packetlag_t	lagdata[NS_COUNT];
	int		losscount[NS_COUNT];
	float		fakelag;			// cached fakelag value
	LONGPACKET	split;
	int		split_flags[NET_MAX_FRAGMENTS];
	int		sequence_number;
	int		ip_sockets[NS_COUNT];
	qboolean		initialized;
	qboolean		threads_initialized;
	qboolean		configured;
	qboolean		allow_ip;
#if XASH_WIN32
	WSADATA		winsockdata;
#endif
} net_state_t;

static net_state_t		net;
static convar_t		*net_ipname;
static convar_t		*net_hostport;
static convar_t		*net_iphostport;
static convar_t		*net_clientport;
static convar_t		*net_ipclientport;
static convar_t		*net_fakelag;
static convar_t		*net_fakeloss;
static convar_t		*net_address;
convar_t			*net_clockwindow;
netadr_t			net_local;

/*
====================
NET_ErrorString
====================
*/
char *NET_ErrorString( void )
{
#if XASH_WIN32
	int	err = WSANOTINITIALISED;

	if( net.initialized )
		err = WSAGetLastError();

	switch( err )
	{
	case WSAEINTR: return "WSAEINTR";
	case WSAEBADF: return "WSAEBADF";
	case WSAEACCES: return "WSAEACCES";
	case WSAEFAULT: return "WSAEFAULT";
	case WSAEINVAL: return "WSAEINVAL";
	case WSAEMFILE: return "WSAEMFILE";
	case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
	case WSAEINPROGRESS: return "WSAEINPROGRESS";
	case WSAEALREADY: return "WSAEALREADY";
	case WSAENOTSOCK: return "WSAENOTSOCK";
	case WSAEDESTADDRREQ: return "WSAEDESTADDRREQ";
	case WSAEMSGSIZE: return "WSAEMSGSIZE";
	case WSAEPROTOTYPE: return "WSAEPROTOTYPE";
	case WSAENOPROTOOPT: return "WSAENOPROTOOPT";
	case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
	case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
	case WSAEOPNOTSUPP: return "WSAEOPNOTSUPP";
	case WSAEPFNOSUPPORT: return "WSAEPFNOSUPPORT";
	case WSAEAFNOSUPPORT: return "WSAEAFNOSUPPORT";
	case WSAEADDRINUSE: return "WSAEADDRINUSE";
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
	case WSAENETDOWN: return "WSAENETDOWN";
	case WSAENETUNREACH: return "WSAENETUNREACH";
	case WSAENETRESET: return "WSAENETRESET";
	case WSAECONNABORTED: return "WSWSAECONNABORTEDAEINTR";
	case WSAECONNRESET: return "WSAECONNRESET";
	case WSAENOBUFS: return "WSAENOBUFS";
	case WSAEISCONN: return "WSAEISCONN";
	case WSAENOTCONN: return "WSAENOTCONN";
	case WSAESHUTDOWN: return "WSAESHUTDOWN";
	case WSAETOOMANYREFS: return "WSAETOOMANYREFS";
	case WSAETIMEDOUT: return "WSAETIMEDOUT";
	case WSAECONNREFUSED: return "WSAECONNREFUSED";
	case WSAELOOP: return "WSAELOOP";
	case WSAENAMETOOLONG: return "WSAENAMETOOLONG";
	case WSAEHOSTDOWN: return "WSAEHOSTDOWN";
	case WSAEDISCON: return "WSAEDISCON";
	case WSASYSNOTREADY: return "WSASYSNOTREADY";
	case WSAVERNOTSUPPORTED: return "WSAVERNOTSUPPORTED";
	case WSANOTINITIALISED: return "WSANOTINITIALISED";
	case WSAHOST_NOT_FOUND: return "WSAHOST_NOT_FOUND";
	case WSATRY_AGAIN: return "WSATRY_AGAIN";
	case WSANO_RECOVERY: return "WSANO_RECOVERY";
	case WSANO_DATA: return "WSANO_DATA";
	default: return "NO ERROR";
	}
#else
	return strerror( errno );
#endif
}

_inline qboolean NET_IsSocketError( int retval )
{
#if XASH_WIN32
	return retval == SOCKET_ERROR ? true : false;
#else
	return retval < 0 ? true : false;
#endif
}

_inline qboolean NET_IsSocketValid( int socket )
{
#if XASH_WIN32
	return socket != INVALID_SOCKET;
#else
	return socket >= 0;
#endif
}

/*
====================
NET_NetadrToSockadr
====================
*/
static void NET_NetadrToSockadr( netadr_t *a, struct sockaddr *s )
{
	memset( s, 0, sizeof( *s ));

	if( a->type == NA_BROADCAST )
	{
		((struct sockaddr_in *)s)->sin_family = AF_INET;
		((struct sockaddr_in *)s)->sin_port = a->port;
		((struct sockaddr_in *)s)->sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if( a->type == NA_IP )
	{
		((struct sockaddr_in *)s)->sin_family = AF_INET;
		((struct sockaddr_in *)s)->sin_addr.s_addr = *(int *)&a->ip;
		((struct sockaddr_in *)s)->sin_port = a->port;
	}
}

/*
====================
NET_SockadrToNetAdr
====================
*/
static void NET_SockadrToNetadr( struct sockaddr *s, netadr_t *a )
{
	if( s->sa_family == AF_INET )
	{
		a->type = NA_IP;
		*(int *)&a->ip = ((struct sockaddr_in *)s)->sin_addr.s_addr;
		a->port = ((struct sockaddr_in *)s)->sin_port;
	}
}

/*
============
NET_GetHostByName
============
*/
int NET_GetHostByName( const char *hostname )
{
#ifdef HAVE_GETADDRINFO
	struct addrinfo *ai = NULL, *cur;
	struct addrinfo hints;
	int ip = 0;

	memset( &hints, 0, sizeof( hints ));
	hints.ai_family = AF_INET;

	if( !getaddrinfo( hostname, NULL, &hints, &ai ))
	{
		for( cur = ai; cur; cur = cur->ai_next )
		{
			if( cur->ai_family == AF_INET )
			{
				ip = *((int*)&((struct sockaddr_in *)cur->ai_addr)->sin_addr);
				break;
			}
		}

		if( ai )
			freeaddrinfo( ai );
	}

	return ip;
#else
	struct hostent *h;
	if(!( h = gethostbyname( hostname )))
		return 0;
	return *(int *)h->h_addr_list[0];
#endif
}

#if !defined XASH_NO_ASYNC_NS_RESOLVE && ( XASH_WIN32 || !XASH_EMSCRIPTEN )
#define CAN_ASYNC_NS_RESOLVE
#endif

#ifdef CAN_ASYNC_NS_RESOLVE
static void NET_ResolveThread( void );

#if !XASH_WIN32
#include <pthread.h>
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define exit_thread( x ) pthread_exit(x)
#define create_thread( pfn ) !pthread_create( &nsthread.thread, NULL, (pfn), NULL )
#define detach_thread( x ) pthread_detach(x)
#define mutex_t  pthread_mutex_t
#define thread_t pthread_t
void *NET_ThreadStart( void *unused )
{
	NET_ResolveThread();
	return NULL;
}
#else // WIN32
#define mutex_lock EnterCriticalSection
#define mutex_unlock LeaveCriticalSection
#define detach_thread( x ) CloseHandle(x)
#define create_thread( pfn ) nsthread.thread = CreateThread( NULL, 0, pfn, NULL, 0, NULL )
#define mutex_t  CRITICAL_SECTION
#define thread_t HANDLE
DWORD WINAPI NET_ThreadStart( LPVOID unused )
{
	NET_ResolveThread();
	ExitThread(0);
	return 0;
}
#endif // !_WIN32

#ifdef DEBUG_RESOLVE
#define RESOLVE_DBG(x) Sys_PrintLog(x)
#else
#define RESOLVE_DBG(x)
#endif //  DEBUG_RESOLVE

static struct nsthread_s
{
	mutex_t mutexns;
	mutex_t mutexres;
	thread_t thread;
	int     result;
	string  hostname;
	qboolean busy;
} nsthread
#if !XASH_WIN32
= { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }
#endif
;

#if XASH_WIN32
static void NET_InitializeCriticalSections( void )
{
	net.threads_initialized = true;
	pInitializeCriticalSection( &nsthread.mutexns );
	pInitializeCriticalSection( &nsthread.mutexres );
}
#endif

void NET_ResolveThread( void )
{
	int sin_addr = 0;

	RESOLVE_DBG( "[resolve thread] starting resolve for " );
	RESOLVE_DBG( nsthread.hostname );
#ifdef HAVE_GETADDRINFO
	RESOLVE_DBG( " with getaddrinfo\n" );
#else
	RESOLVE_DBG( " with gethostbyname\n" );
#endif

	sin_addr = NET_GetHostByName( nsthread.hostname );

	if( sin_addr )
		RESOLVE_DBG( "[resolve thread] success\n" );
	else
		RESOLVE_DBG( "[resolve thread] failed\n" );
	mutex_lock( &nsthread.mutexres );
	nsthread.result = sin_addr;
	nsthread.busy = false;
	RESOLVE_DBG( "[resolve thread] returning result\n" );
	mutex_unlock( &nsthread.mutexres );
	RESOLVE_DBG( "[resolve thread] exiting thread\n" );
}
#endif // CAN_ASYNC_NS_RESOLVE


/*
=============
NET_StringToAdr

localhost
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
=============
*/
static int NET_StringToSockaddr( const char *s, struct sockaddr *sadr, qboolean nonblocking )
{
	int ip = 0;
	char	*colon;
	char	copy[128];

	if( !net.initialized )
		return false;
	
	memset( sadr, 0, sizeof( *sadr ));

	((struct sockaddr_in *)sadr)->sin_family = AF_INET;
	((struct sockaddr_in *)sadr)->sin_port = 0;

	Q_strncpy( copy, s, sizeof( copy ));

	// strip off a trailing :port if present
	for( colon = copy; *colon; colon++ )
	{
		if( *colon == ':' )
		{
			*colon = 0;
			((struct sockaddr_in *)sadr)->sin_port = htons((short)Q_atoi( colon + 1 ));
		}
	}

	if( copy[0] >= '0' && copy[0] <= '9' )
	{
		*(int *)&((struct sockaddr_in *)sadr)->sin_addr = inet_addr( copy );
	}
	else
	{
		qboolean asyncfailed = true;

#ifdef CAN_ASYNC_NS_RESOLVE
		if( net.threads_initialized && !nonblocking )
		{
			mutex_lock( &nsthread.mutexres );

			if( nsthread.busy )
			{
				mutex_unlock( &nsthread.mutexres );
				return 2;
			}

			if( !Q_strcmp( copy, nsthread.hostname ) )
			{
				ip = nsthread.result;
				nsthread.hostname[0] = 0;
				detach_thread( nsthread.thread );
			}
			else
			{
				Q_strncpy( nsthread.hostname, copy, MAX_STRING );
				nsthread.busy = true;
				mutex_unlock( &nsthread.mutexres );

				if( create_thread( NET_ThreadStart ) )
				{
					asyncfailed = false;
					return 2;
				}
				else // failed to create thread
				{
					Con_Reportf( S_ERROR  "NET_StringToSockaddr: failed to create thread!\n");
					nsthread.busy = false;
				}
			}

			mutex_unlock( &nsthread.mutexres );
		}
#endif // CAN_ASYNC_NS_RESOLVE

		if( asyncfailed )
		{
			ip = NET_GetHostByName( copy );
		}

		if( !ip )
			return 0;

		*(int *)&((struct sockaddr_in *)sadr)->sin_addr = ip;
	}

	return 1;
}

/*
====================
NET_AdrToString
====================
*/
char *NET_AdrToString( const netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return "loopback";
	return va( "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs( a.port ));
}

/*
====================
NET_BaseAdrToString
====================
*/
char *NET_BaseAdrToString( const netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return "loopback";
	return va( "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3] );
}

/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( !memcmp( a.ip, b.ip, 4 ))
			return true;
	}

	return false;
}

/*
====================
NET_CompareClassBAdr

Compare local masks
====================
*/
qboolean NET_CompareClassBAdr( netadr_t a, netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] )
			return true;
	}
	return false;
}

/*
====================
NET_IsReservedAdr

Check for reserved ip's
====================
*/
qboolean NET_IsReservedAdr( netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( a.ip[0] == 10 || a.ip[0] == 127 )
			return true;

		if( a.ip[0] == 172 && a.ip[1] >= 16 )
		{
			if( a.ip[1] >= 32 )
				return false;
			return true;
		}

		if( a.ip[0] == 192 && a.ip[1] >= 168 )
			return true;
	}

	return false;
}

/*
====================
NET_CompareAdr

Compare full address
====================
*/
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if(!memcmp( a.ip, b.ip, 4 ) && a.port == b.port )
			return true;
		return false;
	}

	Con_DPrintf( S_ERROR "NET_CompareAdr: bad address type\n" );
	return false;
}

/*
====================
NET_IsLocalAddress
====================
*/
qboolean NET_IsLocalAddress( netadr_t adr )
{
	return (adr.type == NA_LOOPBACK) ? true : false;
}

/*
=============
NET_StringToAdr

idnewt
192.246.40.70
=============
*/
qboolean NET_StringToAdr( const char *string, netadr_t *adr )
{
	struct sockaddr s;

	memset( adr, 0, sizeof( netadr_t ));

	if( !Q_stricmp( string, "localhost" ) || !Q_stricmp( string, "loopback" ) )
	{
		adr->type = NA_LOOPBACK;
		return true;
	}

	if( !NET_StringToSockaddr( string, &s, false ))
		return false;
	NET_SockadrToNetadr( &s, adr );

	return true;
}

int NET_StringToAdrNB( const char *string, netadr_t *adr )
{
	struct sockaddr s;
	int res;

	memset( adr, 0, sizeof( netadr_t ));
	if( !Q_stricmp( string, "localhost" )  || !Q_stricmp( string, "loopback" ) )
	{
		adr->type = NA_LOOPBACK;
		return true;
	}

	res = NET_StringToSockaddr( string, &s, true );

	if( res == 0 || res == 2 )
		return res;

	NET_SockadrToNetadr( &s, adr );

	return true;
}

/*
=============================================================================

LOOPBACK BUFFERS FOR LOCAL PLAYER

=============================================================================
*/
/*
====================
NET_GetLoopPacket
====================
*/
static qboolean NET_GetLoopPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	net_loopback_t	*loop;
	int		i;

	if( !data || !length )
		return false;

	loop = &net.loopbacks[sock];

	if( loop->send - loop->get > MAX_LOOPBACK )
		loop->get = loop->send - MAX_LOOPBACK;

	if( loop->get >= loop->send )
		return false;
	i = loop->get & MASK_LOOPBACK;
	loop->get++;

	memcpy( data, loop->msgs[i].data, loop->msgs[i].datalen );
	*length = loop->msgs[i].datalen;

	memset( from, 0, sizeof( *from ));
	from->type = NA_LOOPBACK;

	return true;
}

/*
====================
NET_SendLoopPacket
====================
*/
static void NET_SendLoopPacket( netsrc_t sock, size_t length, const void *data, netadr_t to )
{
	net_loopback_t	*loop;
	int		i;

	loop = &net.loopbacks[sock^1];

	i = loop->send & MASK_LOOPBACK;
	loop->send++;

	memcpy( loop->msgs[i].data, data, length );
	loop->msgs[i].datalen = length;
}

/*
====================
NET_ClearLoopback
====================
*/
static void NET_ClearLoopback( void )
{
	net.loopbacks[0].send = net.loopbacks[0].get = 0;
	net.loopbacks[1].send = net.loopbacks[1].get = 0;
}

/*
=============================================================================

LAG & LOSS SIMULATION SYSTEM (network debugging)

=============================================================================
*/
/*
==================
NET_RemoveFromPacketList

double linked list remove entry
==================
*/
static void NET_RemoveFromPacketList( packetlag_t *p )
{
	p->prev->next = p->next;
	p->next->prev = p->prev;
	p->prev = NULL;
	p->next = NULL;
}

/*
==================
NET_ClearLaggedList

double linked list remove queue
==================
*/
static void NET_ClearLaggedList( packetlag_t *list )
{
	packetlag_t	*p, *n;

	p = list->next;
	while( p && p != list )
	{
		n = p->next;

		NET_RemoveFromPacketList( p );

		if( p->data )
		{
			Mem_Free( p->data );
			p->data = NULL;
		}

		Mem_Free( p );
		p = n;
	}

	list->prev = list;
	list->next = list;
}

/*
==================
NET_AddToLagged

add lagged packet to stream
==================
*/
static void NET_AddToLagged( netsrc_t sock, packetlag_t *list, packetlag_t *packet, netadr_t *from, size_t length, const void *data, float timestamp )
{
	byte	*pStart;

	if( packet->prev || packet->next )
		return;

	packet->prev = list->prev;
	list->prev->next = packet;
	list->prev = packet;
	packet->next = list;

	pStart = (byte *)Z_Malloc( length );
	memcpy( pStart, data, length );
	packet->data = pStart;
	packet->size = length;
	packet->receivedtime = timestamp;
	memcpy( &packet->from, from, sizeof( netadr_t ));
}

/*
==================
NET_AdjustLag

adjust time to next fake lag
==================
*/
static void NET_AdjustLag( void )
{
	static double	lasttime = 0.0;
	float		diff, converge;
	double		dt;

	dt = host.realtime - lasttime;
	dt = bound( 0.0, dt, 0.1 );
	lasttime = host.realtime;

	if( host_developer.value || !net_fakelag->value )
	{
		if( net_fakelag->value != net.fakelag )
		{
			diff = net_fakelag->value - net.fakelag;
			converge = dt * 200.0f;
			if( fabs( diff ) < converge )
				converge = fabs( diff );
			if( diff < 0.0f )
				converge = -converge;
			net.fakelag += converge;
		}
	}
	else
	{
		Con_Printf( "Server must enable dev-mode to activate fakelag\n" );
		Cvar_SetValue( "fakelag", 0.0 );
		net.fakelag = 0.0f;
	}
}

/*
==================
NET_LagPacket

add fake lagged packet into rececived message
==================
*/
static qboolean NET_LagPacket( qboolean newdata, netsrc_t sock, netadr_t *from, size_t *length, void *data )
{
	packetlag_t	*pNewPacketLag;
	packetlag_t	*pPacket;
	int		ninterval;
	float		curtime;

	if( net.fakelag <= 0.0f )
	{
		NET_ClearLagData( true, true );
		return newdata;
	}

	curtime = host.realtime;

	if( newdata )
	{
		if( net_fakeloss->value != 0.0f )
		{
			if( host_developer.value )
			{
				net.losscount[sock]++;
				if( net_fakeloss->value <= 0.0f )
				{
					ninterval = fabs( net_fakeloss->value );
					if( ninterval < 2 ) ninterval = 2;

					if(( net.losscount[sock] % ninterval ) == 0 )
						return false;
				}
				else
				{
					if( COM_RandomLong( 0, 100 ) <= net_fakeloss->value )
						return false;
				}
			}
			else
			{
				Cvar_SetValue( "fakeloss", 0.0 );
			}
		}

		pNewPacketLag = (packetlag_t *)Z_Malloc( sizeof( packetlag_t ));
		// queue packet to simulate fake lag
		NET_AddToLagged( sock, &net.lagdata[sock], pNewPacketLag, from, *length, data, curtime );
	}

	pPacket = net.lagdata[sock].next;

	while( pPacket != &net.lagdata[sock] )
	{
		if( pPacket->receivedtime <= curtime - ( net.fakelag / 1000.0f ))
			break;

		pPacket = pPacket->next;
	}

	if( pPacket == &net.lagdata[sock] )
		return false;

	NET_RemoveFromPacketList( pPacket );

	// delivery packet from fake lag queue
	memcpy( data, pPacket->data, pPacket->size );
	memcpy( &net_from, &pPacket->from, sizeof( netadr_t ));
	*length = pPacket->size;

	if( pPacket->data )
		Mem_Free( pPacket->data );

	Mem_Free( pPacket );

	return true;
}

/*
==================
NET_GetLong

receive long packet from network
==================
*/
qboolean NET_GetLong( byte *pData, int size, size_t *outSize, int splitsize )
{
	int		i, sequence_number, offset;
	SPLITPACKET	*pHeader = (SPLITPACKET *)pData;
	int		packet_number;
	int		packet_count;
	short		packet_id;
	int body_size = splitsize - sizeof( SPLITPACKET );

	if( body_size < 0 )
		return false;

	if( size < sizeof( SPLITPACKET ))
	{
		Con_Printf( S_ERROR "invalid split packet length %i\n", size );
		return false;
	}

	sequence_number = pHeader->sequence_number;
	packet_id = pHeader->packet_id;
	packet_count = ( packet_id & 0xFF );
	packet_number = ( packet_id >> 8 );

	if( packet_number >= NET_MAX_FRAGMENTS || packet_count > NET_MAX_FRAGMENTS )
	{
		Con_Printf( S_ERROR "malformed packet number (%i/%i)\n", packet_number + 1, packet_count );
		return false;
	}

	if( net.split.current_sequence == -1 || sequence_number != net.split.current_sequence )
	{
		net.split.current_sequence = sequence_number;
		net.split.split_count = packet_count;
		net.split.total_size = 0;

		// clear part's sequence
		for( i = 0; i < NET_MAX_FRAGMENTS; i++ )
			net.split_flags[i] = -1;

		if( net_showpackets && net_showpackets->value == 4.0f )
			Con_Printf( "<-- Split packet restart %i count %i seq\n", net.split.split_count, sequence_number );
	}

	size -= sizeof( SPLITPACKET );

	if( net.split_flags[packet_number] != sequence_number )
	{
		if( packet_number == ( packet_count - 1 ))
			net.split.total_size = size + body_size * ( packet_count - 1 );

		net.split.split_count--;
		net.split_flags[packet_number] = sequence_number;

		if( net_showpackets && net_showpackets->value == 4.0f )
			Con_Printf( "<-- Split packet %i of %i, %i bytes %i seq\n", packet_number + 1, packet_count, size, sequence_number );
	}
	else
	{
		Con_DPrintf( "NET_GetLong: Ignoring duplicated split packet %i of %i ( %i bytes )\n", packet_number + 1, packet_count, size );
	}

	offset = (packet_number * body_size);
	memcpy( net.split.buffer + offset, pData + sizeof( SPLITPACKET ), size );

	// have we received all of the pieces to the packet?
	if( net.split.split_count <= 0 )
	{
		net.split.current_sequence = -1; // Clear packet

		if( net.split.total_size > sizeof( net.split.buffer ))
		{
			Con_Printf( "Split packet too large! %d bytes\n", net.split.total_size );
			return false;
		}

		memcpy( pData, net.split.buffer, net.split.total_size );
		*outSize = net.split.total_size;

		return true;
	}

	return false;
}

/*
==================
NET_QueuePacket

queue normal and lagged packets
==================
*/
qboolean NET_QueuePacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	byte		buf[NET_MAX_FRAGMENT];
	int		ret;
	int		net_socket;
	WSAsize_t	addr_len;
	struct sockaddr	addr;

	*length = 0;

	net_socket = net.ip_sockets[sock];

	if( NET_IsSocketValid( net_socket ) )
	{
		addr_len = sizeof( addr );
		ret = recvfrom( net_socket, buf, sizeof( buf ), 0, (struct sockaddr *)&addr, &addr_len );

		if( !NET_IsSocketError( ret ) )
		{
			NET_SockadrToNetadr( &addr, from );

			if( ret < NET_MAX_FRAGMENT )
			{
				// Transfer data
				memcpy( data, buf, ret );
				*length = ret;
#if !XASH_DEDICATED
				if( CL_LegacyMode() )
					return NET_LagPacket( true, sock, from, length, data );

				// check for split message
				if( sock == NS_CLIENT && *(int *)data == NET_HEADER_SPLITPACKET )
				{
					return NET_GetLong( data, ret, length, CL_GetSplitSize() );
				}
#endif
				// lag the packet, if needed
				return NET_LagPacket( true, sock, from, length, data );
			}
			else
			{
				Con_Reportf( "NET_QueuePacket: oversize packet from %s\n", NET_AdrToString( *from ));
			}
		}
		else
		{
			int	err = WSAGetLastError();

			switch( err )
			{
			case WSAEWOULDBLOCK:
			case WSAECONNRESET:
			case WSAECONNREFUSED:
			case WSAEMSGSIZE:
				break;
			default:	// let's continue even after errors
				Con_DPrintf( S_ERROR "NET_QueuePacket: %s from %s\n", NET_ErrorString(), NET_AdrToString( *from ));
				break;
			}
		}
	}

	return NET_LagPacket( false, sock, from, length, data );
}

/*
==================
NET_GetPacket

Never called by the game logic, just the system event queing
==================
*/
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	if( !data || !length )
		return false;

	NET_AdjustLag();

	if( NET_GetLoopPacket( sock, from, data, length ))
	{
		return NET_LagPacket( true, sock, from, length, data );
	}
	else
	{
		return NET_QueuePacket( sock, from, data, length );
	}
}

/*
==================
NET_SendLong

Fragment long packets, send short directly
==================
*/
int NET_SendLong( netsrc_t sock, int net_socket, const char *buf, size_t len, int flags, const struct sockaddr *to, size_t tolen, size_t splitsize )
{
#ifdef NET_USE_FRAGMENTS
	// do we need to break this packet up?
	if( splitsize > sizeof( SPLITPACKET ) && sock == NS_SERVER && len > splitsize )
	{
		char		packet[SPLITPACKET_MAX_SIZE];
		int		total_sent, size, packet_count;
		int		ret, packet_number;
		int body_size = splitsize - sizeof( SPLITPACKET );
		SPLITPACKET	*pPacket;

		net.sequence_number++;
		if( net.sequence_number <= 0 )
			net.sequence_number = 1;

		pPacket = (SPLITPACKET *)packet;
		pPacket->sequence_number = net.sequence_number;
		pPacket->net_id = NET_HEADER_SPLITPACKET;
		packet_number = 0;
		total_sent = 0;
		packet_count = (len + body_size - 1) / body_size;

		while( len > 0 )
		{
			size = Q_min( body_size, len );
			pPacket->packet_id = (packet_number << 8) + packet_count;
			memcpy( packet + sizeof( SPLITPACKET ), buf + ( packet_number * body_size ), size );

			if( net_showpackets && net_showpackets->value == 3.0f )
			{
				netadr_t	adr;

				memset( &adr, 0, sizeof( adr ));
				NET_SockadrToNetadr((struct sockaddr *)to, &adr );

				Con_Printf( "Sending split %i of %i with %i bytes and seq %i to %s\n",
					packet_number + 1, packet_count, size, net.sequence_number, NET_AdrToString( adr ));
			}

			ret = sendto( net_socket, packet, size + sizeof( SPLITPACKET ), flags, to, tolen );
			if( ret < 0 ) return ret; // error

			if( ret >= size )
				total_sent += size;
			len -= size;
			packet_number++;
			Sys_Sleep( 1 );
		}

		return total_sent;
	}
	else
#endif
	{
		// no fragmenantion for client connection
		return sendto( net_socket, buf, len, flags, to, tolen );
	}
}

/*
==================
NET_SendPacketEx
==================
*/
void NET_SendPacketEx( netsrc_t sock, size_t length, const void *data, netadr_t to, size_t splitsize )
{
	int		ret;
	struct sockaddr	addr;
	SOCKET		net_socket = 0;

	if( !net.initialized || to.type == NA_LOOPBACK )
	{
		NET_SendLoopPacket( sock, length, data, to );
		return;
	}
	else if( to.type == NA_BROADCAST )
	{
		net_socket = net.ip_sockets[sock];
		if( !NET_IsSocketValid( net_socket ) )
			return;
	}
	else if( to.type == NA_IP )
	{
		net_socket = net.ip_sockets[sock];
		if( !NET_IsSocketValid( net_socket ) )
			return;
	}
	else
	{
		Host_Error( "NET_SendPacket: bad address type %i\n", to.type );
	}

	NET_NetadrToSockadr( &to, &addr );

	ret = NET_SendLong( sock, net_socket, data, length, 0, &addr, sizeof( addr ), splitsize );

	if( NET_IsSocketError( ret ))
	{
		int err = WSAGetLastError();

		// WSAEWOULDBLOCK is silent
		if( err == WSAEWOULDBLOCK )
			return;

		// some PPP links don't allow broadcasts
		if( err == WSAEADDRNOTAVAIL && to.type == NA_BROADCAST )
			return;

		if( Host_IsDedicated() )
		{
			Con_DPrintf( S_ERROR "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
		else if( err == WSAEADDRNOTAVAIL || err == WSAENOBUFS )
		{
			Con_DPrintf( S_ERROR "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
		else
		{
			Con_Printf( S_ERROR "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
	}

}

/*
==================
NET_SendPacket
==================
*/
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to )
{
	NET_SendPacketEx( sock, length, data, to, 0 );
}

/*
====================
NET_BufferToBufferCompress

generic fast compression
====================
*/
qboolean NET_BufferToBufferCompress( byte *dest, uint *destLen, byte *source, uint sourceLen )
{
	uint	uCompressedLen = 0;
	byte	*pbOut = NULL;

	memcpy( dest, source, sourceLen );
	pbOut = LZSS_Compress( source, sourceLen, &uCompressedLen );

	if( pbOut && uCompressedLen > 0 && uCompressedLen <= *destLen )
	{
		memcpy( dest, pbOut, uCompressedLen );
		*destLen = uCompressedLen;
		free( pbOut );
		return true;
	}
	else
	{
		if( pbOut ) free( pbOut );
		memcpy( dest, source, sourceLen );
		*destLen = sourceLen;
		return false;
	}
}

/*
====================
NET_BufferToBufferDecompress

generic fast decompression
====================
*/
qboolean NET_BufferToBufferDecompress( byte *dest, uint *destLen, byte *source, uint sourceLen )
{
	if( LZSS_IsCompressed( source ))
	{
		uint	uDecompressedLen = LZSS_GetActualSize( source );

		if( uDecompressedLen <= *destLen )
		{
			*destLen = LZSS_Decompress( source, dest );
		}
		else
		{
			return false;
		}
	}
	else
	{
		memcpy( dest, source, sourceLen );
		*destLen = sourceLen;
	}

	return true;
}

/*
====================
NET_Isocket
====================
*/
static int NET_Isocket( const char *net_interface, int port, qboolean multicast )
{
	struct sockaddr_in	addr;
	int		err, net_socket;
	uint		optval = 1;
	dword		_true = 1;

	if( NET_IsSocketError(( net_socket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP )) ) )
	{
		err = WSAGetLastError();
		if( err != WSAEAFNOSUPPORT )
			Con_DPrintf( S_WARN "NET_UDsocket: port: %d socket: %s\n", port, NET_ErrorString( ));
		return INVALID_SOCKET;
	}

	if( NET_IsSocketError( ioctlsocket( net_socket, FIONBIO, &_true ) ) )
	{
		Con_DPrintf( S_WARN "NET_UDsocket: port: %d ioctl FIONBIO: %s\n", port, NET_ErrorString( ));
		closesocket( net_socket );
		return INVALID_SOCKET;
	}

	// make it broadcast capable
	if( NET_IsSocketError( setsockopt( net_socket, SOL_SOCKET, SO_BROADCAST, (char *)&_true, sizeof( _true ) ) ) )
	{
		Con_DPrintf( S_WARN "NET_UDsocket: port: %d setsockopt SO_BROADCAST: %s\n", port, NET_ErrorString( ));
		closesocket( net_socket );
		return INVALID_SOCKET;
	}

	if( Sys_CheckParm( "-reuse" ) || multicast )
	{
		if( NET_IsSocketError( setsockopt( net_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof( optval )) ) )
		{
			Con_DPrintf( S_WARN "NET_UDsocket: port: %d setsockopt SO_REUSEADDR: %s\n", port, NET_ErrorString( ));
			closesocket( net_socket );
			return INVALID_SOCKET;
		}
	}

	if( Sys_CheckParm( "-tos" ))
	{
		optval = 16;
		Con_Printf( "Enabling LOWDELAY TOS option\n" );

		if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IP, IP_TOS, (const char *)&optval, sizeof( optval )) ) )
		{
			err = WSAGetLastError();
			if( err != WSAENOPROTOOPT )
				Con_Printf( S_WARN "NET_UDsocket: port: %d  setsockopt IP_TOS: %s\n", port, NET_ErrorString( ));
			closesocket( net_socket );
			return INVALID_SOCKET;
		}
	}

	if( !net_interface[0] || !Q_stricmp( net_interface, "localhost" ))
		addr.sin_addr.s_addr = INADDR_ANY;
	else NET_StringToSockaddr( net_interface, (struct sockaddr *)&addr, false );

	if( port == PORT_ANY ) addr.sin_port = 0;
	else addr.sin_port = htons((short)port);

	addr.sin_family = AF_INET;

	if( NET_IsSocketError( bind( net_socket, (void *)&addr, sizeof( addr )) ) )
	{
		Con_DPrintf( S_WARN "NET_UDsocket: port: %d bind: %s\n", port, NET_ErrorString( ));
		closesocket( net_socket );
		return INVALID_SOCKET;
	}

	if( Sys_CheckParm( "-loopback" ))
	{
		optval = 1;
		if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&optval, sizeof( optval )) ) )
			Con_DPrintf( S_WARN "NET_UDsocket: port %d setsockopt IP_MULTICAST_LOOP: %s\n", port, NET_ErrorString( ));
	}

	return net_socket;
}

/*
====================
NET_OpenIP
====================
*/
static void NET_OpenIP( void )
{
	int	port, sv_port = 0, cl_port = 0;

	if( !NET_IsSocketValid( net.ip_sockets[NS_SERVER] ) )
	{
		port = net_iphostport->value;
		if( !port ) port = net_hostport->value;
		if( !port ) port = PORT_SERVER; // forcing to default
		net.ip_sockets[NS_SERVER] = NET_Isocket( net_ipname->string, port, false );

		if( !NET_IsSocketValid( net.ip_sockets[NS_SERVER] ) && Host_IsDedicated() )
			Host_Error( "Couldn't allocate dedicated server IP port %d.\n", port );
		sv_port = port;
	}

	// dedicated servers don't need client ports
	if( Host_IsDedicated() ) return;

	if( !NET_IsSocketValid( net.ip_sockets[NS_CLIENT] ) )
	{
		port = net_ipclientport->value;
		if( !port ) port = net_clientport->value;
		if( !port ) port = PORT_ANY; // forcing to default
		net.ip_sockets[NS_CLIENT] = NET_Isocket( net_ipname->string, port, false );

		if( !NET_IsSocketValid( net.ip_sockets[NS_CLIENT] ) )
			net.ip_sockets[NS_CLIENT] = NET_Isocket( net_ipname->string, PORT_ANY, false );
		cl_port = port;
	}
}

/*
================
NET_GetLocalAddress

Returns the servers' ip address as a string.
================
*/
void NET_GetLocalAddress( void )
{
	char		buff[512];
	struct sockaddr_in	address;
	WSAsize_t		namelen;

	memset( &net_local, 0, sizeof( netadr_t ));
	buff[0] = '\0';

	if( net.allow_ip )
	{
		// If we have changed the ip var from the command line, use that instead.
		if( Q_strcmp( net_ipname->string, "localhost" ))
		{
			Q_strncpy( buff, net_ipname->string, sizeof( buff ) );
		}
		else
		{
			gethostname( buff, 512 );

			// ensure that it doesn't overrun the buffer
			buff[511] = 0;
		}

		if( NET_StringToAdr( buff, &net_local ))
		{
			namelen = sizeof( address );

			if( NET_IsSocketError( getsockname( net.ip_sockets[NS_SERVER], (struct sockaddr *)&address, &namelen ) ) )
			{
				// this may happens if multiple clients running on single machine
				Con_DPrintf( S_ERROR "Could not get TCP/IP address. Reason:  %s\n", NET_ErrorString( ));
//				net.allow_ip = false;
			}
			else
			{
				net_local.port = address.sin_port;
				Con_Printf( "Server IP address %s\n", NET_AdrToString( net_local ));
				Cvar_FullSet( "net_address", va( "%s", NET_AdrToString( net_local )), FCVAR_READ_ONLY );
			}
		}
		else
		{
			Con_DPrintf( S_ERROR "Could not get TCP/IP address, Invalid hostname: '%s'\n", buff );
		}
	}
	else
	{
		Con_Printf( "TCP/IP Disabled.\n" );
	}
}

/*
====================
NET_Config

A single player game will only use the loopback code
====================
*/
void NET_Config( qboolean multiplayer )
{
	static qboolean	bFirst = true;
	static qboolean	old_config;

	if( !net.initialized )
		return;

	if( old_config == multiplayer )
		return;

	old_config = multiplayer;

	if( multiplayer )
	{	
		// open sockets
		if( net.allow_ip ) NET_OpenIP();

		// get our local address, if possible
		if( bFirst )
		{
			NET_GetLocalAddress();
			bFirst = false;
		}
	}
	else
	{	
		int	i;

		// shut down any existing sockets
		for( i = 0; i < NS_COUNT; i++ )
		{
			if( net.ip_sockets[i] != INVALID_SOCKET )
			{
				closesocket( net.ip_sockets[i] );
				net.ip_sockets[i] = INVALID_SOCKET;
			}
		}
	}

	NET_ClearLoopback ();

	net.configured = multiplayer ? true : false;
}

/*
====================
NET_IsConfigured

Is winsock ip initialized?
====================
*/
qboolean NET_IsConfigured( void )
{
	return net.configured;
}

/*
====================
NET_IsActive
====================
*/
qboolean NET_IsActive( void )
{
	return net.initialized;
}

/*
====================
NET_Sleep

sleeps msec or until net socket is ready
====================
*/
void NET_Sleep( int msec )
{
	struct timeval	timeout;
	fd_set		fdset;
	int		i = 0;

	if( !net.initialized || host.type == HOST_NORMAL )
		return; // we're not a dedicated server, just run full speed

	FD_ZERO( &fdset );

	if( net.ip_sockets[NS_SERVER] != INVALID_SOCKET )
	{
		FD_SET( net.ip_sockets[NS_SERVER], &fdset ); // network socket
		i = net.ip_sockets[NS_SERVER];
	}

	timeout.tv_sec = msec / 1000;
	timeout.tv_usec = (msec % 1000) * 1000;
	select( i+1, &fdset, NULL, NULL, &timeout );
}

/*
====================
NET_ClearLagData

clear fakelag list
====================
*/
void NET_ClearLagData( qboolean bClient, qboolean bServer )
{
	if( bClient ) NET_ClearLaggedList( &net.lagdata[NS_CLIENT] );
	if( bServer ) NET_ClearLaggedList( &net.lagdata[NS_SERVER] );
}

/*
====================
NET_Init
====================
*/
void NET_Init( void )
{
	char	cmd[64];
	int	i = 1;

	if( net.initialized ) return;

	net_clockwindow = Cvar_Get( "clockwindow", "0.5", 0, "timewindow to execute client moves" );
	net_address = Cvar_Get( "net_address", "0", FCVAR_READ_ONLY, "contain local address of current client" );
	net_ipname = Cvar_Get( "ip", "localhost", FCVAR_READ_ONLY, "network ip address" );
	net_iphostport = Cvar_Get( "ip_hostport", "0", FCVAR_READ_ONLY, "network ip host port" );
	net_hostport = Cvar_Get( "hostport", va( "%i", PORT_SERVER ), FCVAR_READ_ONLY, "network default host port" );
	net_ipclientport = Cvar_Get( "ip_clientport", "0", FCVAR_READ_ONLY, "network ip client port" );
	net_clientport = Cvar_Get( "clientport", va( "%i", PORT_CLIENT ), FCVAR_READ_ONLY, "network default client port" );
	net_fakelag = Cvar_Get( "fakelag", "0", 0, "lag all incoming network data (including loopback) by xxx ms." );
	net_fakeloss = Cvar_Get( "fakeloss", "0", 0, "act like we dropped the packet this % of the time." );

	// prepare some network data
	for( i = 0; i < NS_COUNT; i++ )
	{
		net.lagdata[i].prev = &net.lagdata[i];
		net.lagdata[i].next = &net.lagdata[i];
		net.ip_sockets[i] = INVALID_SOCKET;
	}

#if XASH_WIN32
	if( WSAStartup( MAKEWORD( 1, 1 ), &net.winsockdata ))
	{
		Con_DPrintf( S_ERROR "network initialization failed.\n" );
		return;
	}
#else
	// we have pthreads by default
	net.threads_initialized = true;
#endif

	if( Sys_CheckParm( "-noip" ))
		net.allow_ip = false;
	else net.allow_ip = true;

	// specify custom host port
	if( Sys_GetParmFromCmdLine( "-port", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( "hostport", cmd, FCVAR_READ_ONLY );

	// specify custom ip
	if( Sys_GetParmFromCmdLine( "-ip", cmd ))
		Cvar_FullSet( "ip", cmd, FCVAR_READ_ONLY );

	// adjust clockwindow
	if( Sys_GetParmFromCmdLine( "-clockwindow", cmd ))
		Cvar_SetValue( "clockwindow", Q_atof( cmd ));

	net.sequence_number = 1;
	net.initialized = true;
	Con_Reportf( "Base networking initialized.\n" );
}


/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown( void )
{
	if( !net.initialized )
		return;

	NET_ClearLagData( true, true );

	NET_Config( false );
#if XASH_WIN32
	WSACleanup();
#endif
	net.initialized = false;
}


/*
=================================================

HTTP downloader

=================================================
*/

typedef struct httpserver_s
{
	char host[256];
	int port;
	char path[MAX_SYSPATH];
	qboolean needfree;
	struct httpserver_s *next;

} httpserver_t;

enum connectionstate
{
	HTTP_QUEUE = 0,
	HTTP_OPENED,
	HTTP_SOCKET,
	HTTP_NS_RESOLVED,
	HTTP_CONNECTED,
	HTTP_REQUEST,
	HTTP_REQUEST_SENT,
	HTTP_RESPONSE_RECEIVED,
	HTTP_FREE
};

typedef struct httpfile_s
{
	struct httpfile_s *next;
	httpserver_t *server;
	char path[MAX_SYSPATH];
	file_t *file;
	int socket;
	int size;
	int downloaded;
	int lastchecksize;
	float checktime;
	float blocktime;
	int id;
	enum connectionstate state;
	qboolean process;

	// query or response
   char buf[BUFSIZ+1];
   int header_size, query_length, bytes_sent;
} httpfile_t;

static struct http_static_s
{
	// file and server lists
	httpfile_t *first_file, *last_file;
	httpserver_t *first_server, *last_server;
} http;


static convar_t *http_useragent;
static convar_t *http_autoremove;
static convar_t *http_timeout;
static convar_t *http_maxconnections;

/*
========================
HTTP_ClearCustomServers
========================
*/
void HTTP_ClearCustomServers( void )
{
	if( http.first_file )
		return; // may be referenced

	while( http.first_server && http.first_server->needfree )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}
}

/*
==============
HTTP_FreeFile

Skip to next server/file
==============
*/
static void HTTP_FreeFile( httpfile_t *file, qboolean error )
{
	char incname[256];

	// Allways close file and socket
	if( file->file )
		FS_Close( file->file );

	file->file = NULL;

	if( file->socket != -1 )
		closesocket( file->socket );

	file->socket = -1;

	Q_snprintf( incname, 256, "downloaded/%s.incomplete", file->path );
	if( error )
	{
		// Switch to next fastdl server if present
		if( file->server && ( file->state > HTTP_QUEUE ) && (file->state != HTTP_FREE ) )
		{
			file->server = file->server->next;
			file->state = HTTP_QUEUE; // Reset download state, HTTP_Run() will open file again
			return;
		}

		// Called because there was no servers to download, free file now
		if( http_autoremove->value == 1 ) // remove broken file
			FS_Delete( incname );
		else // autoremove disabled, keep file
			Con_Printf( "cannot download %s from any server. "
				"You may remove %s now\n", file->path, incname ); // Warn about trash file

		if( file->process )
			CL_ProcessFile( false, file->path ); // Process file, increase counter
	}
	else
	{
		// Success, rename and process file
		char name[256];

		Q_snprintf( name, 256, "downloaded/%s", file->path );
		FS_Rename( incname, name );

		if( file->process )
			CL_ProcessFile( true, name );
		else
			Con_Printf( "successfully downloaded %s, processing disabled!\n", name );
	}

	file->state = HTTP_FREE;
}

/*
===================
HTTP_AutoClean

remove files with HTTP_FREE state from list
===================
*/
static void HTTP_AutoClean( void )
{
	httpfile_t *curfile, *prevfile = 0;

	// clean all files marked to free
	for( curfile = http.first_file; curfile; curfile = curfile->next )
	{
		if( curfile->state != HTTP_FREE )
		{
			prevfile = curfile;
			continue;
		}

		if( curfile == http.first_file )
		{
			http.first_file = http.first_file->next;
			Mem_Free( curfile );
			curfile = http.first_file;
			if( !curfile )
				break;
			continue;
		}

		if( prevfile )
			prevfile->next = curfile->next;
		Mem_Free( curfile );
		curfile = prevfile;
		if( !curfile )
			break;
	}
	http.last_file = prevfile;
}

/*
===================
HTTP_ProcessStream

process incoming data
===================
*/
static qboolean HTTP_ProcessStream( httpfile_t *curfile )
{
	char buf[BUFSIZ+1];
	char *begin = 0;
	int res;

	if( curfile->header_size >= BUFSIZ )
	{
		Con_Reportf( S_ERROR "Header to big\n");
		HTTP_FreeFile( curfile, true );
		return false;
	}

	while( ( res = recv( curfile->socket, buf, BUFSIZ - curfile->header_size, 0 ) ) > 0) // if we got there, we are receiving data
	{
		curfile->blocktime = 0;

		if( curfile->state < HTTP_RESPONSE_RECEIVED ) // Response still not received
		{
			memcpy( curfile->buf + curfile->header_size, buf, res );
			curfile->buf[curfile->header_size + res] = 0;
			begin = Q_strstr( curfile->buf, "\r\n\r\n" );

			if( begin ) // Got full header
			{
				int cutheadersize = begin - curfile->buf + 4; // after that begin of data
				char *length;

				Con_Reportf( "HTTP: Got response!\n" );

				if( !Q_strstr( curfile->buf, "200 OK" ) )
				{
					*begin = 0; // cut string to print out response
					begin = Q_strchr( curfile->buf, '\r' );

					if( !begin ) begin = Q_strchr( curfile->buf, '\n' );
					if( begin )
						*begin = 0;

					Con_Printf( S_ERROR "%s: bad response: %s\n", curfile->path, curfile->buf );
					HTTP_FreeFile( curfile, true );
					return false;
				}

				// print size
				length = Q_stristr( curfile->buf, "Content-Length: " );
				if( length )
				{
					int size = Q_atoi( length += 16 );

					Con_Reportf( "HTTP: File size is %d\n", size );

					if( ( curfile->size != -1 ) && ( curfile->size != size ) ) // check size if specified, not used
						Con_Reportf( S_WARN "Server reports wrong file size!\n" );

					curfile->size = size;
					curfile->header_size = 0;
				}

				if( curfile->size == -1 )
				{
					// Usually fastdl's reports file size if link is correct
					Con_Printf( S_ERROR "file size is unknown, refusing download!\n" );
					HTTP_FreeFile( curfile, true );
					return false;
				}

				curfile->state = HTTP_RESPONSE_RECEIVED; // got response, let's start download
				begin += 4;

				// Write remaining message part
				if( res - cutheadersize - curfile->header_size > 0 )
				{
					int ret = FS_Write( curfile->file, begin, res - cutheadersize - curfile->header_size );

					if( ret != res - cutheadersize - curfile->header_size ) // could not write file
					{
						// close it and go to next
						Con_Printf( S_ERROR "write failed for %s!\n", curfile->path );
						HTTP_FreeFile( curfile, true );
						return false;
					}
					curfile->downloaded += ret;
				}
			}
			else
				curfile->header_size += res;
		}
		else if( res > 0 )
		{
			// data download
			int ret = FS_Write( curfile->file, buf, res );

			if ( ret != res )
			{
				// close it and go to next
				Con_Printf( S_ERROR "write failed for %s!\n", curfile->path );
				curfile->state = HTTP_FREE;
				HTTP_FreeFile( curfile, true );
				return false;
			}

			curfile->downloaded += ret;
			curfile->lastchecksize += ret;

			// as after it will run in same frame
			if( curfile->checktime > 5 )
			{
				float speed = (float)curfile->lastchecksize / ( 5.0f * 1024 );

				curfile->checktime = 0;
				Con_Reportf( "download speed %f KB/s\n", speed );
				curfile->lastchecksize = 0;
			}
		}
	}
	curfile->checktime += host.frametime;

	return true;
}

/*
==============
HTTP_Run

Download next file block of each active file
Call every frame
==============
*/
void HTTP_Run( void )
{
	httpfile_t *curfile;
	int iActiveCount = 0;
	int iProgressCount = 0;
	float flProgress = 0;
	qboolean fResolving = false;

	for( curfile = http.first_file; curfile; curfile = curfile->next )
	{
		int res;
		struct sockaddr addr;

		if( curfile->state == HTTP_FREE )
			continue;

		if( curfile->state == HTTP_QUEUE )
		{
			char name[MAX_SYSPATH];

			if( iActiveCount > http_maxconnections->value )
				continue;

			if( !curfile->server )
			{
				Con_Printf( S_ERROR "no servers to download %s!\n", curfile->path );
				HTTP_FreeFile( curfile, true );
				break;
			}

			Con_Reportf( "HTTP: Starting download %s from %s\n", curfile->path, curfile->server->host );
			Q_snprintf( name, sizeof( name ), "downloaded/%s.incomplete", curfile->path );

			curfile->file = FS_Open( name, "wb", true );

			if( !curfile->file )
			{
				Con_Printf( S_ERROR "cannot open %s!\n", name );
				HTTP_FreeFile( curfile, true );
				break;
			}

			curfile->state = HTTP_OPENED;
			curfile->blocktime = 0;
			curfile->downloaded = 0;
			curfile->lastchecksize = 0;
			curfile->checktime = 0;
		}

		iActiveCount++;

		if( curfile->state < HTTP_SOCKET ) // Socket is not created
		{
			dword mode;

			curfile->socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

			// Now set non-blocking mode
			// You may skip this if not supported by system,
			// but download will lock engine, maybe you will need to add manual returns
			mode = 1;
			ioctlsocket( curfile->socket, FIONBIO, &mode );
#if XASH_LINUX
			// SOCK_NONBLOCK is not portable, so use fcntl
			fcntl( curfile->socket, F_SETFL, fcntl( curfile->socket, F_GETFL, 0 ) | O_NONBLOCK );
#endif
			curfile->state = HTTP_SOCKET;
		}

		if( curfile->state < HTTP_NS_RESOLVED )
		{
			if( fResolving )
				continue;

			res = NET_StringToSockaddr( va( "%s:%d", curfile->server->host, curfile->server->port ), &addr, true );

			if( res == 2 )
			{
				fResolving = true;
				continue;
			}

			if( !res )
			{
				Con_Printf( S_ERROR "failed to resolve server address for %s!\n", curfile->server->host );
				HTTP_FreeFile( curfile, true ); // Cannot connect
				break;
			}
			curfile->state = HTTP_NS_RESOLVED;
		}

		if( curfile->state < HTTP_CONNECTED ) // Connection not enstabilished
		{
			res = connect( curfile->socket, &addr, sizeof( struct sockaddr ) );

			if( res )
			{
				if( WSAGetLastError() == WSAEINPROGRESS || WSAGetLastError() == WSAEWOULDBLOCK ) // Should give EWOOLDBLOCK if try recv too soon
					curfile->state = HTTP_CONNECTED;
				else
				{
					Con_Printf( S_ERROR "cannot connect to server: %s\n", NET_ErrorString( ) );
					HTTP_FreeFile( curfile, true ); // Cannot connect
					break;
				}
				continue; // skip to next file
			}
			curfile->state = HTTP_CONNECTED;
		}

		if( curfile->state < HTTP_REQUEST ) // Request not formatted
		{
			curfile->query_length = Q_snprintf( curfile->buf, sizeof( curfile->buf ),
				"GET %s%s HTTP/1.0\r\n"
				"Host: %s\r\n"
				"User-Agent: %s\r\n\r\n", curfile->server->path,
				curfile->path, curfile->server->host, http_useragent->string );
			curfile->header_size = 0;
			curfile->bytes_sent = 0;
			curfile->state = HTTP_REQUEST;
		}

		if( curfile->state < HTTP_REQUEST_SENT ) // Request not sent
		{
			qboolean wait = false;

			while( curfile->bytes_sent < curfile->query_length )
			{
				res = send( curfile->socket, curfile->buf + curfile->bytes_sent, curfile->query_length - curfile->bytes_sent, 0 );


				if( res < 0 )
				{
					if( WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAENOTCONN )
					{
						Con_Printf( S_ERROR "failed to send request: %s\n", NET_ErrorString() );
						HTTP_FreeFile( curfile, true );
						wait = true;
						break;
					}
					// blocking while waiting connection
					// increase counter when blocking
					curfile->blocktime += host.frametime;
					wait = true;

					if( curfile->blocktime > http_timeout->value )
					{
						Con_Printf( S_ERROR "timeout on request send:\n%s\n", curfile->buf );
						HTTP_FreeFile( curfile, true );
						break;
					}
					break;
				}
				else
				{
					curfile->bytes_sent += res;
					curfile->blocktime = 0;
				}
			}

			if( wait )
				continue;

			Con_Reportf( "HTTP: Request sent!\n");
			memset( curfile->buf, 0, sizeof( curfile->buf ) );
			curfile->state = HTTP_REQUEST_SENT;
		}

		if( !HTTP_ProcessStream( curfile ) )
			break;

		if( curfile->size > 0 )
		{
			flProgress += (float)curfile->downloaded / curfile->size;
			iProgressCount++;
		}

		if( curfile->size > 0 && curfile->downloaded >= curfile->size )
		{
			HTTP_FreeFile( curfile, false ); // success
			break;
		}
		else if( (WSAGetLastError() != WSAEWOULDBLOCK) && (WSAGetLastError() != WSAEINPROGRESS) )
			Con_Reportf( "problem downloading %s:\n%s\n", curfile->path, NET_ErrorString() );
		else
			curfile->blocktime += host.frametime;

		if( curfile->blocktime > http_timeout->value )
		{
			Con_Printf( S_ERROR "timeout on receiving data!\n");
			HTTP_FreeFile( curfile, true );
			break;
		}
	}

	// update progress
	if( !Host_IsDedicated() )
		Cvar_SetValue( "scr_download", flProgress/iProgressCount * 100 );

	HTTP_AutoClean();
}

/*
===================
HTTP_AddDownload

Add new download to end of queue
===================
*/
void HTTP_AddDownload( const char *path, int size, qboolean process )
{
	httpfile_t *httpfile = Z_Calloc( sizeof( httpfile_t ) );

	Con_Reportf( "File %s queued to download\n", path );

	httpfile->size = size;
	httpfile->downloaded = 0;
	httpfile->socket = -1;
	Q_strncpy ( httpfile->path, path, sizeof( httpfile->path ) );

	if( http.last_file )
	{
		// Add next to last download
		httpfile->id = http.last_file->id + 1;
		http.last_file->next= httpfile;
		http.last_file = httpfile;
	}
	else
	{
		// It will be the only download
		httpfile->id = 0;
		http.last_file = http.first_file = httpfile;
	}

	httpfile->file = NULL;
	httpfile->next = NULL;
	httpfile->state = HTTP_QUEUE;
	httpfile->server = http.first_server;
	httpfile->process = process;
}

/*
===============
HTTP_Download_f

Console wrapper
===============
*/
static void HTTP_Download_f( void )
{
	if( Cmd_Argc() < 2 )
	{
		Con_Printf( S_USAGE "download <gamedir_path>\n");
		return;
	}

	HTTP_AddDownload( Cmd_Argv( 1 ), -1, false );
}

/*
==============
HTTP_ParseURL
==============
*/
static httpserver_t *HTTP_ParseURL( const char *url )
{
	httpserver_t *server;
	int i;

	url = Q_strstr( url, "http://" );

	if( !url )
		return NULL;

	url += 7;
	server = Z_Calloc( sizeof( httpserver_t ) );
	i = 0;

	while( *url && ( *url != ':' ) && ( *url != '/' ) && ( *url != '\r' ) && ( *url != '\n' ) )
	{
		if( i > sizeof( server->host ) )
			return NULL;

		server->host[i++] = *url++;
	}

	server->host[i] = 0;

	if( *url == ':' )
	{
		server->port = Q_atoi( ++url );

		while( *url && ( *url != '/' ) && ( *url != '\r' ) && ( *url != '\n' ) )
			url++;
	}
	else
		server->port = 80;

	i = 0;

	while( *url && ( *url != '\r' ) && ( *url != '\n' ) )
	{
		if( i > sizeof( server->path ) )
			return NULL;

		server->path[i++] = *url++;
	}

	server->path[i] = 0;
	server->next = NULL;
	server->needfree = false;

	return server;
}

/*
=======================
HTTP_AddCustomServer
=======================
*/
void HTTP_AddCustomServer( const char *url )
{
	httpserver_t *server = HTTP_ParseURL( url );

	if( !server )
	{
		Con_Printf( S_ERROR "\"%s\" is not valid url!\n", url );
		return;
	}

	server->needfree = true;
	server->next = http.first_server;
	http.first_server = server;
}

/*
=======================
HTTP_AddCustomServer_f
=======================
*/
static void HTTP_AddCustomServer_f( void )
{
	if( Cmd_Argc() == 2 )
	{
		HTTP_AddCustomServer( Cmd_Argv( 1 ) );
	}
}

/*
============
HTTP_Clear_f

Clear all queue
============
*/
static void HTTP_Clear_f( void )
{
	http.last_file = NULL;

	while( http.first_file )
	{
		httpfile_t *file = http.first_file;

		http.first_file = http.first_file->next;

		if( file->file )
			FS_Close( file->file );

		if( file->socket != -1 )
			close ( file->socket );

		Mem_Free( file );
	}
}

/*
==============
HTTP_Cancel_f

Stop current download, skip to next file
==============
*/
static void HTTP_Cancel_f( void )
{
	if( !http.first_file )
		return;

	http.first_file->state = HTTP_FREE;
	HTTP_FreeFile( http.first_file, true );
}

/*
=============
HTTP_Skip_f

Stop current download, skip to next server
=============
*/
static void HTTP_Skip_f( void )
{
	if( http.first_file )
		HTTP_FreeFile( http.first_file, true );
}

/*
=============
HTTP_List_f

Print all pending downloads to console
=============
*/
static void HTTP_List_f( void )
{
	httpfile_t *file = http.first_file;

	while( file )
	{
		if ( file->server )
			Con_Printf ( "\t%d %d http://%s:%d/%s%s %d\n", file->id, file->state,
				file->server->host, file->server->port, file->server->path,
				file->path, file->downloaded );
		else
			Con_Printf ( "\t%d %d (no server) %s\n", file->id, file->state, file->path );

		file = file->next;
	}
}

/*
================
HTTP_ResetProcessState

When connected to new server, all old files should not increase counter
================
*/
void HTTP_ResetProcessState( void )
{
	httpfile_t *file = http.first_file;

	while( file )
	{
		file->process = false;
		file = file->next;
	}
}

/*
=============
HTTP_Init
=============
*/
void HTTP_Init( void )
{
	char *serverfile, *line, token[1024];

	http.last_server = NULL;

	http.first_file = http.last_file = NULL;

	Cmd_AddCommand("http_download", &HTTP_Download_f, "add file to download queue");
	Cmd_AddCommand("http_skip", &HTTP_Skip_f, "skip current download server");
	Cmd_AddCommand("http_cancel", &HTTP_Cancel_f, "cancel current download");
	Cmd_AddCommand("http_clear", &HTTP_Clear_f, "cancel all downloads");
	Cmd_AddCommand("http_list", &HTTP_List_f, "list all queued downloads");
	Cmd_AddCommand("http_addcustomserver", &HTTP_AddCustomServer_f, "add custom fastdl server");
	http_useragent = Cvar_Get( "http_useragent", "xash3d", FCVAR_ARCHIVE, "User-Agent string" );
	http_autoremove = Cvar_Get( "http_autoremove", "1", FCVAR_ARCHIVE, "remove broken files" );
	http_timeout = Cvar_Get( "http_timeout", "45", FCVAR_ARCHIVE, "timeout for http downloader" );
	http_maxconnections =  Cvar_Get( "http_maxconnections", "4", FCVAR_ARCHIVE, "maximum http connection number" );

	// Read servers from fastdl.txt
	line = serverfile = (char *)FS_LoadFile( "fastdl.txt", 0, false );

	if( serverfile )
	{
		while( ( line = COM_ParseFile( line, token ) ) )
		{
			httpserver_t *server = HTTP_ParseURL( token );

			if( !server )
				continue;

			if( !http.last_server )
				http.last_server = http.first_server = server;
			else
			{
				http.last_server->next = server;
				http.last_server = server;
			}
		}

		Mem_Free( serverfile );
	}
}

/*
====================
HTTP_Shutdown
====================
*/
void HTTP_Shutdown( void )
{
	HTTP_Clear_f();

	while( http.first_server )
	{
		httpserver_t *tmp = http.first_server;

		http.first_server = http.first_server->next;
		Mem_Free( tmp );
	}

	http.last_server = NULL;
}
