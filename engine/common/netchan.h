/*
netchan.h - net channel abstraction layer
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

#ifndef NET_MSG_H
#define NET_MSG_H

/*
==========================================================

  ELEMENTS COMMUNICATED ACROSS THE NET

==========================================================
*/
#include "crtlib.h"
#include "net_buffer.h"

// 0 == regular, 1 == file stream
#define MAX_STREAMS			2

// flow control bytes per second limits
#define MAX_RATE			100000.0f
#define MIN_RATE			1000.0f

// default data rate
#define DEFAULT_RATE		(9999.0f)

// NETWORKING INFO

// This is the packet payload without any header bytes (which are attached for actual sending)
#define NET_MAX_PAYLOAD		MAX_INIT_MSG

// Theoretically maximum size of UDP-packet without header and hardware-specific data
#define NET_MAX_FRAGMENT		65535

// because encoded as highpart of uint32
#define NET_MAX_BUFFER_ID		32767

// because encoded as lowpart of uint32
#define NET_MAX_BUFFERS_COUNT		32767

// This is the payload plus any header info (excluding UDP header)

// Packet header is:
//  4 bytes of outgoing seq
//  4 bytes of incoming seq
//  and for each stream
// {
//  byte (on/off)
//  int (fragment id)
//  int (startpos)
//  int (length)
// }
#define HEADER_BYTES		( 8 + MAX_STREAMS * 13 )

// Pad this to next higher 16 byte boundary
// This is the largest packet that can come in/out over the wire, before processing the header
//  bytes will be stripped by the networking channel layer
#define NET_MAX_MESSAGE		PAD_NUMBER(( NET_MAX_PAYLOAD + HEADER_BYTES ), 16 )

#define MS_SCAN_REQUEST		"1\xFF" "0.0.0.0:0\0"

#define PORT_MASTER			27010
#define PORT_SERVER			27015

#define MULTIPLAYER_BACKUP		64	// how many data slots to use when in multiplayer (must be power of 2)
#define SINGLEPLAYER_BACKUP		16	// same for single player
#define CMD_BACKUP			64	// allow a lot of command backups for very fast systems
#define CMD_MASK			(CMD_BACKUP - 1)
#define NUM_PACKET_ENTITIES		256	// 170 Mb for multiplayer with 32 players
#define MAX_CUSTOM_BASELINES		64
#define NET_LEGACY_EXT_SPLIT		(1U<<1)
#define NETSPLIT_BACKUP 8
#define NETSPLIT_BACKUP_MASK (NETSPLIT_BACKUP - 1)
#define NETSPLIT_HEADER_SIZE 18

#if XASH_LOW_MEMORY == 2
	#undef MULTIPLAYER_BACKUP
	#undef SINGLEPLAYER_BACKUP
	#undef NUM_PACKET_ENTITIES
	#undef MAX_CUSTOM_BASELINES
	#undef NET_MAX_FRAGMENT
	#define MULTIPLAYER_BACKUP		4	// breaks protocol in legacy mode, new protocol status unknown
	#define SINGLEPLAYER_BACKUP		4
	#define NUM_PACKET_ENTITIES		32
	#define MAX_CUSTOM_BASELINES		8
	#define NET_MAX_FRAGMENT		32768
#elif XASH_LOW_MEMORY == 1
	#undef SINGLEPLAYER_BACKUP
	#undef NUM_PACKET_ENTITIES
	#undef MAX_CUSTOM_BASELINES
	#undef NET_MAX_FRAGMENT
	#define SINGLEPLAYER_BACKUP		4
	#define NUM_PACKET_ENTITIES		64
	#define MAX_CUSTOM_BASELINES		8
	#define NET_MAX_FRAGMENT		32768
#endif

typedef struct netsplit_chain_packet_s
{
	// bool vector
	uint32_t recieved_v[8];
	// serial number
	uint32_t id;
	byte data[NET_MAX_PAYLOAD];
	byte received;
	byte count;
} netsplit_chain_packet_t;

// raw packet format
typedef struct netsplit_packet_s
{
	uint32_t signature; // 0xFFFFFFFE
	uint32_t length;
	uint32_t part;
	uint32_t id;
	// max 256 parts
	byte count;
	byte index;
	byte data[NET_MAX_PAYLOAD - NETSPLIT_HEADER_SIZE];
} netsplit_packet_t;


typedef struct netsplit_s
{
	netsplit_chain_packet_t packets[NETSPLIT_BACKUP];
	uint64_t total_received;
	uint64_t total_received_uncompressed;
} netsplit_t;

// packet splitting
qboolean NetSplit_GetLong( netsplit_t *ns, netadr_t *from, byte *data, size_t *length );


/*
==============================================================

NET

==============================================================
*/
#define MAX_FLOWS			2

#define FLOW_OUTGOING		0
#define FLOW_INCOMING		1
#define MAX_LATENT			32
#define MASK_LATENT			( MAX_LATENT - 1 )

#define FRAG_NORMAL_STREAM		0
#define FRAG_FILE_STREAM		1

// message data
typedef struct
{
	int		size;		// size of message sent/received
	double		time;		// time that message was sent/received
} flowstats_t;

typedef struct
{
	flowstats_t	stats[MAX_LATENT];	// data for last MAX_LATENT messages
	int		current;		// current message position
	double		nextcompute; 	// time when we should recompute k/sec data
	float		kbytespersec;	// average data
	float		avgkbytespersec;
	int		totalbytes;
} flow_t;

// generic fragment structure
typedef struct fragbuf_s
{
	struct fragbuf_s	*next;				// next buffer in chain
	int		bufferid;				// id of this buffer
	sizebuf_t		frag_message;			// message buffer where raw data is stored
	qboolean		isfile;				// is this a file buffer?
	qboolean		isbuffer;				// is this file buffer from memory ( custom decal, etc. ).
	qboolean		iscompressed;			// is compressed file, we should using filename.ztmp
	char		filename[MAX_OSPATH];		// name of the file to save out on remote host
	int		foffset;				// offset in file from which to read data
	int		size;				// size of data to read at that offset
	byte frag_message_buf[]; // the actual data sits here (flexible)
} fragbuf_t;

// Waiting list of fragbuf chains
typedef struct fbufqueue_s
{
	struct fbufqueue_s	*next;		// next chain in waiting list
	int		fragbufcount;	// number of buffers in this chain
	fragbuf_t		*fragbufs;	// the actual buffers
} fragbufwaiting_t;

typedef enum fragsize_e
{
	FRAGSIZE_FRAG,
	FRAGSIZE_SPLIT,
	FRAGSIZE_UNRELIABLE
} fragsize_t;

typedef enum netchan_flags_e
{
	NETCHAN_USE_LEGACY_SPLIT = BIT( 0 ),
	NETCHAN_USE_MUNGE = BIT( 1 ),
	NETCHAN_USE_BZIP2 = BIT( 2 ),
	NETCHAN_GOLDSRC = BIT( 3 ),
	NETCHAN_USE_LZSS = BIT( 4 ), // mutually exclusive with bzip2
} netchan_flags_t;

// Network Connection Channel
typedef struct netchan_s
{
	netsrc_t		sock;		// NS_SERVER or NS_CLIENT, depending on channel.
	netadr_t		remote_address;	// address this channel is talking to.
	int		qport;		// qport value to write when transmitting

	double		last_received;	// for timeouts
	double		connect_time;	// Usage: host.realtime - netchan.connect_time
	double		rate;		// bandwidth choke. bytes per second
	double		cleartime;	// if realtime > cleartime, free to send next packet

	// Sequencing variables
	unsigned int		incoming_sequence;			// increasing count of sequence numbers
	unsigned int		incoming_acknowledged;		// # of last outgoing message that has been ack'd.
	unsigned int		incoming_reliable_acknowledged;	// toggles T/F as reliable messages are received.
	unsigned int		incoming_reliable_sequence;		// single bit, maintained local
	unsigned int		outgoing_sequence;			// message we are sending to remote
	unsigned int		reliable_sequence;			// whether the message contains reliable payload, single bit
	unsigned int		last_reliable_sequence;		// outgoing sequence number of last send that had reliable data

	// callback to get actual framgment size
	void		*client;
	int (*pfnBlockSize)( void *cl, fragsize_t mode );

	// staging and holding areas
	sizebuf_t		message;
	byte		message_buf[NET_MAX_MESSAGE];

	// reliable message buffer.
	// we keep adding to it until reliable is acknowledged.  Then we clear it.
	int		reliable_length;
	byte		reliable_buf[NET_MAX_MESSAGE];	// unacked reliable message (max size for loopback connection)

	// Waiting list of buffered fragments to go onto queue.
	// Multiple outgoing buffers can be queued in succession
	fragbufwaiting_t	*waitlist[MAX_STREAMS];

	int		reliable_fragment[MAX_STREAMS];	// is reliable waiting buf a fragment?
	uint		reliable_fragid[MAX_STREAMS];		// buffer id for each waiting fragment

	fragbuf_t		*fragbufs[MAX_STREAMS];	// the current fragment being set
	int		fragbufcount[MAX_STREAMS];	// the total number of fragments in this stream

	int		frag_startpos[MAX_STREAMS];	// position in outgoing buffer where frag data starts
	int		frag_length[MAX_STREAMS];	// length of frag data in the buffer

	fragbuf_t		*incomingbufs[MAX_STREAMS];	// incoming fragments are stored here
	qboolean		incomingready[MAX_STREAMS];	// set to true when incoming data is ready

	// Only referenced by the FRAG_FILE_STREAM component
	char		incomingfilename[MAX_OSPATH];	// Name of file being downloaded

	void		*tempbuffer;		// download file buffer
	int		tempbuffersize;		// current size

	// incoming and outgoing flow metrics
	flow_t		flow[MAX_FLOWS];

	// added for net_speeds
	size_t		total_sended;
	size_t		total_received;
	unsigned int	maxpacket;
	unsigned int	splitid;
	netsplit_t	netsplit;

	qboolean	split;
	qboolean	use_munge;
	qboolean	use_bz2;
	qboolean	use_lzss;
	qboolean	gs_netchan;
} netchan_t;

extern netadr_t		net_from;
extern sizebuf_t		net_message;
extern byte		net_message_buffer[NET_MAX_MESSAGE];
extern convar_t		sv_lan;
extern convar_t		sv_lan_rate;
extern int		net_drop;

void Netchan_Init( void );
void Netchan_Shutdown( void );
void Netchan_Setup( netsrc_t sock, netchan_t *chan, netadr_t adr, int qport, void *client, int (*pfnBlockSize)(void *, fragsize_t mode ), uint flags );
void Netchan_CreateFileFragmentsFromBuffer( netchan_t *chan, const char *filename, byte *pbuf, int size );
qboolean Netchan_CopyNormalFragments( netchan_t *chan, sizebuf_t *msg, size_t *length );
qboolean Netchan_CopyFileFragments( netchan_t *chan, sizebuf_t *msg );
void Netchan_CreateFragments( netchan_t *chan, sizebuf_t *msg );
int Netchan_CreateFileFragments( netchan_t *chan, const char *filename );
void Netchan_TransmitBits( netchan_t *chan, int lengthInBits, const byte *data );
void Netchan_OutOfBand( int net_socket, netadr_t adr, int length, const byte *data );
void Netchan_OutOfBandPrint( int net_socket, netadr_t adr, const char *format, ... ) FORMAT_CHECK( 3 );
qboolean Netchan_Process( netchan_t *chan, sizebuf_t *msg );
void Netchan_UpdateProgress( netchan_t *chan );
qboolean Netchan_IncomingReady( netchan_t *chan );
qboolean Netchan_CanPacket( netchan_t *chan, qboolean choke );
qboolean Netchan_IsLocal( netchan_t *chan );
void Netchan_ReportFlow( netchan_t *chan );
void Netchan_FragSend( netchan_t *chan );
void Netchan_Clear( netchan_t *chan );

#endif//NET_MSG_H
