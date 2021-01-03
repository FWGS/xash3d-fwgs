/*
fmt123.h - compact version of famous library mpg123
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef FMT123_H
#define FMT123_H

#define MPG123_RATES	9
#define MPG123_ENCODINGS	2

enum mpg123_enc_enum
{
	// 0000 0000 0000 1111 Some 8 bit  integer encoding. */
	MPG123_ENC_8      = 0x00f,
	// 0000 0000 0100 0000 Some 16 bit integer encoding.
	MPG123_ENC_16     = 0x040,
	// 0000 0000 1000 0000 Some signed integer encoding.
	MPG123_ENC_SIGNED = 0x080,
	// 0000 0000 1101 0000 signed 16 bit
	MPG123_ENC_SIGNED_16   = (MPG123_ENC_16|MPG123_ENC_SIGNED|0x10),
	// 0000 0000 0110 0000 unsigned 16 bit
	MPG123_ENC_UNSIGNED_16 = (MPG123_ENC_16|0x20),
	// 0000 0000 0000 0001 unsigned 8 bit
	MPG123_ENC_UNSIGNED_8  = 0x01,
	// 0000 0000 1000 0010 signed 8 bit
	MPG123_ENC_SIGNED_8    = (MPG123_ENC_SIGNED|0x02),
	// 0000 0000 0000 0100 ulaw 8 bit
	MPG123_ENC_ULAW_8      = 0x04,
	// 0000 0000 0000 1000 alaw 8 bit
	MPG123_ENC_ALAW_8      = 0x08,
};

#endif//FMT123_H
