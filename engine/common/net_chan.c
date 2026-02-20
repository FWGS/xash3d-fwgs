/*
net_chan.c - network channel
Copyright (C) 2008 Uncle Mike

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
#include "netchan.h"
#include "xash3d_mathlib.h"
#include "net_encode.h"
#include "protocol.h"
#if !XASH_DEDICATED
#include <bzlib.h>
#endif // !XASH_DEDICATED

#define MAKE_FRAGID( id, count )	((( id & 0xffff ) << 16 ) | ( count & 0xffff ))
#define FRAG_GETID( fragid )		(( fragid >> 16 ) & 0xffff )
#define FRAG_GETCOUNT( fragid )	( fragid & 0xffff )

#define UDP_HEADER_SIZE		28

#define FLOW_AVG			( 2.0f / 3.0f )	// how fast to converge flow estimates
#define FLOW_INTERVAL		0.1		// don't compute more often than this
#define MAX_RELIABLE_PAYLOAD		1400		// biggest packet that has frag and or reliable data

// forward declarations
void Netchan_FlushIncoming( netchan_t *chan, int stream );
void Netchan_AddBufferToList( fragbuf_t **pplist, fragbuf_t *pbuf );

/*
packet header ( size in bits )
-------------
31	sequence
1	does this message contain a reliable payload
31	acknowledge sequence
1	acknowledge receipt of even/odd message
16	qport

The remote connection never knows if it missed a reliable message, the
local side detects that it has been dropped by seeing a sequence acknowledge
higher thatn the last reliable sequence, but without the correct evon/odd
bit for the reliable set.

If the sender notices that a reliable message has been dropped, it will be
retransmitted.  It will not be retransmitted again until a message after
the retransmit has been acknowledged and the reliable still failed to get there.

if the sequence number is -1, the packet should be handled without a netcon

The reliable message can be added to at any time by doing
MSG_Write* (&netchan->message, <data>).

If the message buffer is overflowed, either by a single message, or by
multiple frames worth piling up while the last reliable transmit goes
unacknowledged, the netchan signals a fatal error.

Reliable messages are allways placed first in a packet, then the unreliable
message is included if there is sufficient room.

To the receiver, there is no distinction between the reliable and unreliable
parts of the message, they are just processed out as a single larger message.

Illogical packet sequence numbers cause the packet to be dropped, but do
not kill the connection.  This, combined with the tight window of valid
reliable acknowledgement numbers provides protection against malicious
address spoofing.

The qport field is a workaround for bad address translating routers that
sometimes remap the client's source port on a packet during gameplay.

If the base part of the net address matches and the qport matches, then the
channel matches even if the IP port differs.  The IP port should be updated
to the new value before sending out any replies.


If there is no information that needs to be transfered on a given frame,
such as during the connection stage while waiting for the client to load,
then a packet only needs to be delivered if there is something in the
unacknowledged reliable
*/
CVAR_DEFINE_AUTO( net_showpackets, "0", FCVAR_PRIVILEGED, "show network packets" );
static CVAR_DEFINE_AUTO( net_chokeloop, "0", 0, "apply bandwidth choke to loopback packets" );
static CVAR_DEFINE_AUTO( net_showdrop, "0", 0, "show packets that are dropped" );
static CVAR_DEFINE_AUTO( net_qport, "0", FCVAR_READ_ONLY, "current quake netport" );
CVAR_DEFINE_AUTO( net_send_debug, "0", FCVAR_PRIVILEGED, "enable debugging output for outgoing messages" );
CVAR_DEFINE_AUTO( net_recv_debug, "0", FCVAR_PRIVILEGED, "enable debugging output for incoming messages" );

int	net_drop;
netadr_t	net_from;
sizebuf_t	net_message;
static poolhandle_t net_mempool;
byte	net_message_buffer[NET_MAX_MESSAGE];

static const char *const ns_strings[NS_COUNT] =
{
	"Client",
	"Server",
};

#if !XASH_DEDICATED
void bz_internal_error( int errcode );
void bz_internal_error( int errcode )
{
	Con_Printf( S_ERROR "bzip2/libbzip2: internal error number %d.\n"
			"This is a bug in bzip2/libbzip2, %s.\n"
			"Please report it at: https://gitlab.com/bzip2/bzip2/-/issues\n"
			"If this happened when you were using some program which uses\n"
			"libbzip2 as a component, you should also report this bug to\n"
			"the author(s) of that program.\n"
			"Please make an effort to report this bug;\n"
			"timely and accurate bug reports eventually lead to higher\n"
			"quality software.  Thanks.\n\n",
			errcode, BZ2_bzlibVersion( ));

	if (errcode == 1007) {
		Con_Printf(
			"\n*** A special note about internal error number 1007 ***\n"
			"\n"
			"Experience suggests that a common cause of i.e. 1007\n"
			"is unreliable memory or other hardware.  The 1007 assertion\n"
			"just happens to cross-check the results of huge numbers of\n"
			"memory reads/writes, and so acts (unintendedly) as a stress\n"
			"test of your memory system.\n"
			"\n"
			"I suggest the following: try compressing the file again,\n"
			"possibly monitoring progress in detail with the -vv flag.\n"
			"\n"
			"* If the error cannot be reproduced, and/or happens at different\n"
			"  points in compression, you may have a flaky memory system.\n"
			"  Try a memory-test program.  I have used Memtest86\n"
			"  (www.memtest86.com).  At the time of writing it is free (GPLd).\n"
			"  Memtest86 tests memory much more thorougly than your BIOSs\n"
			"  power-on test, and may find failures that the BIOS doesn't.\n"
			"\n"
			"* If the error can be repeatably reproduced, this is a bug in\n"
			"  bzip2, and I would very much like to hear about it.  Please\n"
			"  let me know, and, ideally, save a copy of the file causing the\n"
			"  problem -- without which I will be unable to investigate it.\n"
			"\n"
		);
	}

	Sys_Error( "bzip2/libbzip2: internal error number %d\n", errcode );
}
#endif // XASH_DEDICATED

/*
===============
Netchan_Init
===============
*/
void Netchan_Init( void )
{
	char buf[32];
	int	port;

	// pick a port value that should be nice and random
	port = COM_RandomLong( 1, 65535 );
	Q_snprintf( buf, sizeof( buf ), "%i", port );

	Cvar_RegisterVariable( &net_showpackets );
	Cvar_RegisterVariable( &net_chokeloop );
	Cvar_RegisterVariable( &net_showdrop );
	Cvar_RegisterVariable( &net_qport );
	Cvar_RegisterVariable( &net_send_debug );
	Cvar_RegisterVariable( &net_recv_debug );
	Cvar_FullSet( net_qport.name, buf, net_qport.flags );

	net_mempool = Mem_AllocPool( "Network Pool" );
}

void Netchan_Shutdown( void )
{
	Mem_FreePool( &net_mempool );
}

void Netchan_ReportFlow( netchan_t *chan )
{
	char	incoming[64];
	char	outgoing[64];

	if( CL_IsPlaybackDemo( ))
		return;

	Assert( chan != NULL );

	Q_strncpy( incoming, Q_pretifymem((float)chan->flow[FLOW_INCOMING].totalbytes, 3 ), sizeof( incoming ));
	Q_strncpy( outgoing, Q_pretifymem((float)chan->flow[FLOW_OUTGOING].totalbytes, 3 ), sizeof( outgoing ));

	Con_DPrintf( "Signon network traffic:  %s from server, %s to server\n", incoming, outgoing );
}

/*
==============
Netchan_IsLocal

detect a loopback message
==============
*/
qboolean Netchan_IsLocal( netchan_t *chan )
{
	if( !NET_IsActive() || NET_IsLocalAddress( chan->remote_address ))
		return true;
	return false;
}

/*
==============
Netchan_Setup

called to open a channel to a remote system
==============
*/
void Netchan_Setup( netsrc_t sock, netchan_t *chan, netadr_t adr, int qport, void *client, int (*pfnBlockSize)(void *, fragsize_t mode ), uint flags )
{
	Netchan_Clear( chan );

	memset( chan, 0, sizeof( *chan ));

	if( pfnBlockSize == NULL )
	{
		Host_Error( "%s: pfnBlockSize == NULL", __func__ );
		return;
	}

	chan->sock = sock;
	chan->remote_address = adr;
	chan->last_received = host.realtime;
	chan->connect_time = host.realtime;
	chan->incoming_sequence = 0;
	chan->outgoing_sequence = 1;
	chan->rate = DEFAULT_RATE;
	chan->qport = qport;
	chan->client = client;
	chan->pfnBlockSize = pfnBlockSize;
	chan->use_munge = FBitSet( flags, NETCHAN_USE_MUNGE ) ? true : false;
	chan->use_bz2 = FBitSet( flags, NETCHAN_USE_BZIP2 ) ? true : false;
	chan->use_lzss = FBitSet( flags, NETCHAN_USE_LZSS ) ? true : false;
	chan->gs_netchan = FBitSet( flags, NETCHAN_GOLDSRC ) ? true : false;

	MSG_Init( &chan->message, "NetData", chan->message_buf, sizeof( chan->message_buf ));
}

/*
==============================
Netchan_IncomingReady

==============================
*/
qboolean Netchan_IncomingReady( netchan_t *chan )
{
	int	i;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		if( chan->incomingready[i] )
			return true;
	}

	return false;
}

/*
===============
Netchan_CanPacket

Returns true if the bandwidth choke isn't active
================
*/
qboolean Netchan_CanPacket( netchan_t *chan, qboolean choke )
{
	// never choke loopback packets.
	if( !choke || ( !net_chokeloop.value && NET_IsLocalAddress( chan->remote_address ) ))
	{
		chan->cleartime = host.realtime;
		return true;
	}

	return chan->cleartime < host.realtime ? true : false;
}

/*
==============================
Netchan_UnlinkFragment

==============================
*/
static void Netchan_UnlinkFragment( fragbuf_t *buf, fragbuf_t **list )
{
	fragbuf_t	*search;

	if( !list ) return;

	// at head of list
	if( buf == *list )
	{
		// remove first element
		*list = buf->next;

		// destroy remnant
		Mem_Free( buf );
		return;
	}

	search = *list;
	while( search->next )
	{
		if( search->next == buf )
		{
			search->next = buf->next;

			// destroy remnant
			Mem_Free( buf );
			return;
		}
		search = search->next;
	}
}

/*
==============================
Netchan_ClearFragbufs

==============================
*/
static void Netchan_ClearFragbufs( fragbuf_t **ppbuf )
{
	fragbuf_t	*buf, *n;

	if( !ppbuf ) return;

	// Throw away any that are sitting around
	buf = *ppbuf;

	while( buf )
	{
		n = buf->next;
		Mem_Free( buf );
		buf = n;
	}

	*ppbuf = NULL;
}

/*
==============================
Netchan_ClearFragments

==============================
*/
static void Netchan_ClearFragments( netchan_t *chan )
{
	fragbufwaiting_t	*wait, *next;
	int		i;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		wait = chan->waitlist[i];

		while( wait )
		{
			next = wait->next;
			Netchan_ClearFragbufs( &wait->fragbufs );
			Mem_Free( wait );
			wait = next;
		}
		chan->waitlist[i] = NULL;

		Netchan_ClearFragbufs( &chan->fragbufs[i] );
		Netchan_FlushIncoming( chan, i );
	}
}

/*
==============================
Netchan_Clear

==============================
*/
void Netchan_Clear( netchan_t *chan )
{
	int	i;

	Netchan_ClearFragments( chan );

	chan->cleartime = 0.0;
	chan->reliable_length = 0;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		chan->reliable_fragid[i] = 0;
		chan->reliable_fragment[i] = 0;
		chan->fragbufcount[i] = 0;
		chan->frag_startpos[i] = 0;
		chan->frag_length[i] = 0;
		chan->incomingready[i] = false;
	}

	if( chan->tempbuffer )
	{
		Mem_Free( chan->tempbuffer );
		chan->tempbuffer = NULL;
	}
	chan->tempbuffersize = 0;

	memset( chan->flow, 0, sizeof( chan->flow ));
}

/*
===============
Netchan_OutOfBand

Sends an out-of-band datagram
================
*/
void Netchan_OutOfBand( int net_socket, netadr_t adr, int len, const byte *data )
{
	byte buf[MAX_PRINT_MSG + 4] = { 0xff, 0xff, 0xff, 0xff };

	if( CL_IsPlaybackDemo( ))
		return;

	if( len > sizeof( buf ) - 4 )
	{
		Host_Error( "%s: overflow!\n", __func__ );
		return;
	}

	memcpy( &buf[4], data, len );
	NET_SendPacket( net_socket, len + 4, buf, adr );
}

/*
===============
Netchan_OutOfBandPrint

Sends a text message in an out-of-band datagram
================
*/
void Netchan_OutOfBandPrint( int net_socket, netadr_t adr, const char *fmt, ... )
{
	va_list	va;
	byte buf[MAX_PRINT_MSG + 4] = { 0xff, 0xff, 0xff, 0xff };
	int len;

	if( CL_IsPlaybackDemo( ))
		return;

	va_start( va, fmt );
	len = Q_vsnprintf( &buf[4], sizeof( buf ) - 4, fmt, va );
	va_end( va );

	if( len < 0 )
	{
		Host_Error( "%s: snprintf overflow!\n", __func__ );
		return;
	}

	NET_SendPacket( net_socket, len + 4, buf, adr );
}

/*
==============================
Netchan_AllocFragbuf

==============================
*/
static fragbuf_t *Netchan_AllocFragbuf( int fragment_size )
{
	fragbuf_t	*buf;

	buf = (fragbuf_t *)Mem_Calloc( net_mempool, sizeof( fragbuf_t ) + fragment_size );
	MSG_Init( &buf->frag_message, "Frag Message", buf->frag_message_buf, fragment_size );

	return buf;
}

/*
==============================
Netchan_AddFragbufToTail

==============================
*/
static void Netchan_AddFragbufToTail( fragbufwaiting_t *wait, fragbuf_t *buf )
{
	fragbuf_t *p;

	buf->next = NULL;
	wait->fragbufcount++;
	p = wait->fragbufs;

	if( p )
	{
		while( p->next )
			p = p->next;
		p->next = buf;
	}
	else wait->fragbufs = buf;
}

/*
==============================
Netchan_UpdateFlow

==============================
*/
static void Netchan_UpdateFlow( netchan_t *chan )
{
	float	faccumulatedtime = 0.0;
	int	i, bytes = 0;
	int	flow, start;

	if( !chan ) return;

	for( flow = 0; flow < 2; flow++ )
	{
		flow_t	*pflow = &chan->flow[flow];

		if(( host.realtime - pflow->nextcompute ) < FLOW_INTERVAL )
			continue;

		pflow->nextcompute = host.realtime + FLOW_INTERVAL;
		start = pflow->current - 1;

		// compute data flow rate
		for( i = 0; i < MASK_LATENT; i++ )
		{
			flowstats_t *pprev = &pflow->stats[(start - i) & MASK_LATENT];
			flowstats_t *pstat = &pflow->stats[(start - i - 1) & MASK_LATENT];

			faccumulatedtime += ( pprev->time - pstat->time );
			bytes += pstat->size;
		}

		pflow->kbytespersec = (faccumulatedtime == 0.0f) ? 0.0f : bytes / faccumulatedtime / 1024.0f;
		pflow->avgkbytespersec = pflow->avgkbytespersec * FLOW_AVG + pflow->kbytespersec * (1.0f - FLOW_AVG);
	}
}

/*
==============================
Netchan_FragSend

Fragmentation buffer is full and user is prepared to send
==============================
*/
void Netchan_FragSend( netchan_t *chan )
{
	fragbufwaiting_t	*wait;
	int		i;

	if( !chan ) return;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		// already something queued up, just leave in waitlist
		if( chan->fragbufs[i] ) continue;

		wait = chan->waitlist[i];

		// nothing to queue?
		if( !wait ) continue;

		chan->waitlist[i] = wait->next;

		wait->next = NULL;

		// copy in to fragbuf
		chan->fragbufs[i] = wait->fragbufs;
		chan->fragbufcount[i] = wait->fragbufcount;

		// throw away wait list
		Mem_Free( wait );
	}
}

/*
==============================
Netchan_AddBufferToList

==============================
*/
void Netchan_AddBufferToList( fragbuf_t **pplist, fragbuf_t *pbuf )
{
	// Find best slot
	fragbuf_t	*pprev, *n;
	int	id1, id2;

	pbuf->next = NULL;

	if( !pplist )
		return;

	if( !*pplist )
	{
		pbuf->next = *pplist;
		*pplist = pbuf;
		return;
	}

	pprev = *pplist;
	while( pprev->next )
	{
		n = pprev->next; // next item in list
		id1 = FRAG_GETID( n->bufferid );
		id2 = FRAG_GETID( pbuf->bufferid );

		if( id1 > id2 )
		{
			// insert here
			pbuf->next = n->next;
			pprev->next = pbuf;
			return;
		}
		pprev = pprev->next;
	}

	// insert at end
	pprev->next = pbuf;
}

/*
==============================
Netchan_CreateFragments_

==============================
*/
static void Netchan_CreateFragments_( netchan_t *chan, sizebuf_t *msg )
{
	fragbuf_t		*buf;
	int		chunksize;
	int		remaining;
	int		bytes, pos;
	int		bufferid = 1;
	fragbufwaiting_t	*wait, *p;

	if( MSG_GetNumBytesWritten( msg ) == 0 )
		return;

	chunksize = chan->pfnBlockSize( chan->client, FRAGSIZE_FRAG );

	wait = (fragbufwaiting_t *)Mem_Calloc( net_mempool, sizeof( fragbufwaiting_t ));

	if( chan->use_bz2 && memcmp( MSG_GetData( msg ), "BZ2", 4 ))
	{
#if !XASH_DEDICATED
		byte pbOut[0x10000];
		uint uSourceSize = MSG_GetNumBytesWritten( msg );
		uint uCompressedSize = MSG_GetNumBytesWritten( msg ) - 4;
		if( BZ2_bzBuffToBuffCompress( pbOut, &uCompressedSize, MSG_GetData( msg ), uSourceSize, 9, 0, 30 ) == BZ_OK )
		{
			if( uCompressedSize < uSourceSize )
			{
				Con_Reportf( "Compressing split packet with BZip2 (%d -> %d bytes)\n", uSourceSize, uCompressedSize );
				memcpy( msg->pData, "BZ2", 4 );
				memcpy( msg->pData + 4, pbOut, uCompressedSize );
				MSG_SeekToBit( msg, ( uCompressedSize + 4 ) << 3, SEEK_SET );
			}
		}
#else
		Host_Error( "%s: BZ2 compression is not supported for server", __func__ );
#endif
	}
	else if( chan->use_lzss && !LZSS_IsCompressed( MSG_GetData( msg ), MSG_GetMaxBytes( msg )))
	{
		uint uCompressedSize = 0;
		uint uSourceSize = MSG_GetNumBytesWritten( msg );
		byte *pbOut = LZSS_Compress( msg->pData, uSourceSize, &uCompressedSize );

		if( pbOut && uCompressedSize > 0 && uCompressedSize < uSourceSize )
		{
			Con_Reportf( "Compressing split packet with LZSS (%d -> %d bytes)\n", uSourceSize, uCompressedSize );
			memcpy( msg->pData, pbOut, uCompressedSize );
			MSG_SeekToBit( msg, uCompressedSize << 3, SEEK_SET );
		}
		if( pbOut ) free( pbOut );
	}

	remaining = MSG_GetNumBytesWritten( msg );
	pos = 0;	// current position in bytes

	while( remaining > 0 )
	{
		bytes = Q_min( remaining, chunksize );
		remaining -= bytes;

		buf = Netchan_AllocFragbuf( bytes );
		buf->bufferid = bufferid++;

		// Copy in data
		MSG_Clear( &buf->frag_message );
		MSG_WriteBits( &buf->frag_message, &msg->pData[pos], bytes << 3 );

		Netchan_AddFragbufToTail( wait, buf );
		pos += bytes;
	}

	// now add waiting list item to end of buffer queue
	if( !chan->waitlist[FRAG_NORMAL_STREAM] )
	{
		chan->waitlist[FRAG_NORMAL_STREAM] = wait;
	}
	else
	{
		p = chan->waitlist[FRAG_NORMAL_STREAM];

		while( p->next )
			p = p->next;
		p->next = wait;
	}
}

/*
==============================
Netchan_CreateFragments

==============================
*/
void Netchan_CreateFragments( netchan_t *chan, sizebuf_t *msg )
{
	// always queue any pending reliable data ahead of the fragmentation buffer
	if( MSG_GetNumBytesWritten( &chan->message ) > 0 )
	{
		Netchan_CreateFragments_( chan, &chan->message );
		MSG_Clear( &chan->message );
	}

	Netchan_CreateFragments_( chan, msg );
}

/*
==============================
Netchan_FindBufferById

==============================
*/
static fragbuf_t *Netchan_FindBufferById( fragbuf_t **pplist, int id, qboolean allocate )
{
	fragbuf_t	*list = *pplist;
	fragbuf_t	*pnewbuf;

	while( list )
	{
		if( list->bufferid == id )
			return list;

		list = list->next;
	}

	if( !allocate )
		return NULL;

	// create new entry
	pnewbuf = Netchan_AllocFragbuf( NET_MAX_FRAGMENT );
	pnewbuf->bufferid = id;
	Netchan_AddBufferToList( pplist, pnewbuf );

	return pnewbuf;
}

/*
==============================
Netchan_CheckForCompletion

==============================
*/
static void Netchan_CheckForCompletion( netchan_t *chan, int stream, int intotalbuffers )
{
	int	c, id;
	int	size;
	fragbuf_t	*p;

	size = 0;
	c = 0;

	p = chan->incomingbufs[stream];
	if( !p ) return;

	while( p )
	{
		size += MSG_GetNumBytesWritten( &p->frag_message );
		c++;

		id = FRAG_GETID( p->bufferid );
		if( id != c )
		{
			if( chan->sock == NS_CLIENT )
			{
				Con_DPrintf( S_ERROR "Lost/dropped fragment would cause stall, retrying connection\n" );
				Cbuf_AddText( "reconnect\n" );
			}
		}
		p = p->next;
	}

	// received final message
	if( c == intotalbuffers )
		chan->incomingready[stream] = true;
}

/*
==============================
Netchan_CreateFileFragmentsFromBuffer

==============================
*/
void Netchan_CreateFileFragmentsFromBuffer( netchan_t *chan, const char *filename, byte *pbuf, int size )
{
	int        chunksize;
	int        send, pos;
	int        remaining;
	int        bufferid = 1;
	qboolean   firstfragment = true;
	fragbufwaiting_t *wait, *p;
	fragbuf_t  *buf;
	uint       originalSize = size;
	const char *compressor = "";

	if( !size )
		return;

	chunksize = chan->pfnBlockSize( chan->client, FRAGSIZE_FRAG );

	if( chan->gs_netchan )
	{
#if !XASH_DEDICATED
		uint uCompressedSize = size + 600;
		byte *pbOut = Mem_Malloc( net_mempool, uCompressedSize );
		if( BZ2_bzBuffToBuffCompress( pbOut, &uCompressedSize, pbuf, size, 9, 0, 30 ) == BZ_OK && uCompressedSize < size )
		{
			Con_DPrintf( "Compressing filebuffer (%s -> %s)\n", Q_memprint( size ), Q_memprint( uCompressedSize ));
			memcpy( pbuf, pbOut, uCompressedSize );
			size = uCompressedSize;
			compressor = "bz2";
		}
		Mem_Free( pbOut );
#else
		Host_Error( "%s: BZ2 compression is not supported for server\n", __func__ );
#endif
	}
	else
	{
		uint uCompressedSize = 0;
		byte *pbOut = LZSS_Compress( pbuf, size, &uCompressedSize );
		if( pbOut && uCompressedSize > 0 && uCompressedSize < size )
		{
			Con_DPrintf( "Compressing filebuffer (%s -> %s)\n", Q_memprint( size ), Q_memprint( uCompressedSize ));
			memcpy( pbuf, pbOut, uCompressedSize );
			size = uCompressedSize;
			compressor = "lzss";
		}
		if( pbOut )
			free( pbOut );
	}

	wait = (fragbufwaiting_t *)Mem_Calloc( net_mempool, sizeof( fragbufwaiting_t ));
	remaining = size;
	pos = 0;

	while( remaining > 0 )
	{
		send = Q_min( remaining, chunksize );

		buf = Netchan_AllocFragbuf( send );
		buf->bufferid = bufferid++;

		// copy in data
		MSG_Clear( &buf->frag_message );

		if( firstfragment )
		{
			// write filename
			MSG_WriteString( &buf->frag_message, filename );
			
			// write compressor name and uncompressed size
			if( chan->gs_netchan )
			{
				MSG_WriteString( &buf->frag_message, compressor );
				MSG_WriteLong( &buf->frag_message, originalSize );
			}

			// send a bit less on first package
			send -= MSG_GetNumBytesWritten( &buf->frag_message );

			firstfragment = false;
		}

		buf->isbuffer = true;
		buf->isfile = true;
		buf->size = send;
		buf->foffset = pos;

		MSG_WriteBits( &buf->frag_message, pbuf + pos, send << 3 );

		remaining -= send;
		pos += send;

		Netchan_AddFragbufToTail( wait, buf );
	}

	// now add waiting list item to end of buffer queue
	if( !chan->waitlist[FRAG_FILE_STREAM] )
	{
		chan->waitlist[FRAG_FILE_STREAM] = wait;
	}
	else
	{
		p = chan->waitlist[FRAG_FILE_STREAM];

		while( p->next )
			p = p->next;
		p->next = wait;
	}
}

/*
==============================
Netchan_CreateFileFragments

==============================
*/
int Netchan_CreateFileFragments( netchan_t *chan, const char *filename )
{
	int         chunksize;
	int         send, pos;
	int         remaining;
	int         bufferid = 1;
	fs_offset_t filesize = 0;
	fs_offset_t originalSize = 0;
	int         compressedFileTime;
	int         fileTime;
	qboolean    firstfragment = true;
	qboolean    bCompressed = false;
	fragbufwaiting_t *wait, *p;
	fragbuf_t   *buf;
	char        compressedfilename[sizeof( buf->filename ) + 5];
	const char  *compressor = "";
	uint        uCompressedSize = 0;
	byte        *compressed = NULL;
	byte        *uncompressed = NULL;

	// shouldn't be critical, but just in case
	if( Q_strlen( filename ) > sizeof( buf->filename ) - 1 )
	{
		Con_Printf( S_WARN "Unable to transfer %s due to path length overflow\n", filename );
		return 0;
	}

	if(( filesize = FS_FileSize( filename, false )) <= 0 )
	{
		Con_Printf( S_WARN "Unable to open %s for transfer\n", filename );
		return 0;
	}

	originalSize = filesize;
	chunksize = chan->pfnBlockSize( chan->client, FRAGSIZE_FRAG );

	Q_snprintf( compressedfilename, sizeof( compressedfilename ), "%s.ztmp", filename );
	compressedFileTime = FS_FileTime( compressedfilename, false );
	fileTime = FS_FileTime( filename, false );

	if( compressedFileTime >= fileTime )
	{
		// if compressed file already created and newer than source
		fs_offset_t compressedSize = FS_FileSize( compressedfilename, false );
		if( compressedSize != -1 )
		{
			bCompressed = true;
			filesize = compressedSize;
		}
	}
	else
	{
		uncompressed = FS_LoadFile( filename, &filesize, false );
		if( chan->gs_netchan )
		{
#if !XASH_DEDICATED
			compressed = Mem_Malloc( net_mempool, filesize + 600 );
			uCompressedSize = filesize + 600;
			if( BZ2_bzBuffToBuffCompress( compressed, &uCompressedSize, uncompressed, filesize, 9, 0, 30 ) == BZ_OK && uCompressedSize < filesize )
			{
				Con_DPrintf( "compressed file %s (%s -> %s)\n", filename, Q_memprint( filesize ), Q_memprint( uCompressedSize ));
				FS_WriteFile( compressedfilename, compressed, uCompressedSize );
				filesize = uCompressedSize;
				bCompressed = true;
				compressor = "bz2";
			}
			Mem_Free( compressed );
#else
			Host_Error( "%s: BZ2 compression is not supported for server\n", __func__ );
#endif
		}
		else
		{
			compressed = LZSS_Compress( uncompressed, filesize, &uCompressedSize );
			if( compressed && uCompressedSize > 0 && uCompressedSize < filesize )
			{
				Con_DPrintf( "compressed file %s (%s -> %s)\n", filename, Q_memprint( filesize ), Q_memprint( uCompressedSize ));
				FS_WriteFile( compressedfilename, compressed, uCompressedSize );
				filesize = uCompressedSize;
				bCompressed = true;
				compressor = "lzss";
				free( compressed );
			}
		}
		Mem_Free( uncompressed );
	}

	wait = (fragbufwaiting_t *)Mem_Calloc( net_mempool, sizeof( fragbufwaiting_t ));
	remaining = filesize;
	pos = 0;

	while( remaining > 0 )
	{
		send = Q_min( remaining, chunksize );

		buf = Netchan_AllocFragbuf( send );
		buf->bufferid = bufferid++;

		// copy in data
		MSG_Clear( &buf->frag_message );

		if( firstfragment )
		{
			// Write filename
			MSG_WriteString( &buf->frag_message, filename );

			// write compressor name and uncompressed size
			if( chan->gs_netchan )
			{
				MSG_WriteString( &buf->frag_message, compressor );
				MSG_WriteLong( &buf->frag_message, (uint)originalSize );
			}

			// Send a bit less on first package
			send -= MSG_GetNumBytesWritten( &buf->frag_message );

			firstfragment = false;
		}

		buf->isfile = true;
		buf->size = send;
		buf->foffset = pos;
		buf->iscompressed = bCompressed;
		Q_strncpy( buf->filename, filename, sizeof( buf->filename ));

		pos += send;
		remaining -= send;

		Netchan_AddFragbufToTail( wait, buf );
	}

	// now add waiting list item to end of buffer queue
	if( !chan->waitlist[FRAG_FILE_STREAM] )
	{
		chan->waitlist[FRAG_FILE_STREAM] = wait;
	}
	else
	{
		p = chan->waitlist[FRAG_FILE_STREAM];
		while( p->next )
			p = p->next;
		p->next = wait;
	}

	return 1;
}

/*
==============================
Netchan_FlushIncoming

==============================
*/
void Netchan_FlushIncoming( netchan_t *chan, int stream )
{
	fragbuf_t	*p, *n;

	MSG_Clear( &net_message );

	p = chan->incomingbufs[ stream ];

	while( p )
	{
		n = p->next;
		Mem_Free( p );
		p = n;
	}
	chan->incomingbufs[stream] = NULL;
	chan->incomingready[stream] = false;
}

/*
==============================
Netchan_CopyNormalFragments

==============================
*/
qboolean Netchan_CopyNormalFragments( netchan_t *chan, sizebuf_t *msg, size_t *length )
{
	size_t	size = 0;
	fragbuf_t	*p, *n;

	if( !chan->incomingready[FRAG_NORMAL_STREAM] )
		return false;

	if( !chan->incomingbufs[FRAG_NORMAL_STREAM] )
	{
		chan->incomingready[FRAG_NORMAL_STREAM] = false;
		return false;
	}

	p = chan->incomingbufs[FRAG_NORMAL_STREAM];

	MSG_Init( msg, "NetMessage", net_message_buffer, sizeof( net_message_buffer ));

	while( p )
	{
		n = p->next;

		// copy it in
		MSG_WriteBytes( msg, MSG_GetData( &p->frag_message ), MSG_GetNumBytesWritten( &p->frag_message ));
		size += MSG_GetNumBytesWritten( &p->frag_message );

		Mem_Free( p );
		p = n;
	}

	if( chan->use_bz2 && !memcmp( MSG_GetData( msg ), "BZ2", 4 ))
	{
#if !XASH_DEDICATED
		byte buf[0x10000];
		uint uDecompressedLen = sizeof( buf );
		int bz2_err = BZ2_bzBuffToBuffDecompress( buf, &uDecompressedLen, MSG_GetData( msg ) + 4, MSG_GetNumBytesWritten( msg ) - 4, 1, 0 );

		if( bz2_err == BZ_OK )
		{
			size = uDecompressedLen;
			memcpy( msg->pData, buf, size );
		}
		else
		{
			Con_Printf( S_ERROR "%s: BZ2 decompression failed (%d)\n", __func__, bz2_err );
			return false;
		}
#else
		Host_Error( "%s: BZ2 compression is not supported for server\n", __func__ );
#endif
	}
	else if( chan->use_lzss && LZSS_IsCompressed( MSG_GetData( msg ), size ))
	{
		uint	uDecompressedLen = LZSS_GetActualSize( MSG_GetData( msg ), size );
		byte	buf[NET_MAX_MESSAGE];

		if( uDecompressedLen <= sizeof( buf ))
		{
			size = LZSS_Decompress( MSG_GetData( msg ), buf, size, sizeof( buf ));
			memcpy( msg->pData, buf, size );
		}
		else
		{
			// g-cont. this should not happens
			Con_Printf( S_ERROR "buffer to small to decompress message\n" );
			return false;
		}
	}

	chan->incomingbufs[FRAG_NORMAL_STREAM] = NULL;

	// reset flag
	chan->incomingready[FRAG_NORMAL_STREAM] = false;

	// tell about message size
	if( length ) *length = size;

	return true;
}

/*
==============================
Netchan_CopyFileFragments

==============================
*/
qboolean Netchan_CopyFileFragments( netchan_t *chan, sizebuf_t *msg )
{
	char	filename[MAX_OSPATH], compressor[32];
	uint	uncompressedSize;
	int	nsize, pos;
	byte	*buffer;
	fragbuf_t	*p, *n;

	if( !chan->incomingready[FRAG_FILE_STREAM] )
		return false;

	if( !chan->incomingbufs[FRAG_FILE_STREAM] )
	{
		chan->incomingready[FRAG_FILE_STREAM] = false;
		return false;
	}

	p = chan->incomingbufs[FRAG_FILE_STREAM];

	MSG_Init( msg, "NetMessage", net_message_buffer, sizeof( net_message_buffer ));

	// copy in first chunk so we can get filename out
	MSG_WriteBytes( msg, MSG_GetData( &p->frag_message ), MSG_GetNumBytesWritten( &p->frag_message ));
	MSG_Clear( msg );

	Q_strncpy( filename, MSG_ReadString( msg ), sizeof( filename ));
	compressor[0] = 0;
	if( chan->gs_netchan )
	{
		Q_strncpy( compressor, MSG_ReadString( msg ), sizeof( compressor ));
		uncompressedSize = MSG_ReadLong( msg );
	}

	if( COM_StringEmptyOrNULL( filename ))
	{
		Con_Printf( S_ERROR "file fragment received with no filename\nFlushing input queue\n" );
		Netchan_FlushIncoming( chan, FRAG_FILE_STREAM );
		return false;
	}
	else if( filename[0] != '!' && !COM_IsSafeFileToDownload( filename ))
	{
		Con_Printf( S_ERROR "file fragment received with bad path, ignoring\n" );
		Netchan_FlushIncoming( chan, FRAG_FILE_STREAM );
		return false;
	}

	if( filename[0] != '!' )
	{
		string temp_filename;
		Q_snprintf( temp_filename, sizeof( temp_filename ), DEFAULT_DOWNLOADED_DIRECTORY "%s", filename );
		Q_strncpy( filename, temp_filename, sizeof( filename ));
	}

	Q_strncpy( chan->incomingfilename, filename, sizeof( chan->incomingfilename ));

	if( filename[0] != '!' && FS_FileExists( filename, false ))
	{
		Con_Printf( S_ERROR "can't download %s, already exists\n", filename );
		Netchan_FlushIncoming( chan, FRAG_FILE_STREAM );
		return true;
	}

	// create file from buffers
	nsize = 0;
	while ( p )
	{
		nsize += MSG_GetNumBytesWritten( &p->frag_message ); // Size will include a bit of slop, oh well
		if( p == chan->incomingbufs[FRAG_FILE_STREAM] )
			nsize -= MSG_GetNumBytesRead( msg );
		p = p->next;
	}

	buffer = Mem_Calloc( net_mempool, nsize + 1 );
	p = chan->incomingbufs[FRAG_FILE_STREAM];
	pos = 0;

	while( p )
	{
		int	cursize;

		n = p->next;

		cursize = MSG_GetNumBytesWritten( &p->frag_message );

		// first message has the file name, don't write that into the data stream,
		// just write the rest of the actual data
		if( p == chan->incomingbufs[FRAG_FILE_STREAM] )
		{
			// copy it in
			cursize -= MSG_GetNumBytesRead( msg );
			memcpy( &buffer[pos], &p->frag_message.pData[MSG_GetNumBytesRead( msg )], cursize );
		}
		else
		{
			memcpy( &buffer[pos], p->frag_message.pData, cursize );
		}

		pos += cursize;
		Mem_Free( p );
		p = n;
	}

	if( chan->gs_netchan && chan->use_bz2 && !Q_stricmp( compressor, "bz2" ))
	{
#if !XASH_DEDICATED
		byte *uncompressedBuffer = Mem_Calloc( net_mempool, uncompressedSize );

		Con_DPrintf( "Decompressing file %s (%d -> %d bytes)\n", filename, nsize, uncompressedSize );
		if( BZ2_bzBuffToBuffDecompress( uncompressedBuffer, &uncompressedSize, buffer, nsize, 1, 0 ) != BZ_OK )
		{
			Con_DPrintf( S_ERROR "BZ2 decompression failed for %s\n", filename );
			Mem_Free( buffer );
			Mem_Free( uncompressedBuffer );
			Netchan_FlushIncoming( chan, FRAG_FILE_STREAM );
			return false;
		}
		Mem_Free( buffer );
		nsize = uncompressedSize;
		buffer = uncompressedBuffer;
#else
		Host_Error( "%s: BZ2 compression is not supported for server", __func__ );
#endif
	}
	else if( chan->use_lzss && LZSS_IsCompressed( buffer, nsize + 1 ))
	{
		byte *uncompressedBuffer;

		uncompressedSize = LZSS_GetActualSize( buffer, nsize + 1 ) + 1;
		uncompressedBuffer = Mem_Calloc( net_mempool, uncompressedSize );

		nsize = LZSS_Decompress( buffer, uncompressedBuffer, nsize + 1, uncompressedSize );
		Mem_Free( buffer );
		buffer = uncompressedBuffer;
	}

	// customization files goes int tempbuffer
	if( filename[0] == '!' )
	{
		if( chan->tempbuffer )
			Mem_Free( chan->tempbuffer );
		chan->tempbuffer = buffer;
		chan->tempbuffersize = nsize;
	}
	else
	{
		// g-cont. it's will be stored downloaded files directly into game folder
		FS_WriteFile( filename, buffer, nsize );
		Mem_Free( buffer );
	}

	// clear remnants
	MSG_Clear( msg );

	chan->incomingbufs[FRAG_FILE_STREAM] = NULL;
	chan->incomingready[FRAG_FILE_STREAM] = false;

	return true;
}

static qboolean Netchan_Validate( netchan_t *chan, sizebuf_t *sb, qboolean *frag_message, uint *fragid, int *frag_offset, int *frag_length )
{
	int	i, buffer, offset;
	int	count, length;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		if( !frag_message[i] )
			continue;

		buffer = FRAG_GETID( fragid[i] );
		count = FRAG_GETCOUNT( fragid[i] );
		offset = BitByte( frag_offset[i] );
		length = BitByte( frag_length[i] );

		if( buffer < 0 || buffer > NET_MAX_BUFFER_ID )
			return false;

		if( count < 0 || count > NET_MAX_BUFFERS_COUNT )
			return false;

		if( length < 0 || length > ( FRAGMENT_MAX_SIZE << 3 ))
			return false;

		if( offset < 0 || offset > ( FRAGMENT_MAX_SIZE << 3 ))
			return false;
	}

	return true;
}

/*
==============================
Netchan_UpdateProgress

==============================
*/
void Netchan_UpdateProgress( netchan_t *chan )
{
#if !XASH_DEDICATED
	fragbuf_t *p;
	int	i, c = 0;
	int	total = 0;
	float	bestpercent = 0.0;

	if( host.downloadcount == 0 )
	{
		scr_download.value = -1.0f;
		host.downloadfile[0] = '\0';
	}

	// do show slider for file downloads.
	if( !chan->incomingbufs[FRAG_FILE_STREAM] )
		return;

	for( i = MAX_STREAMS - 1; i >= 0; i-- )
	{
		// receiving data
		if( chan->incomingbufs[i] )
		{
			p = chan->incomingbufs[i];

			total = FRAG_GETCOUNT( p->bufferid );

			while( p )
			{
				c++;
				p = p->next;
			}

			if( total )
			{
				float	percent = 100.0f * (float)c / (float)total;

				if( percent > bestpercent )
					bestpercent = percent;
			}

			p = chan->incomingbufs[i];

			if( i == FRAG_FILE_STREAM )
			{
				char	sz[MAX_SYSPATH];
				char	*in, *out;
				int	len = 0;

				in = (char *)MSG_GetData( &p->frag_message );
				out = sz;

				while( *in )
				{
					*out++ = *in++;
					len++;
					if( len > 128 )
						break;
				}
				*out = '\0';

				if( !COM_StringEmpty( sz ) && sz[0] != '!' )
					Q_strncpy( host.downloadfile, sz, sizeof( host.downloadfile ));
			}
		}
		else if( chan->fragbufs[i] )	// Sending data
		{
			if( chan->fragbufcount[i] )
			{
				float	percent = 100.0f * (float)chan->fragbufs[i]->bufferid / (float)chan->fragbufcount[i];

				if( percent > bestpercent )
					bestpercent = percent;
			}
		}

	}

	scr_download.value = bestpercent;
#endif // XASH_DEDICATED
}

/*
===============
Netchan_TransmitBits

tries to send an unreliable message to a connection, and handles the
transmition / retransmition of the reliable messages.

A 0 length will still generate a packet and deal with the reliable messages.
================
*/
void Netchan_TransmitBits( netchan_t *chan, int length, const byte *data )
{
	byte	send_buf[NET_MAX_MESSAGE];
	qboolean	send_reliable_fragment;
	uint	w1, w2, statId;
	qboolean	send_reliable;
	sizebuf_t	send;
	int	i, j;
	float	fRate;

	// check for message overflow
	if( MSG_CheckOverflow( &chan->message ))
	{
		Con_Printf( S_ERROR "%s:outgoing message overflow\n", NET_AdrToString( chan->remote_address ));
		return;
	}

	if( chan->pfnBlockSize == NULL ) // not initialized
		return;

	// if the remote side dropped the last reliable message, resend it
	send_reliable = false;

	if( chan->incoming_acknowledged > chan->last_reliable_sequence && chan->incoming_reliable_acknowledged != chan->reliable_sequence )
		send_reliable = true;

	// A packet can have "reliable payload + frag payload + unreliable payload
	// frag payload can be a file chunk, if so, it needs to be parsed on the receiving end and reliable payload + unreliable payload need
	// to be passed on to the message queue.  The processing routine needs to be able to handle the case where a message comes in and a file
	// transfer completes

	// if the reliable transmit buffer is empty, copy the current message out
	if( !chan->reliable_length )
	{
		qboolean	send_frag = false;
		fragbuf_t	*pbuf;

		// will be true if we are active and should let chan->message get some bandwidth
		int	send_from_frag[MAX_STREAMS] = { 0, 0 };
		int	send_from_regular = 0;

		// if we have data in the waiting list(s) and we have cleared the current queue(s), then
		// push the waitlist(s) into the current queue(s)
		Netchan_FragSend( chan );

		// sending regular payload
		send_from_regular = MSG_GetNumBytesWritten( &chan->message ) ? 1 : 0;

		// check to see if we are sending a frag payload
		for( i = 0; i < MAX_STREAMS; i++ )
		{
			if( chan->fragbufs[i] )
				send_from_frag[i] = 1;
		}

		// stall reliable payloads if sending from frag buffer
		if( send_from_regular && ( send_from_frag[FRAG_NORMAL_STREAM] ))
		{
			int maxsize = chan->pfnBlockSize( chan->client, FRAGSIZE_SPLIT );
			send_from_regular = false;

			if( maxsize == 0 )
				maxsize = MAX_RELIABLE_PAYLOAD;

			// if the reliable buffer has gotten too big, queue it at the end of everything and clear out buffer
			if( MSG_GetNumBytesWritten( &chan->message ) + (((uint)length) >> 3) > maxsize )
			{
				Netchan_CreateFragments_( chan, &chan->message );
				MSG_Clear( &chan->message );
			}
		}

		// startpos will be zero if there is no regular payload
		for( i = 0; i < MAX_STREAMS; i++ )
		{
			chan->frag_startpos[i] = 0;

			// assume no fragment is being sent
			chan->reliable_fragment[i] = 0;
			chan->reliable_fragid[i] = 0;
			chan->frag_length[i] = 0;

			if( send_from_frag[i] )
			{
				send_frag = true;
			}
		}

		if( send_from_regular || send_frag )
		{
			chan->reliable_sequence ^= 1;
			send_reliable = true;
		}

		if( send_from_regular )
		{
			memcpy( chan->reliable_buf, chan->message_buf, MSG_GetNumBytesWritten( &chan->message ));
			chan->reliable_length = MSG_GetNumBitsWritten( &chan->message );
			MSG_Clear( &chan->message );

			// if we send fragments, this is where they'll start
			for( i = 0; i < MAX_STREAMS; i++ )
				chan->frag_startpos[i] = chan->reliable_length;
		}

		for( i = 0; i < MAX_STREAMS; i++ )
		{
			int	newpayloadsize;
			int	fragment_size;

			// is there someting in the fragbuf?
			pbuf = chan->fragbufs[i];
			fragment_size = 0;

			if( pbuf )
			{
				fragment_size = MSG_GetNumBytesWritten( &pbuf->frag_message );

				// files set size a bit differently.
				if( pbuf->isfile && !pbuf->isbuffer )
				{
					fragment_size = pbuf->size;
				}
			}

			newpayloadsize = (( chan->reliable_length + ( fragment_size << 3 )) + 7 ) >> 3;

			// make sure we have enought space left
			if( send_from_frag[i] && pbuf && newpayloadsize < NET_MAX_FRAGMENT )
			{
				sizebuf_t	temp;

				// which buffer are we sending ?
				chan->reliable_fragid[i] = MAKE_FRAGID( pbuf->bufferid, chan->fragbufcount[i] );

				// if it's not in-memory, then we'll need to copy it in frame the file handle.
				if( pbuf->isfile && !pbuf->isbuffer )
				{
					byte	filebuffer[NET_MAX_FRAGMENT];
					file_t	*file;

					if( pbuf->iscompressed )
					{
						char	compressedfilename[sizeof( pbuf->filename ) + 5];

						Q_snprintf( compressedfilename, sizeof( compressedfilename ), "%s.ztmp", pbuf->filename );
						file = FS_Open( compressedfilename, "rb", false );
					}
					else file = FS_Open( pbuf->filename, "rb", false );

					FS_Seek( file, pbuf->foffset, SEEK_SET );
					FS_Read( file, filebuffer, pbuf->size );

					MSG_WriteBits( &pbuf->frag_message, filebuffer, pbuf->size << 3 );
					FS_Close( file );
				}

				// copy frag stuff on top of current buffer
				MSG_StartWriting( &temp, chan->reliable_buf, sizeof( chan->reliable_buf ), chan->reliable_length, -1 );
				MSG_WriteBits( &temp, MSG_GetData( &pbuf->frag_message ), MSG_GetNumBitsWritten( &pbuf->frag_message ));
				chan->reliable_length += MSG_GetNumBitsWritten( &pbuf->frag_message );
				chan->frag_length[i] = MSG_GetNumBitsWritten( &pbuf->frag_message );

				// unlink pbuf
				Netchan_UnlinkFragment( pbuf, &chan->fragbufs[i] );

				chan->reliable_fragment[i] = 1;

				// offset the rest of the starting positions
				for( j = i + 1; j < MAX_STREAMS; j++ )
					chan->frag_startpos[j] += chan->frag_length[i];
			}
		}
	}

	memset( send_buf, 0, sizeof( send_buf ));
	MSG_Init( &send, "NetSend", send_buf, sizeof( send_buf ));

	// prepare the packet header
	w1 = chan->outgoing_sequence | (send_reliable << 31);
	w2 = chan->incoming_sequence | (chan->incoming_reliable_sequence << 31);

	send_reliable_fragment = false;

	for( i = 0; i < MAX_STREAMS; i++ )
	{
		if( chan->reliable_fragment[i] )
		{
			send_reliable_fragment = true;
			break;
		}
	}

	if( send_reliable && send_reliable_fragment )
		SetBits( w1, BIT( 30 ));

	chan->outgoing_sequence++;

	MSG_WriteLong( &send, w1 );
	MSG_WriteLong( &send, w2 );

	// send the qport if we are a client
	if( chan->sock == NS_CLIENT && !chan->gs_netchan )
	{
		MSG_WriteWord( &send, (int)net_qport.value );
	}

	if( send_reliable && send_reliable_fragment )
	{
		for( i = 0; i < MAX_STREAMS; i++ )
		{
			if( chan->reliable_fragment[i] )
			{
				MSG_WriteByte( &send, 1 );
				MSG_WriteLong( &send, chan->reliable_fragid[i] );
				if( chan->gs_netchan )
				{
					MSG_WriteShort( &send, chan->frag_startpos[i] >> 3 );
					MSG_WriteShort( &send, chan->frag_length[i] >> 3 );
				}
				else
				{
					MSG_WriteLong( &send, chan->frag_startpos[i] );
					MSG_WriteLong( &send, chan->frag_length[i] );
				}
			}
			else
			{
				MSG_WriteByte( &send, 0 );
			}
		}
	}

	// copy the reliable message to the packet first
	if( send_reliable )
	{
		MSG_WriteBits( &send, chan->reliable_buf, chan->reliable_length );
		chan->last_reliable_sequence = chan->outgoing_sequence - 1;
	}

	if( length )
	{
		int maxsize = chan->pfnBlockSize( chan->client, FRAGSIZE_UNRELIABLE );

		if( (( MSG_GetNumBytesWritten( &send ) + length ) >> 3) <= maxsize )
			MSG_WriteBits( &send, data, length );
		else Con_Printf( S_WARN "%s: unreliable message overflow: %d\n", __func__, MSG_GetNumBytesWritten( &send ) );
	}

	// deal with packets that are too small for some networks
	if( MSG_GetNumBytesWritten( &send ) < 16 && !NET_IsLocalAddress( chan->remote_address )) // packet too small for some networks
	{
		// go ahead and pad a full 16 extra bytes -- this only happens during authentication / signon
		for( i = MSG_GetNumBytesWritten( &send ); i < 16; i++ )
		{
			if( chan->sock == NS_CLIENT )
				MSG_BeginClientCmd( &send, clc_nop );
			else if( chan->sock == NS_SERVER )
				MSG_BeginServerCmd( &send, svc_nop );
			else break;
		}
	}

	statId = chan->flow[FLOW_OUTGOING].current & MASK_LATENT;
	chan->flow[FLOW_OUTGOING].stats[statId].size = MSG_GetNumBytesWritten( &send ) + UDP_HEADER_SIZE;
	chan->flow[FLOW_OUTGOING].stats[statId].time = host.realtime;
	chan->flow[FLOW_OUTGOING].totalbytes += chan->flow[FLOW_OUTGOING].stats[statId].size;
	chan->flow[FLOW_OUTGOING].current++;

	Netchan_UpdateFlow( chan );

	chan->total_sended += MSG_GetNumBytesWritten( &send );

	// send the datagram
	if( !CL_IsPlaybackDemo( ))
	{
		int splitsize = chan->pfnBlockSize( chan->client, FRAGSIZE_SPLIT );

		if( chan->use_munge )
			COM_Munge2( send.pData + 8, MSG_GetNumBytesWritten( &send ) - 8, (byte)( chan->outgoing_sequence - 1 ));

		NET_SendPacketEx( chan->sock, MSG_GetNumBytesWritten( &send ), MSG_GetData( &send ), chan->remote_address, splitsize );
	}

	if( SV_Active() && sv_lan.value && sv_lan_rate.value > 1000.0f )
		fRate = 1.0f / sv_lan_rate.value;
	else fRate = 1.0f / chan->rate;

	if( chan->cleartime < host.realtime )
		chan->cleartime = host.realtime;

	chan->cleartime += ( MSG_GetNumBytesWritten( &send ) + UDP_HEADER_SIZE ) * fRate;

	if( net_showpackets.value && net_showpackets.value != 2.0f )
	{
		Con_Printf( " %s --> sz=%i seq=%i ack=%i rel=%i tm=%f\n"
			, ns_strings[chan->sock]
			, MSG_GetNumBytesWritten( &send )
			, ( chan->outgoing_sequence - 1 ) & 63
			, chan->incoming_sequence & 63
			, send_reliable ? 1 : 0
			, (float)host.realtime );
	}
}

/*
=================
Netchan_Process

called when the current net_message is from remote_address
modifies net_message so that it points to the packet payload
=================
*/
qboolean Netchan_Process( netchan_t *chan, sizebuf_t *msg )
{
	uint	sequence, sequence_ack;
	uint	reliable_ack, reliable_message;
	uint	fragid[MAX_STREAMS] = { 0, 0 };
	qboolean	frag_message[MAX_STREAMS] = { false, false };
	int	frag_offset[MAX_STREAMS] = { 0, 0 };
	int	frag_length[MAX_STREAMS] = { 0, 0 };
	qboolean	message_contains_fragments;
	int	i, qport, statId;

	// get sequence numbers
	MSG_Clear( msg );
	sequence = MSG_ReadLong( msg );
	sequence_ack = MSG_ReadLong( msg );

	if( chan->use_munge )
		COM_UnMunge2( msg->pData + 8, ( msg->nDataBits >> 3 ) - 8, sequence & 0xFF );

	// read the qport if we are a server
	if( chan->sock == NS_SERVER )
		qport = MSG_ReadShort( msg );

	reliable_message = sequence >> 31;
	reliable_ack = sequence_ack >> 31;

	message_contains_fragments = FBitSet( sequence, BIT( 30 )) ? true : false;

	if( message_contains_fragments )
	{
		for( i = 0; i < MAX_STREAMS; i++ )
		{
			if( MSG_ReadByte( msg ))
			{
				frag_message[i] = true;
				fragid[i] = MSG_ReadLong( msg );
				if( chan->gs_netchan )
				{
					frag_offset[i] = MSG_ReadShort( msg ) << 3;
					frag_length[i] = MSG_ReadShort( msg ) << 3;
				}
				else
				{
					frag_offset[i] = MSG_ReadLong( msg );
					frag_length[i] = MSG_ReadLong( msg );
				}
			}
		}

		if( !Netchan_Validate( chan, msg, frag_message, fragid, frag_offset, frag_length ))
			return false;
	}

	sequence &= ~BIT( 31 );
	sequence &= ~BIT( 30 );
	sequence_ack &= ~BIT( 30 );
	sequence_ack &= ~BIT( 31 );

	if( net_showpackets.value && net_showpackets.value != 3.0f )
	{
		Con_Printf( " %s <-- sz=%i seq=%i ack=%i rel=%i tm=%f\n"
			, ns_strings[chan->sock]
			, MSG_GetMaxBytes( msg )
			, sequence & 63
			, sequence_ack & 63
			, reliable_message
			, host.realtime );
	}

	// discard stale or duplicated packets
	if( sequence <= (uint)chan->incoming_sequence )
	{
		if( net_showdrop.value )
		{
			const char *adr = NET_AdrToString( chan->remote_address );

			if( sequence == (uint)chan->incoming_sequence )
				Con_Printf( "%s:duplicate packet %i at %i\n", adr, sequence, chan->incoming_sequence );
			else Con_Printf( "%s:out of order packet %i at %i\n", adr, sequence, chan->incoming_sequence );
		}
		return false;
	}

	// dropped packets don't keep the message from being used
	net_drop = sequence - ( chan->incoming_sequence + 1 );
	if( net_drop > 0 && net_showdrop.value )
		Con_Printf( "%s:dropped %i packets at %i\n", NET_AdrToString( chan->remote_address ), net_drop, sequence );

	// if the current outgoing reliable message has been acknowledged
	// clear the buffer to make way for the next
	if( reliable_ack == (uint)chan->reliable_sequence )
	{
		// make sure we actually could have ack'd this message
		if( sequence_ack >= (uint)chan->last_reliable_sequence )
		{
			chan->reliable_length = 0;	// it has been received
		}
	}

	// if this message contains a reliable message, bump incoming_reliable_sequence
	chan->incoming_sequence = sequence;
	chan->incoming_acknowledged = sequence_ack;
	chan->incoming_reliable_acknowledged = reliable_ack;
	if( reliable_message )
	{
		chan->incoming_reliable_sequence ^= 1;
	}

	chan->last_received = host.realtime;

	// Update data flow stats
	statId = chan->flow[FLOW_INCOMING].current & MASK_LATENT;
	chan->flow[FLOW_INCOMING].stats[statId].size = MSG_GetMaxBytes( msg ) + UDP_HEADER_SIZE;
	chan->flow[FLOW_INCOMING].stats[statId].time = host.realtime;
	chan->flow[FLOW_INCOMING].totalbytes += chan->flow[FLOW_INCOMING].stats[statId].size;
	chan->flow[FLOW_INCOMING].current++;

	Netchan_UpdateFlow( chan );

	chan->total_received += MSG_GetMaxBytes( msg );

	if( message_contains_fragments )
	{
		for( i = 0; i < MAX_STREAMS; i++ )
		{
			int	j, inbufferid;
			int	intotalbuffers;
			int	oldpos, curbit;
			int	numbitstoremove;
			fragbuf_t	*pbuf;

			if( !frag_message[i] )
				continue;

			inbufferid = FRAG_GETID( fragid[i] );
			intotalbuffers = FRAG_GETCOUNT( fragid[i] );

			if( fragid[i] != 0 )
			{
				pbuf = Netchan_FindBufferById( &chan->incomingbufs[i], fragid[i], true );

				if( pbuf )
				{
					byte	buffer[NET_MAX_FRAGMENT];
					int	bits, size;
					sizebuf_t	temp;

					size = MSG_GetNumBitsRead( msg ) + frag_offset[i];
					bits = frag_length[i];

					// copy in data
					MSG_Clear( &pbuf->frag_message );

					MSG_StartReading( &temp, msg->pData, MSG_GetMaxBytes( msg ), size, -1 );
					MSG_ReadBits( &temp, buffer, bits );
					MSG_WriteBits( &pbuf->frag_message, buffer, bits );
				}

				// count # of incoming bufs we've queued? are we done?
				Netchan_CheckForCompletion( chan, i, intotalbuffers );
			}

			// rearrange incoming data to not have the frag stuff in the middle of it
			oldpos = MSG_GetNumBitsRead( msg );
			curbit = MSG_GetNumBitsRead( msg ) + frag_offset[i];
			numbitstoremove = frag_length[i];

			MSG_ExciseBits( msg, curbit, numbitstoremove );
			MSG_SeekToBit( msg, oldpos, SEEK_SET );

			for( j = i + 1; j < MAX_STREAMS; j++ )
				frag_offset[j] -= frag_length[i];
		}

		// is there anything left to process?
		if( MSG_GetNumBitsLeft( msg ) <= 0 )
		{
			return false;
		}
	}

	return true;
}
