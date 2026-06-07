/*
net_http_tls.c - TLS backend for the built-in HTTP client (mbedTLS)
Copyright (C) 2026 Xash3D FWGS contributors

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
#include "net_ws_private.h"
#include "net_http_tls.h"

#if XASH_MBEDTLS

#include <psa/crypto.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/debug.h>
#include <mbedtls/error.h>

extern poolhandle_t http_mempool;

static CVAR_DEFINE_AUTO( http_tls_cafile, "cacert.pem", FCVAR_PRIVILEGED, "path to CA bundle (PEM) used to verify HTTPS servers" );
static CVAR_DEFINE_AUTO( http_tls_insecure, "0", FCVAR_PRIVILEGED, "skip HTTPS certificate verification (debug only)" );
static CVAR_DEFINE_AUTO( http_tls_verbose, "0", FCVAR_PRIVILEGED, "mbedTLS debug verbosity (0=off, 1=error, 2=state, 3=info, 4=trace)" );

// FIXME: implement certificate pinning
typedef int (*pin_verify_fn_t)( void *cert, int depth, uint32_t *flags );

static struct
{
	qboolean inited;
	qboolean has_ca;
	mbedtls_x509_crt cacert;
	pin_verify_fn_t pin_verify; // reserved
} g_tls;

struct tlsctx_s
{
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	int socket; // not owned; caller closes
};

static void HTTP_TlsLogDebug( void *ctx, int level, const char *file, int line, const char *str )
{
	Con_Printf( "TLS[%d] %s:%d %s", level, COM_FileWithoutPath( file ), line, str );
}

static void HTTP_TlsLogErr( const char *what, int ret )
{
	char msg[128];
	mbedtls_strerror( ret, msg, sizeof( msg ));
	Con_Printf( S_ERROR "TLS %s failed: %s (-0x%04x)\n", what, msg, (unsigned int)-ret );
}

static int HTTP_TlsBioSend( void *ctx, const unsigned char *buf, size_t len )
{
	int fd = *(int *)ctx;
	int n = send( fd, buf, len, 0 );

	if( n >= 0 )
		return n;

	int err = WSAGetLastError();
	if( err == WSAEWOULDBLOCK || err == WSAEINPROGRESS )
		return MBEDTLS_ERR_SSL_WANT_WRITE;

	return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int HTTP_TlsBioRecv( void *ctx, unsigned char *buf, size_t len )
{
	int fd = *(int *)ctx;
	int n = recv( fd, buf, len, 0 );

	if( n > 0 )
		return n;

	if( n == 0 )
		return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;

	int err = WSAGetLastError();
	if( err == WSAEWOULDBLOCK || err == WSAEINPROGRESS )
		return MBEDTLS_ERR_SSL_WANT_READ;

	return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static qboolean HTTP_TlsLoadCA( void )
{
	const char *path = http_tls_cafile.string;
	fs_offset_t len = 0;
	byte *data = FS_LoadFile( path, &len, false );

	if( !data || len <= 0 )
	{
		Con_Printf( S_WARN "TLS: CA bundle '%s' not found; HTTPS verification will fail\n", path );
		Mem_Free( data );
		return false;
	}

	int ret = mbedtls_x509_crt_parse( &g_tls.cacert, data, len + 1 );
	Mem_Free( data );

	if( ret < 0 )
	{
		HTTP_TlsLogErr( "x509_crt_parse", ret );
		return false;
	}

	if( ret > 0 )
		Con_Reportf( S_WARN "TLS: %d certificate(s) in '%s' failed to parse\n", ret, path );

	return true;
}

void HTTP_TlsInit( void )
{
	if( g_tls.inited )
		return;

	Cvar_RegisterVariable( &http_tls_cafile );
	Cvar_RegisterVariable( &http_tls_insecure );
	Cvar_RegisterVariable( &http_tls_verbose );

	psa_status_t pstatus = psa_crypto_init();
	if( pstatus != PSA_SUCCESS )
	{
		Con_Printf( S_ERROR "TLS psa_crypto_init failed (status %d)\n", (int)pstatus );
		return;
	}

	mbedtls_x509_crt_init( &g_tls.cacert );
	g_tls.has_ca = HTTP_TlsLoadCA();
	g_tls.pin_verify = NULL;
	g_tls.inited = true;
}

void HTTP_TlsShutdown( void )
{
	if( !g_tls.inited )
		return;

	mbedtls_x509_crt_free( &g_tls.cacert );
	mbedtls_psa_crypto_free();
	g_tls.inited = false;
	g_tls.has_ca = false;
}

qboolean HTTP_TlsAvailable( void )
{
	return g_tls.inited;
}

tlsctx_t *HTTP_TlsNew( int socket, const char *hostname )
{
	if( !g_tls.inited )
		return NULL;

	tlsctx_t *ctx = Mem_Calloc( http_mempool, sizeof( *ctx ));
	ctx->socket = socket;

	mbedtls_ssl_init( &ctx->ssl );
	mbedtls_ssl_config_init( &ctx->conf );

	int ret = mbedtls_ssl_config_defaults( &ctx->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT );
	if( ret != 0 )
	{
		HTTP_TlsLogErr( "ssl_config_defaults", ret );
		HTTP_TlsFree( ctx );
		return NULL;
	}

	int authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
	if( http_tls_insecure.value != 0 || !g_tls.has_ca )
	{
		if( !g_tls.has_ca )
			Con_Printf( S_WARN "TLS: no CA bundle loaded, peer cert will not be verified\n" );
		else
			Con_Printf( S_WARN "TLS: http_tls_insecure is set, peer cert will not be verified\n" );

		authmode = MBEDTLS_SSL_VERIFY_NONE;
	}

	mbedtls_ssl_conf_authmode( &ctx->conf, authmode );
	mbedtls_ssl_conf_ca_chain( &ctx->conf, &g_tls.cacert, NULL );
	mbedtls_ssl_conf_dbg( &ctx->conf, HTTP_TlsLogDebug, NULL );
	mbedtls_debug_set_threshold( http_tls_verbose.value );

	ret = mbedtls_ssl_setup( &ctx->ssl, &ctx->conf );
	if( ret != 0 )
	{
		HTTP_TlsLogErr( "ssl_setup", ret );
		HTTP_TlsFree( ctx );
		return NULL;
	}

	if( hostname && *hostname )
	{
		ret = mbedtls_ssl_set_hostname( &ctx->ssl, hostname );
		if( ret != 0 )
		{
			HTTP_TlsLogErr( "ssl_set_hostname", ret );
			HTTP_TlsFree( ctx );
			return NULL;
		}
	}

	mbedtls_ssl_set_bio( &ctx->ssl, &ctx->socket, HTTP_TlsBioSend, HTTP_TlsBioRecv, NULL );

	return ctx;
}

void HTTP_TlsFree( tlsctx_t *ctx )
{
	if( !ctx )
		return;

	mbedtls_ssl_free( &ctx->ssl );
	mbedtls_ssl_config_free( &ctx->conf );
	Mem_Free( ctx );
}

int HTTP_TlsHandshake( tlsctx_t *ctx )
{
	int ret = mbedtls_ssl_handshake( &ctx->ssl );

	if( ret == 0 )
		return HTTP_TLS_OK;

	if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
		return HTTP_TLS_WANT;

	HTTP_TlsLogErr( "handshake", ret );
	return HTTP_TLS_ERROR;
}

int HTTP_TlsSend( tlsctx_t *ctx, const void *buf, int len )
{
	int ret = mbedtls_ssl_write( &ctx->ssl, (const unsigned char *)buf, (size_t)len );

	if( ret >= 0 )
		return ret;

	if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
		return HTTP_TLS_WANT;

	HTTP_TlsLogErr( "send", ret );
	return HTTP_TLS_ERROR;
}

int HTTP_TlsRecv( tlsctx_t *ctx, void *buf, int len )
{
	int ret = mbedtls_ssl_read( &ctx->ssl, (unsigned char *)buf, (size_t)len );

	if( ret > 0 )
		return ret;

	if( ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY )
		return 0;

	if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
		return HTTP_TLS_WANT;

	HTTP_TlsLogErr( "recv", ret );
	return HTTP_TLS_ERROR;
}

#endif // XASH_MBEDTLS
