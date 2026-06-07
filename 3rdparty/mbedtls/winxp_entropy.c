/*
winxp_entropy.c - platform entropy override for mbedTLS on 32-bit Windows
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

#include <windows.h>
#include <wincrypt.h>
#include "mbedtls/platform.h"
#include "psa/crypto.h"

int mbedtls_platform_get_entropy( psa_driver_get_entropy_flags_t flags, size_t *estimate_bits, unsigned char *output, size_t output_size )
{
	if( flags != 0 )
		return PSA_ERROR_NOT_SUPPORTED;

	HCRYPTPROV prov;
	if( !CryptAcquireContextW( &prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT ))
		return PSA_ERROR_INSUFFICIENT_ENTROPY;

	if( !CryptGenRandom( prov, output_size, output ))
	{
		CryptReleaseContext( prov, 0 );
		return PSA_ERROR_INSUFFICIENT_ENTROPY;
	}

	CryptReleaseContext( prov, 0 );
	*estimate_bits = 8 * output_size;
	return 0;
}
