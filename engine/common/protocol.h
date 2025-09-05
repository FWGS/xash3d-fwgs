/*
protocol.h - communications protocols
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

#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#define PROTOCOL_VERSION		49

// server to client
#define svc_bad			0	// immediately crash client when received
#define svc_nop			1	// does nothing
#define svc_disconnect		2	// kick client from server
#define svc_event			3	// playback event queue
#define svc_changing		4	// changelevel by server request
#define svc_setview			5	// [short] entity number
#define svc_sound			6	// <see code>
#define svc_time			7	// [float] server time
#define svc_print			8	// [byte] id [string] null terminated string
#define svc_stufftext		9	// [string] stuffed into client's console buffer
#define svc_setangle		10	// [angle angle angle] set the view angle to this absolute value
#define svc_serverdata		11	// [int] protocol ...
#define svc_lightstyle		12	// [index][pattern][float]
#define svc_updateuserinfo		13	// [byte] playernum, [string] userinfo
#define svc_deltatable		14	// [table header][...]
#define svc_clientdata		15	// [...]
#define svc_resource		16	// [...] late-precached resource will be download in-game
#define svc_pings			17	// [bit][idx][ping][packet_loss]
#define svc_particle		18	// [float*3][char*3][byte][byte]
#define svc_restoresound		19	// <see code>
#define svc_spawnstatic		20	// creates a static client entity
#define svc_event_reliable		21	// playback event directly from message, not queue
#define svc_spawnbaseline		22	// <see code>
#define svc_temp_entity		23	// <variable sized>
#define svc_setpause		24	// [byte] 0 = unpaused, 1 = paused
#define svc_signonnum		25	// [byte] used for the signon sequence
#define svc_centerprint		26	// [string] to put in center of the screen
// reserved
// reserved
// reserved
#define svc_intermission		30	// empty message (event)
#define svc_finale			31	// empty message (event)
#define svc_cdtrack			32	// [string] trackname
#define svc_restore			33	// [string] savename
#define svc_cutscene		34	// empty message (event)
#define svc_weaponanim		35	// [byte]iAnim [byte]body
#define svc_bspdecal		36	// [float*3][short][short][short]
#define svc_roomtype		37	// [short] room type
#define svc_addangle		38	// [angle] add angles when client turn on mover
#define svc_usermessage		39	// [byte][byte][string] REG_USER_MSG stuff
#define svc_packetentities		40	// [short][...]
#define svc_deltapacketentities	41	// [short][byte][...]
#define svc_choke			42	// just event
#define svc_resourcelist		43	// [short][...]
#define svc_deltamovevars		44	// [movevars_t]
#define svc_resourcerequest		45	// <see code>
#define svc_customization		46
#define svc_crosshairangle		47	// [byte][byte]
#define svc_soundfade		48	// [float*4] sound fade parms
#define svc_filetxferfailed		49	// [string]
#define svc_hltv			50	// sending from the game.dll
#define svc_director		51	// <variable sized>
#define svc_voiceinit		52	// <see code>
#define svc_voicedata		53	// [byte][short][...]
// reserved
// reserved
#define svc_resourcelocation		56	// [string]
#define svc_querycvarvalue		57	// [string]
#define svc_querycvarvalue2		58	// [string][int] (context)
#define svc_exec				59	// [byte][...]
#define svc_lastmsg			59	// start user messages at this point

// client to server
#define clc_bad			0	// immediately drop client when received
#define clc_nop			1
#define clc_move			2	// [[usercmd_t]
#define clc_stringcmd		3	// [string] message
#define clc_delta			4	// [byte] sequence number, requests delta compression of message
#define clc_resourcelist		5
// reserved
#define clc_fileconsistency		7
#define clc_voicedata		8
#define clc_requestcvarvalue		9
#define clc_requestcvarvalue2		10
#define clc_lastmsg			11	// end client messages (11 is GoldSrc message)

#define MAX_VISIBLE_PACKET_BITS	11	// 2048 visible entities per frame (hl1 has 256)
#define MAX_VISIBLE_PACKET		(1<<MAX_VISIBLE_PACKET_BITS)
#define MAX_VISIBLE_PACKET_VIS_BYTES	((MAX_VISIBLE_PACKET + 7) / 8)

// additional protocol data
#define MAX_CLIENT_BITS		5
#define MAX_CLIENTS			(1<<MAX_CLIENT_BITS)// 5 bits == 32 clients ( int32 limit )

#define MAX_WEAPON_BITS		6
#define MAX_WEAPONS			(1<<MAX_WEAPON_BITS)// 6 bits == 64 predictable weapons

#define MAX_EVENT_BITS		10
#define MAX_EVENTS			(1<<MAX_EVENT_BITS)	// 10 bits == 1024 events (the original Half-Life limit)

#define MAX_MODEL_BITS		12		// 12 bits == 4096 models
#define MAX_MODELS			(1<<MAX_MODEL_BITS)

#define MAX_SOUND_BITS		11
#define MAX_SOUNDS			(1<<MAX_SOUND_BITS)	// 11 bits == 2048 sounds
#define MAX_SOUNDS_NONSENTENCE MAX_SOUNDS

#define MAX_ENTITY_BITS		13		// 13 bits = 8192 edicts
#define MAX_EDICTS			(1<<MAX_ENTITY_BITS)
#define MAX_EDICTS_BYTES		((MAX_EDICTS + 7) / 8)
#define LAST_EDICT			(MAX_EDICTS - 1)

#define MIN_EDICTS			64

#define MAX_CUSTOM_BITS		10
#define MAX_CUSTOM			(1<<MAX_CUSTOM_BITS)// 10 bits == 1024 generic file
#define MAX_USER_MESSAGES		197		// another 58 messages reserved for engine routines
#define MAX_DLIGHTS			32		// dynamic lights (rendered per one frame)
#define MAX_ELIGHTS			128		// a1ba: increased from 64 to 128, entity only point lights
#define MAX_LIGHTSTYLES		256		// a1ba: increased from 64 to 256, protocol limit
#define MAX_RENDER_DECALS		4096		// max rendering decals per a level

// sound proto
#define MAX_SND_FLAGS_BITS		14
#define MAX_SND_CHAN_BITS		4

// sound flags
#define SND_VOLUME			(1<<0)	// a scaled byte
#define SND_ATTENUATION		(1<<1)	// a byte
#define SND_SEQUENCE		(1<<2)	// get sentence from a script
#define SND_PITCH			(1<<3)	// a byte
#define SND_SENTENCE		(1<<4)	// set if sound num is actually a sentence num
#define SND_STOP			(1<<5)	// stop the sound
#define SND_CHANGE_VOL		(1<<6)	// change sound vol
#define SND_CHANGE_PITCH		(1<<7)	// change sound pitch
#define SND_SPAWNING		(1<<8)	// we're spawning, used in some cases for ambients (not sent across network)
#define SND_LOCALSOUND		(1<<9)	// not paused, not looped, for internal use
#define SND_STOP_LOOPING		(1<<10)	// stop all looping sounds on the entity.
#define SND_FILTER_CLIENT		(1<<11)	// don't send sound from local player if prediction was enabled
#define SND_RESTORE_POSITION		(1<<12)	// passed playing position and the forced end

// decal flags
#define FDECAL_PERMANENT		0x01	// This decal should not be removed in favor of any new decals
#define FDECAL_USE_LANDMARK		0x02	// This is a decal applied on a bmodel without origin-brush so we done in absoulute pos
#define FDECAL_CUSTOM		0x04	// This is a custom clan logo and should not be saved/restored
// reserved
// reserved
#define FDECAL_DONTSAVE		0x20	// Decal was loaded from adjacent level, don't save it for this level
#define FDECAL_STUDIO		0x40	// Indicates a studio decal
#define FDECAL_LOCAL_SPACE		0x80	// decal is in local space (any decal after serialization)

// game type
#define GAME_SINGLEPLAYER		0
#define GAME_DEATHMATCH		1
#define GAME_COOP			2
#define GAME_TEAMPLAY		4

// Max number of history commands to send ( 2 by default ) in case of dropped packets
#define NUM_BACKUP_COMMAND_BITS 4
#define MAX_BACKUP_COMMANDS     BIT( NUM_BACKUP_COMMAND_BITS )
#define MAX_TOTAL_CMDS          32

#define MAX_RESOURCES		(MAX_MODELS+MAX_SOUNDS+MAX_CUSTOM+MAX_EVENTS)
#define MAX_RESOURCE_BITS		13	// 13 bits 8192 resource (4096 models + 2048 sounds + 1024 events + 1024 files)
#define	FRAGMENT_MIN_SIZE			508		// RFC 791: 576(min ip packet) - 60 (ip header) - 8 (udp header)
#define FRAGMENT_DEFAULT_SIZE		1200		// default MTU
#define FRAGMENT_MAX_SIZE		64000		// maximal fragment size
#define FRAGMENT_LOCAL_SIZE		FRAGMENT_MAX_SIZE	// local connection

#if XASH_LOW_MEMORY == 2
#undef MAX_VISIBLE_PACKET
#undef MAX_VISIBLE_PACKET_VIS_BYTES
#undef MAX_EVENTS
#undef MAX_MODELS
#undef MAX_SOUNDS
#undef MAX_CUSTOM
#undef MAX_DLIGHTS
#undef MAX_ELIGHTS
#undef MAX_RENDER_DECALS
#undef MAX_RESOURCES
// memory reduced protocol, not for use in multiplayer (but still compatible)
#define MAX_VISIBLE_PACKET		128
#define MAX_VISIBLE_PACKET_VIS_BYTES	((MAX_VISIBLE_PACKET + 7) / 8)
#define MAX_EVENTS			128
#define MAX_MODELS			512
#define MAX_SOUNDS			512
#define MAX_CUSTOM			32
#define MAX_DLIGHTS			16		// dynamic lights (rendered per one frame)
#define MAX_ELIGHTS			32		// entity only point lights
#define MAX_RENDER_DECALS		64		// max rendering decals per a level
#define MAX_RESOURCES		1024
#elif XASH_LOW_MEMORY == 1
#undef MAX_VISIBLE_PACKET
#undef MAX_VISIBLE_PACKET_VIS_BYTES
#undef MAX_EVENTS
#undef MAX_MODELS
#undef MAX_CUSTOM
#undef MAX_RENDER_DECALS
#undef MAX_RESOURCES
#define MAX_VISIBLE_PACKET		256
#define MAX_VISIBLE_PACKET_VIS_BYTES	((MAX_VISIBLE_PACKET + 7) / 8)
#define MAX_EVENTS			128
#define MAX_MODELS			1024
#define MAX_CUSTOM			512
#define MAX_RENDER_DECALS	128
#define MAX_RESOURCES		1024
#endif

// Quake1 Protocol
#define PROTOCOL_VERSION_QUAKE	15

// listed only unmatched ops
#define svc_updatestat		3	// [byte] [int]			(svc_event)
#define svc_version			4	// [int] server version		(svc_changing)
#define svc_updatename		13	// [byte] [string]			(svc_updateuserinfo)
#define svc_updatefrags		14	// [byte] [short]			(svc_deltatable)
#define svc_stopsound		16	// <see code>			(svc_resource)
#define svc_updatecolors		17	// [byte] [byte]			(svc_pings)
#define svc_damage			19	//				(svc_restoresound)
#define svc_spawnbinary		21	//				(svc_event_reliable)
#define svc_killedmonster		27
#define svc_foundsecret		28
#define svc_spawnstaticsound		29	// [coord3] [byte] samp [byte] vol [byte] aten
#define svc_sellscreen		33	//				(svc_restore)
// Nehahra added
#define svc_showlmp			35	// [string] slotname [string] lmpfilename [coord] x [coord] y
#define svc_hidelmp			36	// [string] slotname
#define svc_skybox			37	// [string] skyname
#define svc_skyboxsize		50	// [coord] size (default is 4096)
#define svc_fog			51	// [byte] enable <optional past this point, only included if enable is true>
					// [float] density [byte] red [byte] green [byte] blue

// if the high bit of the servercmd is set, the low bits are fast update flags:
#define U_MOREBITS		(1<<0)
#define U_ORIGIN1		(1<<1)
#define U_ORIGIN2		(1<<2)
#define U_ORIGIN3		(1<<3)
#define U_ANGLE2		(1<<4)
#define U_NOLERP		(1<<5)		// don't interpolate movement
#define U_FRAME		(1<<6)
#define U_SIGNAL		(1<<7)		// just differentiates from other updates

// svc_update can pass all of the fast update bits, plus more
#define U_ANGLE1		(1<<8)
#define U_ANGLE3		(1<<9)
#define U_MODEL		(1<<10)
#define U_COLORMAP		(1<<11)
#define U_SKIN		(1<<12)
#define U_EFFECTS		(1<<13)
#define U_LONGENTITY	(1<<14)
#define U_TRANS		(1<<15)		// nehahra

// clientdata flags
#define SU_VIEWHEIGHT	(1<<0)
#define SU_IDEALPITCH	(1<<1)
#define SU_PUNCH1		(1<<2)
#define SU_PUNCH2		(1<<3)
#define SU_PUNCH3		(1<<4)
#define SU_VELOCITY1	(1<<5)
#define SU_VELOCITY2	(1<<6)
#define SU_VELOCITY3	(1<<7)
//define	SU_AIMENT		(1<<8)  AVAILABLE BIT
#define SU_ITEMS		(1<<9)
#define SU_ONGROUND		(1<<10)		// no data follows, the bit is it
#define SU_INWATER		(1<<11)		// no data follows, the bit is it
#define SU_WEAPONFRAME	(1<<12)
#define SU_ARMOR		(1<<13)
#define SU_WEAPON		(1<<14)

extern const char *const svc_strings[svc_lastmsg+1];
extern const char *const svc_legacy_strings[svc_lastmsg+1];
extern const char *const svc_quake_strings[svc_lastmsg+1];
extern const char *const svc_goldsrc_strings[svc_lastmsg+1];

// FWGS extensions
#define NET_EXT_SPLITSIZE (1U<<0) // set splitsize by cl_dlmax

// legacy protocol definitons
#define PROTOCOL_LEGACY_VERSION		48
#define svc_legacy_modelindex		31	// [index][modelpath]
#define svc_legacy_soundindex		28	// [index][soundpath]
#define svc_legacy_eventindex		34	// [index][eventname]
#define svc_legacy_ambientsound		29
#define svc_legacy_chokecount 42		// old client specified count, new just sends svc_choke
#define svc_legacy_event			27	// playback event queue
#define svc_legacy_changing			3	// changelevel by server request

#define clc_legacy_userinfo		6	// [[userinfo string]

#define SND_LEGACY_LARGE_INDEX		(1<<2)	// a send sound as short
#define MAX_LEGACY_ENTITY_BITS		12
#define MAX_LEGACY_WEAPON_BITS		5
#define MAX_LEGACY_MODEL_BITS  11
#define MAX_LEGACY_TOTAL_CMDS  16 // 28 - 16 = 12 real legacy max backup
#define MAX_LEGACY_BACKUP_CMDS 12

#define MAX_LEGACY_EDICTS (1 << MAX_LEGACY_ENTITY_BITS) // 4096 edicts
#define MIN_LEGACY_EDICTS 30

// legacy engine features that can be implemented through currently supported features
#define ENGINE_LEGACY_FEATURES_MASK   \
	( ENGINE_WRITE_LARGE_COORD    \
	| ENGINE_LOAD_DELUXEDATA      \
	| ENGINE_LARGE_LIGHTMAPS      \
	| ENGINE_COMPENSATE_QUAKE_BUG \
	| ENGINE_COMPUTE_STUDIO_LERP  )

// Master Server protocol
#define MS_SCAN_REQUEST "1\xFF" "0.0.0.0:0\0" // TODO: implement IP filter

// GoldSrc protocol definitions
#define PROTOCOL_GOLDSRC_VERSION 48

#define svc_goldsrc_version           svc_changing
#define svc_goldsrc_stopsound         svc_resource
#define svc_goldsrc_damage            svc_restoresound
#define svc_goldsrc_killedmonster     27
#define svc_goldsrc_foundsecret       28
#define svc_goldsrc_spawnstaticsound  29
#define svc_goldsrc_decalname         svc_bspdecal
#define svc_goldsrc_sendextrainfo     54
#define svc_goldsrc_timescale         55

#define clc_goldsrc_hltv              clc_requestcvarvalue  // 9
#define clc_goldsrc_requestcvarvalue  clc_requestcvarvalue2 // 10
#define clc_goldsrc_requestcvarvalue2 11
#define clc_goldsrc_lastmsg           11

#define MAX_GOLDSRC_BACKUP_CMDS   8
#define MAX_GOLDSRC_TOTAL_CMDS    16
#define MAX_GOLDSRC_EXTENDED_TOTAL_CMDS 62
#define MAX_GOLDSRC_MODEL_BITS    10
#define MAX_GOLDSRC_RESOURCE_BITS 12
#define MAX_GOLDSRC_ENTITY_BITS   11
// #define MAX_GOLDSRC_EDICTS        BIT( MAX_ENTITY_BITS )
#define MAX_GOLDSRC_EDICTS        ( BIT( MAX_ENTITY_BITS ) + ( MAX_CLIENTS * 15 ))
#define LAST_GOLDSRC_EDICT        ( BIT( MAX_ENTITY_BITS ) - 1 )


// from any to any (must be handled on both server and client)

#define A2A_PING         "ping" // reply with A2A_ACK
#define A2A_ACK          "ack" // no-op
#define A2A_INFO         "info" // different format for client and server, see code
#define A2A_NETINFO      "netinfo" // different format for client and server, see code
#define A2A_GOLDSRC_PING "i" // reply with A2A_GOLDSRC_ACK
#define A2A_GOLDSRC_ACK  "j" // no-op

// from any to server
#define A2S_GOLDSRC_INFO    "TSource Engine Query"
#define A2S_GOLDSRC_RULES   'V'
#define A2S_GOLDSRC_PLAYERS 'U'

// from server to any
#define S2A_GOLDSRC_INFO    'I'
#define S2A_GOLDSRC_RULES   'E'
#define S2A_GOLDSRC_PLAYERS 'D'

// from master to server
#define M2S_CHALLENGE     "s"
#define M2S_NAT_CONNECT   "c"

// from server to master
#define S2M_INFO          "0\n"

// from client to server
#define C2S_BANDWIDTHTEST "bandwidth"
#define C2S_GETCHALLENGE  "getchallenge"
#define C2S_CONNECT       "connect"
#define C2S_RCON          "rcon"

// from server to client
#define S2C_BANDWIDTHTEST              "testpacket"
#define S2C_CHALLENGE                  "challenge"
#define S2C_CONNECTION                 "client_connect"
#define S2C_ERRORMSG                   "errormsg"
#define S2C_REJECT                     "disconnect"
#define S2C_GOLDSRC_REJECT_BADPASSWORD '8'
#define S2C_GOLDSRC_REJECT             '9'
#define S2C_GOLDSRC_CHALLENGE          "A00000000"
#define S2C_GOLDSRC_CONNECTION         "B"

// from any to client
#define A2C_PRINT           "print"
#define A2C_GOLDSRC_PRINT   'l'

// from master to client
#define M2A_SERVERSLIST "f"

#endif//NET_PROTOCOL_H
