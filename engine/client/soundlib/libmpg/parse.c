/*
parse.c - compact version of famous library mpg123
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

#include "mpeghead.h"
#include "mpg123.h"
#include "getbits.h"
#include <limits.h>

#define TRACK_MAX_FRAMES	(ULONG_MAX / 4 / 1152)
#define FORGET_INTERVAL	1024	// used by callers to set forget flag each <n> bytes.

// use 4 bytes from buf to construct 28bit uint value and return 1; return 0 if bytes are not synchsafe
#define synchsafe_to_long( buf, res ) \
	((((buf)[0]|(buf)[1]|(buf)[2]|(buf)[3]) & 0x80) ? \
	0 : (res = (((ulong)(buf)[0]) << 21) | (((ulong)(buf)[1]) << 14)|(((ulong)(buf)[2]) << 7)|((ulong)(buf)[3]), 1 ))

#define check_bytes_left( n ) if( fr->framesize < lame_offset + n ) \
		goto check_lame_tag_yes

// PARSE_GOOD and PARSE_BAD have to be 1 and 0 (TRUE and FALSE), others can vary.
enum parse_codes
{
	PARSE_MORE = MPG123_NEED_MORE,
	PARSE_ERR  = MPG123_ERR,
	PARSE_END  = 10,		/* No more audio data to find. */
	PARSE_GOOD = 1,		/* Everything's fine. */
	PARSE_BAD  = 0,		/* Not fine (invalid data). */
	PARSE_RESYNC = 2,		/* Header not good, go into resync. */
	PARSE_AGAIN  = 3,		/* Really start over, throw away and read a new header, again. */
};

// bitrates for [mpeg1/2][layer]
static const int tabsel_123[2][3][16] =
{
{
{0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,},
{0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384,},
{0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320,}
},
{
{0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,},
{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,},
{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,}
}
};

static const long freqs[9] = { 44100, 48000, 32000, 22050, 24000, 16000 , 11025 , 12000 , 8000 };

void set_pointer( mpg123_handle_t *fr, long backstep )
{
	fr->wordpointer = fr->bsbuf + fr->ssize - backstep;

	if( backstep )
		memcpy( fr->wordpointer, fr->bsbufold + fr->fsizeold - backstep, backstep );

	fr->bitindex = 0;
}

static int frame_bitrate( mpg123_handle_t *fr )
{
	return tabsel_123[fr->lsf][fr->lay-1][fr->bitrate_index];
}

long frame_freq( mpg123_handle_t *fr )
{
	return freqs[fr->sampling_frequency];
}

double compute_bpf( mpg123_handle_t *fr )
{
	double	bpf;

	switch( fr->lay )
	{
	case 1:
		bpf = tabsel_123[fr->lsf][0][fr->bitrate_index];
		bpf *= 12000.0 * 4.0;
		bpf /= freqs[fr->sampling_frequency] << (fr->lsf);
		break;
	case 2:
	case 3:
		bpf = tabsel_123[fr->lsf][fr->lay-1][fr->bitrate_index];
		bpf *= 144000;
		bpf /= freqs[fr->sampling_frequency] << (fr->lsf);
		break;
	default:
		bpf = 1.0;
	}

	return bpf;
}

int mpg123_spf( mpg123_handle_t *mh )
{
	if( mh == NULL )
		return MPG123_ERR;

	return mh->firsthead ? mh->spf : MPG123_ERR;
}

double mpg123_tpf( mpg123_handle_t *fr )
{
	static int	bs[4] = { 0,384, 1152, 1152 };
	double		tpf;

	if( fr == NULL || !fr->firsthead )
		return MPG123_ERR;

	tpf = (double)bs[fr->lay];
	tpf /= freqs[fr->sampling_frequency] << (fr->lsf);

	return tpf;
}

int get_songlen( mpg123_handle_t *fr, int no )
{
	double	tpf;

	if( !fr ) return 0;

	if( no < 0 )
	{
		if( !fr->rd || fr->rdat.filelen < 0 )
			return 0;

		no = (int)((double)fr->rdat.filelen / compute_bpf( fr ));
	}

	tpf = mpg123_tpf( fr );
	return (int)(no * tpf);
}

// just tell if the header is some mono.
static int header_mono( ulong newhead )
{
	return HDR_CHANNEL_VAL( newhead ) == MPG_MD_MONO ? TRUE : FALSE;
}

static int head_check(ulong head)
{
	if((( head & HDR_SYNC ) != HDR_SYNC ) || (!(HDR_LAYER_VAL(head))) || (HDR_BITRATE_VAL(head) == 0xf) || (HDR_SAMPLERATE_VAL(head) == 0x3 ))
		return FALSE;

	return TRUE;
}

// true if the two headers will work with the same decoding routines
static int head_compatible( ulong fred, ulong bret )
{
	return (( fred & HDR_CMPMASK ) == ( bret & HDR_CMPMASK ) && header_mono( fred ) == header_mono( bret ));
}

// this is moderately sized buffers. Int offset is enough.
static ulong bit_read_long( byte *buf, int *offset )
{
	ulong val = (((ulong)buf[*offset]) << 24) | (((ulong) buf[*offset+1]) << 16) | (((ulong) buf[*offset+2]) << 8) | ((ulong)buf[*offset+3]);
	*offset += 4;

	return val;
}

static word bit_read_short( byte *buf, int *offset )
{
	word val = (((word) buf[*offset]  ) << 8) | ((word) buf[*offset+1]);
	*offset += 2;

	return val;
}

static int check_lame_tag( mpg123_handle_t *fr )
{
	int	lame_offset = (fr->stereo == 2) ? (fr->lsf ? 17 : 32) : (fr->lsf ? 9  : 17);
	ulong	xing_flags;
	ulong	long_tmp;
	int	i;

	// going to look for Xing or Info at some position after the header
	//	                                   MPEG 1  MPEG 2/2.5 (LSF)
	//	Stereo, Joint Stereo, Dual Channel  32      17
	//	Mono                                17       9

	if( fr->p.flags & MPG123_IGNORE_INFOFRAME )
		goto check_lame_tag_no;

	// note: CRC or not, that does not matter here.
	// but, there is any combination of Xing flags in the wild. There are headers
	// without the search index table! I cannot assume a reasonable minimal size
	// for the actual data, have to check if each byte of information is present.
	// but: 4 B Info/Xing + 4 B flags is bare minimum.
	if( fr->framesize < lame_offset + 8 )
		goto check_lame_tag_no;

	// only search for tag when all zero before it (apart from checksum)
	for( i = 2; i < lame_offset; ++i )
	{
		if( fr->bsbuf[i] != 0 )
			goto check_lame_tag_no;
	}

	if((fr->bsbuf[lame_offset] == 'I') && (fr->bsbuf[lame_offset+1] == 'n') && (fr->bsbuf[lame_offset+2] == 'f') && (fr->bsbuf[lame_offset+3] == 'o'))
	{
		// we still have to see what there is
	}
	else if((fr->bsbuf[lame_offset] == 'X') && (fr->bsbuf[lame_offset+1] == 'i') && (fr->bsbuf[lame_offset+2] == 'n') && (fr->bsbuf[lame_offset+3] == 'g'))
	{
		// Xing header means always VBR
		fr->vbr = MPG123_VBR;
	}
	else goto check_lame_tag_no;

	lame_offset += 4;
	xing_flags = bit_read_long( fr->bsbuf, &lame_offset );

	// from now on, I have to carefully check if the announced data is actually
	// there! I'm always returning 'yes', though.
	if( xing_flags & 1 )
	{
		// total bitstream frames
		check_bytes_left( 4 );
		long_tmp = bit_read_long( fr->bsbuf, &lame_offset );
		if( fr->p.flags & MPG123_IGNORE_STREAMLENGTH )
		{
		}
		else
		{
			// check for endless stream, but: TRACK_MAX_FRAMES sensible at all?
			fr->track_frames = long_tmp > TRACK_MAX_FRAMES ? 0 : (mpg_off_t)long_tmp;

			// all or nothing: Only if encoder delay/padding is known, we'll cut
			// samples for gapless.
			if( fr->p.flags & MPG123_GAPLESS )
				frame_gapless_init( fr, fr->track_frames, 0, 0 );
		}
	}

	if( xing_flags & 0x2 )
	{
		// total bitstream bytes
		check_bytes_left( 4 );
		long_tmp = bit_read_long( fr->bsbuf, &lame_offset );

		if( fr->p.flags & MPG123_IGNORE_STREAMLENGTH )
		{
		}
		else
		{
			// the Xing bitstream length, at least as interpreted by the Lame
			// encoder, encompasses all data from the Xing header frame on,
			// ignoring leading ID3v2 data. Trailing tags (ID3v1) seem to be
			// included, though.
			if( fr->rdat.filelen < 1 )
			{
				fr->rdat.filelen = (mpg_off_t)long_tmp + fr->audio_start; // Overflow?
			}
		}
	}

	if( xing_flags & 0x4 ) // TOC
	{
		check_bytes_left( 100 );
		frame_fill_toc( fr, fr->bsbuf + lame_offset );
		lame_offset += 100;
	}

	// VBR quality
	if( xing_flags & 0x8 )
	{
		check_bytes_left( 4 );
		long_tmp = bit_read_long( fr->bsbuf, &lame_offset );
	}

	// either zeros/nothing, or:
	// 0-8: LAME3.90a
	// 9: revision/VBR method
	// 10: lowpass
	// 11-18: ReplayGain
	// 19: encoder flags
	// 20: ABR
	// 21-23: encoder delays
	check_bytes_left( 24 ); // I'm interested in 24 B of extra info.

	if( fr->bsbuf[lame_offset] != 0 )
	{
		byte	lame_vbr;
		float	replay_gain[2] = { 0, 0 };
		float	peak = 0;
		float	gain_offset = 0; // going to be +6 for old lame that used 83dB
		char	nb[10];
		mpg_off_t	pad_in;
		mpg_off_t	pad_out;

		memcpy( nb, fr->bsbuf + lame_offset, 9 );
		nb[9] = 0;

		if(!strncmp( "LAME", nb, 4 ))
		{
			uint	major, minor;
			char	rest[6];

			rest[0] = 0;

			// Lame versions before 3.95.1 used 83 dB reference level, later
			// versions 89 dB. We stick with 89 dB as being "normal", adding 6 dB.
			if( sscanf( nb + 4, "%u.%u%s", &major, &minor, rest ) >= 2 )
			{
				// We cannot detect LAME 3.95 reliably (same version string as
				// 3.95.1), so this is a blind spot. Everything < 3.95 is safe, though.
				if( major < 3 || ( major == 3 && minor < 95 ))
					gain_offset = 6;
			}
		}

		lame_offset += 9; // 9 in

		// the 4 big bits are tag revision, the small bits vbr method.
		lame_vbr = fr->bsbuf[lame_offset] & 15;
		lame_offset += 1; // 10 in

		// from rev1 proposal... not sure if all good in practice
		switch( lame_vbr )
		{
		case 1:
		case 8: fr->vbr = MPG123_CBR; break;
		case 2:
		case 9: fr->vbr = MPG123_ABR; break;
		default: fr->vbr = MPG123_VBR; break;	// 00 ==unknown is taken as VBR
		}

		lame_offset += 1; // 11 in, skipping lowpass filter value
		peak = 0; // until better times arrived
		lame_offset += 4; // 15 in

		// ReplayGain values - lame only writes radio mode gain...
		// 16bit gain, 3 bits name, 3 bits originator, sign (1=-, 0=+),
		// dB value * 10 in 9 bits (fixed point) ignore the setting if name or
		// originator == 000!
		// radio      0 0 1 0 1 1 1 0 0 1 1 1 1 1 0 1
		// audiophile 0 1 0 0 1 0 0 0 0 0 0 1 0 1 0 0
		for( i = 0; i < 2; ++i )
		{
			byte	gt = fr->bsbuf[lame_offset] >> 5;
			byte	origin = (fr->bsbuf[lame_offset] >> 2) & 0x7;
			float	factor = (fr->bsbuf[lame_offset] & 0x2) ? -0.1f : 0.1f;
			word	gain  = bit_read_short( fr->bsbuf, &lame_offset ) & 0x1ff; // 19 in (2 cycles)

			if( origin == 0 || gt < 1 || gt > 2 )
				continue;

			gt--;
			replay_gain[gt] = factor * (float)gain;

			// apply gain offset for automatic origin.
			if( origin == 3 ) replay_gain[gt] += gain_offset;
		}

		for( i = 0; i < 2; ++i )
		{
			if( fr->rva.level[i] <= 0 )
			{
				fr->rva.peak[i] = 0; // TODO: use parsed peak?
				fr->rva.gain[i] = replay_gain[i];
				fr->rva.level[i] = 0;
			}
		}

		lame_offset += 1; // 20 in, skipping encoding flags byte

		// ABR rate
		if( fr->vbr == MPG123_ABR )
			fr->abr_rate = fr->bsbuf[lame_offset];
		lame_offset += 1; // 21 in

		// Encoder delay and padding, two 12 bit values
		// ... lame does write them from int.
		pad_in  = ((((int) fr->bsbuf[lame_offset]) << 4) | (((int) fr->bsbuf[lame_offset+1]) >> 4));
		pad_out = ((((int) fr->bsbuf[lame_offset+1]) << 8) | ((int) fr->bsbuf[lame_offset+2])) & 0xfff;
		lame_offset += 3; // 24 in

		if( fr->p.flags & MPG123_GAPLESS )
			frame_gapless_init( fr, fr->track_frames, pad_in, pad_out );
		// final: 24 B LAME data
	}

check_lame_tag_yes:
	// switch buffer back ...
	fr->bsbuf = fr->bsspace[fr->bsnum] + 512;
	fr->bsnum = (fr->bsnum + 1) & 1;

	return 1;

check_lame_tag_no:
	return 0;
}

// first attempt of read ahead check to find the real first header; cannot believe what junk is out there!
static int do_readahead( mpg123_handle_t *fr, ulong newhead )
{
	ulong	nexthead = 0;
	int	hd = 0;
	mpg_off_t	start, oret;
	int	ret;

	if(!( !fr->firsthead && fr->rdat.flags & ( READER_SEEKABLE|READER_BUFFERED )))
		return PARSE_GOOD;

	start = fr->rd->tell( fr );

	// step framesize bytes forward and read next possible header
	if((oret = fr->rd->skip_bytes( fr, fr->framesize )) < 0 )
		return oret == MPG123_NEED_MORE ? PARSE_MORE : PARSE_ERR;

	// read header, seek back.
	hd = fr->rd->head_read( fr, &nexthead );

	if( fr->rd->back_bytes( fr, fr->rd->tell( fr ) - start ) < 0 )
		return PARSE_ERR;

	if( hd == MPG123_NEED_MORE )
		return PARSE_MORE;

	if( !hd ) return PARSE_END;

	if( !head_check( nexthead ) || !head_compatible( newhead, nexthead ))
	{
		fr->oldhead = 0; // start over

		// try next byte for valid header
		if(( ret = fr->rd->back_bytes( fr, 3 )) < 0 )
			return PARSE_ERR;
		return PARSE_AGAIN;
	}

	return PARSE_GOOD;
}

static void halfspeed_prepare( mpg123_handle_t *fr )
{
	if( fr->p.halfspeed && fr->lay == 3 )
		memcpy( fr->ssave, fr->bsbuf, fr->ssize );
}

// if this returns 1, the next frame is the repetition.
static int halfspeed_do( mpg123_handle_t *fr )
{
	// speed-down hack: Play it again, Sam (the frame, I mean).
	if( fr->p.halfspeed )
	{
		if( fr->halfphase ) // repeat last frame
		{
			fr->to_decode = fr->to_ignore = TRUE;
			fr->halfphase--;
			fr->bitindex = 0;
			fr->wordpointer = (byte *)fr->bsbuf;

			if( fr->lay == 3 )
				memcpy( fr->bsbuf, fr->ssave, fr->ssize );

			if( fr->error_protection )
				fr->crc = getbits( fr, 16 ); // skip crc
			return 1;
		}
		else
		{
			fr->halfphase = fr->p.halfspeed - 1;
		}
	}

	return 0;
}

// read ahead and find the next MPEG header, to guess framesize
// return value: success code
// PARSE_GOOD: found a valid frame size (stored in the handle).
// < 0: error codes, possibly from feeder buffer (NEED_MORE)
// PARSE_BAD: cannot get the framesize for some reason and shall silentry try the next possible header (if this is no free format stream after all...)
static int guess_freeformat_framesize( mpg123_handle_t *fr, ulong oldhead )
{
	ulong	head;
	int	ret;
	long	i;

	if(!( fr->rdat.flags & ( READER_SEEKABLE|READER_BUFFERED )))
		return PARSE_BAD;

	if(( ret = fr->rd->head_read( fr, &head )) <= 0 )
		return ret;

	// we are already 4 bytes into it
	for( i = 4; i < MAXFRAMESIZE + 4; i++ )
	{
		if(( ret = fr->rd->head_shift( fr, &head )) <= 0 )
			return ret;

		// no head_check needed, the mask contains all relevant bits.
		if(( head & HDR_SAMEMASK ) == ( oldhead & HDR_SAMEMASK ))
		{
			fr->rd->back_bytes( fr, i + 1 );
			fr->framesize = i - 3;
			return PARSE_GOOD;	// success!
		}
	}

	fr->rd->back_bytes( fr, i );

	return PARSE_BAD;
}

// decode a header and write the information
// into the frame structure
// return values are compatible with those of read_frame, namely:
//  1: success
//  0: no valid header
// <0: some error
// you are required to do a head_check() before calling!
static int decode_header( mpg123_handle_t *fr, ulong newhead, int *freeformat_count )
{
	// for some reason, the layer and sampling freq settings used to be wrapped
	// in a weird conditional including MPG123_NO_RESYNC. what was I thinking?
	// this information has to be consistent.
	fr->lay = 4 - HDR_LAYER_VAL( newhead );

	if( HDR_VERSION_VAL( newhead ) & 0x2 )
	{
		fr->lsf = (HDR_VERSION_VAL( newhead ) & 0x1 ) ? 0 : 1;
		fr->sampling_frequency = HDR_SAMPLERATE_VAL( newhead ) + (fr->lsf * 3);
		fr->mpeg25 = 0;
	}
	else
	{
		fr->sampling_frequency = 6 + HDR_SAMPLERATE_VAL( newhead );
		fr->mpeg25 = 1;
		fr->lsf = 1;
	}

	fr->error_protection = HDR_CRC_VAL( newhead ) ^ 0x1;
	fr->bitrate_index = HDR_BITRATE_VAL( newhead );
	fr->padding = HDR_PADDING_VAL( newhead );
	fr->extension = HDR_PRIVATE_VAL( newhead );
	fr->mode = HDR_CHANNEL_VAL( newhead );
	fr->mode_ext = HDR_CHANEX_VAL( newhead );
	fr->copyright = HDR_COPYRIGHT_VAL( newhead );
	fr->original = HDR_ORIGINAL_VAL( newhead );
	fr->emphasis = HDR_EMPHASIS_VAL( newhead );
	fr->freeformat = !( newhead & HDR_BITRATE );
	fr->stereo = (fr->mode == MPG_MD_MONO) ? 1 : 2;

	// we can't use tabsel_123 for freeformat, so trying to guess framesize...
	if( fr->freeformat )
	{
		// when we first encounter the frame with freeformat, guess framesize
		if( fr->freeformat_framesize < 0 )
		{
			int	ret;

			*freeformat_count += 1;
			if( *freeformat_count > 5 )
				return PARSE_BAD;

			ret = guess_freeformat_framesize( fr, newhead );

			if( ret == PARSE_GOOD )
			{
				fr->freeformat_framesize = fr->framesize - fr->padding;
			}
			else
			{
				return ret;
			}
		}
		else
		{
			// freeformat should be CBR, so the same framesize can be used at the 2nd reading or later
			fr->framesize = fr->freeformat_framesize + fr->padding;
		}
	}

	switch( fr->lay )
	{
	case 3:
		fr->spf = fr->lsf ? 576 : 1152; /* MPEG 2.5 implies LSF.*/
		fr->do_layer = do_layer3;
		if( fr->lsf ) fr->ssize = (fr->stereo == 1) ? 9 : 17;
		else fr->ssize = (fr->stereo == 1) ? 17 : 32;

		if( fr->error_protection )
			fr->ssize += 2;

		if( !fr->freeformat )
		{
			fr->framesize  = (long)tabsel_123[fr->lsf][2][fr->bitrate_index] * 144000;
			fr->framesize /= freqs[fr->sampling_frequency]<<(fr->lsf);
			fr->framesize = fr->framesize + fr->padding - 4;
		}
		break;
	default:
		return PARSE_BAD;
	}

	if( fr->framesize > MAXFRAMESIZE )
		return PARSE_BAD;

	return PARSE_GOOD;
}

// advance a byte in stream to get next possible header and forget
// buffered data if possible (for feed reader).
static int forget_head_shift( mpg123_handle_t *fr, ulong *newheadp, int forget )
{
	int	ret;

	if(( ret = fr->rd->head_shift( fr, newheadp )) <= 0 )
		return ret;

	// try to forget buffered data as early as possible to speed up parsing where
	// new data needs to be added for resync (and things would be re-parsed again
	// and again because of the start from beginning after hitting end).
	if( forget && fr->rd->forget != NULL )
	{
		// ensure that the last 4 bytes stay in buffers for reading the header anew.
		if(!fr->rd->back_bytes( fr, 4 ))
		{
			fr->rd->forget( fr );
			fr->rd->back_bytes( fr, -4 );
		}
	}

	return ret; // No surprise here, error already triggered early return.
}

// trying to parse ID3v2.3 and ID3v2.4 tags...
// returns:  0: bad or just unparseable tag
//           1: good, (possibly) new tag info
//          <0: reader error (may need more data feed, try again)
static int parse_new_id3( mpg123_handle_t *fr, ulong first4bytes )
{
	byte	buf[6];
	ulong	length=0;
	byte	flags = 0;
	int	ret = 1;
	int	ret2;
	byte	major = (byte)(first4bytes & 0xff);

	if( major == 0xff )
		return 0;

	if(( ret2 = fr->rd->read_frame_body( fr, buf, 6 )) < 0 ) // read more header information
		return ret2;

	if( buf[0] == 0xff )
		return 0; // revision, will never be 0xff.

	// second new byte are some nice flags, if these are invalid skip the whole thing
	flags = buf[1];

	// length-10 or length-20 (footer present); 4 synchsafe integers == 28 bit number
	// we have already read 10 bytes, so left are length or length+10 bytes belonging to tag
	if( !synchsafe_to_long( buf + 2, length ))
		return 0;

	if(( ret2 = fr->rd->skip_bytes( fr, length )) < 0 ) // will not store data in backbuff!
		ret = ret2;

	// skip footer if present
	if(( ret > 0 ) && ( flags & 16 ) && (( ret2 = fr->rd->skip_bytes( fr, length )) < 0 ))
		ret = ret2;

	return ret;
}

static int handle_id3v2( mpg123_handle_t *fr, ulong newhead )
{
	int	ret;

	fr->oldhead = 0;	// think about that. Used to be present only for skipping of junk, not resync-style wetwork.
	ret = parse_new_id3( fr, newhead );
	if( ret < 0 ) return ret;

	return PARSE_AGAIN;
}

// watch out for junk/tags on beginning of stream by invalid header
static int skip_junk( mpg123_handle_t *fr, ulong *newheadp, long *headcount )
{
	int	ret;
	int	freeformat_count = 0;
	ulong	newhead = *newheadp;
	uint	forgetcount = 0;
	long	limit = 65536;

	// check for id3v2; first three bytes (of 4) are "ID3"
	if(( newhead & (ulong)0xffffff00 ) == (ulong)0x49443300 )
	{
		return handle_id3v2( fr, newhead );
	}

	// I even saw RIFF headers at the beginning of MPEG streams ;(
	if( newhead == ('R'<<24)+('I'<<16)+('F'<<8)+'F' )
	{
		if(( ret = fr->rd->head_read( fr, &newhead )) <= 0 )
			return ret;

		while( newhead != ('d'<<24)+('a'<<16)+('t'<<8)+'a' )
		{
			if( ++forgetcount > FORGET_INTERVAL )
				forgetcount = 0;

			if(( ret = forget_head_shift( fr, &newhead, !forgetcount )) <= 0 )
				return ret;
		}

		if(( ret = fr->rd->head_read( fr, &newhead )) <= 0 )
			return ret;

		fr->oldhead = 0;
		*newheadp = newhead;

		return PARSE_AGAIN;
	}

	// unhandled junk... just continue search for a header, stepping in single bytes through next 64K.
	// this is rather identical to the resync loop.
	*newheadp = 0;	// invalidate the external value.
	ret = 0;		// we will check the value after the loop.

	// we prepare for at least the 64K bytes as usual, unless
	// user explicitly wanted more (even infinity). Never less.
	if( fr->p.resync_limit < 0 || fr->p.resync_limit > limit )
		limit = fr->p.resync_limit;

	do
	{
		++(*headcount);
		if( limit >= 0 && *headcount >= limit )
			break;

		if( ++forgetcount > FORGET_INTERVAL )
			forgetcount = 0;

		if(( ret = forget_head_shift( fr, &newhead, !forgetcount )) <= 0 )
			return ret;

		if( head_check( newhead ) && (ret = decode_header( fr, newhead, &freeformat_count )))
			break;
	} while( 1 );

	if( ret < 0 )
		return ret;

	if( limit >= 0 && *headcount >= limit )
		return PARSE_END;

	// If the new header ist good, it is already decoded.
	*newheadp = newhead;

	return PARSE_GOOD;
}

// the newhead is bad, so let's check if it is something special, otherwise just resync.
static int wetwork( mpg123_handle_t *fr, ulong *newheadp )
{
	int	ret = PARSE_ERR;
	ulong	newhead = *newheadp;

	*newheadp = 0;

	// classic ID3 tags. Read, then start parsing again.
	if(( newhead & 0xffffff00 ) == ( 'T'<<24 )+( 'A'<<16 )+( 'G'<<8 ))
	{
		fr->id3buf[0] = (byte)((newhead >> 24) & 0xff);
		fr->id3buf[1] = (byte)((newhead >> 16) & 0xff);
		fr->id3buf[2] = (byte)((newhead >> 8)  & 0xff);
		fr->id3buf[3] = (byte)( newhead        & 0xff);

		if(( ret = fr->rd->fullread( fr, fr->id3buf + 4, 124 )) < 0 )
			return ret;

		fr->metaflags  |= MPG123_NEW_ID3|MPG123_ID3;
		fr->rdat.flags |= READER_ID3TAG; // that marks id3v1

		return PARSE_AGAIN;
	}

	// this is similar to initial junk skipping code...
	// check for id3v2; first three bytes (of 4) are "ID3"
	if(( newhead & (ulong)0xffffff00 ) == (ulong)0x49443300 )
	{
		return handle_id3v2( fr, newhead );
	}

	// now we got something bad at hand, try to recover.
	if( !( fr->p.flags & MPG123_NO_RESYNC ))
	{
		long	try = 0;
		long	limit = fr->p.resync_limit;
		uint	forgetcount = 0;

		// if a resync is needed the bitreservoir of previous frames is no longer valid
		fr->bitreservoir = 0;

		do	// ... shift the header with additional single bytes until be found something that could be a header.
		{
			try++;

			if( limit >= 0 && try >= limit )
				break;

			if( ++forgetcount > FORGET_INTERVAL )
				forgetcount = 0;

			if(( ret = forget_head_shift( fr, &newhead, !forgetcount )) <= 0 )
			{
				*newheadp = newhead;
				return ret ? ret : PARSE_END;
			}
		} while( !head_check( newhead ));

		*newheadp = newhead;

		// now we either got something that could be a header, or we gave up.
		if( limit >= 0 && try >= limit )
		{
			fr->err = MPG123_RESYNC_FAIL;
			return PARSE_ERR;
		}
		else
		{
			fr->oldhead = 0;
			return PARSE_RESYNC;
		}
	}
	else
	{
		fr->err = MPG123_OUT_OF_SYNC;
		return PARSE_ERR;
	}
}

// that's a big one: read the next frame. 1 is success, <= 0 is some error
// special error READER_MORE means: Please feed more data and try again.
int read_frame( mpg123_handle_t *fr )
{
	// TODO: rework this thing
	int	freeformat_count = 0;
	int	oldsize  = fr->framesize;
	int	oldphase = fr->halfphase;
	long	headcount = 0;
	byte	*newbuf;
	ulong	newhead;
	mpg_off_t	framepos;
	int	ret;

	fr->fsizeold = fr->framesize;	// for Layer3

	if( halfspeed_do( fr ) == 1 )
		return 1;

read_again:
	// in case we are looping to find a valid frame, discard any buffered data before the current position.
	// this is essential to prevent endless looping, always going back to the beginning when feeder buffer is exhausted.
	if( fr->rd->forget != NULL )
		fr->rd->forget( fr );

	if(( ret = fr->rd->head_read( fr, &newhead )) <= 0 )
		goto read_frame_bad;
init_resync:
	if( !fr->firsthead && !head_check( newhead ))
	{
		ret = skip_junk(fr, &newhead, &headcount);

		if( ret < 0 )
			goto read_frame_bad;
		else if( ret == PARSE_AGAIN )
			goto read_again;
		else if( ret == PARSE_RESYNC )
			goto init_resync;
		else if( ret == PARSE_END )
		{
			ret = 0;
			goto read_frame_bad;
		}
	}

	ret = head_check( newhead );
	if( ret ) ret = decode_header( fr, newhead, &freeformat_count );

	if( ret < 0 )
		goto read_frame_bad;
	else if( ret == PARSE_AGAIN )
		goto read_again;
	else if( ret == PARSE_RESYNC )
		goto init_resync;
	else if( ret == PARSE_END )
	{
		ret = 0;
		goto read_frame_bad;
	}

	if( ret == PARSE_BAD )
	{
		// header was not good.
		ret = wetwork( fr, &newhead ); // Messy stuff, handle junk, resync ...

		if( ret < 0 )
			goto read_frame_bad;
		else if( ret == PARSE_AGAIN )
			goto read_again;
		else if( ret == PARSE_RESYNC )
			goto init_resync;
		else if( ret == PARSE_END )
		{
			ret = 0;
			goto read_frame_bad;
		}

		// normally, we jumped already.
		// if for some reason everything's fine to continue, do continue.
		if( ret != PARSE_GOOD )
			goto read_frame_bad;
	}

	if( !fr->firsthead )
	{
		ret = do_readahead( fr, newhead );

		// readahead can fail mit NEED_MORE, in which case we must also make
		// the just read header available again for next go
		if( ret < 0 ) fr->rd->back_bytes( fr, 4 );

		if( ret < 0 )
			goto read_frame_bad;
		else if( ret == PARSE_AGAIN )
			goto read_again;
		else if( ret == PARSE_RESYNC )
			goto init_resync;
		else if( ret == PARSE_END )
		{
			ret = 0;
			goto read_frame_bad;
		}
	}

	// now we should have our valid header and proceed to reading the frame.

	// if filepos is invalid, so is framepos
	framepos = fr->rd->tell( fr ) - 4;

	// flip/init buffer for Layer 3
	newbuf = fr->bsspace[fr->bsnum] + 512;

	// read main data into memory
	if(( ret = fr->rd->read_frame_body( fr, newbuf, fr->framesize )) < 0 )
	{
		// if failed: flip back
		goto read_frame_bad;
	}

	fr->bsbufold = fr->bsbuf;
	fr->bsbuf = newbuf;
	fr->bsnum = (fr->bsnum + 1) & 1;

	if( !fr->firsthead )
	{
		fr->firsthead = newhead; // _now_ it's time to store it... the first real header */
		// this is the first header of our current stream segment.
		// it is only the actual first header of the whole stream when fr->num is still below zero!
		// think of resyncs where firsthead has been reset for format flexibility.
		if( fr->num < 0 )
		{
			fr->audio_start = framepos;

			// only check for LAME tag at beginning of whole stream
			// ... when there indeed is one in between, it's the user's problem.
			if( fr->lay == 3 && check_lame_tag( fr ) == 1 )
			{
				// ...in practice, Xing/LAME tags are layer 3 only.
				if( fr->rd->forget != NULL )
					fr->rd->forget( fr );

				fr->oldhead = 0;
				goto read_again;
			}

			// now adjust volume
			do_rva( fr );
		}
	}

	fr->bitindex = 0;
	fr->wordpointer = (byte *)fr->bsbuf;

	// question: How bad does the floating point value get with repeated recomputation?
	// also, considering that we can play the file or parts of many times.
	if( ++fr->mean_frames != 0 )
		fr->mean_framesize = ((fr->mean_frames - 1) * fr->mean_framesize + compute_bpf( fr )) / fr->mean_frames ;

	fr->num++; // 0 for first frame!

	if(!( fr->state_flags & FRAME_FRANKENSTEIN ) && (( fr->track_frames > 0 && fr->num >= fr->track_frames ) || ( fr->gapless_frames > 0 && fr->num >= fr->gapless_frames )))
		fr->state_flags |= FRAME_FRANKENSTEIN;

	halfspeed_prepare( fr );

	// index the position
	fr->input_offset = framepos;

	// keep track of true frame positions in our frame index.
	// but only do so when we are sure that the frame number is accurate...
	if(( fr->state_flags & FRAME_ACCURATE ) && FI_NEXT( fr->index, fr->num ))
		fi_add( &fr->index, framepos );

	if( fr->silent_resync > 0 )
		fr->silent_resync--;

	if( fr->rd->forget != NULL )
		fr->rd->forget( fr );

	fr->to_decode = fr->to_ignore = TRUE;
	if( fr->error_protection )
		fr->crc = getbits( fr, 16 ); // skip crc

	// let's check for header change after deciding that the new one is good
	// and actually having read a frame.
	// header_change > 1: decoder structure has to be updated
	// preserve header_change value from previous runs if it is serious.
	// If we still have a big change pending, it should be dealt with outside,
	// fr->header_change set to zero afterwards.
	if( fr->header_change < 2 )
	{
		fr->header_change = 2;	// output format change is possible...
		if( fr->oldhead )		// check a following header for change
		{
			if( fr->oldhead == newhead )
			{
				fr->header_change = 0;
			}
			else if( head_compatible( fr->oldhead, newhead ))
			{
				// headers that match in this test behave the same for the outside world.
				// namely: same decoding routines, same amount of decoded data.
				fr->header_change = 1;
			}
			else
			{
				fr->state_flags |= FRAME_FRANKENSTEIN;
			}
		}
		else if( fr->firsthead && !head_compatible( fr->firsthead, newhead ))
		{
			fr->state_flags |= FRAME_FRANKENSTEIN;
		}
	}

	fr->oldhead = newhead;

	return 1;
read_frame_bad:

	// also if we searched for valid data in vein, we can forget skipped data.
	// otherwise, the feeder would hold every dead old byte in memory until the first valid frame!
	if( fr->rd->forget != NULL )
		fr->rd->forget( fr );

	fr->silent_resync = 0;
	if( fr->err == MPG123_OK )
		fr->err = MPG123_ERR_READER;
	fr->framesize = oldsize;
	fr->halfphase = oldphase;

	// that return code might be inherited from some feeder action, or reader error.
	return ret;
}
