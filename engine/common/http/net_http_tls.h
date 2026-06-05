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

// No TLS for you! Yet.
static inline qboolean HTTP_TlsAvailable( void )
{
	return false;
}

#endif // NET_HTTP_TLS_H
