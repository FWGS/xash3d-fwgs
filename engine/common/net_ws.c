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
#include "xash3d_mathlib.h"
#include "ipv6text.h"
#include "net_ws_private.h"
#include "server.h" // sv_cheats

#if XASH_SDL == 2
#include <SDL_thread.h>
#endif

#define NET_USE_FRAGMENTS

#define MAX_LOOPBACK		4
#define MASK_LOOPBACK		(MAX_LOOPBACK - 1)

#define MAX_ROUTEABLE_PACKET      1400
#define SPLITPACKET_MIN_SIZE      508   // RFC 791: 576(min ip packet) - 60 (ip header) - 8 (udp header)
#define SPLITPACKET_MAX_SIZE      64000
#define NET_MAX_FRAGMENTS         ( NET_MAX_FRAGMENT / (SPLITPACKET_MIN_SIZE - sizeof( SPLITPACKET )))
#define NET_MAX_GOLDSRC_FRAGMENTS 5 // magic number

// ff02:1
static const uint8_t k_ipv6Bytes_LinkLocalAllNodes[16] =
{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

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

typedef struct
{
	int		net_id;
	int		sequence_number;
	unsigned char	packet_id;
} SPLITPACKETGS;
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
	int		ip6_sockets[NS_COUNT];
	qboolean		initialized;
	qboolean		threads_initialized;
	qboolean		configured;
	qboolean		allow_ip;
	qboolean		allow_ip6;
#if XASH_WIN32
	WSADATA		winsockdata;
#endif
} net_state_t;

static net_state_t		net;
static CVAR_DEFINE_AUTO( net_address, "0", FCVAR_PRIVILEGED|FCVAR_READ_ONLY, "contain local address of current client" );
static CVAR_DEFINE( net_ipname, "ip", "localhost", FCVAR_PRIVILEGED, "network ip address" );
static CVAR_DEFINE( net_iphostport, "ip_hostport", "0", FCVAR_READ_ONLY, "network ip host port" );
static CVAR_DEFINE( net_hostport, "hostport", "0", FCVAR_READ_ONLY, "network default host port" );
static CVAR_DEFINE( net_ipclientport, "ip_clientport", "0", FCVAR_READ_ONLY, "network ip client port" );
static CVAR_DEFINE( net_clientport, "clientport", "0", FCVAR_READ_ONLY, "network default client port" );
static CVAR_DEFINE( net_fakelag, "fakelag", "0", FCVAR_PRIVILEGED, "lag all incoming network data (including loopback) by xxx ms." );
static CVAR_DEFINE( net_fakeloss, "fakeloss", "0", FCVAR_PRIVILEGED, "act like we dropped the packet this % of the time." );
static CVAR_DEFINE_AUTO( net_resolve_debug, "0", FCVAR_PRIVILEGED, "print resolve thread debug messages" );
CVAR_DEFINE( net_clockwindow, "clockwindow", "0.5", FCVAR_PRIVILEGED, "timewindow to execute client moves" );

netadr_t			net_local;
static netadr_t		net6_local;

// cvars equivalents for IPv6
static CVAR_DEFINE( net_ip6name, "ip6", "localhost", FCVAR_PRIVILEGED, "network ip6 address" );
static CVAR_DEFINE( net_ip6hostport, "ip6_hostport", "0", FCVAR_READ_ONLY, "network ip6 host port" );
static CVAR_DEFINE( net_ip6clientport, "ip6_clientport", "0", FCVAR_READ_ONLY, "network ip6 client port" );
static CVAR_DEFINE_AUTO( net6_address, "0", FCVAR_PRIVILEGED|FCVAR_READ_ONLY, "contain local IPv6 address of current client" );

static void NET_ClearLagData( qboolean bClient, qboolean bServer );

static inline qboolean NET_IsSocketError( int retval )
{
#if XASH_WIN32 || XASH_DOS4GW
	return retval == SOCKET_ERROR ? true : false;
#else
	return retval < 0 ? true : false;
#endif
}

static inline qboolean NET_IsSocketValid( int socket )
{
#if XASH_WIN32 || XASH_DOS4GW
	return socket != INVALID_SOCKET;
#else
	return socket >= 0;
#endif
}

void NET_NetadrToIP6Bytes( uint8_t *ip6, const netadr_t *adr )
{
	memcpy( &ip6[0], adr->ip6_0, 2 );
	memcpy( &ip6[2], adr->ip6_1, 14 );
}

void NET_IP6BytesToNetadr( netadr_t *adr, const uint8_t *ip6 )
{
	memcpy( adr->ip6_0, &ip6[0], 2 );
	memcpy( adr->ip6_1, &ip6[2], 14 );
}

static int NET_NetadrIP6Compare( const netadr_t *a, const netadr_t *b )
{
	uint8_t ip6_a[16], ip6_b[16];

	NET_NetadrToIP6Bytes( ip6_a, a );
	NET_NetadrToIP6Bytes( ip6_b, b );

	return memcmp( ip6_a, ip6_b, sizeof( ip6_a ));
}

/*
====================
NET_NetadrToSockadr
====================
*/
static void NET_NetadrToSockadr( netadr_t *a, struct sockaddr_storage *s )
{
	netadrtype_t type = NET_NetadrType( a );

	memset( s, 0, sizeof( *s ));

	if( type == NA_BROADCAST )
	{
		s->ss_family = AF_INET;
		((struct sockaddr_in *)s)->sin_port = a->port;
		((struct sockaddr_in *)s)->sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if( type == NA_IP )
	{
		s->ss_family = AF_INET;
		((struct sockaddr_in *)s)->sin_port = a->port;
		((struct sockaddr_in *)s)->sin_addr.s_addr = a->ip4;
	}
	else if( type == NA_IP6 )
	{
		s->ss_family = AF_INET6;
		((struct sockaddr_in6 *)s)->sin6_port = a->port;
		NET_NetadrToIP6Bytes(((struct sockaddr_in6 *)s)->sin6_addr.s6_addr, a );
	}
	else if( type == NA_MULTICAST_IP6 )
	{
		s->ss_family = AF_INET6;
		((struct sockaddr_in6 *)s)->sin6_port = a->port;
		memcpy(((struct sockaddr_in6 *)s)->sin6_addr.s6_addr, k_ipv6Bytes_LinkLocalAllNodes, sizeof( struct in6_addr ));
	}
}

/*
====================
NET_SockadrToNetAdr
====================
*/
static void NET_SockadrToNetadr( const struct sockaddr_storage *s, netadr_t *a )
{
	if( s->ss_family == AF_INET )
	{
		NET_NetadrSetType( a, NA_IP );
		a->ip4 = ((struct sockaddr_in *)s)->sin_addr.s_addr;
		a->port = ((struct sockaddr_in *)s)->sin_port;
	}
	else if( s->ss_family == AF_INET6 )
	{
		NET_NetadrSetType( a, NA_IP6 );
		NET_IP6BytesToNetadr( a, ((struct sockaddr_in6 *)s)->sin6_addr.s6_addr );
		a->port = ((struct sockaddr_in6 *)s)->sin6_port;
	}
}

/*
============
NET_GetHostByName
============
*/
static qboolean NET_GetHostByName( const char *hostname, int family, struct sockaddr_storage *addr )
{
	struct addrinfo *ai = NULL, *cur;
	struct addrinfo hints;
	qboolean ret = false;

#if XASH_NO_IPV6_RESOLVE
	if( family == AF_INET6 )
		return false;
#endif

	memset( &hints, 0, sizeof( hints ));
	hints.ai_family = family;

	if( !getaddrinfo( hostname, NULL, &hints, &ai ))
	{
		for( cur = ai; cur; cur = cur->ai_next )
		{
			if( family == AF_UNSPEC || cur->ai_family == family )
			{
				memcpy( addr, cur->ai_addr, cur->ai_addrlen );
				ret = true;
				break;
			}
		}

		if( ai )
			freeaddrinfo( ai );
	}

	return ret;
}

static void NET_ResolveThread( void );

#if XASH_SDL == 2
#define mutex_create( x )    (( x ) = SDL_CreateMutex() )
#define mutex_destroy( x )   SDL_DestroyMutex(( x ))
#define mutex_lock( x )      SDL_LockMutex(( x ))
#define mutex_unlock( x )    SDL_UnlockMutex(( x ))
#define create_thread( thread, pfn ) (( thread ) = SDL_CreateThread(( pfn ), "DNS resolver thread", NULL ))
#define detach_thread( x )   SDL_DetachThread(( x ))
typedef SDL_mutex *mutex_t;
typedef SDL_Thread *thread_t;
static int NET_ThreadStart( void *ununsed )
{
	NET_ResolveThread();
	return 0;
}
#elif !XASH_WIN32
#include <pthread.h>
#define mutex_create( x )     pthread_mutex_init( &( x ), NULL )
#define mutex_destroy( x )    pthread_mutex_destroy( &( x ))
#define mutex_lock( x )       pthread_mutex_lock( &( x ))
#define mutex_unlock( x )     pthread_mutex_unlock( &( x ))
#define create_thread( thread, pfn ) !pthread_create( &( thread ), NULL, ( pfn ), NULL )
#define detach_thread( x )    pthread_detach( x )
typedef pthread_mutex_t mutex_t;
typedef pthread_t thread_t;
static void *NET_ThreadStart( void *unused )
{
	NET_ResolveThread();
	return NULL;
}
#else // WIN32
#define mutex_create( x )   InitializeCriticalSection( &( x ))
#define mutex_destroy( x )  DeleteCriticalSection( &( x ))
#define mutex_lock( x )     EnterCriticalSection( &( x ))
#define mutex_unlock( x )   LeaveCriticalSection( &( x ))
#define create_thread( thread, pfn ) (( thread ) = CreateThread( NULL, 0, ( pfn ), NULL, 0, NULL ))
#define detach_thread( x )   CloseHandle(( x ))
typedef CRITICAL_SECTION mutex_t;
typedef HANDLE thread_t;
DWORD WINAPI NET_ThreadStart( LPVOID unused )
{
	NET_ResolveThread();
	ExitThread( 0 );
	return 0;
}
#endif // !_WIN32

#define RESOLVE_DBG( x ) do { if( net_resolve_debug.value ) Sys_PrintLog(( x )); } while( 0 )

static struct nsthread_s
{
	mutex_t  mutexns;
	mutex_t  mutexres;
	thread_t thread;
	int      result;
	string   hostname;
	int      family;
	struct sockaddr_storage addr;
	qboolean busy;
} nsthread;

static void NET_InitializeCriticalSections( void )
{
	net.threads_initialized = true;

	mutex_create( nsthread.mutexns );
	mutex_create( nsthread.mutexres );
}

static void NET_DeleteCriticalSections( void )
{
	if( net.threads_initialized )
	{
		mutex_destroy( nsthread.mutexns );
		mutex_destroy( nsthread.mutexres );

		net.threads_initialized = false;
	}

	memset( &nsthread, 0, sizeof( nsthread ));
}

static void NET_ResolveThread( void )
{
	struct sockaddr_storage addr;
	qboolean res;

	RESOLVE_DBG( "[resolve thread] starting resolve for " );
	RESOLVE_DBG( nsthread.hostname );
	RESOLVE_DBG( " with getaddrinfo\n" );

	if(( res = NET_GetHostByName( nsthread.hostname, nsthread.family, &addr )))
		RESOLVE_DBG( "[resolve thread] success\n" );
	else
		RESOLVE_DBG( "[resolve thread] failed\n" );
	mutex_lock( nsthread.mutexres );
	nsthread.addr = addr;
	nsthread.busy = false;
	nsthread.result = res ? NET_EAI_OK : NET_EAI_NONAME;
	RESOLVE_DBG( "[resolve thread] returning result\n" );
	mutex_unlock( nsthread.mutexres );
	RESOLVE_DBG( "[resolve thread] exiting thread\n" );
}

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
net_gai_state_t NET_StringToSockaddr( const char *s, struct sockaddr_storage *sadr, qboolean nonblocking, int family )
{
	int ret = 0, port;
	char	*colon;
	char	copy[128];
	byte ip6[16];
	struct sockaddr_storage temp;

	if( !net.initialized )
		return NET_EAI_NONAME;

	memset( sadr, 0, sizeof( *sadr ));

	// try to parse it as IPv6 first
	if(( family == AF_UNSPEC || family == AF_INET6 ) && ParseIPv6Addr( s, ip6, &port, NULL ))
	{
		((struct sockaddr_in6 *)sadr)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)sadr)->sin6_port = htons((short)port);
		memcpy(((struct sockaddr_in6 *)sadr)->sin6_addr.s6_addr, ip6, sizeof( struct in6_addr ));

		return NET_EAI_OK;
	}

	Q_strncpy( copy, s, sizeof( copy ));

	// strip off a trailing :port if present
	((struct sockaddr_in *)sadr)->sin_port = 0;
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
		((struct sockaddr_in *)sadr)->sin_family = AF_INET;
		((struct sockaddr_in *)sadr)->sin_addr.s_addr = inet_addr( copy );
	}
	else
	{
		qboolean asyncfailed = true;

		if( net.threads_initialized && nonblocking )
		{
			mutex_lock( nsthread.mutexres );

			if( nsthread.busy )
			{
				mutex_unlock( nsthread.mutexres );
				return NET_EAI_AGAIN;
			}

			if( !Q_strcmp( copy, nsthread.hostname ))
			{
				ret = nsthread.result;

				nsthread.hostname[0] = '\0';
				nsthread.family = AF_UNSPEC;
				temp = nsthread.addr;
				memset( &nsthread.addr, 0, sizeof( nsthread.addr ));

				detach_thread( nsthread.thread );
				asyncfailed = false;
			}
			else
			{
				Q_strncpy( nsthread.hostname, copy, sizeof( nsthread.hostname ));
				nsthread.family = family;
				nsthread.busy = true;
				mutex_unlock( nsthread.mutexres );

				if( create_thread( nsthread.thread, NET_ThreadStart ))
				{
					asyncfailed = false;
					return NET_EAI_AGAIN;
				}

				Con_Reportf( S_ERROR "%s: failed to create thread!\n", __func__ );
				nsthread.busy = false;
			}

			mutex_unlock( nsthread.mutexres );
		}

		if( asyncfailed )
			ret = NET_GetHostByName( copy, family, &temp );

		if( !ret )
		{
			if( family == AF_INET6 )
				sadr->ss_family = AF_INET6;
			else sadr->ss_family = AF_INET;

			return NET_EAI_NONAME;
		}

		sadr->ss_family = temp.ss_family;

		if( temp.ss_family == AF_INET )
			((struct sockaddr_in *)sadr)->sin_addr = ((struct sockaddr_in*)&temp)->sin_addr;
		else if( temp.ss_family == AF_INET6 )
			((struct sockaddr_in6 *)sadr)->sin6_addr = ((struct sockaddr_in6*)&temp)->sin6_addr;
	}

	return NET_EAI_OK;
}

/*
====================
NET_StringToFilterAdr

====================
*/
qboolean NET_StringToFilterAdr( const char *s, netadr_t *adr, uint *prefixlen )
{
	char copy[128], *temp;
	qboolean hasCIDR = false;
	byte ip6[16];
	uint len;

	if( COM_StringEmpty( s ))
		return false;

	memset( adr, 0, sizeof( *adr ));

	// copy the string and remove CIDR prefix
	Q_strncpy( copy, s, sizeof( copy ));
	temp = Q_strrchr( copy, '/' );

	if( temp )
	{
		*temp = 0;
		if( Q_isdigit( temp + 1 ))
		{
			len = Q_atoi( temp + 1 );
			hasCIDR = len != 0;
		}
	}

	// try to parse as IPv6 first
	if( ParseIPv6Addr( copy, ip6, NULL, NULL ))
	{
		NET_NetadrSetType( adr, NA_IP6 );
		NET_IP6BytesToNetadr( adr, ip6 );

		if( !hasCIDR )
			*prefixlen = 128;
		else
			*prefixlen = len;
	}
	else
	{
		int num = 0;
		int octet = 0;

		// parse as ipv4 but we don't need to allow all forms here
		for( temp = copy; *temp; temp++ )
		{
			char c = *temp;

			if( c >= '0' && c <= '9' )
			{
				num *= 10;
				num += c - '0';
			}
			else if( c == '.' )
			{
				if( num > 255 )
					return false;

				adr->ip[octet++] = num;
				num = 0;

				if( octet > 3 )
					return false;
			}
			else
			{
				return false;
			}
		}

		if( num > 255 )
			return false;

		adr->ip[octet++] = num;

		if( !hasCIDR )
		{
			int i;

			*prefixlen = 32;

			for( i = 3; i >= 0; i-- )
			{
				if( !adr->ip[i] )
					*prefixlen -= 8;
				else
					break;
			}
		}
		else
		{
			uint32_t mask;

			len = bound( 0, len, 32 );
			*prefixlen = len;

			// drop unneeded bits
			mask = htonl( adr->ip4 ) & ( 0xFFFFFFFF << ( 32 - len ));
			adr->ip4 = ntohl( mask );
		}

		NET_NetadrSetType( adr, NA_IP );
	}

	return true;
}

/*
====================
NET_AdrToString
====================
*/
const char *NET_AdrToString( const netadr_t a )
{
	static char s[64];
	netadrtype_t type = NET_NetadrType( &a );

	if( type == NA_LOOPBACK )
		return "loopback";
	if( type == NA_IP6 || type == NA_MULTICAST_IP6 )
	{
		uint8_t ip6[16];

		NET_NetadrToIP6Bytes( ip6, &a );
		IPv6AddrToString( s, ip6, ntohs( a.port ), 0 );

		return s;
	}

	Q_snprintf( s, sizeof( s ),
		"%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], ntohs( a.port ));

	return s;
}

/*
====================
NET_BaseAdrToString
====================
*/
const char *NET_BaseAdrToString( const netadr_t a )
{
	static char s[64];
	netadrtype_t type = NET_NetadrType( &a );

	if( type == NA_LOOPBACK )
		return "loopback";
	if( type == NA_IP6 || type == NA_MULTICAST_IP6 )
	{
		uint8_t ip6[16];

		NET_NetadrToIP6Bytes( ip6, &a );
		IPv6IPToString( s, ip6 );

		return s;
	}

	Q_snprintf( s, sizeof( s ),
		"%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3] );

	return s;
}

/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b )
{
	netadrtype_t type_a = NET_NetadrType( &a );
	netadrtype_t type_b = NET_NetadrType( &b );

	if( type_a != type_b )
		return false;

	if( type_a == NA_LOOPBACK )
		return true;

	if( type_a == NA_IP )
		return a.ip4 == b.ip4;

	if( type_a == NA_IP6 )
	{
		if( !NET_NetadrIP6Compare( &a, &b ))
		    return true;
	}

	return false;
}

/*
====================
NET_CompareAdrByMask

Checks if adr is a part of subnet
====================
*/
qboolean NET_CompareAdrByMask( const netadr_t a, const netadr_t b, uint prefixlen )
{
	netadrtype_t type_a = NET_NetadrType( &a );
	netadrtype_t type_b = NET_NetadrType( &b );

	if( type_a != type_b || type_a == NA_LOOPBACK )
		return false;

	if( type_a == NA_IP )
	{
		uint32_t ipa = htonl( a.ip4 );
		uint32_t ipb = htonl( b.ip4 );

		if(( ipa & (( 0xFFFFFFFFU ) << ( 32 - prefixlen ))) == ipb )
			return true;
	}
	else if( type_a == NA_IP6 )
	{
		uint16_t a_[8], b_[8];
		size_t check     = prefixlen / 16;
		size_t remaining = prefixlen % 16;

		// convert to 16-bit pieces first
		NET_NetadrToIP6Bytes( (uint8_t*)a_, &a );
		NET_NetadrToIP6Bytes( (uint8_t*)b_, &b );

		// check complete hextets first, if not equal, then it's different subnets
		if( check && memcmp( a_, b_, check * sizeof( uint16_t )))
			return false;

		// check by bits now, similar to v4 check but with 16-bit type
		if( remaining )
		{
			uint16_t hexa, hexb, mask = 0xFFFFU << ( 16 - remaining );

			hexa = htons( a_[check] );
			hexb = htons( b_[check] );

			if(( hexa & mask ) == ( hexb & mask ))
				return true;
		}
		else
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
	netadrtype_t type_a = NET_NetadrType( &a );

	if( type_a == NA_LOOPBACK )
		return true;

	// Following checks was imported from GameNetworkingSockets library
	if( type_a == NA_IP )
	{
		if(( a.ip[0] == 10 ) || // 10.x.x.x is reserved
			( a.ip[0] == 127 ) || // 127.x.x.x
			( a.ip[0] == 169 && a.ip[1] == 254 ) || // 169.254.x.x is link-local ipv4
			( a.ip[0] == 172 && a.ip[1] >= 16 && a.ip[1] <= 31 ) || // 172.16.x.x  - 172.31.x.x
			( a.ip[0] == 192 && a.ip[1] >= 168 )) // 192.168.x.x
		{
			return true;
		}
	}

	if( type_a == NA_IP6 )
	{
		uint8_t ip6[16];

		NET_NetadrToIP6Bytes( ip6, &a );

		// Private addresses, fc00::/7
		// Range is fc00:: to fdff:ffff:etc
		if( ip6[0] >= 0xFC && ip6[1] <= 0xFD )
		{
			return true;
		}

		// Link-local fe80::/10
		// Range is fe80:: to febf::
		if( ip6[0] == 0xFE && ( ip6[1] >= 0x80 && ip6[1] <= 0xBF ))
		{
			return true;
		}
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
	netadrtype_t type_a = NET_NetadrType( &a );
	netadrtype_t type_b = NET_NetadrType( &b );

	if( type_a != type_b )
		return false;

	if( type_a == NA_LOOPBACK )
		return true;

	if( type_a == NA_IP )
	{
		if( a.ip4 == b.ip4 && a.port == b.port )
			return true;
		return false;
	}

	if( type_a == NA_IP6 )
	{
		if( a.port == b.port && !NET_NetadrIP6Compare( &a, &b ))
			return true;
		return false;
	}

	Con_DPrintf( S_ERROR "%s: bad address type\n", __func__ );
	return false;
}

/*
====================
NET_CompareAdrSort

Network address sorting comparator
guaranteed to return -1, 0 or 1
====================
*/
int NET_CompareAdrSort( const void *_a, const void *_b )
{
	const netadr_t *a = _a, *b = _b;
	int porta, portb, portdiff, addrdiff;
	netadrtype_t type_a, type_b;

	type_a = NET_NetadrType( a );
	type_b = NET_NetadrType( b );

	if( type_a != type_b )
		return bound( -1, (int)type_a - (int)type_b, 1 );

	porta = ntohs( a->port );
	portb = ntohs( b->port );
	if( porta < portb )
		portdiff = -1;
	else if( porta > portb )
		portdiff = 1;
	else
		portdiff = 0;

	switch( type_a )
	{
	case NA_IP6:
		if(( addrdiff = NET_NetadrIP6Compare( a, b )))
			return addrdiff;
		// fallthrough
	case NA_MULTICAST_IP6:
		return portdiff;

	case NA_IP:
		if(( addrdiff = memcmp( a->ip, b->ip, sizeof( a->ipx ))))
			return addrdiff;
		// fallthrough
	case NA_BROADCAST:
		return portdiff;

	case NA_IPX:
		if(( addrdiff = memcmp( a->ipx, b->ipx, sizeof( a->ipx ))))
			return addrdiff;
		// fallthrough
	case NA_BROADCAST_IPX:
		return portdiff;
	}

	return 0;
}

/*
=============
NET_StringToAdr

idnewt
192.246.40.70
=============
*/
static qboolean NET_StringToAdrEx( const char *string, netadr_t *adr, int family )
{
	struct sockaddr_storage s;

	memset( adr, 0, sizeof( netadr_t ));

	if( !Q_stricmp( string, "localhost" ) || !Q_stricmp( string, "loopback" ))
	{
		NET_NetadrSetType( adr, NA_LOOPBACK );
		return true;
	}

	if( NET_StringToSockaddr( string, &s, false, family ) != NET_EAI_OK )
		return false;
	NET_SockadrToNetadr( &s, adr );
	return true;
}


qboolean NET_StringToAdr( const char *string, netadr_t *adr )
{
	return NET_StringToAdrEx( string, adr, AF_UNSPEC );
}

net_gai_state_t NET_StringToAdrNB( const char *string, netadr_t *adr, qboolean v6only )
{
	struct sockaddr_storage s;
	net_gai_state_t res;

	memset( adr, 0, sizeof( netadr_t ));

	if( !Q_stricmp( string, "localhost" ) || !Q_stricmp( string, "loopback" ))
	{
		NET_NetadrSetType( adr, NA_LOOPBACK );
		return NET_EAI_OK;
	}

	res = NET_StringToSockaddr( string, &s, true, v6only ? AF_INET6 : AF_UNSPEC );

	if( res == NET_EAI_OK )
		NET_SockadrToNetadr( &s, adr );

	return res;
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
	NET_NetadrSetType( from, NA_LOOPBACK );

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
	packet->from = *from;
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

	if(( host_developer.value && sv_cheats.value ) || !net_fakelag.value )
	{
		if( net_fakelag.value != net.fakelag )
		{
			diff = net_fakelag.value - net.fakelag;
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
		if( net_fakeloss.value != 0.0f )
		{
			if( host_developer.value )
			{
				net.losscount[sock]++;
				if( net_fakeloss.value <= 0.0f )
				{
					ninterval = fabs( net_fakeloss.value );
					if( ninterval < 2 ) ninterval = 2;

					if(( net.losscount[sock] % ninterval ) == 0 )
						return false;
				}
				else
				{
					if( COM_RandomLong( 0, 100 ) <= net_fakeloss.value )
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
	net_from = pPacket->from;
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
static qboolean NET_GetLong( byte *pData, int size, size_t *outSize, int splitsize, connprotocol_t proto )
{
	int		i, sequence_number, offset;
	int		packet_number;
	int		packet_count;
	short		packet_id;
	size_t header_size = proto == PROTO_GOLDSRC ? sizeof( SPLITPACKETGS ) : sizeof( SPLITPACKET );
	int body_size = splitsize - header_size;
	int max_splits;

	if( body_size < 0 )
		return false;

	if( size < header_size )
	{
		Con_Printf( S_ERROR "invalid split packet length %i\n", size );
		return false;
	}

	if( proto == PROTO_GOLDSRC )
	{
		SPLITPACKETGS *pHeader = (SPLITPACKETGS *)pData;

		sequence_number = pHeader->sequence_number;
		packet_id = pHeader->packet_id;
		packet_count = ( packet_id & 0xF );
		packet_number = ( packet_id >> 4 );

		max_splits = NET_MAX_GOLDSRC_FRAGMENTS;
	}
	else
	{
		SPLITPACKET *pHeader = (SPLITPACKET *)pData;

		sequence_number = pHeader->sequence_number;
		packet_id = pHeader->packet_id;
		packet_count = ( packet_id & 0xFF );
		packet_number = ( packet_id >> 8 );

		max_splits = ARRAYSIZE( net.split_flags );
	}

	if( packet_number >= max_splits || packet_count > max_splits )
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
		for( i = 0; i < ARRAYSIZE( net.split_flags ); i++ )
			net.split_flags[i] = -1;

		if( net_showpackets.value == 4.0f )
			Con_Printf( "<-- Split packet restart %i count %i seq\n", net.split.split_count, sequence_number );
	}

	size -= header_size;

	if( net.split_flags[packet_number] != sequence_number )
	{
		if( packet_number == ( packet_count - 1 ))
			net.split.total_size = size + body_size * ( packet_count - 1 );

		net.split.split_count--;
		net.split_flags[packet_number] = sequence_number;

		if( net_showpackets.value == 4.0f )
			Con_Printf( "<-- Split packet %i of %i, %i bytes %i seq\n", packet_number + 1, packet_count, size, sequence_number );
	}
	else
	{
		Con_DPrintf( "%s: Ignoring duplicated split packet %i of %i ( %i bytes )\n", __func__, packet_number + 1, packet_count, size );
	}

	offset = (packet_number * body_size);
	memcpy( net.split.buffer + offset, pData + header_size, size );

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
static qboolean NET_QueuePacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	byte		buf[NET_MAX_FRAGMENT];
	int		ret, protocol;
	int		net_socket;
	WSAsize_t	addr_len;
	struct sockaddr_storage	addr = { 0 };

	*length = 0;

	for( protocol = 0; protocol < 2; protocol++ )
	{
		switch( protocol )
		{
		case 0: net_socket = net.ip_sockets[sock]; break;
		case 1: net_socket = net.ip6_sockets[sock]; break;
		}

		if( !NET_IsSocketValid( net_socket ))
			continue;

		addr_len = sizeof( addr );
		ret = recvfrom( net_socket, buf, sizeof( buf ), 0, (struct sockaddr *)&addr, &addr_len );

		NET_SockadrToNetadr( &addr, from );

		if( !NET_IsSocketError( ret ))
		{
			if( ret < NET_MAX_FRAGMENT )
			{
				// Transfer data
				memcpy( data, buf, ret );
				*length = ret;

#if !XASH_DEDICATED
				// check for split message
				if( sock == NS_CLIENT && *(int *)data == NET_HEADER_SPLITPACKET )
					return NET_GetLong( data, ret, length, CL_GetSplitSize( ), CL_Protocol( ));
#endif

				// lag the packet, if needed
				return NET_LagPacket( true, sock, from, length, data );
			}
			else
			{
				Con_Reportf( "%s: oversize packet from %s\n", __func__, NET_AdrToString( *from ));
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
			case WSAETIMEDOUT:
				break;
			default:	// let's continue even after errors
				Con_DPrintf( S_ERROR "%s: %s from %s\n", __func__, NET_ErrorString(), NET_AdrToString( *from ));
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
static int NET_SendLong( netsrc_t sock, int net_socket, const char *buf, size_t len, int flags, const struct sockaddr_storage *to, size_t tolen, size_t splitsize )
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

			if( net_showpackets.value == 3.0f )
			{
				netadr_t	adr;

				memset( &adr, 0, sizeof( adr ));
				NET_SockadrToNetadr( to, &adr );

				Con_Printf( "Sending split %i of %i with %i bytes and seq %i to %s\n",
					packet_number + 1, packet_count, size, net.sequence_number, NET_AdrToString( adr ));
			}

			ret = sendto( net_socket, packet, size + sizeof( SPLITPACKET ), flags, (const struct sockaddr *)to, tolen );
			if( ret < 0 ) return ret; // error

			if( ret >= size )
				total_sent += size;
			len -= size;
			packet_number++;
			Platform_NanoSleep( 100 * 1000 );
		}

		return total_sent;
	}
	else
#endif
	{
		// no fragmenantion for client connection
		return sendto( net_socket, buf, len, flags, (const struct sockaddr *)to, tolen );
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
	struct sockaddr_storage	addr = { 0 };
	SOCKET		net_socket = 0;
	netadrtype_t type = NET_NetadrType( &to );

	if( !net.initialized || type == NA_LOOPBACK )
	{
		NET_SendLoopPacket( sock, length, data, to );
		return;
	}
	else if( type == NA_BROADCAST || type == NA_IP )
	{
		net_socket = net.ip_sockets[sock];
		if( !NET_IsSocketValid( net_socket ))
			return;
	}
	else if( type == NA_MULTICAST_IP6 || type == NA_IP6 )
	{
		net_socket = net.ip6_sockets[sock];
		if( !NET_IsSocketValid( net_socket ))
			return;
	}
	else
	{
		Host_Error( "%s: bad address type %i (%i, %i)\n", __func__, to.type, to.ip6_0[0], to.ip6_0[1] );
	}

	NET_NetadrToSockadr( &to, &addr );

	ret = NET_SendLong( sock, net_socket, data, length, 0, &addr, NET_SockAddrLen( &addr ), splitsize );

	if( NET_IsSocketError( ret ))
	{
		int err = WSAGetLastError();

		// WSAEWOULDBLOCK is silent
		if( err == WSAEWOULDBLOCK )
			return;

		// some PPP links don't allow broadcasts
		if( err == WSAEADDRNOTAVAIL && ( type == NA_BROADCAST || type == NA_MULTICAST_IP6 ))
			return;

		if( Host_IsDedicated( ))
		{
			Con_DPrintf( S_ERROR "%s: %s to %s\n", __func__, NET_ErrorString(), NET_AdrToString( to ));
		}
		else if( err == WSAEADDRNOTAVAIL || err == WSAENOBUFS )
		{
			Con_DPrintf( S_ERROR "%s: %s to %s\n", __func__, NET_ErrorString(), NET_AdrToString( to ));
		}
		else
		{
			Con_Printf( S_ERROR "%s: %s to %s\n", __func__, NET_ErrorString(), NET_AdrToString( to ));
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
NET_IPSocket
====================
*/
static int NET_IPSocket( const char *net_iface, int port, int family )
{
	struct sockaddr_storage	addr = { 0 };
	int		err, net_socket;
	uint		optval = 1;
	dword		_true = 1;
	int pfamily = PF_INET;

	if( family == AF_INET6 )
		pfamily = PF_INET6;

	if( NET_IsSocketError(( net_socket = socket( pfamily, SOCK_DGRAM, IPPROTO_UDP ))))
	{
		err = WSAGetLastError();
		if( err != WSAEAFNOSUPPORT )
			Con_DPrintf( S_WARN "%s: port: %d socket: %s\n", __func__, port, NET_ErrorString( ));
		return INVALID_SOCKET;
	}

	if( NET_IsSocketError( ioctlsocket( net_socket, FIONBIO, (void*)&_true )))
	{
		struct timeval timeout;

		Con_DPrintf( S_WARN "%s: port: %d ioctl FIONBIO: %s\n", __func__, port, NET_ErrorString( ));
		// try timeout instead of NBIO
		timeout.tv_sec = timeout.tv_usec = 0;
		setsockopt( net_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
	}

	// make it broadcast capable
	if( NET_IsSocketError( setsockopt( net_socket, SOL_SOCKET, SO_BROADCAST, (char *)&_true, sizeof( _true ))))
	{
		Con_DPrintf( S_WARN "%s: port: %d setsockopt SO_BROADCAST: %s\n", __func__, port, NET_ErrorString( ));
	}

	if( NET_IsSocketError( setsockopt( net_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof( optval ))))
	{
		Con_DPrintf( S_WARN "%s: port: %d setsockopt SO_REUSEADDR: %s\n", __func__, port, NET_ErrorString( ));
		closesocket( net_socket );
		return INVALID_SOCKET;
	}

	addr.ss_family = family;

	if( family == AF_INET6 )
	{
		if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&_true, sizeof( _true ))))
		{
			Con_DPrintf( S_WARN "%s: port: %d setsockopt IPV6_V6ONLY: %s\n", __func__, port, NET_ErrorString( ));
			closesocket( net_socket );
			return INVALID_SOCKET;
		}

		if( Sys_CheckParm( "-loopback" ))
		{
			if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (char *)&_true, sizeof( _true ))))
				Con_DPrintf( S_WARN "%s: port %d setsockopt IPV6_MULTICAST_LOOP: %s\n", __func__, port, NET_ErrorString( ));
		}

		if( !COM_StringEmpty( net_iface ) && Q_stricmp( net_iface, "localhost" ))
			NET_StringToSockaddr( net_iface, &addr, false, AF_INET6 );
		else ((struct sockaddr_in6 *)&addr)->sin6_addr = in6addr_any;

		if( port == PORT_ANY ) ((struct sockaddr_in6 *)&addr)->sin6_port = 0;
		else ((struct sockaddr_in6 *)&addr)->sin6_port = htons((short)port);

		if( NET_IsSocketError( bind( net_socket, (struct sockaddr *)&addr, sizeof( struct sockaddr_in6 ))))
		{
			Con_DPrintf( S_WARN "%s: port: %d bind6: %s\n", __func__, port, NET_ErrorString( ));
			closesocket( net_socket );
			return INVALID_SOCKET;
		}
	}
	else if( family == AF_INET )
	{
		if( Sys_CheckParm( "-tos" ))
		{
			optval = 0x10; // IPTOS_LOWDELAY
			Con_Printf( "Enabling LOWDELAY TOS option\n" );

			if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IP, IP_TOS, (const char *)&optval, sizeof( optval ))))
			{
				err = WSAGetLastError();
				if( err != WSAENOPROTOOPT )
					Con_Printf( S_WARN "%s: port: %d  setsockopt IP_TOS: %s\n", __func__, port, NET_ErrorString( ));
				closesocket( net_socket );
				return INVALID_SOCKET;
			}
		}

		if( Sys_CheckParm( "-loopback" ))
		{
		    if( NET_IsSocketError( setsockopt( net_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&_true, sizeof( _true ))))
				Con_DPrintf( S_WARN "%s: port %d setsockopt IP_MULTICAST_LOOP: %s\n", __func__, port, NET_ErrorString( ));
		}

		if( !COM_StringEmpty( net_iface ) && Q_stricmp( net_iface, "localhost" ))
			NET_StringToSockaddr( net_iface, &addr, false, AF_INET );
		else ((struct sockaddr_in *)&addr)->sin_addr.s_addr = INADDR_ANY;

		if( port == PORT_ANY ) ((struct sockaddr_in *)&addr)->sin_port = 0;
		else ((struct sockaddr_in *)&addr)->sin_port = htons((short)port);

		if( NET_IsSocketError( bind( net_socket, (struct sockaddr *)&addr, sizeof( struct sockaddr_in ))))
		{
			Con_DPrintf( S_WARN "%s: port: %d bind: %s\n", __func__, port, NET_ErrorString( ));
			closesocket( net_socket );
			return INVALID_SOCKET;
		}
	}

	return net_socket;
}

/*
====================
NET_OpenIP
====================
*/
static void NET_OpenIP( qboolean change_port, int *sockets, const char *net_iface, int hostport, int clientport, int family )
{
	int port;
	qboolean sv_nat = Cvar_VariableInteger( "sv_nat" );
	qboolean cl_nat = Cvar_VariableInteger( "cl_nat" );

	if( change_port && ( FBitSet( net_hostport.flags, FCVAR_CHANGED ) || sv_nat ))
	{
		// reopen socket to set random port
		if( NET_IsSocketValid( sockets[NS_SERVER] ))
			closesocket( sockets[NS_SERVER] );

		sockets[NS_SERVER] = INVALID_SOCKET;
		ClearBits( net_hostport.flags, FCVAR_CHANGED );
	}

	if( !NET_IsSocketValid( sockets[NS_SERVER] ))
	{
		port = hostport;
		if( !port )
		{
			port = sv_nat ? PORT_ANY : net_hostport.value;

			if( !port )
				port = PORT_SERVER; // forcing to default
		}
		sockets[NS_SERVER] = NET_IPSocket( net_iface, port, family );

		if( !NET_IsSocketValid( sockets[NS_SERVER] ) && Host_IsDedicated( ))
			return;
	}

	// dedicated servers don't need client ports
	if( Host_IsDedicated( )) return;

	if( change_port && ( FBitSet( net_clientport.flags, FCVAR_CHANGED ) || cl_nat ))
	{
		// reopen socket to set random port
		if( NET_IsSocketValid( sockets[NS_CLIENT] ))
			closesocket( sockets[NS_CLIENT] );

		sockets[NS_CLIENT] = INVALID_SOCKET;
		ClearBits( net_clientport.flags, FCVAR_CHANGED );
	}

	if( !NET_IsSocketValid( sockets[NS_CLIENT] ))
	{
		port = clientport;
		if( !port )
		{
			port = cl_nat ? PORT_ANY : net_clientport.value;

			if( !port )
				port = PORT_ANY; // forcing to default
		}
		sockets[NS_CLIENT] = NET_IPSocket( net_iface, port, family );

		if( !NET_IsSocketValid( sockets[NS_CLIENT] ))
			sockets[NS_CLIENT] = NET_IPSocket( net_ipname.string, PORT_ANY, family );
	}

	return;
}

/*
================
NET_DetermineLocalAddress

Returns the servers' ip address as a string.
================
*/
static void NET_DetermineLocalAddress( void )
{
	char		hostname[512];
	char		buff[512];
	struct sockaddr_storage	address;
	WSAsize_t		namelen;
	const char		*net_addr_string;

	memset( &net_local, 0, sizeof( netadr_t ));
	memset( &net6_local, 0, sizeof( netadr_t ));

	if( !net.allow_ip && !net.allow_ip6 )
	{
		Con_Printf( "TCP/IP Disabled.\n" );
		return;
	}

	gethostname( hostname, sizeof( hostname ));
	hostname[sizeof(hostname) - 1] = 0;

	if( net.allow_ip )
	{
		// If we have changed the ip var from the command line, use that instead.
		if( Q_stricmp( net_ipname.string, "localhost" ))
			Q_strncpy( buff, net_ipname.string, sizeof( buff ));
		else Q_strncpy( buff, hostname, sizeof( buff ));

		if( NET_StringToAdrEx( buff, &net_local, AF_INET ))
		{
			namelen = sizeof( struct sockaddr_in );

			if( !NET_IsSocketError( getsockname( net.ip_sockets[NS_SERVER], (struct sockaddr *)&address, &namelen )))
			{
				net_local.port = ((struct sockaddr_in *)&address)->sin_port;
				net_addr_string = NET_AdrToString( net_local );
				Con_Printf( "Server IPv4 address %s\n", net_addr_string );
				Cvar_FullSet( "net_address", net_addr_string, net_address.flags );
			}
			else Con_DPrintf( S_ERROR "Could not get TCP/IPv4 address. Reason: %s\n", NET_ErrorString( ));
		}
		else Con_DPrintf( S_ERROR "Could not get TCP/IPv4 address, Invalid hostname: '%s'\n", buff );
	}

	if( net.allow_ip6 )
	{
		// If we have changed the ip var from the command line, use that instead.
		if( Q_stricmp( net_ip6name.string, "localhost" ))
			Q_strncpy( buff, net_ip6name.string, sizeof( buff ));
		else Q_strncpy( buff, hostname, sizeof( buff ));

		if( NET_StringToAdrEx( buff, &net6_local, AF_INET6 ))
		{
			namelen = sizeof( struct sockaddr_in6 );

			if( !NET_IsSocketError( getsockname( net.ip6_sockets[NS_SERVER], (struct sockaddr *)&address, &namelen )))
			{
				net6_local.port = ((struct sockaddr_in6 *)&address)->sin6_port;
				net_addr_string = NET_AdrToString( net6_local );
				Con_Printf( "Server IPv6 address %s\n", net_addr_string );
				Cvar_FullSet( "net6_address", net_addr_string, net6_address.flags );
			}
			else Con_DPrintf( S_ERROR "Could not get TCP/IPv6 address. Reason: %s\n", NET_ErrorString( ));
		}
		else Con_DPrintf( S_ERROR "Could not get TCP/IPv6 address, Invalid hostname: '%s'\n", buff );
	}
}

/*
====================
NET_Config

A single player game will only use the loopback code
====================
*/
void NET_Config( qboolean multiplayer, qboolean changeport )
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
		if( net.allow_ip )
			NET_OpenIP( changeport, net.ip_sockets, net_ipname.string, net_iphostport.value, net_ipclientport.value, AF_INET );

		if( net.allow_ip6 )
			NET_OpenIP( changeport, net.ip6_sockets, net_ip6name.string, net_ip6hostport.value, net_ip6clientport.value, AF_INET6 );

		// validate sockets for dedicated
		if( Host_IsDedicated( ))
		{
			qboolean nov4, nov6;
			nov4 = net.allow_ip  && NET_IsSocketError( net.ip_sockets[NS_SERVER] );
			nov6 = net.allow_ip6 && NET_IsSocketError( net.ip6_sockets[NS_SERVER] );

			if( nov4 && nov6 )
				Host_Error( "Couldn't allocate IPv4 and IPv6 server ports.\n" );
			else if( nov4 && !nov6 )
				Con_Printf( S_ERROR "Couldn't allocate IPv4 server port\n" );
			else if( !nov4 && nov6 )
				Con_Printf( S_ERROR "Couldn't allocate IPv6 server_port\n" );
		}

		// get our local address, if possible
		if( bFirst )
		{
			NET_DetermineLocalAddress();
			bFirst = false;
		}
	}
	else
	{
		int	i;

		// shut down any existing sockets
		for( i = 0; i < NS_COUNT; i++ )
		{
			if( NET_IsSocketValid( net.ip_sockets[i] ))
			{
				closesocket( net.ip_sockets[i] );
				net.ip_sockets[i] = INVALID_SOCKET;
			}

			if( NET_IsSocketValid( net.ip6_sockets[i] ))
			{
				closesocket( net.ip6_sockets[i] );
				net.ip6_sockets[i] = INVALID_SOCKET;
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
#ifndef XASH_NO_NETWORK
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
#endif
}

/*
====================
NET_ClearLagData

clear fakelag list
====================
*/
static void NET_ClearLagData( qboolean bClient, qboolean bServer )
{
	if( bClient ) NET_ClearLaggedList( &net.lagdata[NS_CLIENT] );
	if( bServer ) NET_ClearLaggedList( &net.lagdata[NS_SERVER] );
}

/*
====================
NET_GetLocalAddress

get local server addresses
====================
*/
void NET_GetLocalAddress( netadr_t *ip4, netadr_t *ip6 )
{
	if( ip4 )
	{
		if( net.allow_ip )
			*ip4 = net_local;
		else
			memset( ip4, 0, sizeof( *ip4 ));
	}

	if( ip6 )
	{
		if( net.allow_ip6 )
			*ip6 = net6_local;
		else
			memset( ip6, 0, sizeof( *ip6 ));
	}
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

	Cvar_RegisterVariable( &net_address );
	Cvar_RegisterVariable( &net_ipname );
	Cvar_RegisterVariable( &net_iphostport );
	Cvar_RegisterVariable( &net_hostport );
	Cvar_RegisterVariable( &net_ipclientport );
	Cvar_RegisterVariable( &net_clientport );
	Cvar_RegisterVariable( &net_fakelag );
	Cvar_RegisterVariable( &net_fakeloss );
	Cvar_RegisterVariable( &net_resolve_debug );
	Cvar_RegisterVariable( &net_clockwindow );

	Q_snprintf( cmd, sizeof( cmd ), "%i", PORT_SERVER );
	Cvar_FullSet( "hostport", cmd, FCVAR_READ_ONLY );

	// cvar equivalents for IPv6
	Cvar_RegisterVariable( &net_ip6name );
	Cvar_RegisterVariable( &net_ip6hostport );
	Cvar_RegisterVariable( &net_ip6clientport );
	Cvar_RegisterVariable( &net6_address );

	// prepare some network data
	for( i = 0; i < NS_COUNT; i++ )
	{
		net.lagdata[i].prev = &net.lagdata[i];
		net.lagdata[i].next = &net.lagdata[i];
		net.ip_sockets[i]  = INVALID_SOCKET;
		net.ip6_sockets[i] = INVALID_SOCKET;
	}

#if XASH_WIN32
	if( WSAStartup( MAKEWORD( 2, 0 ), &net.winsockdata ))
	{
		Con_DPrintf( S_ERROR "network initialization failed.\n" );
		return;
	}
#endif

	NET_InitializeCriticalSections();

	net.allow_ip = !Sys_CheckParm( "-noip" );
	net.allow_ip6 = !Sys_CheckParm( "-noip6" );

	// specify custom host port
	if( Sys_GetParmFromCmdLine( "-port", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( net_hostport.name, cmd, net_hostport.flags );

	// specify custom IPv6 host port
	if( Sys_GetParmFromCmdLine( "-port6", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( net_ip6hostport.name, cmd, net_ip6hostport.flags );

	// specify custom client port
	if( Sys_GetParmFromCmdLine( "-clientport", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( net_clientport.name, cmd, net_clientport.flags );

	// specify custom IPv6 client port
	if( Sys_GetParmFromCmdLine( "-clientport6", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( net_ip6clientport.name, cmd, net_ip6clientport.flags );

	// specify custom ip
	if( Sys_GetParmFromCmdLine( "-ip", cmd ))
		Cvar_DirectSet( &net_ipname, cmd );

	// specify custom ip6
	if( Sys_GetParmFromCmdLine( "-ip6", cmd ))
		Cvar_DirectSet( &net_ip6name, cmd );

	// adjust clockwindow
	if( Sys_GetParmFromCmdLine( "-clockwindow", cmd ))
		Cvar_DirectSetValue( &net_clockwindow, Q_atof( cmd ));

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

	NET_Config( false, false );

	NET_DeleteCriticalSections();

#if XASH_WIN32
	WSACleanup();
#endif
	net.initialized = false;
}


