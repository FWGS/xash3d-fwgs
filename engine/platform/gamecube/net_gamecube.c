/*
 * net_gamecube.c - Soft-fail broadband bring-up for BBA / eth2sp1 (libogc2).
 *
 * When XASH_GAMECUBE_HAVE_LIBBBA is unset, this file only exports soft-fail
 * bring-up stubs (no libogc network.h) so Flipper XFB/MEM1 stays healthy for
 * menu bring-up. Full socket mapping lives behind XASH_GAMECUBE_LINK_BBA=1.
 */
#include "common.h"

#if XASH_GAMECUBE

#if XASH_DODGC_NETWORK && XASH_GAMECUBE_LIBOGC2
/* libogc2's 1 MiB default kernel workspace cannot fit after this title's
 * fixed MEM1 arenas under the loader's ArenaHi=0x81700000. Our main and
 * network thread stacks are static. Reserve 256 KiB for kernel objects;
 * keep the loader's high-memory reservation intact. */
void __real___lwp_wkspace_init( unsigned int size );
void __wrap___lwp_wkspace_init( unsigned int size )
{
	(void)size;
	__real___lwp_wkspace_init( 256u * 1024u );
}
#endif


/* In-game network configuration. Archived cvars so any menu (mainui Game
 * options, console, config.cfg) can set them. They are read when the link is
 * first brought up (first multiplayer use); libogc2 cannot re-init after a
 * failed bring-up, so changing them later needs a reboot. */
static CVAR_DEFINE( gc_net_dhcp, "gc_net_dhcp", "1", FCVAR_ARCHIVE, "GameCube network: 1 = DHCP, 0 = static address" );
static CVAR_DEFINE( gc_net_ip_cvar, "gc_net_ip", "10.0.0.2", FCVAR_ARCHIVE, "GameCube network: static IP address" );
static CVAR_DEFINE( gc_net_mask_cvar, "gc_net_mask", "255.255.255.0", FCVAR_ARCHIVE, "GameCube network: static subnet mask" );
static CVAR_DEFINE( gc_net_gw_cvar, "gc_net_gw", "10.0.0.1", FCVAR_ARCHIVE, "GameCube network: static gateway" );
static CVAR_DEFINE( gc_net_dns, "gc_net_dns", "", FCVAR_ARCHIVE, "GameCube network: DNS server (empty = gateway, then 1.1.1.1 / 8.8.8.8)" );
static CVAR_DEFINE( gc_net_status, "gc_net_status", "not started", FCVAR_READ_ONLY, "GameCube network: link state / address" );

void GC_NetRegisterCvars( void )
{
	static qboolean registered;

	/* Bring-up can run (-net) before NET_Init, so registration must be
	 * idempotent: whichever path runs first wins. */
	if( registered )
		return;
	registered = true;
	Cvar_RegisterVariable( &gc_net_dhcp );
	Cvar_RegisterVariable( &gc_net_ip_cvar );
	Cvar_RegisterVariable( &gc_net_mask_cvar );
	Cvar_RegisterVariable( &gc_net_gw_cvar );
	Cvar_RegisterVariable( &gc_net_dns );
	Cvar_RegisterVariable( &gc_net_status );
}

#if defined(XASH_GAMECUBE_HAVE_LIBBBA) && XASH_GAMECUBE_HAVE_LIBBBA

/* gc_net_status is FCVAR_READ_ONLY (display only): Cvar_DirectSet refuses
 * those, so force it through Cvar_FullSet. */
static void GC_NetSetStatus( const char *text )
{
	GC_NetRegisterCvars();
	Cvar_FullSet( gc_net_status.name, text, FCVAR_READ_ONLY );
	Con_Reportf( "Xash3D GameCube: gc_net_status = \"%s\"\n", gc_net_status.string );
}

#if !XASH_GAMECUBE_LIBOGC2
#include <netdb.h>
#endif
#include <stdlib.h>
#include "platform/gamecube/net_gamecube.h"
#include "platform/gamecube/net_config.h"
#include "platform/gamecube/net_dns.h"

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
static qboolean gc_net_attempted;
static qboolean gc_net_initialized;
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

#if !XASH_GAMECUBE_LIBOGC2
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

#endif /* classic libogc getsockname shim */

/* ---- DNS resolver (internet play: master servers, hostname connect) ---- */
#define GC_DNS_CACHE 8
#define GC_DNS_TIMEOUT_MS 1500

static struct { char name[96]; u32 ip; } gc_dns_cache[GC_DNS_CACHE];
static int gc_dns_cache_next;

static int GC_DnsQueryServer( u32 server_be, const char *name, u16 id, u32 *ip_be )
{
	uint8_t pkt[GC_DNS_MAX_PACKET];
	struct sockaddr_in sa;
	struct timeval tv;
	fd_set rfds;
	size_t qlen;
	int s, got = 0;
	uint8_t ip4[4];

	qlen = GC_DnsBuildQuery( pkt, sizeof( pkt ), id, name );
	if( !qlen ) return 0;
	s = net_socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s < 0 ) return 0;
	memset( &sa, 0, sizeof( sa ));
	sa.sin_len = sizeof( sa );
	sa.sin_family = AF_INET;
	sa.sin_port = htons( 53 );
	sa.sin_addr.s_addr = server_be;
	if( net_sendto( s, pkt, qlen, 0, (struct sockaddr *)&sa, sizeof( sa )) >= 0 )
	{
		FD_ZERO( &rfds );
		FD_SET( s, &rfds );
		tv.tv_sec = GC_DNS_TIMEOUT_MS / 1000;
		tv.tv_usec = ( GC_DNS_TIMEOUT_MS % 1000 ) * 1000;
		if( net_select( s + 1, &rfds, NULL, NULL, &tv ) > 0 )
		{
			s32 n = net_recv( s, pkt, sizeof( pkt ), 0 );
			if( n > 0 && GC_DnsParseResponse( pkt, (size_t)n, id, ip4 ))
			{
				memcpy( ip_be, ip4, 4 );
				got = 1;
			}
		}
	}
	net_close( s );
	return got;
}

int GC_DnsResolve( const char *name, u32 *ip_be )
{
	static u16 next_id = 0x4421;
	u32 servers[4];
	int i, nservers = 0;
	string override_ip;

	if( !name || !ip_be || !gc_net_ready )
		return 0;
	for( i = 0; i < GC_DNS_CACHE; i++ )
	{
		if( gc_dns_cache[i].name[0] && !Q_stricmp( gc_dns_cache[i].name, name ))
		{
			*ip_be = gc_dns_cache[i].ip;
			return 1;
		}
	}
	/* Priority: -gcdns override, DHCP gateway (most routers proxy DNS), public fallbacks. */
	if( !Sys_GetParmFromCmdLine( "-gcdns", override_ip ) || !override_ip[0] )
		Q_strncpy( override_ip, gc_net_dns.string, sizeof( override_ip ));
	if( override_ip[0] )
	{
		u32 v;
		if( GC_NetParseIPv4( override_ip, &v )) servers[nservers++] = htonl( v );
	}
	if( gc_net_gw[0] && inet_addr( gc_net_gw ) != INADDR_NONE )
		servers[nservers++] = inet_addr( gc_net_gw );
	servers[nservers++] = inet_addr( "1.1.1.1" );
	servers[nservers++] = inet_addr( "8.8.8.8" );
	for( i = 0; i < nservers; i++ )
	{
		if( GC_DnsQueryServer( servers[i], name, next_id++, ip_be ))
		{
			Q_strncpy( gc_dns_cache[gc_dns_cache_next].name, name, sizeof( gc_dns_cache[0].name ));
			gc_dns_cache[gc_dns_cache_next].ip = *ip_be;
			gc_dns_cache_next = ( gc_dns_cache_next + 1 ) % GC_DNS_CACHE;
			Con_Reportf( "Xash3D GameCube: dns %s resolved\n", name );
			return 1;
		}
	}
	Con_Reportf( S_WARN "Xash3D GameCube: dns lookup failed for %s\n", name );
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
	{
		/* libbba has no resolver; do a UDP DNS lookup ourselves. */
		if( !GC_DnsResolve( addrString, &ip ))
			return NULL;
	}
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
	s32 ret = GC_NetResult( net_sendto( (s32)s, data, len, flags, (struct sockaddr *)to, tolen ));

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

void GC_NetReportStatus( const char *phase )
{
#if XASH_GAMECUBE_LIBOGC2
	extern u32 __lwp_wkspace_heapsize( void );
	extern u32 __lwp_wkspace_heapfree( void );
	char interface_name[4] = "?";
	if_indextoname( 1, interface_name );
	Con_Reportf( "GameCube DoD: network phase=%s ip=%s interface=%s kernel_bytes=%u kernel_free=%u\n",
		phase, GC_NetLocalIP(), interface_name, __lwp_wkspace_heapsize(), __lwp_wkspace_heapfree() );
	GCube_AppendSwissEvidence( "network phase=%s ip=%s interface=%s kernel_free=%u", phase, GC_NetLocalIP(), interface_name, __lwp_wkspace_heapfree() );
#else
	(void)phase;
#endif
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
	if( gc_net_initialized )
	{
		gc_net_ready = true;
		return 1;
	}
	/* libbba keeps an internal initialized flag even after failed bring-up.
	 * Calling if_config again can return success with no usable interface. */
	if( gc_net_attempted )
		return 0;
	gc_net_attempted = true;
	/* Early (-net / disc cfg "net") bring-up runs before NET_Init, so the
	 * gc_net_* cvars are still unregistered here and gc_net_dhcp.value reads
	 * 0. Without this the static branch below always won and DHCP was never
	 * attempted. Registration is idempotent. */
	GC_NetRegisterCvars();
	if( max_retries < 1 )
		max_retries = 1;
	for( i = 0; i < GC_NET_PORT_SLOTS; i++ )
		gc_bound_ports[i].sock = -1;
	gc_net_ip[0] = gc_net_mask[0] = gc_net_gw[0] = '\0';
	Con_Reportf( "Xash3D GameCube: net bring-up (BBA/eth2sp1 via libogc, dhcp retries=%d)\n",
		max_retries );
	/* if_config() owns net_init internally. Calling net_init() first hangs
	 * Dolphin BuiltIn BBA (guest never returns from EXI bring-up). */
	if( Sys_CheckParm( "-netstatic" ) || !gc_net_dhcp.value )
	{
		string override_ip;

		/* Dolphin BuiltIn / TapServer / lab static: 10.0.0.0/24 (default .2). */
		Q_strncpy( gc_net_ip, gc_net_ip_cvar.string[0] ? gc_net_ip_cvar.string : "10.0.0.2", sizeof( gc_net_ip ));
		if( Sys_GetParmFromCmdLine( "-gcnetip", override_ip ) && override_ip[0] )
			Q_strncpy( gc_net_ip, override_ip, sizeof( gc_net_ip ));
		Q_strncpy( gc_net_mask, gc_net_mask_cvar.string[0] ? gc_net_mask_cvar.string : "255.255.255.0", sizeof( gc_net_mask ));
		Q_strncpy( gc_net_gw, gc_net_gw_cvar.string[0] ? gc_net_gw_cvar.string : "10.0.0.1", sizeof( gc_net_gw ));
		if( Sys_GetParmFromCmdLine( "-gcnetmask", override_ip ))
			Q_strncpy( gc_net_mask, override_ip, sizeof( gc_net_mask ));
		if( Sys_GetParmFromCmdLine( "-gcnetgw", override_ip ))
			Q_strncpy( gc_net_gw, override_ip, sizeof( gc_net_gw ));
		if( !GC_NetValidateStatic( gc_net_ip, gc_net_mask, gc_net_gw ))
		{
			Con_Reportf( S_ERROR "GameCube: invalid static IP/mask/gateway; network disabled.\n" );
			GC_NetSetStatus( "invalid static IP/mask/gateway" );
			return 0;
		}
		Con_Reportf( "Xash3D GameCube: net static ip=%s mask=%s gw=%s\n",
			gc_net_ip, gc_net_mask, gc_net_gw );
		#if XASH_GAMECUBE_LIBOGC2
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, false );
#else
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, false, max_retries );
#endif
	}
	else
	{
#if XASH_GAMECUBE_LIBOGC2
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, true );
#else
		ret = if_config( gc_net_ip, gc_net_mask, gc_net_gw, true, max_retries );
#endif
	}
	if( ret < 0 || !net_gethostip() || net_gethostip() == INADDR_NONE )
	{
		Con_Reportf( S_WARN "Xash3D GameCube: net bring-up failed (ret=%d) — offline/loopback only\n",
			(int)ret );
		Con_Reportf( "GameCube: check adapter/cable/address settings and reboot to retry.\n" );
		gc_net_ready = false;
		GC_NetSetStatus( "no link (check adapter/cable; reboot to retry)" );
		return 0;
	}
	gc_net_initialized = true;
	gc_net_ready = true;
	{
		string status;
		Q_snprintf( status, sizeof( status ), "connected %s", gc_net_ip );
		GC_NetSetStatus( status );
	}
	Con_Reportf( "Xash3D GameCube: net ready ip=%s mask=%s gw=%s (eth2sp1/BBA)\n",
		gc_net_ip, gc_net_mask, gc_net_gw );
	GC_NetReportStatus( "ready" );
	return 1;
}

void GC_NetShutdown( void )
{
	if( !gc_net_ready )
		return;
	net_deinit();
	gc_net_ready = false;
	/* The Cube driver has no teardown; retain interface addresses for reuse. */
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

void GC_NetReportStatus( const char *phase ) { (void)phase; }
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
