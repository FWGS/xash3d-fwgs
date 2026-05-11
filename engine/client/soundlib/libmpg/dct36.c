/*
	This is an optimized DCT from Jeff Tsay's maplay 1.2+ package.
	Saved one multiplication by doing the 'twiddle factor' stuff
	together with the window mul. (MH)

	This uses Byeong Gi Lee's Fast Cosine Transform algorithm, but the
	9 point IDCT needs to be reduced further. Unfortunately, I don't
	know how to do that, because 9 is not an even number. - Jeff.

	Original Message:

	9 Point Inverse Discrete Cosine Transform

	This piece of code is Copyright 1997 Mikko Tommila and is freely usable
	by anybody. The algorithm itself is of course in the public domain.

	Again derived heuristically from the 9-point WFTA.

	The algorithm is optimized (?) for speed, not for small rounding errors or
	good readability.

	36 additions, 11 multiplications

	Again this is very likely sub-optimal.

	The code is optimized to use a minimum number of temporary variables,
	so it should compile quite well even on 8-register Intel x86 processors.
	This makes the code quite obfuscated and very difficult to understand.

	References:
	[1] S. Winograd: "On Computing the Discrete Fourier Transform",
	    Mathematics of Computation, Volume 32, Number 141, January 1978,
	    Pages 175-199
*/

#include "mpg123.h"
#include <math.h>

#define MACRO(v) { \
	float tmpval; \
	tmpval = tmp[(v)] + tmp[17-(v)]; \
	out2[9+(v)] = REAL_MUL(tmpval, w[27+(v)]); \
	out2[8-(v)] = REAL_MUL(tmpval, w[26-(v)]); \
	tmpval = tmp[(v)] - tmp[17-(v)]; \
	ts[SBLIMIT*(8-(v))] = out1[8-(v)] + REAL_MUL(tmpval, w[8-(v)]); \
	ts[SBLIMIT*(9+(v))] = out1[9+(v)] + REAL_MUL(tmpval, w[9+(v)]); }

#define DCT12_PART1 \
	in5 = in[5*3];  \
	in5 += (in4 = in[4*3]); \
	in4 += (in3 = in[3*3]); \
	in3 += (in2 = in[2*3]); \
	in2 += (in1 = in[1*3]); \
	in1 += (in0 = in[0*3]); \
	\
	in5 += in3; in3 += in1; \
	\
	in2 = REAL_MUL(in2, COS6_1); \
	in3 = REAL_MUL(in3, COS6_1);

#define DCT12_PART2 \
	in0 += REAL_MUL(in4, COS6_2); \
	\
	in4 = in0 + in2; \
	in0 -= in2;      \
	\
	in1 += REAL_MUL(in5, COS6_2); \
	\
	in5 = REAL_MUL((in1 + in3), tfcos12[0]); \
	in1 = REAL_MUL((in1 - in3), tfcos12[2]); \
	\
	in3 = in4 + in5; \
	in4 -= in5;      \
	\
	in2 = in0 + in1; \
	in0 -= in1;

// calculation of the inverse MDCT
// used to be static without 3dnow - does that floatly matter?
void dct36( float *inbuf, float *o1, float *o2, float *wintab, float *tsbuf )
{
	float	tmp[18];

	{
		register float *in = inbuf;

		in[17] += in[16]; in[16] += in[15]; in[15] += in[14];
		in[14] += in[13]; in[13] += in[12]; in[12] += in[11];
		in[11] += in[10]; in[10] += in[9]; in[9] += in[8];
		in[8] += in[7]; in[7] += in[6]; in[6] += in[5];
		in[5] += in[4]; in[4] += in[3]; in[3] += in[2];
		in[2] += in[1]; in[1] += in[0];

		in[17] += in[15]; in[15] += in[13]; in[13] += in[11]; in[11] += in[9];
		in[9] += in[7]; in[7] += in[5]; in[5] += in[3]; in[3] += in[1];

		{
			float t3;
			{
				float t0, t1, t2;

				t0 = REAL_MUL(COS6_2, (in[8] + in[16] - in[4]));
				t1 = REAL_MUL(COS6_2, in[12]);

				t3 = in[0];
				t2 = t3 - t1 - t1;
				tmp[1] = tmp[7] = t2 - t0;
				tmp[4]          = t2 + t0 + t0;
				t3 += t1;

				t2 = REAL_MUL(COS6_1, (in[10] + in[14] - in[2]));
				tmp[1] -= t2;
				tmp[7] += t2;
			}
			{
				float t0, t1, t2;

				t0 = REAL_MUL(cos9[0], (in[4] + in[8] ));
				t1 = REAL_MUL(cos9[1], (in[8] - in[16]));
				t2 = REAL_MUL(cos9[2], (in[4] + in[16]));

				tmp[2] = tmp[6] = t3 - t0      - t2;
				tmp[0] = tmp[8] = t3 + t0 + t1;
				tmp[3] = tmp[5] = t3      - t1 + t2;
			}
		}
		{
			float t1, t2, t3;

			t1 = REAL_MUL(cos18[0], (in[2]  + in[10]));
			t2 = REAL_MUL(cos18[1], (in[10] - in[14]));
			t3 = REAL_MUL(COS6_1,    in[6]);

			{
				float t0 = t1 + t2 + t3;
				tmp[0] += t0;
				tmp[8] -= t0;
			}

			t2 -= t3;
			t1 -= t3;

			t3 = REAL_MUL(cos18[2], (in[2] + in[14]));

			t1 += t3;
			tmp[3] += t1;
			tmp[5] -= t1;

			t2 -= t3;
			tmp[2] += t2;
			tmp[6] -= t2;
		}
		{
			float t0, t1, t2, t3, t4, t5, t6, t7;

			t1 = REAL_MUL(COS6_2, in[13]);
			t2 = REAL_MUL(COS6_2, (in[9] + in[17] - in[5]));

			t3 = in[1] + t1;
			t4 = in[1] - t1 - t1;
			t5 = t4 - t2;

			t0 = REAL_MUL(cos9[0], (in[5] + in[9]));
			t1 = REAL_MUL(cos9[1], (in[9] - in[17]));

			tmp[13] = REAL_MUL((t4 + t2 + t2), tfcos36[17-13]);
			t2 = REAL_MUL(cos9[2], (in[5] + in[17]));

			t6 = t3 - t0 - t2;
			t0 += t3 + t1;
			t3 += t2 - t1;

			t2 = REAL_MUL(cos18[0], (in[3]  + in[11]));
			t4 = REAL_MUL(cos18[1], (in[11] - in[15]));
			t7 = REAL_MUL(COS6_1, in[7]);

			t1 = t2 + t4 + t7;
			tmp[17] = REAL_MUL((t0 + t1), tfcos36[17-17]);
			tmp[9]  = REAL_MUL((t0 - t1), tfcos36[17-9]);
			t1 = REAL_MUL(cos18[2], (in[3] + in[15]));
			t2 += t1 - t7;

			tmp[14] = REAL_MUL((t3 + t2), tfcos36[17-14]);
			t0 = REAL_MUL(COS6_1, (in[11] + in[15] - in[3]));
			tmp[12] = REAL_MUL((t3 - t2), tfcos36[17-12]);

			t4 -= t1 + t7;

			tmp[16] = REAL_MUL((t5 - t0), tfcos36[17-16]);
			tmp[10] = REAL_MUL((t5 + t0), tfcos36[17-10]);
			tmp[15] = REAL_MUL((t6 + t4), tfcos36[17-15]);
			tmp[11] = REAL_MUL((t6 - t4), tfcos36[17-11]);
		}



		{
			register float *out2 = o2;
			register float *w = wintab;
			register float *out1 = o1;
			register float *ts = tsbuf;

			MACRO(0);
			MACRO(1);
			MACRO(2);
			MACRO(3);
			MACRO(4);
			MACRO(5);
			MACRO(6);
			MACRO(7);
			MACRO(8);
		}

	}
}

void dct12( float *in, float *rawout1, float *rawout2, register float *wi, register float *ts )
{
	{
		float in0,in1,in2,in3,in4,in5;
		register float *out1 = rawout1;
		ts[SBLIMIT*0] = out1[0]; ts[SBLIMIT*1] = out1[1]; ts[SBLIMIT*2] = out1[2];
		ts[SBLIMIT*3] = out1[3]; ts[SBLIMIT*4] = out1[4]; ts[SBLIMIT*5] = out1[5];

		DCT12_PART1

		{
			float tmp0,tmp1 = (in0 - in4);
			{
				float tmp2 = REAL_MUL((in1 - in5), tfcos12[1]);
				tmp0 = tmp1 + tmp2;
				tmp1 -= tmp2;
			}
			ts[(17-1)*SBLIMIT] = out1[17-1] + REAL_MUL(tmp0, wi[11-1]);
			ts[(12+1)*SBLIMIT] = out1[12+1] + REAL_MUL(tmp0, wi[6+1]);
			ts[(6 +1)*SBLIMIT] = out1[6 +1] + REAL_MUL(tmp1, wi[1]);
			ts[(11-1)*SBLIMIT] = out1[11-1] + REAL_MUL(tmp1, wi[5-1]);
		}

		DCT12_PART2

		ts[(17-0)*SBLIMIT] = out1[17-0] + REAL_MUL(in2, wi[11-0]);
		ts[(12+0)*SBLIMIT] = out1[12+0] + REAL_MUL(in2, wi[6+0]);
		ts[(12+2)*SBLIMIT] = out1[12+2] + REAL_MUL(in3, wi[6+2]);
		ts[(17-2)*SBLIMIT] = out1[17-2] + REAL_MUL(in3, wi[11-2]);

		ts[(6 +0)*SBLIMIT]  = out1[6+0] + REAL_MUL(in0, wi[0]);
		ts[(11-0)*SBLIMIT] = out1[11-0] + REAL_MUL(in0, wi[5-0]);
		ts[(6 +2)*SBLIMIT]  = out1[6+2] + REAL_MUL(in4, wi[2]);
		ts[(11-2)*SBLIMIT] = out1[11-2] + REAL_MUL(in4, wi[5-2]);
	}

	in++;

	{
		float in0,in1,in2,in3,in4,in5;
		register float *out2 = rawout2;

		DCT12_PART1

		{
			float tmp0,tmp1 = (in0 - in4);
			{
				float tmp2 = REAL_MUL((in1 - in5), tfcos12[1]);
				tmp0 = tmp1 + tmp2;
				tmp1 -= tmp2;
			}
			out2[5-1] = REAL_MUL(tmp0, wi[11-1]);
			out2[0+1] = REAL_MUL(tmp0, wi[6+1]);
			ts[(12+1)*SBLIMIT] += REAL_MUL(tmp1, wi[1]);
			ts[(17-1)*SBLIMIT] += REAL_MUL(tmp1, wi[5-1]);
		}

		DCT12_PART2

		out2[5-0] = REAL_MUL(in2, wi[11-0]);
		out2[0+0] = REAL_MUL(in2, wi[6+0]);
		out2[0+2] = REAL_MUL(in3, wi[6+2]);
		out2[5-2] = REAL_MUL(in3, wi[11-2]);

		ts[(12+0)*SBLIMIT] += REAL_MUL(in0, wi[0]);
		ts[(17-0)*SBLIMIT] += REAL_MUL(in0, wi[5-0]);
		ts[(12+2)*SBLIMIT] += REAL_MUL(in4, wi[2]);
		ts[(17-2)*SBLIMIT] += REAL_MUL(in4, wi[5-2]);
	}

	in++;

	{
		float in0,in1,in2,in3,in4,in5;
		register float *out2 = rawout2;
		out2[12]=out2[13]=out2[14]=out2[15]=out2[16]=out2[17]=0.0;

		DCT12_PART1

		{
			float tmp0,tmp1 = (in0 - in4);
			{
				float tmp2 = REAL_MUL((in1 - in5), tfcos12[1]);
				tmp0 = tmp1 + tmp2;
				tmp1 -= tmp2;
			}
			out2[11-1] = REAL_MUL(tmp0, wi[11-1]);
			out2[6 +1] = REAL_MUL(tmp0, wi[6+1]);
			out2[0+1] += REAL_MUL(tmp1, wi[1]);
			out2[5-1] += REAL_MUL(tmp1, wi[5-1]);
		}

		DCT12_PART2

		out2[11-0] = REAL_MUL(in2, wi[11-0]);
		out2[6 +0] = REAL_MUL(in2, wi[6+0]);
		out2[6 +2] = REAL_MUL(in3, wi[6+2]);
		out2[11-2] = REAL_MUL(in3, wi[11-2]);

		out2[0+0] += REAL_MUL(in0, wi[0]);
		out2[5-0] += REAL_MUL(in0, wi[5-0]);
		out2[0+2] += REAL_MUL(in4, wi[2]);
		out2[5-2] += REAL_MUL(in4, wi[5-2]);
	}
}
