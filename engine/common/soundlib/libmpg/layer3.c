/*
layer3.c - compact version of famous library mpg123
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

#include "mpg123.h"
#include "huffman.h"
#include "getbits.h"
#include <math.h>
 
// static one-time calculated tables... or so
float		COS6_1;		// dct12 wants to use that
float		COS6_2;		// dct12 wants to use that
float		cos9[3];		// dct36 wants to use that
float		cos18[3];		// dct36 wants to use that
float		tfcos12[3];	// dct12 wants to use that
float		tfcos36[9];	// dct36 wants to use that
static float	ispow[8207];
static float	COS9[9];
static float	aa_ca[8];
static float	aa_cs[8];
static float	win[4][36];
static float	win1[4][36];
static float	tan1_1[16];
static float	tan2_1[16];
static float	tan1_2[16];
static float	tan2_2[16];
static float	pow1_1[2][16];
static float	pow2_1[2][16];
static float	pow1_2[2][16];
static float	pow2_2[2][16];
static int	mapbuf0[9][152];
static int	mapbuf1[9][156];
static int	mapbuf2[9][44];
static int	*map[9][3];
static int	*mapend[9][3];
static uint	n_slen2[512];	// MPEG 2.0 slen for 'normal' mode
static uint	i_slen2[256];	// MPEG 2.0 slen for intensity stereo

// Decoder state data, living on the stack of do_layer3.
typedef struct gr_info_s
{
	int	scfsi;
	uint	part2_3_length;
	uint	big_values;
	uint	scalefac_compress;
	uint	block_type;
	uint	mixed_block_flag;
	uint	table_select[3];

	// making those two signed int as workaround for open64/pathscale/sun compilers,
	// and also for consistency, since they're worked on together with other signed variables.
	int	maxband[3];
	int	maxbandl;
	uint	maxb;
	uint	region1start;
	uint	region2start;
	uint	preflag;
	uint	scalefac_scale;
	uint	count1table_select;
	float	*full_gain[3];
	float	*pow2gain;
} gr_info_t;

typedef struct 
{
	uint	main_data_begin;
	uint	private_bits;

	// hm, funny... struct inside struct...
	struct
	{
		gr_info_t	gr[2];
	} ch[2];
} III_sideinfo;

typedef struct 
{
	word	longIdx[23];
	byte	longDiff[22];
	word	shortIdx[14];
	byte	shortDiff[13];
} bandInfoStruct;

// techy details about our friendly MPEG data. Fairly constant over the years ;-)
static const bandInfoStruct bandInfo[9] =
{
	{ // MPEG 1.0
	{0,4,8,12,16,20,24,30,36,44,52,62,74, 90,110,134,162,196,238,288,342,418,576},
	{4,4,4,4,4,4,6,6,8, 8,10,12,16,20,24,28,34,42,50,54, 76,158},
	{0,4*3,8*3,12*3,16*3,22*3,30*3,40*3,52*3,66*3, 84*3,106*3,136*3,192*3},
	{4,4,4,4,6,8,10,12,14,18,22,30,56}
	},
	{
	{0,4,8,12,16,20,24,30,36,42,50,60,72, 88,106,128,156,190,230,276,330,384,576},
	{4,4,4,4,4,4,6,6,6, 8,10,12,16,18,22,28,34,40,46,54, 54,192},
	{0,4*3,8*3,12*3,16*3,22*3,28*3,38*3,50*3,64*3, 80*3,100*3,126*3,192*3},
	{4,4,4,4,6,6,10,12,14,16,20,26,66}
	},
	{
	{0,4,8,12,16,20,24,30,36,44,54,66,82,102,126,156,194,240,296,364,448,550,576},
	{4,4,4,4,4,4,6,6,8,10,12,16,20,24,30,38,46,56,68,84,102, 26},
	{0,4*3,8*3,12*3,16*3,22*3,30*3,42*3,58*3,78*3,104*3,138*3,180*3,192*3},
	{4,4,4,4,6,8,12,16,20,26,34,42,12}
	},
	{ // MPEG 2.0
	{0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
	{6,6,6,6,6,6,8,10,12,14,16,20,24,28,32,38,46,52,60,68,58,54 } ,
	{0,4*3,8*3,12*3,18*3,24*3,32*3,42*3,56*3,74*3,100*3,132*3,174*3,192*3} ,
	{4,4,4,6,6,8,10,14,18,26,32,42,18 }
	},
	{ // twiddling 3 values here (not just 330->332!) fixed bug 1895025.
	{0,6,12,18,24,30,36,44,54,66,80,96,114,136,162,194,232,278,332,394,464,540,576},
	{6,6,6,6,6,6,8,10,12,14,16,18,22,26,32,38,46,54,62,70,76,36 },
	{0,4*3,8*3,12*3,18*3,26*3,36*3,48*3,62*3,80*3,104*3,136*3,180*3,192*3},
	{4,4,4,6,8,10,12,14,18,24,32,44,12 }
	},
	{
	{0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
	{6,6,6,6,6,6,8,10,12,14,16,20,24,28,32,38,46,52,60,68,58,54 },
	{0,4*3,8*3,12*3,18*3,26*3,36*3,48*3,62*3,80*3,104*3,134*3,174*3,192*3},
	{4,4,4,6,8,10,12,14,18,24,30,40,18 }
	},
	{ // MPEG 2.5
	{0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
	{6,6,6,6,6,6,8,10,12,14,16,20,24,28,32,38,46,52,60,68,58,54},
	{0,12,24,36,54,78,108,144,186,240,312,402,522,576},
	{4,4,4,6,8,10,12,14,18,24,30,40,18}
	},
	{
	{0,6,12,18,24,30,36,44,54,66,80,96,116,140,168,200,238,284,336,396,464,522,576},
	{6,6,6,6,6,6,8,10,12,14,16,20,24,28,32,38,46,52,60,68,58,54},
	{0,12,24,36,54,78,108,144,186,240,312,402,522,576},
	{4,4,4,6,8,10,12,14,18,24,30,40,18}
	},
	{
	{0,12,24,36,48,60,72,88,108,132,160,192,232,280,336,400,476,566,568,570,572,574,576},
	{12,12,12,12,12,12,16,20,24,28,32,40,48,56,64,76,90,2,2,2,2,2},
	{0, 24, 48, 72,108,156,216,288,372,480,486,492,498,576},
	{8,8,8,12,16,20,24,28,36,2,2,2,26}
	}
};

static byte pretab_choice[2][22] =
{
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,2,0}
};

// init tables for layer-3 ... specific with the downsampling...
void init_layer3( void )
{
	int	i, j, k, l;

	for( i = 0; i < 8207; i++ )
		ispow[i] = DOUBLE_TO_REAL_POW43( pow( (double)i, (double)4.0 / 3.0 ));

	for( i = 0; i < 8; i++ )
	{
		const double Ci[8] = { -0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037 };
		double sq = sqrt( 1.0 + Ci[i] * Ci[i] );
		aa_cs[i] = DOUBLE_TO_REAL( 1.0 / sq );
		aa_ca[i] = DOUBLE_TO_REAL( Ci[i] / sq );
	}

	for( i = 0; i < 18; i++ )
	{
		win[0][i] = win[1][i] = DOUBLE_TO_REAL( 0.5 * sin( M_PI / 72.0 * (double)(2 * (i + 0) + 1)) / cos( M_PI * (double)(2 * (i + 0) + 19) / 72.0) );
		win[0][i+18] = win[3][i+18] = DOUBLE_TO_REAL( 0.5 * sin( M_PI/72.0 * (double)(2 * (i + 18) + 1)) / cos( M_PI * (double)(2 * (i + 18) + 19) / 72.0) );
	}

	for( i = 0; i < 6; i++ )
	{
		win[1][i+18] = DOUBLE_TO_REAL( 0.5 / cos ( M_PI * (double)(2 * (i + 18) + 19) / 72.0 ));
		win[3][i+12] = DOUBLE_TO_REAL( 0.5 / cos ( M_PI * (double)(2 * (i + 12) + 19) / 72.0 ));
		win[1][i+24] = DOUBLE_TO_REAL( 0.5 * sin( M_PI / 24.0 * (double)(2 * i + 13)) / cos( M_PI * (double)(2 * (i + 24) + 19) / 72.0 ));
		win[1][i+30] = win[3][i] = DOUBLE_TO_REAL( 0.0 );
		win[3][i+6 ] = DOUBLE_TO_REAL( 0.5 * sin( M_PI / 24.0 * (double)(2 * i + 1)) / cos( M_PI * (double)(2 * (i + 6 ) + 19) / 72.0 ));
	}

	for( i = 0; i < 9; i++ )
		COS9[i] = DOUBLE_TO_REAL( cos( M_PI / 18.0 * (double)i ));

	for( i = 0; i < 9; i++ )
		tfcos36[i] = DOUBLE_TO_REAL( 0.5 / cos( M_PI * (double)(i * 2 + 1) / 36.0 ));

	for( i = 0; i < 3; i++ )
		tfcos12[i] = DOUBLE_TO_REAL( 0.5 / cos( M_PI * (double)(i * 2 + 1) / 12.0 ));

	COS6_1 = DOUBLE_TO_REAL( cos( M_PI / 6.0 * (double)1 ));
	COS6_2 = DOUBLE_TO_REAL( cos( M_PI / 6.0 * (double)2 ));

	cos9[0]  = DOUBLE_TO_REAL( cos( 1.0 * M_PI / 9.0));
	cos9[1]  = DOUBLE_TO_REAL( cos( 5.0 * M_PI / 9.0));
	cos9[2]  = DOUBLE_TO_REAL( cos( 7.0 * M_PI / 9.0));
	cos18[0] = DOUBLE_TO_REAL( cos( 1.0 * M_PI / 18.0));
	cos18[1] = DOUBLE_TO_REAL( cos( 11.0 * M_PI / 18.0));
	cos18[2] = DOUBLE_TO_REAL( cos( 13.0 * M_PI / 18.0));

	for( i = 0; i < 12; i++ )
		win[2][i] = DOUBLE_TO_REAL( 0.5 * sin( M_PI / 24.0 * (double)(2 * i + 1) ) / cos( M_PI * (double)(2 * i + 7) / 24.0 ));

	for( i = 0; i < 16; i++ )
	{
		double t = tan((double)i * M_PI / 12.0 );
		tan1_1[i] = DOUBLE_TO_REAL_15( t / (1.0 + t));
		tan2_1[i] = DOUBLE_TO_REAL_15( 1.0 / (1.0 + t));
		tan1_2[i] = DOUBLE_TO_REAL_15( M_SQRT2 * t / (1.0 + t));
		tan2_2[i] = DOUBLE_TO_REAL_15( M_SQRT2 / (1.0 + t));

		for( j = 0; j < 2; j++ )
		{
			double base = pow( 2.0, -0.25 * (j + 1.0));
			double p1 = 1.0, p2 = 1.0;

			if( i > 0 )
			{
				if( i & 1 ) p1 = pow( base,(i + 1.0) * 0.5);
				else p2 = pow( base, i * 0.5 );
			}

			pow1_1[j][i] = DOUBLE_TO_REAL_15( p1 );
			pow2_1[j][i] = DOUBLE_TO_REAL_15( p2 );
			pow1_2[j][i] = DOUBLE_TO_REAL_15( M_SQRT2 * p1 );
			pow2_2[j][i] = DOUBLE_TO_REAL_15( M_SQRT2 * p2 );
		}
	}

	for( j = 0; j < 4; j++ )
	{
		const int len[4] = { 36, 36, 12, 36 };

		for( i = 0; i < len[j]; i += 2 )
			win1[j][i] = +win[j][i];

		for( i = 1; i < len[j]; i += 2 )
			win1[j][i] = -win[j][i];
	}

	for( j = 0; j < 9; j++ )
	{
		const bandInfoStruct	*bi = &bandInfo[j];
		int			cb, lwin;
		const byte		*bdf;
		int			*mp;

		mp = map[j][0] = mapbuf0[j];
		bdf = bi->longDiff;

		for( i = 0, cb = 0; cb < 8 ; cb++, i += *bdf++ )
		{
			*mp++ = (*bdf) >> 1;
			*mp++ = i;
			*mp++ = 3;
			*mp++ = cb;
		}

		bdf = bi->shortDiff + 3;
		for( cb = 3;cb < 13; cb++ )
		{
			int l = (*bdf++) >> 1;
			for( lwin = 0; lwin < 3; lwin++ )
			{
				*mp++ = l;
				*mp++ = i + lwin;
				*mp++ = lwin;
				*mp++ = cb;
			}
			i += 6 * l;
		}

		mapend[j][0] = mp;
		mp = map[j][1] = mapbuf1[j];
		bdf = bi->shortDiff + 0;

		for( i = 0, cb = 0; cb < 13; cb++ )
		{
			int l = (*bdf++) >> 1;
			for( lwin = 0; lwin < 3; lwin++ )
			{
				*mp++ = l;
				*mp++ = i + lwin;
				*mp++ = lwin;
				*mp++ = cb;
			}
			i += 6 * l;
		}

		mapend[j][1] = mp;
		mp = map[j][2] = mapbuf2[j];
		bdf = bi->longDiff;

		for( cb = 0; cb < 22; cb++ )
		{
			*mp++ = (*bdf++) >> 1;
			*mp++ = cb;
		}
		mapend[j][2] = mp;
	}

	// now for some serious loopings!
	for( i = 0; i < 5; i++ )
	{
		for( j = 0; j < 6; j++ )
		{
			for( k = 0; k < 6; k++ )
			{
				int n = k + j * 6 + i * 36;
				i_slen2[n] = i|(j<<3)|(k<<6)|(3<<12);
			}
		}
	}

	for( i = 0; i < 4; i++ )
	{
		for( j = 0; j < 4; j++ )
		{
			for( k = 0; k < 4; k++ )
			{
				int n = k + j * 4 + i * 16;
				i_slen2[n+180] = i|(j<<3)|(k<<6)|(4<<12);
			}
		}
	}

	for( i = 0; i < 4; i++ )
	{
		for( j = 0; j < 3; j++ )
		{
			int n = j + i * 3;
			i_slen2[n+244] = i|(j<<3) | (5<<12);
			n_slen2[n+500] = i|(j<<3) | (2<<12) | (1<<15);
		}
	}

	for( i = 0; i < 5; i++ )
	{
		for( j = 0; j < 5; j++ )
		{
			for( k = 0; k < 4; k++ )
			{
				for( l = 0; l < 4; l++ )
				{
					int n = l + k * 4 + j * 16 + i * 80;
					n_slen2[n] = i|(j<<3)|(k<<6)|(l<<9)|(0<<12);
				}
			}
		}
	}

	for( i = 0; i < 5; i++ )
	{
		for( j = 0; j < 5; j++ )
		{
			for( k = 0; k < 4; k++ )
			{
				int n = k + j * 4 + i * 20;
				n_slen2[n+400] = i|(j<<3)|(k<<6)|(1<<12);
			}
		}
	}
}

void init_layer3_stuff( mpg123_handle_t *fr )
{
	int i,j;

	for( i = -256; i < 118 + 4; i++ )
		fr->gainpow2[i+256] = DOUBLE_TO_REAL_SCALE_LAYER3( pow((double)2.0, -0.25 * (double)(i + 210)), i + 256 );

	for( j = 0; j < 9; j++ )
	{
		for( i = 0; i < 23; i++ )
		{
			fr->longLimit[j][i] = (bandInfo[j].longIdx[i] - 1 + 8) / 18 + 1;

			if( fr->longLimit[j][i] > fr->down_sample_sblimit )
				fr->longLimit[j][i] = fr->down_sample_sblimit;
		}

		for( i = 0; i < 14; i++ )
		{
			fr->shortLimit[j][i] = (bandInfo[j].shortIdx[i] - 1) / 18 + 1;

			if( fr->shortLimit[j][i] > fr->down_sample_sblimit )
				fr->shortLimit[j][i] = fr->down_sample_sblimit;
		}
	}
}

// read additional side information (for MPEG 1 and MPEG 2)
static int III_get_side_info( mpg123_handle_t *fr, III_sideinfo *si, int stereo, int ms_stereo, long sfreq, int single )
{
	int	powdiff = (single == SINGLE_MIX) ? 4 : 0;
	const int	tabs[2][5] = { { 2,9,5,3,4 } , { 1,8,1,2,9 } };
	const int	*tab = tabs[fr->lsf];
	int	ch, gr;

	si->main_data_begin = getbits( fr, tab[1] );

	if( si->main_data_begin > fr->bitreservoir )
	{
		//  overwrite main_data_begin for the floatly available bit reservoir
		backbits( fr, tab[1] );

		if( fr->lsf == 0 )
		{
			fr->wordpointer[0] = (byte)(fr->bitreservoir >> 1);
			fr->wordpointer[1] = (byte)((fr->bitreservoir & 1) << 7);
		}
		else fr->wordpointer[0] = (byte)fr->bitreservoir;

		// zero "side-info" data for a silence-frame
		// without touching audio data used as bit reservoir for following frame
		memset( fr->wordpointer + 2, 0, fr->ssize - 2 );

		// reread the new bit reservoir offset
		si->main_data_begin = getbits( fr, tab[1] );
	}

	// keep track of the available data bytes for the bit reservoir.
	// think: Substract the 2 crc bytes in parser already?
	fr->bitreservoir = fr->bitreservoir + fr->framesize - fr->ssize - (fr->error_protection ? 2 : 0);
	// limit the reservoir to the max for MPEG 1.0 or 2.x.
	if( fr->bitreservoir > (uint)(fr->lsf == 0 ? 511 : 255 ))
		fr->bitreservoir = (fr->lsf == 0 ? 511 : 255);

	// now back into less commented territory. It's code. It works.

	if( stereo == 1 ) si->private_bits = getbits_fast( fr, tab[2] );
	else si->private_bits = getbits_fast( fr, tab[3] );

	if( !fr->lsf )
	{
		for( ch = 0; ch < stereo; ch++ )
		{
			si->ch[ch].gr[0].scfsi = -1;
			si->ch[ch].gr[1].scfsi = getbits_fast( fr, 4 );
		}
	}

	for( gr = 0; gr < tab[0]; gr++ )
	{
		for( ch = 0; ch < stereo; ch++ )
		{
			register gr_info_t	*gr_info = &( si->ch[ch].gr[gr] );

			gr_info->part2_3_length = getbits( fr, 12 );
			gr_info->big_values = getbits( fr, 9 );

			if( gr_info->big_values > 288 )
				gr_info->big_values = 288;

			gr_info->pow2gain = fr->gainpow2 + 256 - getbits_fast( fr, 8 ) + powdiff;
			if( ms_stereo ) gr_info->pow2gain += 2;

			gr_info->scalefac_compress = getbits( fr, tab[4] );

			if( get1bit( fr ))
			{
				int	i;

				// window switch flag
				gr_info->block_type = getbits_fast( fr, 2 );
				gr_info->mixed_block_flag = get1bit( fr );
				gr_info->table_select[0] = getbits_fast( fr, 5 );
				gr_info->table_select[1] = getbits_fast( fr, 5 );

				// table_select[2] not needed, because there is no region2,
				// but to satisfy some verification tools we set it either.
				gr_info->table_select[2] = 0;

				for( i = 0; i < 3; i++ )
					gr_info->full_gain[i] = gr_info->pow2gain + (getbits_fast( fr, 3 ) << 3);

				if( gr_info->block_type == 0 )
					return 1;

				// region_count/start parameters are implicit in this case.
				if(( !fr->lsf || ( gr_info->block_type == 2 )) && !fr->mpeg25 )
				{
					gr_info->region1start = 36 >> 1;
					gr_info->region2start = 576 >> 1;
				}
				else
				{
					if( fr->mpeg25 )
					{ 
						int	r0c, r1c;

						if(( gr_info->block_type == 2 ) && ( !gr_info->mixed_block_flag ))
							r0c = 5;
						else r0c = 7;

						// r0c + 1 + r1c + 1 == 22, always.
						r1c = 20 - r0c;
						gr_info->region1start = bandInfo[sfreq].longIdx[r0c+1] >> 1 ;
						gr_info->region2start = bandInfo[sfreq].longIdx[r0c+1+r1c+1] >> 1; 
					}
					else
					{
						gr_info->region1start = 54 >> 1;
						gr_info->region2start = 576 >> 1; 
					} 
				}
			}
			else
			{
				int	i, r0c, r1c;

				for( i = 0; i < 3; i++ )
					gr_info->table_select[i] = getbits_fast( fr, 5 );

				r0c = getbits_fast( fr, 4 ); // 0 .. 15
				r1c = getbits_fast( fr, 3 ); // 0 .. 7
				gr_info->region1start = bandInfo[sfreq].longIdx[r0c+1] >> 1 ;

				// max( r0c + r1c + 2 ) = 15 + 7 + 2 = 24
				if( r0c + 1 + r1c + 1 > 22 )
					gr_info->region2start = 576 >> 1;
				else gr_info->region2start = bandInfo[sfreq].longIdx[r0c+1+r1c+1] >> 1;

				gr_info->block_type = 0;
				gr_info->mixed_block_flag = 0;
			}

			if( !fr->lsf )
				gr_info->preflag = get1bit( fr );

			gr_info->scalefac_scale = get1bit( fr );
			gr_info->count1table_select = get1bit( fr );
		}
	}

	return 0;
}

// read scalefactors
static int III_get_scale_factors_1( mpg123_handle_t *fr, int *scf, gr_info_t *gr_info )
{
	const byte slen[2][16] =
	{
	{ 0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4 },
	{ 0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3 }
	};
	int	num0 = slen[0][gr_info->scalefac_compress];
	int	num1 = slen[1][gr_info->scalefac_compress];
	int	numbits;

	if( gr_info->block_type == 2 )
	{
		int i = 18;
		numbits = (num0 + num1) * 18;

		if( gr_info->mixed_block_flag )
		{
			for( i = 8; i; i-- )
				*scf++ = getbits_fast( fr, num0 );

			i = 9;
			numbits -= num0; // num0 * 17 + num1 * 18
		}

		for( ; i; i-- )
			*scf++ = getbits_fast( fr, num0 );

		for( i = 18; i; i-- )
			*scf++ = getbits_fast( fr, num1 );

		// short[13][0..2] = 0
		*scf++ = 0;
		*scf++ = 0;
		*scf++ = 0;
	}
	else
	{
		int	i, scfsi = gr_info->scfsi;

		if( scfsi < 0 )
		{
			// scfsi < 0 => granule == 0
			for( i = 11; i; i-- )
				*scf++ = getbits_fast( fr, num0 );

			for( i = 10; i; i-- )
				*scf++ = getbits_fast( fr, num1 );

			numbits = (num0 + num1) * 10 + num0;
			*scf++ = 0;
		}
		else
		{
			numbits = 0;

			if(!( scfsi & 0x8 ))
			{
				for( i = 0; i < 6; i++ )
					*scf++ = getbits_fast( fr, num0 );

				numbits += num0 * 6;
			}
			else scf += 6; 

			if(!( scfsi & 0x4 ))
			{
				for( i = 0; i < 5; i++ )
					*scf++ = getbits_fast( fr, num0 );

				numbits += num0 * 5;
			}
			else scf += 5;

			if(!( scfsi & 0x2 ))
			{
				for( i = 0; i < 5; i++ )
					*scf++ = getbits_fast( fr, num1 );

				numbits += num1 * 5;
			}
			else scf += 5;

			if(!( scfsi & 0x1 ))
			{
				for( i = 0; i < 5; i++ )
					*scf++ = getbits_fast( fr, num1 );

				numbits += num1 * 5;
			}
			else scf += 5;

			// no l[21] in original sources
			*scf++ = 0;
		}
	}

	return numbits;
}

static int III_get_scale_factors_2( mpg123_handle_t *fr, int *scf, gr_info_t *gr_info, int i_stereo )
{
	const byte	*pnt;
	int		i, j, n = 0;
	int		numbits = 0;
	uint		slen;

	const byte stab[3][6][4] =
	{
	{
	{ 6, 5, 5,5 } , { 6, 5, 7,3 } , { 11,10,0,0},
	{ 7, 7, 7,0 } , { 6, 6, 6,3 } , {  8, 8,5,0}
	},
	{
	{ 9, 9, 9,9 } , { 9, 9,12,6 } , { 18,18,0,0},
	{12,12,12,0 } , {12, 9, 9,6 } , { 15,12,9,0}
	},
	{
	{ 6, 9, 9,9 } , { 6, 9,12,6 } , { 15,18,0,0},
	{ 6,15,12,0 } , { 6,12, 9,6 } , {  6,18,9,0}
	}
	}; 

	// i_stereo AND second channel -> do_layer3() checks this
	if( i_stereo ) slen = i_slen2[gr_info->scalefac_compress>>1];
	else slen = n_slen2[gr_info->scalefac_compress];

	gr_info->preflag = (slen >> 15) & 0x1;
	n = 0;
  
	if( gr_info->block_type == 2 )
	{
		if( gr_info->mixed_block_flag )
			n++;
		n++;
	}

	pnt = stab[n][(slen>>12)&0x7];

	for( i = 0; i < 4; i++ )
	{
		int	num = slen & 0x7;

		slen >>= 3;
		if( num )
		{
			for( j = 0; j < (int)(pnt[i]); j++ )
				*scf++ = getbits_fast( fr, num );
			numbits += pnt[i] * num;
		}
		else
		{
			for( j = 0; j < (int)(pnt[i]); j++ )
				*scf++ = 0;
		}
	}
  
	n = (n << 1) + 1;

	for( i = 0; i < n; i++ )
		*scf++ = 0;

	return numbits;
}

static int III_dequantize_sample( mpg123_handle_t *fr, float xr[SBLIMIT][SSLIMIT], int *scf, gr_info_t *gr_info, int sfreq, int part2bits )
{
	int	shift = 1 + gr_info->scalefac_scale;
	int	part2remain = gr_info->part2_3_length - part2bits;
	int	region1 = gr_info->region1start;
	int	region2 = gr_info->region2start;
	int	bv = gr_info->big_values;
	int	num = getbitoffset( fr );
	float	*xrpnt = (float *)xr;
	int	l[3], l3;
	long	mask;
	int	*me;

	// we must split this, because for num == 0 the shift is undefined if you do it in one step.
	mask  = ((ulong)getbits( fr, num )) << BITSHIFT;
	mask <<= 8 - num;
	part2remain -= num;

	l3 = ((576>>1)-bv)>>1;   

	// we may lose the 'odd' bit here !! check this later again
	if( bv <= region1 )
	{
		l[0] = bv;
		l[1] = 0;
		l[2] = 0;
	}
	else
	{
		l[0] = region1;

		if( bv <= region2 )
		{
			l[1] = bv - l[0];
			l[2] = 0;
		}
		else
		{
			l[1] = region2 - l[0];
			l[2] = bv - region2;
		}
	}
 
	if( gr_info->block_type == 2 )
	{
		int		i, max[4];
		int		step = 0;
		int		lwin = 3;
		register float	v = 0.0f;
		int		cb = 0;
		register int	*m, mc;
		int		rmax;

		// decoding with short or mixed mode BandIndex table
		if( gr_info->mixed_block_flag )
		{
			max[3] = -1;
			max[0] = max[1] = max[2] = 2;
			m = map[sfreq][0];
			me = mapend[sfreq][0];
		}
		else
		{
			max[0] = max[1] = max[2] = max[3] = -1;
			// max[3] not floatly needed in this case
			m = map[sfreq][1];
			me = mapend[sfreq][1];
		}

		mc = 0;

		for( i = 0; i < 2; i++ )
		{
			const struct newhuff	*h = ht + gr_info->table_select[i];
			int			lp = l[i];

			for( ; lp; lp--, mc-- )
			{
				register long	x, y;

				if( (!mc) )
				{
					mc = *m++;
					xrpnt = ((float *)xr) + (*m++);
					lwin = *m++;
					cb = *m++;

					if( lwin == 3 )
					{
						v = gr_info->pow2gain[(*scf++) << shift];
						step = 1;
					}
					else
					{
						v = gr_info->full_gain[lwin][(*scf++) << shift];
						step = 3;
					}
				}
				{
					const short *val = h->table;
					REFRESH_MASK;

					while(( y = *val++ ) < 0 )
					{
						if( mask < 0 )
							val -= y;

						num--;
						mask <<= 1;
					}
					x = y >> 4;
					y &= 0xf;
				}

				if( x == 15 && h->linbits )
				{
					max[lwin] = cb;
					REFRESH_MASK;

					x += ((ulong)mask) >> (BITSHIFT + 8 - h->linbits);
					num -= h->linbits + 1;
					mask <<= h->linbits;

					if( mask < 0 ) *xrpnt = REAL_MUL_SCALE_LAYER3( -ispow[x], v );
					else *xrpnt = REAL_MUL_SCALE_LAYER3( ispow[x], v );

					mask <<= 1;
				}
				else if( x )
				{
					max[lwin] = cb;

					if( mask < 0 ) *xrpnt = REAL_MUL_SCALE_LAYER3( -ispow[x], v );
					else *xrpnt = REAL_MUL_SCALE_LAYER3( ispow[x], v );

					num--;
					mask <<= 1;
				}
				else *xrpnt = DOUBLE_TO_REAL(0.0);

				xrpnt += step;

				if( y == 15 && h->linbits )
				{
					max[lwin] = cb;
					REFRESH_MASK;

					y += ((ulong) mask) >> (BITSHIFT + 8 - h->linbits);
					num -= h->linbits + 1;
					mask <<= h->linbits;

					if( mask < 0 ) *xrpnt = REAL_MUL_SCALE_LAYER3( -ispow[y], v );
					else *xrpnt = REAL_MUL_SCALE_LAYER3( ispow[y], v );

					mask <<= 1;
				}
				else if( y )
				{
					max[lwin] = cb;

					if( mask < 0 ) *xrpnt = REAL_MUL_SCALE_LAYER3( -ispow[y], v );
					else *xrpnt = REAL_MUL_SCALE_LAYER3( ispow[y], v );

					num--;
					mask <<= 1;
				}
				else *xrpnt = DOUBLE_TO_REAL(0.0);

				xrpnt += step;
			}
		}

		for( ; l3 && (part2remain + num > 0); l3-- )
		{
			const struct newhuff	*h;
			const short		*val;
			register short		a;

			// this is only a humble hack to prevent a special segfault.
			// more insight into the float workings is still needed.
			// especially why there are (valid?) files that make xrpnt exceed the array with 4 bytes without segfaulting
			// more seems to be floatly bad, though.

			if(!( xrpnt < &xr[SBLIMIT][0] + 5 ))
				return 2;

			h = htc + gr_info->count1table_select;
			val = h->table;

			REFRESH_MASK;

			while(( a = *val++ ) < 0 )
			{
				if( mask < 0 )
					val -= a;

				num--;
				mask <<= 1;
			}

			if( part2remain + num <= 0 )
			{
				num -= part2remain + num;
				break;
			}

			for( i = 0; i < 4; i++ )
			{
				if(!( i & 1 ))
				{
					if( !mc )
					{
						mc = *m++;
						xrpnt = ((float *)xr) + (*m++);
						lwin = *m++;
						cb = *m++;

						if( lwin == 3 )
						{
							v = gr_info->pow2gain[(*scf++) << shift];
							step = 1;
						}
						else
						{
							v = gr_info->full_gain[lwin][(*scf++) << shift];
							step = 3;
						}
					}
					mc--;
				}

				if(( a & ( 0x8 >> i )))
				{
					max[lwin] = cb;

					if( part2remain + num <= 0 )
						break;

					if( mask < 0 ) *xrpnt = -REAL_SCALE_LAYER3( v );
					else *xrpnt =  REAL_SCALE_LAYER3( v );

					num--;
					mask <<= 1;
				}
				else *xrpnt = DOUBLE_TO_REAL( 0.0 );

				xrpnt += step;
			}
		}

		if( lwin < 3 )
		{
			// short band?
			while( 1 )
			{
				for( ; mc > 0; mc-- )
				{
					*xrpnt = DOUBLE_TO_REAL( 0.0 );
					xrpnt += 3; // short band -> step = 3
					*xrpnt = DOUBLE_TO_REAL( 0.0 );
					xrpnt += 3;
				}

				if( m >= me ) break;

				mc = *m++;
				xrpnt = ((float *)xr) + *m++;
				if( *m++ == 0 ) break; // optimize: field will be set to zero at the end of the function

				m++; // cb
			}
		}

		gr_info->maxband[0] = max[0]+1;
		gr_info->maxband[1] = max[1]+1;
		gr_info->maxband[2] = max[2]+1;
		gr_info->maxbandl   = max[3]+1;

		rmax = max[0] > max[1] ? max[0] : max[1];
		rmax = (rmax > max[2] ? rmax : max[2]) + 1;
		gr_info->maxb = rmax ? fr->shortLimit[sfreq][rmax] : fr->longLimit[sfreq][max[3]+1];
	}
	else
	{
		// decoding with 'long' BandIndex table (block_type != 2)
		const byte	*pretab = pretab_choice[gr_info->preflag];
		int		*m = map[sfreq][2];
		int		i,max = -1;
		int		cb = 0;
		register float	v = 0.0;
		int		mc = 0;

		// long hash table values
		for( i = 0; i < 3; i++ )
		{
			const struct newhuff	*h = ht + gr_info->table_select[i];
			int			lp = l[i];

			for( ; lp; lp--, mc-- )
			{
				long	x, y;

				if( !mc )
				{
					mc = *m++;
					cb = *m++;
					v = gr_info->pow2gain[(*(scf++) + (*pretab++)) << shift];
				}
				{
					const short *val = h->table;
					REFRESH_MASK;

					while(( y = *val++ ) < 0 )
					{
						if( mask < 0 )
							val -= y;

						num--;
						mask <<= 1;
					}

					x = y >> 4;
					y &= 0xf;
				}

				if( x == 15 && h->linbits )
				{
					max = cb;
					REFRESH_MASK;

					x += ((ulong)mask) >> (BITSHIFT + 8 - h->linbits);
					num -= h->linbits+1;
					mask <<= h->linbits;

					if( mask < 0 ) *xrpnt++ = REAL_MUL_SCALE_LAYER3(-ispow[x], v );
					else *xrpnt++ = REAL_MUL_SCALE_LAYER3( ispow[x], v );

					mask <<= 1;
				}
				else if( x )
				{
					max = cb;

					if( mask < 0 ) *xrpnt++ = REAL_MUL_SCALE_LAYER3( -ispow[x], v );
					else *xrpnt++ = REAL_MUL_SCALE_LAYER3( ispow[x], v );
					num--;

					mask <<= 1;
				}
				else *xrpnt++ = DOUBLE_TO_REAL( 0.0 );

				if( y == 15 && h->linbits )
				{
					max = cb;
					REFRESH_MASK;
					y += ((ulong)mask) >> (BITSHIFT + 8 - h->linbits);
					num -= h->linbits+1;
					mask <<= h->linbits;

					if( mask < 0 ) *xrpnt++ = REAL_MUL_SCALE_LAYER3( -ispow[y], v );
					else *xrpnt++ = REAL_MUL_SCALE_LAYER3( ispow[y], v );

					mask <<= 1;
				}
				else if( y )
				{
					max = cb;
					if( mask < 0 ) *xrpnt++ = REAL_MUL_SCALE_LAYER3( -ispow[y], v );
					else *xrpnt++ = REAL_MUL_SCALE_LAYER3( ispow[y], v );

					num--;
					mask <<= 1;
				}
				else *xrpnt++ = DOUBLE_TO_REAL( 0.0 );
			}
		}

		// short (count1table) values
		for( ; l3 && (part2remain + num > 0); l3-- )
		{
			const struct newhuff	*h = htc+gr_info->count1table_select;
			const short		*val = h->table;
			register short		a;

			REFRESH_MASK;
			while(( a = *val++ ) < 0 )
			{
				if( mask < 0 )
					val -= a;

				num--;
				mask <<= 1;
			}

			if( part2remain + num <= 0 )
			{
				num -= part2remain + num;
				break;
			}

			for( i = 0; i < 4; i++ )
			{
				if(!( i & 1 ))
				{
					if( !mc )
					{
						mc = *m++;
						cb = *m++;

						v = gr_info->pow2gain[((*scf++) + (*pretab++)) << shift];
					}
					mc--;
				}

				if(( a & (0x8 >> i)))
				{
					max = cb;
					if( part2remain + num <= 0 )
						break;

					if( mask < 0 ) *xrpnt++ = -REAL_SCALE_LAYER3( v );
					else *xrpnt++ = REAL_SCALE_LAYER3( v );

					num--;
					mask <<= 1;
				}
				else *xrpnt++ = DOUBLE_TO_REAL( 0.0 );
			}
		}

		gr_info->maxbandl = max+1;
		gr_info->maxb = fr->longLimit[sfreq][gr_info->maxbandl];
	}

	part2remain += num;
	backbits( fr, num );
	num = 0;

	while( xrpnt < &xr[SBLIMIT][0] ) 
		*xrpnt++ = DOUBLE_TO_REAL( 0.0 );

	while( part2remain > 16 )
	{
		skipbits( fr, 16 ); // dismiss stuffing Bits
		part2remain -= 16;
	}

	if( part2remain > 0 )
	{
		skipbits( fr, part2remain );
	}
	else if( part2remain < 0 )
	{
		// error
		return 1;
	}

	return 0;
}

// calculate float channel values for Joint-I-Stereo-mode
static void III_i_stereo( float xr_buf[2][SBLIMIT][SSLIMIT], int *scalefac, gr_info_t *gr_info, int sfreq, int ms_stereo, int lsf )
{
	float (*xr)[SBLIMIT*SSLIMIT] = (float(*)[SBLIMIT*SSLIMIT])xr_buf;
	const bandInfoStruct *bi = &bandInfo[sfreq];
	const float *tab1, *tab2;
	int tab;
	
	// TODO: optimize as static
	const float *tabs[3][2][2] =
	{ 
	{ { tan1_1,tan2_1 }       , { tan1_2,tan2_2 } },
	{ { pow1_1[0],pow2_1[0] } , { pow1_2[0],pow2_2[0] } },
	{ { pow1_1[1],pow2_1[1] } , { pow1_2[1],pow2_2[1] } }
	};

	tab = lsf + (gr_info->scalefac_compress & lsf);
	tab1 = tabs[tab][ms_stereo][0];
	tab2 = tabs[tab][ms_stereo][1];

	if( gr_info->block_type == 2 )
	{
		int	lwin, do_l = 0;

		if( gr_info->mixed_block_flag )
			do_l = 1;

		for( lwin = 0; lwin < 3; lwin++ )
		{
			int	is_p, sb, idx;
			int	sfb = gr_info->maxband[lwin];  // sfb is minimal 3 for mixed mode

			if( sfb > 3 ) do_l = 0;

			// process each window
			// get first band with zero values
			for( ; sfb < 12; sfb++ )
			{
				is_p = scalefac[sfb * 3 + lwin - gr_info->mixed_block_flag]; // scale: 0-15

				if( is_p != 7 )
				{
					float	t1, t2;

					sb = bi->shortDiff[sfb];
					idx = bi->shortIdx[sfb] + lwin;
					t1 = tab1[is_p];
					t2 = tab2[is_p];

					for( ; sb > 0; sb--, idx += 3 )
					{
						float v = xr[0][idx];
						xr[0][idx] = REAL_MUL_15( v, t1 );
						xr[1][idx] = REAL_MUL_15( v, t2 );
					}
				}
			}

			// in the original: copy 10 to 11 , here: copy 11 to 12 
			// maybe still wrong??? (copy 12 to 13?)
			is_p = scalefac[11 * 3 + lwin - gr_info->mixed_block_flag]; // scale: 0-15
			sb = bi->shortDiff[12];
			idx  = bi->shortIdx[12] + lwin;

			if( is_p != 7 )
			{
				float	t1, t2;

				t1 = tab1[is_p];
				t2 = tab2[is_p];

				for( ; sb > 0; sb--, idx += 3 )
				{  
					float v = xr[0][idx];
					xr[0][idx] = REAL_MUL_15( v, t1 );
					xr[1][idx] = REAL_MUL_15( v, t2 );
				}
			}
		}

		// also check l-part, if ALL bands in the three windows are 'empty' and mode = mixed_mode
		if( do_l )
		{
			int	idx, sfb = gr_info->maxbandl;

			if( sfb > 21 ) return; // similarity fix related to CVE-2006-1655

			idx = bi->longIdx[sfb];

			for( ; sfb < 8; sfb++ )
			{
				int	sb = bi->longDiff[sfb];
				int	is_p = scalefac[sfb]; // scale: 0-15

				if( is_p != 7 )
				{
					float	t1, t2;

					t1 = tab1[is_p];
					t2 = tab2[is_p];

					for( ; sb > 0; sb--, idx++ )
					{
						float v = xr[0][idx];
						xr[0][idx] = REAL_MUL_15( v, t1 );
						xr[1][idx] = REAL_MUL_15( v, t2 );
					}
				}
				else idx += sb;
			}
		}     
	} 
	else
	{
		int	sfb = gr_info->maxbandl;
		int	is_p, idx;

		if( sfb > 21 ) return; // tightened fix for CVE-2006-1655

		idx = bi->longIdx[sfb];

		for( ; sfb < 21; sfb++ )
		{
			int	sb = bi->longDiff[sfb];

			is_p = scalefac[sfb]; // scale: 0-15

			if( is_p != 7 )
			{
				float	t1, t2;

				t1 = tab1[is_p];
				t2 = tab2[is_p];

				for( ; sb > 0; sb--, idx++ )
				{
					 float v = xr[0][idx];
					 xr[0][idx] = REAL_MUL_15( v, t1 );
					 xr[1][idx] = REAL_MUL_15( v, t2 );
				}
			}
			else idx += sb;
		}

		is_p = scalefac[20];

		if( is_p != 7 )
		{
			float	t1, t2;
			int	sb;

			t1 = tab1[is_p],
			t2 = tab2[is_p]; 

			// copy l-band 20 to l-band 21
			for( sb = bi->longDiff[21]; sb > 0; sb--, idx++ )
			{
				float v = xr[0][idx];
				xr[0][idx] = REAL_MUL_15( v, t1 );
				xr[1][idx] = REAL_MUL_15( v, t2 );
			}
		}
	}
}

static void III_antialias( float xr[SBLIMIT][SSLIMIT], gr_info_t *gr_info )
{
	int	sblim, sb;
	float	*xr1;

	if( gr_info->block_type == 2 )
	{
		if( !gr_info->mixed_block_flag )
			return;
		sblim = 1; 
	}
	else
	{
		sblim = gr_info->maxb-1;
	}

	// 31 alias-reduction operations between each pair of sub-bands
	// with 8 butterflies between each pair
	xr1 = (float *)xr[1];

	for( sb = sblim; sb; sb--, xr1 += 10 )
	{
		float	*cs = aa_cs;
		float	*ca = aa_ca;
		float	*xr2 = xr1;
		int	ss;

		for( ss = 7; ss >= 0; ss-- )
		{
			// upper and lower butterfly inputs
			register float bu = *--xr2;
			register float bd = *xr1;

			*xr2 = REAL_MUL( bu, *cs ) - REAL_MUL( bd, *ca );
			*xr1++ = REAL_MUL( bd, *cs++ ) + REAL_MUL( bu, *ca++ );
		}
	}
}

static void III_hybrid( float fsIn[SBLIMIT][SSLIMIT], float tsOut[SSLIMIT][SBLIMIT], int ch, gr_info_t *gr_info, mpg123_handle_t *fr )
{
	float	(*block)[2][SBLIMIT*SSLIMIT] = fr->hybrid_block;
	int	*blc = fr->hybrid_blc;
	float	*tspnt = (float *)tsOut;
	float	*rawout1, *rawout2;
	int	bt = 0, b, i;
	size_t	sb = 0;

	b = blc[ch];
	rawout1 = block[b][ch];
	b=-b + 1;
	rawout2 = block[b][ch];
	blc[ch] = b;
  
	if( gr_info->mixed_block_flag )
	{
		sb = 2;
		dct36( fsIn[0], rawout1, rawout2, win[0], tspnt );
		dct36( fsIn[1], rawout1+18, rawout2+18, win1[0], tspnt + 1 );
		rawout1 += 36; rawout2 += 36; tspnt += 2;
	}
 
	bt = gr_info->block_type;

	if( bt == 2 )
	{
		for( ; sb < gr_info->maxb; sb += 2, tspnt += 2, rawout1 += 36, rawout2 += 36 )
		{
			dct12( fsIn[sb], rawout1, rawout2, win[2], tspnt );
			dct12( fsIn[sb+1], rawout1 + 18, rawout2 + 18, win1[2], tspnt + 1 );
		}
	}
	else
	{
		for( ; sb < gr_info->maxb; sb += 2, tspnt += 2, rawout1 += 36, rawout2 += 36 )
		{
			dct36( fsIn[sb], rawout1, rawout2, win[bt], tspnt );
			dct36( fsIn[sb+1], rawout1 + 18, rawout2 + 18, win1[bt], tspnt + 1 );
		}
	}

	for( ; sb < SBLIMIT; sb++, tspnt++ )
	{
		for( i = 0; i < SSLIMIT; i++ )
		{
			tspnt[i*SBLIMIT] = *rawout1++;
			*rawout2++ = DOUBLE_TO_REAL( 0.0 );
		}
	}
}

// and at the end... the main layer3 handler
int do_layer3( mpg123_handle_t *fr )
{
	int		gr, ch, ss, clip = 0;
	int		stereo = fr->stereo;
	int		single = fr->single;
	int		ms_stereo, i_stereo;
	int		sfreq = fr->sampling_frequency;
	int		scalefacs[2][39]; // max 39 for short[13][3] mode, mixed: 38, long: 22
	int		stereo1, granules;
	III_sideinfo	sideinfo;

	if( stereo == 1 )
	{
		// stream is mono
		stereo1 = 1;
		single = SINGLE_LEFT;
	}
	else if( single != SINGLE_STEREO )
	{
		// stream is stereo, but force to mono
		stereo1 = 1;
	}
	else
	{
		stereo1 = 2;
	}

	if( fr->mode == MPG_MD_JOINT_STEREO )
	{
		ms_stereo = (fr->mode_ext & 0x2) >> 1;
		i_stereo  = fr->mode_ext & 0x1;
	}
	else
	{
		ms_stereo = i_stereo = 0;
	}

	granules = fr->lsf ? 1 : 2;

	// quick hack to keep the music playing
	// after having seen this nasty test file...
	if( III_get_side_info( fr, &sideinfo, stereo, ms_stereo, sfreq, single ))
		return clip;

	set_pointer( fr, sideinfo.main_data_begin );

	for( gr = 0; gr < granules; gr++ )
	{
		float	(*hybridIn)[SBLIMIT][SSLIMIT] = fr->layer3.hybrid_in;	//  hybridIn[2][SBLIMIT][SSLIMIT]
		float	(*hybridOut)[SSLIMIT][SBLIMIT] = fr->layer3.hybrid_out;	//  hybridOut[2][SSLIMIT][SBLIMIT]
		gr_info_t	*gr_info = &(sideinfo.ch[0].gr[gr]);
		long	part2bits;

		if( fr->lsf ) part2bits = III_get_scale_factors_2( fr, scalefacs[0], gr_info, 0 );
		else part2bits = III_get_scale_factors_1( fr, scalefacs[0], gr_info );

		if( III_dequantize_sample( fr, hybridIn[0], scalefacs[0], gr_info, sfreq, part2bits ))
			return clip;

		if( stereo == 2 )
		{
			register float	*in0, *in1;
			register int	i;

			gr_info = &(sideinfo.ch[1].gr[gr]);

			if( fr->lsf ) part2bits = III_get_scale_factors_2( fr, scalefacs[1], gr_info, i_stereo );
			else part2bits = III_get_scale_factors_1( fr, scalefacs[1], gr_info );

			if( III_dequantize_sample( fr, hybridIn[1], scalefacs[1], gr_info, sfreq, part2bits ))
				return clip;

			if( ms_stereo )
			{
				uint	maxb = sideinfo.ch[0].gr[gr].maxb;
				int	i;

				if( sideinfo.ch[1].gr[gr].maxb > maxb )
					maxb = sideinfo.ch[1].gr[gr].maxb;

				for( i = 0; i < SSLIMIT * (int)maxb; i++ )
				{
					float tmp0 = ((float *)hybridIn[0])[i];
					float tmp1 = ((float *)hybridIn[1])[i];
					((float *)hybridIn[0])[i] = tmp0 + tmp1;
					((float *)hybridIn[1])[i] = tmp0 - tmp1;
				}
			}

			if( i_stereo )
				III_i_stereo( hybridIn, scalefacs[1], gr_info, sfreq, ms_stereo, fr->lsf );

			if( ms_stereo || i_stereo || ( single == SINGLE_MIX ))
			{
				if( gr_info->maxb > sideinfo.ch[0].gr[gr].maxb ) 
					sideinfo.ch[0].gr[gr].maxb = gr_info->maxb;
				else gr_info->maxb = sideinfo.ch[0].gr[gr].maxb;
			}

			switch( single )
			{
			case SINGLE_MIX:
				in0 = (float *)hybridIn[0];
				in1 = (float *)hybridIn[1];

				for( i = 0; i < SSLIMIT * (int)gr_info->maxb; i++, in0++ )
					*in0 = (*in0 + *in1++); // *0.5 done by pow-scale
				break;
			case SINGLE_RIGHT:
				in0 = (float *)hybridIn[0];
				in1 = (float *)hybridIn[1];

				for( i = 0; i < SSLIMIT * (int)gr_info->maxb; i++ )
					*in0++ = *in1++;
				break;
			}
		}

		for( ch = 0; ch < stereo1; ch++ )
		{
			gr_info = &(sideinfo.ch[ch].gr[gr]);
			III_antialias( hybridIn[ch], gr_info );
			III_hybrid( hybridIn[ch], hybridOut[ch], ch,gr_info, fr );
		}

		for( ss = 0; ss < SSLIMIT; ss++ )
		{
			if( single != SINGLE_STEREO )
				clip += (fr->synth_mono)(hybridOut[0][ss], fr );
			else clip += (fr->synth_stereo)(hybridOut[0][ss], hybridOut[1][ss], fr );

		}
	}
  
	return clip;
}