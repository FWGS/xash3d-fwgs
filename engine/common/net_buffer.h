/*
net_buffer.h - network message io functions
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

#ifndef NET_BUFFER_H
#define NET_BUFFER_H

#include "enginefeatures.h"

/*
==============================================================================

			MESSAGE IO FUNCTIONS
	       Handles byte ordering and avoids alignment errors
==============================================================================
*/

// Pad a number so it lies on an N byte boundary.
// So PAD_NUMBER(0,4) is 0 and PAD_NUMBER(1,4) is 4
#define PAD_NUMBER( num, boundary )	((( num ) + (( boundary ) - 1 )) / ( boundary )) * ( boundary )

static inline int BitByte( int bits )
{
	return PAD_NUMBER( bits, 8 ) >> 3;
}

struct sizebuf_s
{
	byte        *pData;
	qboolean    bOverflow;   // overflow reading or writing
	int         iCurBit;
	int         nDataBits;
	const char	*pDebugName; // buffer name (pointer to const name)

	// to support GoldSrc broken signed integers
	int iAlternateSign;
};

#define MSG_StartReading     MSG_StartWriting
#define MSG_GetNumBytesRead  MSG_GetNumBytesWritten
#define MSG_GetRealBytesRead MSG_GetRealBytesWritten
#define MSG_GetNumBitsRead   MSG_GetNumBitsWritten
#define MSG_ReadBitAngles    MSG_ReadBitVec3Coord
#define MSG_ReadAngle( sb )               (float)( MSG_ReadChar( sb ) * ( 360.0f / 256.0f ))
#define MSG_Init( sb, name, data, bytes ) MSG_InitExt( sb, name, data, bytes, -1 )
#define MSG_CheckOverflow( sb )           MSG_Overflow( sb, 0 )

// common functions
static inline void MSG_Clear( sizebuf_t *sb )
{
	sb->bOverflow = false;
	sb->iCurBit = 0;
}

static inline void MSG_InitExt( sizebuf_t *sb, const char *pDebugName, void *pData, int nBytes, int nBits )
{
	sb->pData = pData;
	MSG_Clear( sb );

	if( nBits < 0 )
		sb->nDataBits = nBytes << 3;
	else
		sb->nDataBits = nBits;

	sb->pDebugName = pDebugName;
	sb->iAlternateSign = 0;
}

static inline void MSG_StartWriting( sizebuf_t *sb, void *pData, int nBytes, int iStartBit, int nBits )
{
	MSG_InitExt( sb, "Unnamed", pData, nBytes, nBits );
	sb->iCurBit = iStartBit;
}

static inline int MSG_SeekToBit( sizebuf_t *sb, int bitPos, int whence )
{
	// compute the file offset
	switch( whence )
	{
	case SEEK_CUR:
		bitPos += sb->iCurBit;
		break;
	case SEEK_SET:
		break;
	case SEEK_END:
		bitPos += sb->nDataBits;
		break;
	default:
		return -1;
	}

	if( unlikely( bitPos < 0 || bitPos > sb->nDataBits ))
		return -1;

	sb->iCurBit = bitPos;

	return 0;
}

static inline int MSG_TellBit( sizebuf_t *sb )
{
	return sb->iCurBit;
}

static inline const char *MSG_GetName( sizebuf_t *sb )
{
	return sb->pDebugName;
}

static inline int MSG_GetNumBytesWritten( sizebuf_t *sb )
{
	return BitByte( sb->iCurBit );
}

static inline int MSG_GetRealBytesWritten( sizebuf_t *sb )
{
	return sb->iCurBit >> 3; // unpadded
}
static inline int MSG_GetNumBitsWritten( sizebuf_t *sb )
{
	return sb->iCurBit;
}

static inline int MSG_GetMaxBits( sizebuf_t *sb )
{
	return sb->nDataBits;
}

static inline int MSG_GetMaxBytes( sizebuf_t *sb )
{
	return sb->nDataBits >> 3;
}

static inline int MSG_GetNumBitsLeft( sizebuf_t *sb )
{
	return sb->nDataBits - sb->iCurBit;
}

static inline int MSG_GetNumBytesLeft( sizebuf_t *sb )
{
	return MSG_GetNumBitsLeft( sb ) >> 3;
}

static inline byte *MSG_GetData( sizebuf_t *sb )
{
	return sb->pData;
}

#if XASH_BIG_ENDIAN
#define MSG_BigShort( x ) ( x )
#else
static inline uint16_t MSG_BigShort( const uint16_t x )
{
	return (x >> 8) | (x << 8);
}
#endif

static inline qboolean MSG_Overflow( sizebuf_t *sb, int nBits )
{
	if( sb->iCurBit + nBits > sb->nDataBits )
		sb->bOverflow = true;
	return sb->bOverflow;
}

static inline void MSG_EndBitWriting( sizebuf_t *sb )
{
	sb->iAlternateSign--;

	if( sb->iAlternateSign < 0 )
	{
		Con_Printf( "%s: non-even MSG_Start/EndBitWriting\n", __func__ );
		sb->iAlternateSign = 0;
	}

	// we have native bit ops here, just pad to closest byte
	if(( sb->iCurBit & 7 ) != 0 )
		MSG_SeekToBit( sb, 8 - ( sb->iCurBit & 7 ), SEEK_CUR );
}

static inline void MSG_StartBitWriting( sizebuf_t *sb )
{
	sb->iAlternateSign++;
}

void MSG_ExciseBits( sizebuf_t *sb, int startbit, int bitstoremove );

// Bit-write functions
static inline void MSG_WriteOneBit( sizebuf_t *sb, int nValue )
{
	if( !MSG_Overflow( sb, 1 ))
	{
		if( nValue ) sb->pData[sb->iCurBit>>3] |= BIT( sb->iCurBit & 7 );
		else sb->pData[sb->iCurBit>>3] &= ~BIT( sb->iCurBit & 7 );

		sb->iCurBit++;
	}
}

NO_ASAN void MSG_WriteUBitLong( sizebuf_t *sb, uint curData, int numbits );
void MSG_WriteSBitLong( sizebuf_t *sb, int data, int numbits );
void MSG_WriteBitLong( sizebuf_t *sb, uint data, int numbits, qboolean bSigned );
qboolean MSG_WriteBits( sizebuf_t *sb, const void *pData, int nBits );
void MSG_WriteBitAngle( sizebuf_t *sb, float fAngle, int numbits );

// Byte-write functions
#define MSG_BeginServerCmd( sb, cmd ) MSG_WriteCmdExt( sb, cmd, NS_SERVER, NULL )
#define MSG_BeginClientCmd( sb, cmd ) MSG_WriteCmdExt( sb, cmd, NS_CLIENT, NULL )
void MSG_WriteCmdExt( sizebuf_t *sb, int cmd, netsrc_t type, const char *name );		// message marker
void MSG_WriteChar( sizebuf_t *sb, int val );
void MSG_WriteByte( sizebuf_t *sb, int val );
void MSG_WriteShort( sizebuf_t *sb, int val );
void MSG_WriteWord( sizebuf_t *sb, int val );
void MSG_WriteLong( sizebuf_t *sb, int val );
void MSG_WriteDword( sizebuf_t *sb, uint val );
void MSG_WriteCoord( sizebuf_t *sb, float val );
void MSG_WriteFloat( sizebuf_t *sb, float val );
void MSG_WriteVec3Coord( sizebuf_t *sb, const float *fa );
void MSG_WriteVec3Angles( sizebuf_t *sb, const float *fa );
qboolean MSG_WriteString( sizebuf_t *sb, const char *pStr );		// returns false if it overflows the buffer.
qboolean MSG_WriteStringf( sizebuf_t *sb, const char *format, ... ) FORMAT_CHECK( 2 );
qboolean MSG_WriteBytes( sizebuf_t *sb, const void *pBuf, int nBytes );

// helper functions

// Bit-read functions
int MSG_ReadOneBit( sizebuf_t *sb );
qboolean MSG_ReadBits( sizebuf_t *sb, void *pOutData, int nBits );
float MSG_ReadBitAngle( sizebuf_t *sb, int numbits );
int MSG_ReadSBitLong( sizebuf_t *sb, int numbits );
uint MSG_ReadUBitLong( sizebuf_t *sb, int numbits );
uint MSG_ReadBitLong( sizebuf_t *sb, int numbits, qboolean bSigned );

// Byte-read functions
#define MSG_ReadServerCmd( sb ) MSG_ReadCmd( sb, NS_SERVER )
#define MSG_ReadClientCmd( sb ) MSG_ReadCmd( sb, NS_CLIENT )
int MSG_ReadCmd( sizebuf_t *sb, netsrc_t type );		// message marker
int MSG_ReadChar( sizebuf_t *sb );
int MSG_ReadByte( sizebuf_t *sb );
int MSG_ReadShort( sizebuf_t *sb );
int MSG_ReadWord( sizebuf_t *sb );
int MSG_ReadLong( sizebuf_t *sb );
uint MSG_ReadDword( sizebuf_t *sb );
float MSG_ReadCoord( sizebuf_t *sb );
float MSG_ReadFloat( sizebuf_t *sb );
void MSG_ReadVec3Coord( sizebuf_t *sb, vec3_t fa );
void MSG_ReadVec3Angles( sizebuf_t *sb, vec3_t fa );
char *MSG_ReadString( sizebuf_t *sb ) RETURNS_NONNULL;
char *MSG_ReadStringLine( sizebuf_t *sb ) RETURNS_NONNULL;
qboolean MSG_ReadBytes( sizebuf_t *sb, void *pOut, int nBytes );

#endif//NET_BUFFER_H
