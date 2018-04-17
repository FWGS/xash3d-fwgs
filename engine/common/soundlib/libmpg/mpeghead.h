/*
mpeghead.h - compact version of famous library mpg123
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

#ifndef MPEGHEAD_H
#define MPEGHEAD_H

#define HDR_SYNC		0xffe00000
#define HDR_SYNC_VAL(h)	(((h) & HDR_SYNC) >> 21)
#define HDR_VERSION		0x00180000
#define HDR_VERSION_VAL(h)	(((h) & HDR_VERSION) >> 19)
#define HDR_LAYER		0x00060000
#define HDR_LAYER_VAL(h)	(((h) & HDR_LAYER) >> 17)
#define HDR_CRC		0x00010000
#define HDR_CRC_VAL(h)	(((h) & HDR_CRC) >> 16)
#define HDR_BITRATE		0x0000f000
#define HDR_BITRATE_VAL(h)	(((h) & HDR_BITRATE) >> 12)
#define HDR_SAMPLERATE	0x00000c00
#define HDR_SAMPLERATE_VAL(h)	(((h) & HDR_SAMPLERATE) >> 10)
#define HDR_PADDING		0x00000200
#define HDR_PADDING_VAL(h)	(((h) & HDR_PADDING) >> 9)
#define HDR_PRIVATE		0x00000100
#define HDR_PRIVATE_VAL(h)	(((h) & HDR_PRIVATE) >> 8)
#define HDR_CHANNEL		0x000000c0
#define HDR_CHANNEL_VAL(h)	(((h) & HDR_CHANNEL) >> 6)
#define HDR_CHANEX		0x00000030
#define HDR_CHANEX_VAL(h)	(((h) & HDR_CHANEX) >> 4)
#define HDR_COPYRIGHT	0x00000008
#define HDR_COPYRIGHT_VAL(h)	(((h) & HDR_COPYRIGHT) >> 3)
#define HDR_ORIGINAL	0x00000004
#define HDR_ORIGINAL_VAL(h)	(((h) & HDR_ORIGINAL) >> 2)
#define HDR_EMPHASIS	0x00000003
#define HDR_EMPHASIS_VAL(h)	(((h) & HDR_EMPHASIS) >> 0)


// a generic mask for telling if a header is somewhat valid for the current stream.
// meaning: Most basic info is not allowed to change.
// checking of channel count needs to be done, too, though. So,
// if channel count matches, frames are decoded the same way: frame buffers and decoding
// routines can stay the same, especially frame buffers (think spf * channels!).
#define HDR_CMPMASK		(HDR_SYNC|HDR_VERSION|HDR_LAYER|HDR_SAMPLERATE)

// A stricter mask, for matching free format headers.
#define HDR_SAMEMASK	(HDR_SYNC|HDR_VERSION|HDR_LAYER|HDR_BITRATE|HDR_SAMPLERATE|HDR_CHANNEL|HDR_CHANEX)

// free format headers have zero bitrate value.
#define HDR_FREE_FORMAT(head)	(!(head & HDR_BITRATE))

// a mask for changed sampling rate (version or rate bits).
#define HDR_SAMPMASK	(HDR_VERSION|HDR_SAMPLERATE)

#endif//MPEGHEAD_H