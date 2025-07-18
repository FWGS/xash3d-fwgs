/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org>
*/

#ifndef NETAPI_H
#define NETAPI_H

#include "netadr.h"

enum
{
	NET_SUCCESS	= 0,
	NET_ERROR_TIMEOUT = 1 << 0,
	NET_ERROR_PROTO_UNSUPPORTED = 1 << 1,
	NET_ERROR_UNDEFINED = 1 << 2,
	NET_ERROR_FORBIDDEN	= 1 << 3
};

enum
{
	NETAPI_REQUEST_SERVERLIST,
	NETAPI_REQUEST_PING,
	NETAPI_REQUEST_RULES,
	NETAPI_REQUEST_PLAYERS,
	NETAPI_REQUEST_DETAILS
};

enum
{
	FNETAPI_MULTIPLE_RESPONSE = 1 << 0,
	FNETAPI_LEGACY_PROTOCOL	= 1 << 1
};

typedef struct net_response_s net_response_t;
typedef struct net_adrlist_s net_adrlist_t;
typedef struct net_status_s net_status_t;
typedef struct net_api_s net_api_t;
typedef void (*net_api_response_func_t)(struct net_response_s *);

struct net_response_s {
	int                        error;                /*     0     4 */
	int                        context;              /*     4     4 */
	int                        type;                 /*     8     4 */
	netadr_t                   remote_address;       /*    12    20 */
	double                     ping;                 /*    32     8 */
	void *                     response;             /*    40     4 */

	/* size: 44, cachelines: 1, members: 6 */
	/* last cacheline: 44 bytes */
};

struct net_adrlist_s {
	struct net_adrlist_s *     next;                 /*     0     4 */
	netadr_t                   remote_address;       /*     4    20 */

	/* size: 24, cachelines: 1, members: 2 */
	/* last cacheline: 24 bytes */
};

struct net_status_s {
	int                        connected;            /*     0     4 */
	netadr_t                   local_address;        /*     4    20 */
	netadr_t                   remote_address;       /*    24    20 */
	int                        packet_loss;          /*    44     4 */
	double                     latency;              /*    48     8 */
	double                     connection_time;      /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	double                     rate;                 /*    64     8 */

	/* size: 72, cachelines: 2, members: 7 */
	/* last cacheline: 8 bytes */
};

struct net_api_s {
	void                       (*InitNetworking)(void); /*     0     4 */
	void                       (*Status)(struct net_status_s *); /*     4     4 */
	void                       (*SendRequest)(int, int, int, double, struct netadr_s *, net_api_response_func_t); /*     8     4 */
	void                       (*CancelRequest)(int); /*    12     4 */
	void                       (*CancelAllRequests)(void); /*    16     4 */
	const char  *              (*AdrToString)(struct netadr_s *); /*    20     4 */
	int                        (*CompareAdr)(struct netadr_s *, struct netadr_s *); /*    24     4 */
	int                        (*StringToAdr)(char *, struct netadr_s *); /*    28     4 */
	const char  *              (*ValueForKey)(const char  *, const char  *); /*    32     4 */
	void                       (*RemoveKey)(char *, const char  *); /*    36     4 */
	void                       (*SetValueForKey)(char *, const char  *, const char  *, int); /*    40     4 */

	/* size: 44, cachelines: 1, members: 11 */
	/* last cacheline: 44 bytes */
};

#endif
