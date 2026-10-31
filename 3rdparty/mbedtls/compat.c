/*
compat.c - mbedTLS platform overrides for targets upstream doesn't cover
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

/*
WinXP: legacy CryptGenRandom via advapi32
Source: ttps://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptgenrandom

PSVita: sceKernelGetRandomNumber from psp2/kernel/rng.h (64-byte cap).
Source: https://github.com/vitasdk/vita-headers/blob/master/include/psp2/kernel/rng.h

NSwitch: randomGet from libnx.
Source: https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/random.h

PS4: kern.arandom of FreeBSD 9 based kernel, returns up to 256 bytes at once.
Source: https://man.freebsd.org/cgi/man.cgi?query=random&sektion=4&manpath=FreeBSD+9.0-RELEASE
*/

#include "mbedtls/platform.h"
#include "psa/crypto.h"

#if defined( MBEDTLS_PLATFORM_MS_TIME_ALT )

mbedtls_ms_time_t mbedtls_ms_time( void )
{
	extern double Platform_DoubleTime( void );

	return (mbedtls_ms_time_t)( Platform_DoubleTime() * 1000.0 );
}

#endif /* MBEDTLS_PLATFORM_MS_TIME_ALT */

#if defined( MBEDTLS_PSA_DRIVER_GET_ENTROPY )
#if defined( _WIN32 ) && !defined( _WIN64 )
#include <windows.h>
#include <wincrypt.h>
int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size )
{
	if( flags != 0 )
		return PSA_ERROR_NOT_SUPPORTED;

	HCRYPTPROV prov;
	if( !CryptAcquireContextW( &prov, NULL, NULL, PROV_RSA_FULL,
		CRYPT_VERIFYCONTEXT | CRYPT_SILENT ))
		return PSA_ERROR_INSUFFICIENT_ENTROPY;

	if( !CryptGenRandom( prov, (DWORD)output_size, output ))
	{
		CryptReleaseContext( prov, 0 );
		return PSA_ERROR_INSUFFICIENT_ENTROPY;
	}

	CryptReleaseContext( prov, 0 );
	*estimate_bits = 8 * output_size;
	return 0;
}
#elif defined( __vita__ )
#include <psp2/kernel/rng.h>

int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size )
{
	if( flags != 0 )
		return PSA_ERROR_NOT_SUPPORTED;

	size_t total = 0;
	while( total < output_size )
	{
		size_t chunk = output_size - total;

		if( chunk > 64 )
			chunk = 64;

		if( sceKernelGetRandomNumber( output + total, chunk ) != 0 )
			return PSA_ERROR_INSUFFICIENT_ENTROPY;

		total += chunk;
	}

	*estimate_bits = 8 * output_size;
	return 0;
}

#elif defined( __SWITCH__ )
#include <switch.h>

int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size )
{
	if( flags != 0 )
		return PSA_ERROR_NOT_SUPPORTED;

	randomGet( output, output_size );
	*estimate_bits = 8 * output_size;
	return 0;
}
#elif defined( __ORBIS__ )
#include <stddef.h>

/* OpenOrbis headers have no sys/sysctl.h */
#define XASH_CTL_KERN 1
#define XASH_KERN_ARND 37

extern int sysctl( const int *name, unsigned int namelen, void *oldp, size_t *oldlenp, const void *newp, size_t newlen );

int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size )
{
	const int mib[2] = { XASH_CTL_KERN, XASH_KERN_ARND };

	if( flags != 0 )
		return PSA_ERROR_NOT_SUPPORTED;

	size_t total = 0;
	while( total < output_size )
	{
		size_t chunk = output_size - total;

		if( chunk > 256 )
			chunk = 256;

		if( sysctl( mib, 2, output + total, &chunk, NULL, 0 ) != 0 || chunk == 0 )
			return PSA_ERROR_INSUFFICIENT_ENTROPY;

		total += chunk;
	}

	*estimate_bits = 8 * output_size;
	return 0;
}
#else
#error "MBEDTLS_PSA_DRIVER_GET_ENTROPY enabled but no platform impl in compat.c"
#endif

#endif /* MBEDTLS_PSA_DRIVER_GET_ENTROPY */
