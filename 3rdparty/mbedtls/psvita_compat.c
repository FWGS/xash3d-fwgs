/*
psvita_compat.c - mbedTLS platform overrides for PlayStation Vita
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

/* Upstream mbedTLS has no PSVita target, so:
   - mbedtls_ms_time() forwards to the engine's Platform_DoubleTime
   - mbedtls_platform_get_entropy() uses sceKernelGetRandomNumber
   Wired in only when DEST_OS == 'psvita'. */

#include <psp2/kernel/utils.h>

#include "mbedtls/platform.h"
#include "psa/crypto.h"

extern double Platform_DoubleTime( void );

mbedtls_ms_time_t mbedtls_ms_time( void )
{
	return (mbedtls_ms_time_t)( Platform_DoubleTime() * 1000.0 );
}

int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags,
	size_t *estimate_bits, unsigned char *output, size_t output_size )
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
