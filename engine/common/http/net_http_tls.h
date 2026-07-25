/*
net_http_tls.h - TLS plumbing for HTTPS in the built-in HTTP client
Copyright (C) 2026 Xash3D FWGS Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef NET_HTTP_TLS_H
#define NET_HTTP_TLS_H

#include "xash3d_types.h"

typedef struct tlsctx_s tlsctx_t;

enum
{
	HTTP_TLS_OK    =  0, // handshake/op completed
	HTTP_TLS_WANT  = -1, // would block, retry next frame
	HTTP_TLS_ERROR = -2  // permanent failure, tear the connection down
};

#if XASH_MBEDTLS

void HTTP_TlsInit( void );
void HTTP_TlsShutdown( void );
qboolean HTTP_TlsAvailable( void );
tlsctx_t *HTTP_TlsNew( int socket, const char *hostname );
void HTTP_TlsFree( tlsctx_t *ctx );
int HTTP_TlsHandshake( tlsctx_t *ctx );
int HTTP_TlsSend( tlsctx_t *ctx, const void *buf, int len );
int HTTP_TlsRecv( tlsctx_t *ctx, void *buf, int len );

#else // !XASH_MBEDTLS

static inline void HTTP_TlsInit( void ) { }
static inline void HTTP_TlsShutdown( void ) { }
static inline qboolean HTTP_TlsAvailable( void ) { return false; }
static inline tlsctx_t *HTTP_TlsNew( int socket, const char *hostname ) { return NULL; }
static inline void HTTP_TlsFree( tlsctx_t *ctx ) { }
static inline int HTTP_TlsHandshake( tlsctx_t *ctx ) { return HTTP_TLS_ERROR; }
static inline int HTTP_TlsSend( tlsctx_t *ctx, const void *buf, int len ) { return HTTP_TLS_ERROR; }
static inline int HTTP_TlsRecv( tlsctx_t *ctx, void *buf, int len ) { return HTTP_TLS_ERROR; }

#endif // XASH_MBEDTLS

#endif // NET_HTTP_TLS_H
