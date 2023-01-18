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

_inline int BitByte( int bits )
{
	return PAD_NUMBER( bits, 8 ) >> 3;
}

struct sizebuf_s
{
	qboolean		bOverflow;	// overflow reading or writing
	const char	*pDebugName;	// buffer name (pointer to const name)

	byte		*pData;
	int		iCurBit;
	int		nDataBits;
};

#define MSG_StartReading			MSG_StartWriting
#define MSG_GetNumBytesRead			MSG_GetNumBytesWritten
#define MSG_GetRealBytesRead			MSG_GetRealBytesWritten
#define MSG_GetNumBitsRead			MSG_GetNumBitsWritten
#define MSG_ReadBitAngles			MSG_ReadBitVec3Coord
#define MSG_ReadString( sb )			MSG_ReadStringExt( sb, false )
#define MSG_ReadStringLine( sb )		MSG_ReadStringExt( sb, true )
#define MSG_ReadAngle( sb )			(float)(MSG_ReadChar( sb ) * ( 360.0f / 256.0f ))
#define MSG_Init( sb, name, data, bytes )	MSG_InitExt( sb, name, data, bytes, -1 )

// common functions
void MSG_InitExt( sizebuf_t *sb, const char *pDebugName, void *pData, int nBytes, int nMaxBits );
void MSG_InitMasks( void );	// called once at startup engine
int MSG_SeekToBit( sizebuf_t *sb, int bitPos, int whence );
void MSG_ExciseBits( sizebuf_t *sb, int startbit, int bitstoremove );
_inline int MSG_TellBit( sizebuf_t *sb ) { return sb->iCurBit; }
_inline const char *MSG_GetName( sizebuf_t *sb ) { return sb->pDebugName; }
qboolean MSG_CheckOverflow( sizebuf_t *sb );

#if XASH_BIG_ENDIAN
#define MSG_BigShort( x ) ( x )
#else
static inline uint16_t MSG_BigShort( const uint16_t x )
{
	return (x >> 8) | (x << 8);
}
#endif

// init writing
void MSG_StartWriting( sizebuf_t *sb, void *pData, int nBytes, int iStartBit, int nBits );
void MSG_Clear( sizebuf_t *sb );

// Bit-write functions
void MSG_WriteOneBit( sizebuf_t *sb, int nValue );
void MSG_WriteUBitLong( sizebuf_t *sb, uint curData, int numbits );
void MSG_WriteSBitLong( sizebuf_t *sb, int data, int numbits );
void MSG_WriteBitLong( sizebuf_t *sb, uint data, int numbits, qboolean bSigned );
qboolean MSG_WriteBits( sizebuf_t *sb, const void *pData, int nBits );
void MSG_WriteBitAngle( sizebuf_t *sb, float fAngle, int numbits );
void MSG_WriteBitFloat( sizebuf_t *sb, float val );

// Byte-write functions
#define MSG_BeginServerCmd( sb, cmd ) MSG_WriteCmdExt( sb, cmd, NS_SERVER, NULL )
#define MSG_BeginClientCmd( sb, cmd ) MSG_WriteCmdExt( sb, cmd, NS_CLIENT, NULL )
void MSG_WriteCmdExt( sizebuf_t *sb, int cmd, netsrc_t type, const char *name );		// message marker
void MSG_WriteChar( sizebuf_t *sb, int val );
void MSG_WriteByte( sizebuf_t *sb, int val );
void MSG_WriteShort( sizebuf_t *sb, int val );
void MSG_WriteWord( sizebuf_t *sb, int val );
void MSG_WriteLong( sizebuf_t *sb, int val );
void MSG_WriteDword( sizebuf_t *sb, dword val );
void MSG_WriteCoord( sizebuf_t *sb, float val );
void MSG_WriteFloat( sizebuf_t *sb, float val );
void MSG_WriteVec3Coord( sizebuf_t *sb, const float *fa );
void MSG_WriteVec3Angles( sizebuf_t *sb, const float *fa );
qboolean MSG_WriteBytes( sizebuf_t *sb, const void *pBuf, int nBytes );	// same as MSG_WriteData
qboolean MSG_WriteString( sizebuf_t *sb, const char *pStr );		// returns false if it overflows the buffer.

// helper functions
_inline int MSG_GetNumBytesWritten( sizebuf_t *sb ) { return BitByte( sb->iCurBit ); }
_inline int MSG_GetRealBytesWritten( sizebuf_t *sb ) { return sb->iCurBit >> 3; }	// unpadded
_inline int MSG_GetNumBitsWritten( sizebuf_t *sb ) { return sb->iCurBit; }
_inline int MSG_GetMaxBits( sizebuf_t *sb ) { return sb->nDataBits; }
_inline int MSG_GetMaxBytes( sizebuf_t *sb ) { return sb->nDataBits >> 3; }
_inline int MSG_GetNumBitsLeft( sizebuf_t *sb ) { return sb->nDataBits - sb->iCurBit; }
_inline int MSG_GetNumBytesLeft( sizebuf_t *sb ) { return MSG_GetNumBitsLeft( sb ) >> 3; }
_inline byte *MSG_GetData( sizebuf_t *sb ) { return sb->pData; }
_inline byte *MSG_GetBuf( sizebuf_t *sb ) { return sb->pData; } // just an alias

// Bit-read functions
int MSG_ReadOneBit( sizebuf_t *sb );
float MSG_ReadBitFloat( sizebuf_t *sb );
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
dword MSG_ReadDword( sizebuf_t *sb );
float MSG_ReadCoord( sizebuf_t *sb );
float MSG_ReadFloat( sizebuf_t *sb );
void MSG_ReadVec3Coord( sizebuf_t *sb, vec3_t fa );
void MSG_ReadVec3Angles( sizebuf_t *sb, vec3_t fa );
qboolean MSG_ReadBytes( sizebuf_t *sb, void *pOut, int nBytes );
char *MSG_ReadStringExt( sizebuf_t *sb, qboolean bLine );

#endif//NET_BUFFER_H
