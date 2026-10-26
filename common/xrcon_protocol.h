/*
xrcon_protocol.h - XRCON wire protocol constants shared by the engine server and external clients
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

#ifndef XRCON_PROTOCOL_H
#define XRCON_PROTOCOL_H

#include <stdint.h>

#include "xash3d_types.h"

// Frame type tags. The wire form is the four ASCII bytes in transmission order;
// HostFourCC gives the host-endian uint32_t representation engine-side code uses
// to compare against parsed headers.
#define XRCON_TYPE_CMND          HostFourCC( 'C', 'M', 'N', 'D' )
#define XRCON_TYPE_CHAN          HostFourCC( 'C', 'H', 'A', 'N' )
#define XRCON_TYPE_AINF          HostFourCC( 'A', 'I', 'N', 'F' )
#define XRCON_TYPE_ADON          HostFourCC( 'A', 'D', 'O', 'N' )
#define XRCON_TYPE_PRNT          HostFourCC( 'P', 'R', 'N', 'T' )

#define XRCON_CMND_VERSION       0x000000D4u
#define XRCON_CMND_VERSION_CS2RC 0x00D40000u // CS2RemoteConsole <= 1.3.0 byte-slot bug

// Frame header: type (u32) + version (u32) + length (u16) + handle (u16).
#define XRCON_HEADER_SIZE        ( sizeof( uint32_t ) * 2 + sizeof( uint16_t ) * 2 )

// Body-specific layouts.
#define XRCON_CHAN_NAME_SIZE     34 // including null terminator
#define XRCON_AINF_PACKET_SIZE   ( sizeof( uint32_t ) * 19 + sizeof( uint8_t ))
#define XRCON_PRNT_HEADER_SIZE   ( sizeof( uint32_t ) * 7 ) // channel_id + padding[5] + RGBA

// Application limits.
#define XRCON_MAX_FRAME_SIZE     4096
#define XRCON_PRINT_BUFFER_SIZE  4096
#define XRCON_MAX_PACKET_SIZE    ( XRCON_HEADER_SIZE + XRCON_PRNT_HEADER_SIZE + XRCON_PRINT_BUFFER_SIZE )

// Buffer sizes derived from the frame limit.
#define XRCON_TX_BUFFER_SIZE     ( XRCON_MAX_FRAME_SIZE * 4 )
#define XRCON_RX_BUFFER_SIZE     ( XRCON_MAX_FRAME_SIZE + 64 )

#endif // XRCON_PROTOCOL_H
