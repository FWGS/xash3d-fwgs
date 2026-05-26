/*
sys_wii.c - misc wii stubs
Copyright (C) 2026 mintferret

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

#include "platform/platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <fat.h>
#include <SDL.h>

#include <network.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

//TODO: move these to a in_wii.c
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>

void Platform_ShellExecute( const char *path, const char *parms )
{
	Con_Reportf( S_WARN "Tried to shell execute ;%s; -- not supported\n", path );
}

void Wii_Init( void )
{
	SYS_STDIO_Report(true);
	printf( "%s\n", __func__ );

	WPAD_Init();
	KEYBOARD_Init(NULL);
}

void Wii_Shutdown( void )
{
	printf( "%s\n", __func__ );
}

/*
  Socket stuff that I stole from the quake3 port
*/
#include <arpa/inet.h>
int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *sin;
    int family = hints ? hints->ai_family : AF_INET;
    (void)service;

    if (family != AF_INET && family != AF_UNSPEC)
        return EAI_FAMILY;

    ai = (struct addrinfo *)calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
    if (!ai) return EAI_MEMORY;

    sin = (struct sockaddr_in *)(ai + 1);
    sin->sin_family = AF_INET;

    if (!node || !node[0]) {
        sin->sin_addr.s_addr = INADDR_ANY;
    } else if (inet_aton(node, &sin->sin_addr)) {
        /* numeric IP */
    } else {
        struct hostent *he = net_gethostbyname(node);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            free(ai);
            return EAI_NONAME;
        }
        memcpy(&sin->sin_addr, he->h_addr_list[0], he->h_length);
    }

    ai->ai_family   = AF_INET;
    ai->ai_socktype = hints ? hints->ai_socktype : SOCK_DGRAM;
    ai->ai_addrlen  = sizeof(struct sockaddr_in);
    ai->ai_addr     = (struct sockaddr *)sin;
    *res = ai;
    return 0;
}
void freeaddrinfo(struct addrinfo *ai)
{
    struct addrinfo *next;
    while (ai) { next = ai->ai_next; free(ai); ai = next; }
}
const char *gai_strerror(int e)
{
    switch (e) {
    case 0:           return "Success";
    case EAI_FAMILY:  return "Address family not supported";
    case EAI_MEMORY:  return "Out of memory";
    case EAI_NONAME:  return "Name or service not known";
    default:          return "Resolver error";
    }
}
int getnameinfo(const struct sockaddr *sa, socklen_t sl,
                char *h, socklen_t hl,
                char *sv, socklen_t svl, int f)
{
    (void)sl; (void)sv; (void)svl; (void)f;
    if (sa->sa_family == AF_INET && h && hl > 0) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        if (inet_ntop(AF_INET, &sin->sin_addr, h, hl))
            return 0;
    }
    if (h && hl > 0) h[0] = '\0';
    return EAI_FAMILY;
}
int gethostname(char *n,size_t l)      { if(n&&l>0)n[0]='\0'; return 0; }

const struct in6_addr in6addr_any = {{ 0 }};

