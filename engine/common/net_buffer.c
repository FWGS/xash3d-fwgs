/*
net_buffer.c - network bitbuffer io functions
Copyright (C) 2010 Uncle Mike

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
#include "protocol.h"
#include "net_buffer.h"
#include "xash3d_mathlib.h"
//#define DEBUG_NET_MESSAGES_SEND
//#define DEBUG_NET_MESSAGES_READ

// precalculated bit masks for WriteUBitLong.
// Using these tables instead of doing the calculations
// gives a 33% speedup in WriteUBitLong.
static uint32_t	BitWriteMasks[32][33];
static uint32_t	ExtraMasks[32];
const char *svc_strings[svc_lastmsg+1] =
{
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_event",
	"svc_changing",
	"svc_setview",
	"svc_sound",
	"svc_time",
	"svc_print",
	"svc_stufftext",
	"svc_setangle",
	"svc_serverdata",
	"svc_lightstyle",
	"svc_updateuserinfo",
	"svc_deltatable",
	"svc_clientdata",
	"svc_resource",
	"svc_pings",
	"svc_particle",
	"svc_restoresound",
	"svc_spawnstatic",
	"svc_event_reliable",
	"svc_spawnbaseline",
	"svc_temp_entity",
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_unused27",
	"svc_unused28",
	"svc_unused29",
	"svc_intermission",
	"svc_finale",
	"svc_cdtrack",
	"svc_restore",
	"svc_cutscene",
	"svc_weaponanim",
	"svc_bspdecal",
	"svc_roomtype",
	"svc_addangle",
	"svc_usermessage",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_choke",
	"svc_resourcelist",
	"svc_deltamovevars",
	"svc_resourcerequest",
	"svc_customization",
	"svc_crosshairangle",
	"svc_soundfade",
	"svc_filetxferfailed",
	"svc_hltv",
	"svc_director",
	"svc_voiceinit",
	"svc_voicedata",
	"svc_deltapacketbones",
	"svc_unused55",
	"svc_resourcelocation",
	"svc_querycvarvalue",
	"svc_querycvarvalue2",
	"svc_exec",
};

const char *svc_legacy_strings[svc_lastmsg+1] =
{
	[svc_legacy_changing] = "svc_legacy_changing",
	[svc_legacy_ambientsound] = "svc_legacy_ambientsound",
	[svc_legacy_soundindex] = "svc_legacy_soundindex",
	[svc_legacy_ambientsound] = "svc_legacy_ambientsound",
	[svc_legacy_modelindex] = "svc_legacy_modelindex",
	[svc_legacy_eventindex] = "svc_legacy_eventindex",
	[svc_legacy_chokecount] = "svc_legacy_chokecount",
};

const char *svc_goldsrc_strings[svc_lastmsg+1] =
{
	[svc_goldsrc_version] = "svc_goldsrc_version",
	[svc_goldsrc_serverinfo] = "svc_goldsrc_serverinfo",
	[svc_goldsrc_deltadescription] = "svc_goldsrc_deltadescription",
	[svc_goldsrc_stopsound] = "svc_goldsrc_stopsound",
	[svc_goldsrc_damage] = "svc_goldsrc_damage",
	[svc_goldsrc_killedmonster] = "svc_goldsrc_killedmonster",
	[svc_goldsrc_foundsecret] = "svc_goldsrc_foundsecret",
	[svc_goldsrc_spawnstaticsound] = "svc_goldsrc_spawnstaticsound",
	[svc_goldsrc_decalname] = "svc_goldsrc_decalname",
	[svc_goldsrc_newusermsg] = "svc_goldsrc_newusermsg",
	[svc_goldsrc_newmovevars] = "svc_goldsrc_newmovevars",
	[svc_goldsrc_sendextrainfo] = "svc_goldsrc_sendextrainfo",
	[svc_goldsrc_timescale] = "svc_goldsrc_timescale",
	[svc_goldsrc_sendcvarvalue] = "svc_goldsrc_sendcvarvalue",
	[svc_goldsrc_sendcvarvalue2] = "svc_goldsrc_sendcvarvalue2",
};

const char *svc_quake_strings[svc_lastmsg+1] =
{
	[svc_updatestat] = "svc_quake_updatestat",
	[svc_version] = "svc_quake_version",
	[svc_updatename] = "svc_quake_updatename",
	[svc_updatefrags] = "svc_quake_updatefrags",
	[svc_stopsound] = "svc_quake_stopsound",
	[svc_updatecolors] = "svc_quake_updatecolors",
	[svc_damage] = "svc_quake_damage",
	[svc_spawnbinary] = "svc_quake_spawnbinary",
	[svc_killedmonster] = "svc_quake_killedmonster",
	[svc_foundsecret] = "svc_quake_foundsecret",
	[svc_spawnstaticsound] = "svc_quake_spawnstaticsound",
	[svc_sellscreen] = "svc_quake_sellscreen",
	[svc_showlmp] = "svc_quake_showlmp",
	[svc_hidelmp] = "svc_quake_hidelmp",
	[svc_skybox] = "svc_quake_skybox",
	[svc_skyboxsize] = "svc_quake_skyboxsize",
	[svc_fog] = "svc_quake_fog",
};

void MSG_InitMasks( void )
{
	uint	startbit, endbit;
	uint	maskBit, nBitsLeft;

	for( startbit = 0; startbit < 32; startbit++ )
	{
		for( nBitsLeft = 0; nBitsLeft < 33; nBitsLeft++ )
		{
			endbit = startbit + nBitsLeft;

			BitWriteMasks[startbit][nBitsLeft] = (uint)BIT( startbit ) - 1;
			if( endbit < 32 ) BitWriteMasks[startbit][nBitsLeft] |= ~((uint)BIT( endbit ) - 1 );
		}
	}

	for( maskBit = 0; maskBit < 32; maskBit++ )
		ExtraMasks[maskBit] = (uint)BIT( maskBit ) - 1;
}

void MSG_WriteOneBit( sizebuf_t *sb, int nValue )
{
	if( !MSG_Overflow( sb, 1 ))
	{
		if( nValue ) sb->pData[sb->iCurBit>>3] |= BIT( sb->iCurBit & 7 );
		else sb->pData[sb->iCurBit>>3] &= ~BIT( sb->iCurBit & 7 );

		sb->iCurBit++;
	}
}

void MSG_WriteUBitLong( sizebuf_t *sb, uint curData, int numbits )
{
	Assert( numbits >= 0 && numbits <= 32 );

	// bounds checking..
	if(( sb->iCurBit + numbits ) > sb->nDataBits )
	{
		sb->bOverflow = true;
		sb->iCurBit = sb->nDataBits;
	}
	else
	{
		int	nBitsLeft = numbits;
		int	iCurBit = sb->iCurBit;
		uint	iDWord = iCurBit >> 5;	// Mask in a dword.
		uint32_t	iCurBitMasked;
		int	nBitsWritten;

		Assert(( iDWord * 4 + sizeof( int )) <= (uint)MSG_GetMaxBytes( sb ));

		iCurBitMasked = iCurBit & 31;
		((uint32_t *)sb->pData)[iDWord] &= BitWriteMasks[iCurBitMasked][nBitsLeft];
		((uint32_t *)sb->pData)[iDWord] |= curData << iCurBitMasked;

		// did it span a dword?
		nBitsWritten = 32 - iCurBitMasked;

		if( nBitsWritten < nBitsLeft )
		{
			nBitsLeft -= nBitsWritten;
			iCurBit += nBitsWritten;
			curData >>= nBitsWritten;

			iCurBitMasked = iCurBit & 31;
			((uint32_t *)sb->pData)[iDWord+1] &= BitWriteMasks[iCurBitMasked][nBitsLeft];
			((uint32_t *)sb->pData)[iDWord+1] |= curData << iCurBitMasked;
		}
		sb->iCurBit += numbits;
	}
}

/*
=======================
MSG_WriteSBitLong

sign bit comes first
=======================
*/
void MSG_WriteSBitLong( sizebuf_t *sb, int data, int numbits )
{
	// do we have a valid # of bits to encode with?
	Assert( numbits >= 1 && numbits <= 32 );

	// NOTE: it does this wierdness here so it's bit-compatible with regular integer data in the buffer.
	// (Some old code writes direct integers right into the buffer).
	if( data < 0 )
	{
		MSG_WriteUBitLong( sb, (uint)( 0x80000000 + data ), numbits - 1 );
		MSG_WriteOneBit( sb, 1 );
	}
	else
	{
		MSG_WriteUBitLong( sb, (uint)data, numbits - 1 );
		MSG_WriteOneBit( sb, 0 );
	}
}

void MSG_WriteBitLong( sizebuf_t *sb, uint data, int numbits, qboolean bSigned )
{
	if( bSigned )
		MSG_WriteSBitLong( sb, (int)data, numbits );
	else MSG_WriteUBitLong( sb, data, numbits );
}

qboolean MSG_WriteBits( sizebuf_t *sb, const void *pData, int nBits )
{
	byte	*pOut = (byte *)pData;
	int	nBitsLeft = nBits;

	// get output dword-aligned.
	while((( uint32_t )pOut & 3 ) != 0 && nBitsLeft >= 8 )
	{
		MSG_WriteUBitLong( sb, *pOut, 8 );

		nBitsLeft -= 8;
		++pOut;
	}

	// read dwords.
	while( nBitsLeft >= 32 )
	{
		MSG_WriteUBitLong( sb, *(( uint32_t *)pOut ), 32 );

		pOut += sizeof( uint32_t );
		nBitsLeft -= 32;
	}

	// read the remaining bytes.
	while( nBitsLeft >= 8 )
	{
		MSG_WriteUBitLong( sb, *pOut, 8 );

		nBitsLeft -= 8;
		++pOut;
	}

	// Read the remaining bits.
	if( nBitsLeft )
	{
		MSG_WriteUBitLong( sb, *pOut, nBitsLeft );
	}

	return !sb->bOverflow;
}

void MSG_WriteBitAngle( sizebuf_t *sb, float fAngle, int numbits )
{
	uint	mask, shift;
	int	d;

	// clamp the angle before receiving
	fAngle = fmod( fAngle, 360.0f );
	if( fAngle < 0 ) fAngle += 360.0f;

	shift = ( 1 << numbits );
	mask = shift - 1;

	d = (int)(( fAngle * shift ) / 360.0f );
	d &= mask;

	MSG_WriteUBitLong( sb, (uint)d, numbits );
}

void MSG_WriteCoord( sizebuf_t *sb, float val )
{
	// g-cont. we loose precision here but keep old size of coord variable!
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
		MSG_WriteShort( sb, Q_rint( val ));
	else MSG_WriteShort( sb, (int)( val * 8.0f ));
}

void MSG_WriteVec3Coord( sizebuf_t *sb, const float *fa )
{
	MSG_WriteCoord( sb, fa[0] );
	MSG_WriteCoord( sb, fa[1] );
	MSG_WriteCoord( sb, fa[2] );
}

void MSG_WriteVec3Angles( sizebuf_t *sb, const float *fa )
{
	MSG_WriteBitAngle( sb, fa[0], 16 );
	MSG_WriteBitAngle( sb, fa[1], 16 );
	MSG_WriteBitAngle( sb, fa[2], 16 );
}

void MSG_WriteCmdExt( sizebuf_t *sb, int cmd, netsrc_t type, const char *name )
{
#ifdef DEBUG_NET_MESSAGES_SEND
	if( name != NULL )
	{
		// get custom name
		Con_Printf( "^1sv^7 write: %s\n", name );
	}
	else if( type == NS_SERVER )
	{
		if( cmd >= 0 && cmd <= svc_lastmsg )
		{
			// get engine message name
			Con_Printf( "^1sv^7 write: %s\n", svc_strings[cmd] );
		}
	}
	else if( type == NS_CLIENT )
	{
		if( cmd >= 0 && cmd <= clc_lastmsg )
		{
			Con_Printf( "^1cl^7 write: %s\n", clc_strings[cmd] );
		}
	}
#endif
	MSG_WriteUBitLong( sb, cmd, sizeof( uint8_t ) << 3 );
}

void MSG_WriteChar( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int8_t ) << 3 );
}

void MSG_WriteByte( sizebuf_t *sb, int val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint8_t ) << 3 );
}

void MSG_WriteShort( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int16_t ) << 3 );
}

void MSG_WriteWord( sizebuf_t *sb, int val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint16_t ) << 3 );
}

void MSG_WriteLong( sizebuf_t *sb, int val )
{
	MSG_WriteSBitLong( sb, val, sizeof( int32_t ) << 3 );
}

void MSG_WriteDword( sizebuf_t *sb, uint val )
{
	MSG_WriteUBitLong( sb, val, sizeof( uint32_t ) << 3 );
}

void MSG_WriteFloat( sizebuf_t *sb, float val )
{
	MSG_WriteBits( sb, &val, sizeof( val ) << 3 );
}

qboolean MSG_WriteBytes( sizebuf_t *sb, const void *pBuf, int nBytes )
{
	return MSG_WriteBits( sb, pBuf, nBytes << 3 );
}

qboolean MSG_WriteString( sizebuf_t *sb, const char *pStr )
{
	if( pStr )
	{
		do
		{
			MSG_WriteChar( sb, (signed char)*pStr );
			pStr++;
		} while( *( pStr - 1 ));
	}
	else MSG_WriteChar( sb, 0 );

	return !sb->bOverflow;
}

qboolean MSG_WriteStringf( sizebuf_t *sb, const char *format, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, format );
	Q_vsnprintf( buf, sizeof( buf ), format, va );
	va_end( va );

	return MSG_WriteString( sb, buf );
}

int MSG_ReadOneBit( sizebuf_t *sb )
{
	if( !MSG_Overflow( sb, 1 ))
	{
		int value = sb->pData[sb->iCurBit >> 3] & (1 << ( sb->iCurBit & 7 ));
		sb->iCurBit++;
		return !!value;
	}
	return 0;
}

uint MSG_ReadUBitLong( sizebuf_t *sb, int numbits )
{
	int	idword1;
	uint	dword1, ret;

	if( numbits == 8 )
	{
		int leftBits = MSG_GetNumBitsLeft( sb );

		if( leftBits >= 0 && leftBits < 8 )
			return 0;	// end of message
	}

	if(( sb->iCurBit + numbits ) > sb->nDataBits )
	{
		sb->bOverflow = true;
		sb->iCurBit = sb->nDataBits;
		return 0;
	}

	Assert( numbits > 0 && numbits <= 32 );

	// Read the current dword.
	idword1 = sb->iCurBit >> 5;
	dword1 = ((uint *)sb->pData)[idword1];
	dword1 >>= ( sb->iCurBit & 31 );	// get the bits we're interested in.

	sb->iCurBit += numbits;
	ret = dword1;

	// Does it span this dword?
	if(( sb->iCurBit - 1 ) >> 5 == idword1 )
	{
		if( numbits != 32 )
			ret &= ExtraMasks[numbits];
	}
	else
	{
		int	nExtraBits = sb->iCurBit & 31;
		uint	dword2 = ((uint *)sb->pData)[idword1+1] & ExtraMasks[nExtraBits];

		// no need to mask since we hit the end of the dword.
		// shift the second dword's part into the high bits.
		ret |= (dword2 << ( numbits - nExtraBits ));
	}
	return ret;
}

qboolean MSG_ReadBits( sizebuf_t *sb, void *pOutData, int nBits )
{
	byte	*pOut = (byte *)pOutData;
	int	nBitsLeft = nBits;

	// get output dword-aligned.
	while((( uint32_t )pOut & 3) != 0 && nBitsLeft >= 8 )
	{
		*pOut = (byte)MSG_ReadUBitLong( sb, 8 );
		++pOut;
		nBitsLeft -= 8;
	}

	// read dwords.
	while( nBitsLeft >= 32 )
	{
		*((uint32_t *)pOut) = MSG_ReadUBitLong( sb, 32 );
		pOut += sizeof( uint32_t );
		nBitsLeft -= 32;
	}

	// read the remaining bytes.
	while( nBitsLeft >= 8 )
	{
		*pOut = MSG_ReadUBitLong( sb, 8 );
		++pOut;
		nBitsLeft -= 8;
	}

	// read the remaining bits.
	if( nBitsLeft )
	{
		*pOut = MSG_ReadUBitLong( sb, nBitsLeft );
	}

	return !sb->bOverflow;
}

float MSG_ReadBitAngle( sizebuf_t *sb, int numbits )
{
	float	fReturn, shift;
	int	i;

	shift = (float)( 1 << numbits );

	i = MSG_ReadUBitLong( sb, numbits );
	fReturn = (float)i * ( 360.0f / shift );

	// clamp the finale angle
	if( fReturn < -180.0f ) fReturn += 360.0f;
	else if( fReturn > 180.0f ) fReturn -= 360.0f;

	return fReturn;
}

// Append numbits least significant bits from data to the current bit stream
int MSG_ReadSBitLong( sizebuf_t *sb, int numbits )
{
	int	r, sign;

	r = MSG_ReadUBitLong( sb, numbits - 1 );

	// NOTE: it does this wierdness here so it's bit-compatible with regular integer data in the buffer.
	// (Some old code writes direct integers right into the buffer).
	sign = MSG_ReadOneBit( sb );
	if( sign ) r = -( BIT( numbits - 1 ) - r );

	return r;
}

uint MSG_ReadBitLong( sizebuf_t *sb, int numbits, qboolean bSigned )
{
	if( bSigned )
		return (uint)MSG_ReadSBitLong( sb, numbits );
	return MSG_ReadUBitLong( sb, numbits );
}

int MSG_ReadCmd( sizebuf_t *sb, netsrc_t type )
{
	int	cmd = MSG_ReadUBitLong( sb, sizeof( uint8_t ) << 3 );

#ifdef DEBUG_NET_MESSAGES_READ
	if( type == NS_SERVER )
	{
		Con_Printf( "^1cl^7 read: %s\n", CL_MsgInfo( cmd ));
	}
	else if( cmd >= 0 && cmd <= clc_lastmsg )
	{
		Con_Printf( "^1sv^7 read: %s\n", clc_strings[cmd] );
	}
#endif
	return cmd;
}

int MSG_ReadChar( sizebuf_t *sb )
{
	return MSG_ReadSBitLong( sb, sizeof( int8_t ) << 3 );
}

int MSG_ReadByte( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint8_t ) << 3 );
}

int MSG_ReadShort( sizebuf_t *sb )
{
	return MSG_ReadSBitLong( sb, sizeof( int16_t ) << 3 );
}

int MSG_ReadWord( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint16_t ) << 3 );
}

float MSG_ReadCoord( sizebuf_t *sb )
{
	// g-cont. we loose precision here but keep old size of coord variable!
	if( FBitSet( host.features, ENGINE_WRITE_LARGE_COORD ))
		return (float)(MSG_ReadShort( sb ));
	return (float)(MSG_ReadShort( sb ) * ( 1.0f / 8.0f ));
}

void MSG_ReadVec3Coord( sizebuf_t *sb, vec3_t fa )
{
	fa[0] = MSG_ReadCoord( sb );
	fa[1] = MSG_ReadCoord( sb );
	fa[2] = MSG_ReadCoord( sb );
}

void MSG_ReadVec3Angles( sizebuf_t *sb, vec3_t fa )
{
	fa[0] = MSG_ReadBitAngle( sb, 16 );
	fa[1] = MSG_ReadBitAngle( sb, 16 );
	fa[2] = MSG_ReadBitAngle( sb, 16 );
}

int MSG_ReadLong( sizebuf_t *sb )
{
	return MSG_ReadSBitLong( sb, sizeof( int32_t ) << 3 );
}

uint MSG_ReadDword( sizebuf_t *sb )
{
	return MSG_ReadUBitLong( sb, sizeof( uint32_t ) << 3 );
}

float MSG_ReadFloat( sizebuf_t *sb )
{
	float	ret;

	Assert( sizeof( ret ) == 4 );

	MSG_ReadBits( sb, &ret, 32 );

	return ret;
}

qboolean MSG_ReadBytes( sizebuf_t *sb, void *pOut, int nBytes )
{
	return MSG_ReadBits( sb, pOut, nBytes << 3 );
}

static char *MSG_ReadStringExt( sizebuf_t *sb, qboolean bLine )
{
	static char	string[4096];
	int		l = 0, c;

	do
	{
		// use MSG_ReadByte so -1 is out of bounds
		c = MSG_ReadByte( sb );

		if( c == 0 ) break;
		else if( bLine && c == '\n' )
			break;

		// translate all fmt spec to avoid crash bugs
		// NOTE: but game strings leave unchanged. see pfnWriteString for details
		if( c == '%' ) c = '.';

		string[l] = c;
		l++;
	} while( l < sizeof( string ) - 1 );
	string[l] = 0; // terminator

	return string;
}

char *MSG_ReadString( sizebuf_t *sb )
{
	return MSG_ReadStringExt( sb, false );
}

char *MSG_ReadStringLine( sizebuf_t *sb )
{
	return MSG_ReadStringExt( sb, true );
}

void MSG_ExciseBits( sizebuf_t *sb, int startbit, int bitstoremove )
{
	int	i, endbit = startbit + bitstoremove;
	int	remaining_to_end = sb->nDataBits - endbit;
	sizebuf_t	temp;

	MSG_StartWriting( &temp, sb->pData, MSG_GetMaxBytes( sb ), startbit, -1 );
	MSG_SeekToBit( sb, endbit, SEEK_SET );

	for( i = 0; i < remaining_to_end; i++ )
	{
		MSG_WriteOneBit( &temp, MSG_ReadOneBit( sb ));
	}

	MSG_SeekToBit( sb, startbit, SEEK_SET );
	sb->nDataBits -= bitstoremove;
}

#ifdef XASH_ENGINE_TESTS
#include "tests.h"

static const void *g_testbuf = "asdf\xba\xa1\xba\xa1\xed\xc8\x15\x7a";
static const size_t g_testbuf_bits = (( 4 + 4 + 2 + 1 ) << 3 ) + 4;

static void Test_Buffer_BitByte( void )
{
	TASSERT_EQi( BitByte( 0 ), 0 );
	TASSERT_EQi( BitByte( 1 ), 1 );
	TASSERT_EQi( BitByte( 8 ), 1 );
	TASSERT_EQi( BitByte( 9 ), 2 );
}

static void Test_Buffer_Write( void )
{
	sizebuf_t sb;
	char testdata[0x100] = { 0 };

	MSG_Init( &sb, __func__, testdata, sizeof( testdata ));
	TASSERT_EQi( sb.iCurBit, 0 );
	TASSERT_EQi( sb.nDataBits, sizeof( testdata ) << 3 );
	TASSERT_EQi( sb.pData, testdata );
	TASSERT_EQi( sb.bOverflow, false );

	MSG_WriteBytes( &sb, "asdf", 4 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 32 );

	MSG_WriteDword( &sb, 0xa1baa1ba );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 64 );

	MSG_WriteShort( &sb, -0x3713 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 80 );

	MSG_WriteOneBit( &sb, 1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 81 );

	MSG_WriteOneBit( &sb, 0 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 82 );

	MSG_WriteOneBit( &sb, 1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 83 );

	MSG_WriteOneBit( &sb, 0 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 84 );

	MSG_WriteByte( &sb, 0xa1 );
	TASSERT_EQi( sb.bOverflow, false );
	TASSERT_EQi( sb.iCurBit, 92 );

	TASSERT_EQi( MSG_GetNumBitsWritten( &sb ), g_testbuf_bits );
	TASSERT_EQi( MSG_GetNumBytesWritten( &sb ), BitByte( g_testbuf_bits ));
	TASSERT_EQi( MSG_GetRealBytesWritten( &sb ), g_testbuf_bits >> 3 );

	// if tests fails here on big endian, it's possible due to endian issues
	TASSERT( !memcmp( sb.pData, g_testbuf, g_testbuf_bits >> 3 ));

	// must check last 4 bits separately because we never care about uninitialized bits
	MSG_SeekToBit( &sb, g_testbuf_bits & ~7, SEEK_SET );
	TASSERT_EQi( sb.iCurBit, 88 );
	TASSERT_EQi( MSG_ReadUBitLong( &sb, 4 ), 0xa );
}

static void Test_Buffer_Read( void )
{
	sizebuf_t sb;
	char buf[4];

	MSG_StartReading( &sb, (void *)g_testbuf, -1, 0, g_testbuf_bits );
	TASSERT_EQi( sb.iCurBit, 0 );
	TASSERT_EQi( sb.nDataBits, g_testbuf_bits );
	TASSERT_EQi( sb.pData, g_testbuf );
	TASSERT_EQi( sb.bOverflow, false );

	MSG_ReadBytes( &sb, buf, 4 );
	TASSERT( !memcmp( buf, "asdf", 4 ));
	TASSERT_EQi( sb.iCurBit, 32 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadWord( &sb ), 0xa1ba );
	TASSERT_EQi( sb.iCurBit, 48 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadDword( &sb ), 0xc8eda1baU );
	TASSERT_EQi( sb.iCurBit, 80 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 1 );
	TASSERT_EQi( sb.iCurBit, 81 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 0 );
	TASSERT_EQi( sb.iCurBit, 82 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 1 );
	TASSERT_EQi( sb.iCurBit, 83 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadOneBit( &sb ), 0 );
	TASSERT_EQi( sb.iCurBit, 84 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_ReadByte( &sb ), 0xa1 );
	TASSERT_EQi( sb.iCurBit, 92 );
	TASSERT_EQi( sb.bOverflow, false );

	TASSERT_EQi( MSG_Overflow( &sb, 1 ), true );
	TASSERT_EQi( sb.bOverflow, true );
}

static void Test_Buffer_ExciseBits( void )
{
	sizebuf_t sb;
	char testdata[0x100];

	memcpy( testdata, g_testbuf, BitByte( g_testbuf_bits ));

	MSG_StartWriting( &sb, testdata, 0, 0, g_testbuf_bits );
	MSG_ExciseBits( &sb, 8, 28 );

	TASSERT_EQi( MSG_CheckOverflow( &sb ), false );
	TASSERT_EQi( MSG_GetMaxBits( &sb ), 64 );
	TASSERT( !memcmp( MSG_GetData( &sb ), "a\x1b\xaa\x1b\xda\x8e\x5c\xa1", 8 ));

	memcpy( testdata, g_testbuf, BitByte( g_testbuf_bits ));

	MSG_StartWriting( &sb, testdata, 0, 0, g_testbuf_bits );
	MSG_ExciseBits( &sb, 16, 32 );

	TASSERT_EQi( MSG_CheckOverflow( &sb ), false );
	TASSERT_EQi( MSG_GetMaxBits( &sb ), g_testbuf_bits - 32 );
	TASSERT( !memcmp( MSG_GetData( &sb ), "as\xba\xa1\xed\xc8\x15", 7 ));

	MSG_SeekToBit( &sb, 7 << 3, SEEK_SET );
	TASSERT_EQi( MSG_ReadUBitLong( &sb, 4 ), 0xa );
}

void Test_RunBuffer( void )
{
	MSG_InitMasks();

	TRUN( Test_Buffer_BitByte( ));
	TRUN( Test_Buffer_Write( ));
	TRUN( Test_Buffer_Read( ));
	TRUN( Test_Buffer_ExciseBits( ));
}

#endif // XASH_ENGINE_TESTS
