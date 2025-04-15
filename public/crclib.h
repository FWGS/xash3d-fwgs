#pragma once
#ifndef CRCLIB_H
#define CRCLIB_H

#include "xash3d_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	uint	buf[4];
	uint	bits[2];
	uint	in[16];
} MD5Context_t;


void CRC32_Init( dword *pulCRC );
byte CRC32_BlockSequence( byte *base, int length, int sequence );
void CRC32_ProcessBuffer( dword *pulCRC, const void *pBuffer, int nBuffer );
void CRC32_ProcessByte( dword *pulCRC, byte ch );
dword CRC32_Final( dword pulCRC );
void MD5Init( MD5Context_t *ctx );
void MD5Update( MD5Context_t *ctx, const byte *buf, uint len );
void MD5Final( byte digest[16], MD5Context_t *ctx );
uint COM_HashKey( const char *string, uint hashSize );
char *MD5_Print( byte hash[16] );

#ifdef __cplusplus
}
#endif

#endif // CRCLIB_H
