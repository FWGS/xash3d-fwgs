/*
getbits.h - compact version of famous library mpg123
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

#ifndef GETBITS_H
#define GETBITS_H

#define backbits( fr, nob )	((void)( \
	fr->bitindex -= nob, \
	fr->wordpointer += (fr->bitindex>>3), \
	fr->bitindex &= 0x7 ))

#define getbitoffset( fr )	((-fr->bitindex) & 0x7)
#define getbyte( fr )	(*fr->wordpointer++)

#define skipbits( fr, nob )	fr->ultmp = ( \
	fr->ultmp = fr->wordpointer[0], fr->ultmp <<= 8, fr->ultmp |= fr->wordpointer[1], \
	fr->ultmp <<= 8, fr->ultmp |= fr->wordpointer[2], fr->ultmp <<= fr->bitindex, \
	fr->ultmp &= 0xffffff, fr->bitindex += nob, \
	fr->ultmp >>= (24-nob), fr->wordpointer += (fr->bitindex>>3), \
	fr->bitindex &= 7 )

#define getbits_fast( fr, nob )( \
	fr->ultmp = (byte) (fr->wordpointer[0] << fr->bitindex), \
	fr->ultmp |= ((ulong)fr->wordpointer[1] << fr->bitindex) >> 8, \
	fr->ultmp <<= nob, fr->ultmp >>= 8, \
	fr->bitindex += nob, fr->wordpointer += (fr->bitindex >> 3), \
	fr->bitindex &= 7, fr->ultmp )

#define get1bit( fr )	( \
	fr->uctmp = *fr->wordpointer << fr->bitindex, fr->bitindex++, \
	fr->wordpointer += (fr->bitindex >> 3), fr->bitindex &= 7, fr->uctmp >> 7 )

// 24 is enough because tab13 has max. a 19 bit huffvector
#define BITSHIFT	((sizeof(long) - 1) * 8)

#define REFRESH_MASK \
	while( num < BITSHIFT ) { \
		mask |= ((ulong)getbyte( fr )) << (BITSHIFT - num); \
		num += 8; \
		part2remain -= 8; }

static uint getbits( mpg123_handle_t *fr, int number_of_bits )
{
	ulong	rval;

	rval = fr->wordpointer[0];
	rval <<= 8;
	rval |= fr->wordpointer[1];
	rval <<= 8;
	rval |= fr->wordpointer[2];

	rval <<= fr->bitindex;
	rval &= 0xffffff;

	fr->bitindex += number_of_bits;
	rval >>= (24-number_of_bits);

	fr->wordpointer += (fr->bitindex>>3);
	fr->bitindex &= 7;

	return rval;
}
  
#endif//GETBITS_H