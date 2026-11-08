/*
 * net_gamecube.c - Soft-fail broadband bring-up for BBA / eth2sp1 (libogc2).
 *
 * When XASH_GAMECUBE_HAVE_LIBBBA is unset, this file only exports soft-fail
 * bring-up stubs (no libogc network.h) so Flipper XFB/MEM1 stays healthy for
 * menu bring-up. Full socket mapping lives behind XASH_GAMECUBE_LINK_BBA=1.
 */
#include "common.h"

#if XASH_GAMECUBE

#if defined(XASH_GAMECUBE_HAVE_LIBBBA) && XASH_GAMECUBE_HAVE_LIBBBA

#include <netdb.h>
#include <stdlib.h>
#include "platform/gamecube/net_gamecube.h"

#ifndef EAI_FAIL
#define EAI_FAIL   -1
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY -6
#endif
#ifndef EAI_NONAME
#define EAI_NONAME -2
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY -10
#endif

const struct in6_addr in6addr_any = { { { 0 } } };

/* libbba.a (GameCube) exports if_config/net_init/net_gethostip but not
 * getsockname / gethostbyname / deinit — provide GC-side shims. */

static qboolean gc_net_ready;
static char     gc_net_ip[16];
static char     gc_net_mask[16];
static char     gc_net_gw[16];

typedef struct
{
	s32 sock;
	u16 port_be;
} gc_bound_port_t;

static gc_bound_port_t gc_bound_ports[GC_NET_PORT_SLOTS];

void GC_NetRememberBoundPort( int s, u16 port_be )
{
	int i, free_slot = -1;

	if( s < 0 )
		return;
	for( i = 0; i < GC_NET_PORT_SLOTS; i++ )
	{
		if( gc_bound_ports[i].sock == (s32)s )
		{
			gc_bound_ports[i].port_be = port_be;
			return;
		}
		if( free_slot < 0 && gc_bound_ports[i].sock < 0 )
			free_slot = i;
	}
	if( free_slot < 0 )
		free_slot = (int)( (u32)s % (u32)GC_NET_PORT_SLOTS );
	gc_bound_ports[free_slot].sock = (s32)s;
	gc_bound_ports[free_slot].port_be = port_be;
}

u16 GC_NetLookupBoundPort( int s )
{
	int i;

	for( i = 0; i < GC_NET_PORT_SLOTS; i++ )
	{
		if( gc_bound_ports[i].sock == (s32)s )
			return gc_bound_ports[i].port_be;
	}
	return 0;
}

void net_deinit( void )
{
	int i;

	/* No teardown symbol in cube libbba; keep soft no-op. */
	for( i = 0; i < GC_NET_PORT_SLOTS; i++ )
		gc_bound_ports[i].sock = -1;
}

s32 net_getsockname( s32 s, struct sockaddr *addr, socklen_t *addrlen )
{
	struct sockaddr_in *sa;
	u32 ip;
	u16 port;

	if( !addr || !addrlen || *addrlen < (socklen_t)sizeof( *sa ))
	{
		errno = EINVAL;
		return -1;
	}
	sa = (struct sockaddr_in *)addr;
	memset( sa, 0, sizeof( *sa ));
	sa->sin_family = AF_INET;
	ip = net_gethostip();
	if( !ip && gc_net_ready && gc_net_ip[0] )
		ip = inet_addr( gc_net_ip );
	sa->sin_addr.s_addr = ip;
	port = GC_NetLookupBoundPort( (int)s );
	/* Fallback: server/client default GoldSrc ports when bind recorded none. */
	if( !port )
		port = htons( 27015 );
	sa->sin_port = port;
	*addrlen = sizeof( *sa );
	return 0;
}

struct hostent *net_gethostbyname( const char *addrString )
{
	static struct hostent he;
	static char           *alias_list[1];
	static char           *addr_list[2];
	static struct in_addr addr;
	static char            name_buf[256];
	u32                    ip;

	if( !addrString || !addrString[0] )
		return NULL;
	ip = inet_addr( addrString );
	if( ip == INADDR_NONE || ip == (u32)-1 )
		return NULL; /* no DNS in cube libbba */
	Q_strncpy( name_buf, addrString, sizeof( name_buf ));
	addr.s_addr = ip;
	alias_list[0] = NULL;
	addr_list[0] = (char *)&addr;
	addr_list[1] = NULL;
	he.h_name = name_buf;
	he.h_aliases = alias_list;
	he.h_addrtype = AF_INET;
	he.h_length = sizeof( addr );
	he.h_addr_list = addr_list;
	return &he;
}

s32 GC_SendTo( int s, const void *data, s32 len, u32 flags,
	const struct sockaddr *to, socklen_t tolen )
{
	static int log_left = 16;
	s32 ret = net_sendto( (s32)s, data, len, flags, (struct sockaddr *)to, tolen );

	if( log_left > 0 )
	{
		char ipbuf[16] = "?";
		unsigned port = 0;

		if( to && tolen >= (socklen_t)sizeof( struct sockaddr_in )
			&& to->sa_family == AF_INET )
		{
			const struct sockaddr_in *sa = (const struct sockaddr_in *)to;
			const u32 a = ntohl( sa->sin_addr.s_addr );

			port = ntohs( sa->sin_port );
			Q_snprintf( ipbuf, sizeof( ipbuf ), "%u.%u.%u.%u",
				( a >> 24 ) & 255, ( a >> 16 ) & 255, ( a >> 8 ) & 255, a & 255 );
		}
		Con_Reportf( "Xash3D GameCube: sendto fd=%d len=%d -> %s:%u ret=%d errno=%d\n",
			s, (int)len, ipbuf, port, (int)ret, ret < 0 ? (int)errno : 0 );
		log_left--;
	}
	return ret;
}

int GC_NetIsReady( void )
{
	return gc_net_ready ? 1 : 0;
}

const char *GC_NetLocalIP( void )
{
	return gc_net_ready ? gc_net_ip : "0.0.0.0";
}

int GC_NetBringUp( int max_retries )
{
	s32 ret;
	int i;

	if( gc_net_ready )
		return 1;
	if( max_retries < 1 )
		max_retries = 1;
	for( i = 0; i < GC_NET_PORT_SLOTS; i++ )
		gc_bound_ports[i].sock = -1;
	gc_net_ip[0] = gc_net_mask[0] = gc_net_gw[0] = '\0';
	Con_Reportf( "Xash3D GameCube: net bring-up (BBA/eth2sp1 via libogc, dhcp retries=%d)\n",
		max_retries );
	/* if_config() owns net_init internally. Calling net_init() first hangs
	 * Dolphin BuiltIn BBA (guest never returns from EXI bring-up). */
	if( Sys_CheckParm( "-netstatic" ))
	{
		string override_ip;

		/* Dolphin BuiltIn / TapServer / lab static: 10.0.0.0/24 (default .2). */
		Q_strncpy( gc_net_ip, "10.0.0.2", sizeof( gc_net_ip ));
		if( Sys_GetParmFromCmdLine( "-gcnetip", override_ip ) && override_ip[0] )
			Q_strncpy( gc_net_ip, override_ip, sizeof( gc_net_ip ));
		Q_strncpy( gc_net_mask, "255.255.255.0", sizeof( gc_net_mask ));
		Q_strncpy( gc_net_gw, "10.0.0.1", sizeof( gc_net_gw ));
		Con_Reportf( "Xash3D GameCube: net static ip=%s mask=%s gw=%s\n",
			gc_net_ip, gc_net_mask, gc_net_gw );
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, false, max_retries );
	}
	else
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, true, max_retries );
	if( ret < 0 )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: net bring-up failed (ret=%d) — offline/loopback only\n",
			(int)ret );
		gc_net_ready = false;
		return 0;
	}
	gc_net_ready = true;
	Con_Reportf( "Xash3D GameCube: net ready ip=%s mask=%s gw=%s (eth2sp1/BBA)\n",
		gc_net_ip, gc_net_mask, gc_net_gw );
	return 1;
}

void GC_NetShutdown( void )
{
	if( !gc_net_ready )
		return;
	net_deinit();
	gc_net_ready = false;
	gc_net_ip[0] = gc_net_mask[0] = gc_net_gw[0] = '\0';
	Con_Reportf( "Xash3D GameCube: net shutdown\n" );
}

int GC_NetEnsureReady( void )
{
	int retries = 20;

	if( gc_net_ready )
		return 1;
	if( Sys_CheckParm( "-nonet" ) || Sys_CheckParm( "-noip" ))
	{
		Con_Reportf( "Xash3D GameCube: net skipped (-nonet/-noip)\n" );
		return 0;
	}
	if( Sys_CheckParm( "-netfast" ))
		retries = 5;
	return GC_NetBringUp( retries );
}

int GC_GetHostname( char *name, size_t len )
{
	if( !name || len == 0 )
		return -1;
	Q_strncpy( name, "gamecube", (int)len );
	return 0;
}

int GC_GetAddrInfo( const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **res )
{
	struct hostent *he;
	struct sockaddr_in *sa;
	struct addrinfo *ai;
	int family = AF_INET;

	(void)service;
	if( !res )
		return EAI_FAIL;
	*res = NULL;
	if( hints )
	{
		if( hints->ai_family == AF_INET6 )
			return EAI_FAMILY;
		if( hints->ai_family != AF_UNSPEC )
			family = hints->ai_family;
	}
	if( family != AF_INET && family != AF_UNSPEC )
		return EAI_FAMILY;
	if( !node || !node[0] )
		return EAI_NONAME;
	{
		u32 addr = inet_addr( node );
		if( addr != INADDR_NONE && addr != (u32)-1 )
		{
			ai = (struct addrinfo *)calloc( 1, sizeof( *ai ) + sizeof( *sa ));
			if( !ai )
				return EAI_MEMORY;
			sa = (struct sockaddr_in *)( ai + 1 );
			sa->sin_family = AF_INET;
			sa->sin_addr.s_addr = addr;
			ai->ai_family = AF_INET;
			ai->ai_socktype = SOCK_DGRAM;
			ai->ai_protocol = IPPROTO_UDP;
			ai->ai_addrlen = sizeof( *sa );
			ai->ai_addr = (struct sockaddr *)sa;
			*res = ai;
			return 0;
		}
	}
	he = net_gethostbyname( node );
	if( !he || !he->h_addr_list || !he->h_addr_list[0] )
		return EAI_NONAME;
	ai = (struct addrinfo *)calloc( 1, sizeof( *ai ) + sizeof( *sa ));
	if( !ai )
		return EAI_MEMORY;
	sa = (struct sockaddr_in *)( ai + 1 );
	sa->sin_family = AF_INET;
	memcpy( &sa->sin_addr, he->h_addr_list[0], sizeof( sa->sin_addr ));
	ai->ai_family = AF_INET;
	ai->ai_socktype = SOCK_DGRAM;
	ai->ai_protocol = IPPROTO_UDP;
	ai->ai_addrlen = sizeof( *sa );
	ai->ai_addr = (struct sockaddr *)sa;
	*res = ai;
	return 0;
}

void GC_FreeAddrInfo( struct addrinfo *res )
{
	free( res );
}

#else /* !HAVE_LIBBBA — offline Flipper / menu path */

int GC_NetIsReady( void ) { return 0; }
const char *GC_NetLocalIP( void ) { return "0.0.0.0"; }
int GC_NetBringUp( int max_retries )
{
	(void)max_retries;
	Con_Reportf( "Xash3D GameCube: net offline stub (link with XASH_GAMECUBE_LINK_BBA=1 for eth2sp1/BBA)\n" );
	return 0;
}
void GC_NetShutdown( void ) {}
int GC_NetEnsureReady( void )
{
	Con_Reportf( "Xash3D GameCube: net ensure skipped (no libbba in this DOL)\n" );
	return 0;
}

#endif /* HAVE_LIBBBA */

#endif /* XASH_GAMECUBE */
