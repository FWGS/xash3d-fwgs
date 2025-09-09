/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// d_scan.c
//
// Portable C scan-level rasterization code, all pixel depths.

#include "r_local.h"

pixel_t    *r_turb_pbase, *r_turb_pdest;
short      *r_turb_pz;
fixed16_t  r_turb_s, r_turb_t, r_turb_sstep, r_turb_tstep;
int        r_turb_izistep, r_turb_izi;
int        *r_turb_turb;
static int r_turb_spancount;
int        alpha;

/*
=============
D_DrawTurbulent8Span
=============
*/
void D_DrawTurbulent8Span( void )
{
	int sturb, tturb;

	do
	{
		sturb = (( r_turb_s + r_turb_turb[( r_turb_t >> 16 ) & ( CYCLE - 1 )] ) >> 16 ) & 63;
		tturb = (( r_turb_t + r_turb_turb[( r_turb_s >> 16 ) & ( CYCLE - 1 )] ) >> 16 ) & 63;
		*r_turb_pdest++ = *( r_turb_pbase + ( tturb << 6 ) + sturb );
		r_turb_s += r_turb_sstep;
		r_turb_t += r_turb_tstep;
	}
	while( --r_turb_spancount > 0 );
}

/*
=============
D_DrawTurbulent8Span
=============
*/
static void D_DrawTurbulent8ZSpan( void )
{
	int sturb, tturb;

	do
	{
		sturb = (( r_turb_s + r_turb_turb[( r_turb_t >> 16 ) & ( CYCLE - 1 )] ) >> 16 ) & 63;
		tturb = (( r_turb_t + r_turb_turb[( r_turb_s >> 16 ) & ( CYCLE - 1 )] ) >> 16 ) & 63;
		if( *r_turb_pz <= ( r_turb_izi >> 16 ))
		{
			pixel_t btemp = *( r_turb_pbase + ( tturb << 6 ) + sturb );
			if( alpha == 7 )
				*r_turb_pdest = btemp;
			else
				*r_turb_pdest = BLEND_ALPHA( alpha, btemp, *r_turb_pdest );
		}
		r_turb_pdest++;
		r_turb_pz++;
		r_turb_izi += r_turb_izistep;
		r_turb_s += r_turb_sstep;
		r_turb_t += r_turb_tstep;
	}
	while( --r_turb_spancount > 0 );
}

/*
=============
Turbulent8
=============
*/
void Turbulent8( espan_t *pspan )
{
	int       count;
	fixed16_t snext, tnext;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz16stepu, tdivz16stepu, zi16stepu;

	r_turb_turb = sintable + ((int)( gp_cl->time * SPEED ) & ( CYCLE - 1 ));

	r_turb_sstep = 0; // keep compiler happy
	r_turb_tstep = 0; // ditto

	r_turb_pbase = cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;

	do
	{
		r_turb_pdest = ( d_viewbuffer
				 + ( r_screenwidth * pspan->v ) + pspan->u );

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		r_turb_s = (int)( sdivz * z ) + sadjust;
		if( r_turb_s > bbextents )
			r_turb_s = bbextents;
		else if( r_turb_s < 0 )
			r_turb_s = 0;

		r_turb_t = (int)( tdivz * z ) + tadjust;
		if( r_turb_t > bbextentt )
			r_turb_t = bbextentt;
		else if( r_turb_t < 0 )
			r_turb_t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 16 )
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				r_turb_sstep = ( snext - r_turb_s ) >> 4;
				r_turb_tstep = ( tnext - r_turb_t ) >> 4;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( r_turb_spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				if( r_turb_spancount > 1 )
				{
					r_turb_sstep = ( snext - r_turb_s ) / ( r_turb_spancount - 1 );
					r_turb_tstep = ( tnext - r_turb_t ) / ( r_turb_spancount - 1 );
				}
			}

			r_turb_s = r_turb_s & (( CYCLE << 16 ) - 1 );
			r_turb_t = r_turb_t & (( CYCLE << 16 ) - 1 );

			D_DrawTurbulent8Span();

			r_turb_s = snext;
			r_turb_t = tnext;

		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}

/*
=============
Turbulent8
=============
*/
void TurbulentZ8( espan_t *pspan, int alpha1 )
{
	int       count;
	fixed16_t snext, tnext;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz16stepu, tdivz16stepu, zi16stepu;
	alpha = alpha1;

	if( alpha > 7 )
		alpha = 7;
	if( alpha == 0 )
		return;

	r_turb_turb = sintable + ((int)( gp_cl->time * SPEED ) & ( CYCLE - 1 ));

	r_turb_sstep = 0; // keep compiler happy
	r_turb_tstep = 0; // ditto

	r_turb_pbase = cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;
	r_turb_izistep = (int)( d_zistepu * 0x8000 * 0x10000 );

	do
	{
		r_turb_pdest = ( d_viewbuffer
				 + ( r_screenwidth * pspan->v ) + pspan->u );
		r_turb_pz = d_pzbuffer + ( d_zwidth * pspan->v ) + pspan->u;

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
		r_turb_izi = (int)( zi * 0x8000 * 0x10000 );

		r_turb_s = (int)( sdivz * z ) + sadjust;
		if( r_turb_s > bbextents )
			r_turb_s = bbextents;
		else if( r_turb_s < 0 )
			r_turb_s = 0;

		r_turb_t = (int)( tdivz * z ) + tadjust;
		if( r_turb_t > bbextentt )
			r_turb_t = bbextentt;
		else if( r_turb_t < 0 )
			r_turb_t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 16 )
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				r_turb_sstep = ( snext - r_turb_s ) >> 4;
				r_turb_tstep = ( tnext - r_turb_t ) >> 4;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( r_turb_spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;

				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				if( r_turb_spancount > 1 )
				{
					r_turb_sstep = ( snext - r_turb_s ) / ( r_turb_spancount - 1 );
					r_turb_tstep = ( tnext - r_turb_t ) / ( r_turb_spancount - 1 );
				}
			}

			r_turb_s = r_turb_s & (( CYCLE << 16 ) - 1 );
			r_turb_t = r_turb_t & (( CYCLE << 16 ) - 1 );

			D_DrawTurbulent8ZSpan();

			r_turb_s = snext;
			r_turb_t = tnext;

		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}



// ====================
// PGM
/*
=============
NonTurbulent8 - this is for drawing scrolling textures. they're warping water textures
	but the turbulence is automatically 0.
=============
*/
void NonTurbulent8( espan_t *pspan )
{
	int       count;
	fixed16_t snext, tnext;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz16stepu, tdivz16stepu, zi16stepu;

//	r_turb_turb = sintable + ((int)(r_newrefdef.time*SPEED)&(CYCLE-1));
	r_turb_turb = blanktable;

	r_turb_sstep = 0; // keep compiler happy
	r_turb_tstep = 0; // ditto

	r_turb_pbase = cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;

	do
	{
		r_turb_pdest = ( d_viewbuffer
				 + ( r_screenwidth * pspan->v ) + pspan->u );

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		r_turb_s = (int)( sdivz * z ) + sadjust;
		if( r_turb_s > bbextents )
			r_turb_s = bbextents;
		else if( r_turb_s < 0 )
			r_turb_s = 0;

		r_turb_t = (int)( tdivz * z ) + tadjust;
		if( r_turb_t > bbextentt )
			r_turb_t = bbextentt;
		else if( r_turb_t < 0 )
			r_turb_t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 16 )
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				r_turb_sstep = ( snext - r_turb_s ) >> 4;
				r_turb_tstep = ( tnext - r_turb_t ) >> 4;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( r_turb_spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 16 )
					snext = 16; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 16 )
					tnext = 16; // guard against round-off error on <0 steps

				if( r_turb_spancount > 1 )
				{
					r_turb_sstep = ( snext - r_turb_s ) / ( r_turb_spancount - 1 );
					r_turb_tstep = ( tnext - r_turb_t ) / ( r_turb_spancount - 1 );
				}
			}

			r_turb_s = r_turb_s & (( CYCLE << 16 ) - 1 );
			r_turb_t = r_turb_t & (( CYCLE << 16 ) - 1 );

			D_DrawTurbulent8Span();

			r_turb_s = snext;
			r_turb_t = tnext;

		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}
// PGM
// ====================

int kernel[2][2][2] =
{
	{
		{16384, 0},
		{49152, 32768}
	}
	,
	{
		{32768, 49152},
		{0, 16384}
	}
};
#ifndef DISABLE_TEXFILTER
#define SW_TEXFILT ( sw_texfilt.value == 1.0f )
#else
#define SW_TEXFILT 0
#endif
/*
=============
D_DrawSpans16

  FIXME: actually make this subdivide by 16 instead of 8!!!
=============
*/
void D_DrawSpans16( espan_t *pspan )
{
	int       count, spancount;
	pixel_t   *pbase, *pdest;
	fixed16_t s, t, snext, tnext, sstep, tstep;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz8stepu, tdivz8stepu, zi8stepu;

	sstep = 0; // keep compiler happy
	tstep = 0; // ditto

	pbase = cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;

	do
	{
		pdest = ( d_viewbuffer
			  + ( r_screenwidth * pspan->v ) + pspan->u );

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		s = (int)( sdivz * z ) + sadjust;
		if( s > bbextents )
			s = bbextents;
		else if( s < 0 )
			s = 0;

		t = (int)( tdivz * z ) + tadjust;
		if( t > bbextentt )
			t = bbextentt;
		else if( t < 0 )
			t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 8 )
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				sstep = ( snext - s ) >> 3;
				tstep = ( tnext - t ) >> 3;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				if( spancount > 1 )
				{
					sstep = ( snext - s ) / ( spancount - 1 );
					tstep = ( tnext - t ) / ( spancount - 1 );
				}
			}


			// Drawing phrase
			if( !SW_TEXFILT )
			{
				do
				{
					*pdest++ = *( pbase + ( s >> 16 ) + ( t >> 16 ) * cachewidth );
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}
			else
			{
				do
				{
					int idiths = s;
					int iditht = t;

					int X = ( pspan->u + spancount ) & 1;
					int Y = ( pspan->v ) & 1;

					// Using the kernel
					idiths += kernel[X][Y][0];
					iditht += kernel[X][Y][1];

					idiths = idiths >> 16;
					idiths = idiths ? idiths - 1 : idiths;


					iditht = iditht >> 16;
					iditht = iditht ? iditht - 1 : iditht;


					*pdest++ = *( pbase + idiths + iditht * cachewidth );
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}


		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}


/*
=============
D_DrawSpans16

  FIXME: actually make this subdivide by 16 instead of 8!!!
=============
*/
void D_AlphaSpans16( espan_t *pspan )
{
	int       count, spancount;
	pixel_t   *pbase, *pdest;
	fixed16_t s, t, snext, tnext, sstep, tstep;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz8stepu, tdivz8stepu, zi8stepu;
	int       izi, izistep;
	short     *pz;

	sstep = 0; // keep compiler happy
	tstep = 0; // ditto

	pbase = cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;
	izistep = (int)( d_zistepu * 0x8000 * 0x10000 );

	do
	{
		pdest = ( d_viewbuffer
			  + ( r_screenwidth * pspan->v ) + pspan->u );
		pz = d_pzbuffer + ( d_zwidth * pspan->v ) + pspan->u;

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		izi = (int)( zi * 0x8000 * 0x10000 );
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		s = (int)( sdivz * z ) + sadjust;
		if( s > bbextents )
			s = bbextents;
		else if( s < 0 )
			s = 0;

		t = (int)( tdivz * z ) + tadjust;
		if( t > bbextentt )
			t = bbextentt;
		else if( t < 0 )
			t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 8 )
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				sstep = ( snext - s ) >> 3;
				tstep = ( tnext - t ) >> 3;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				if( spancount > 1 )
				{
					sstep = ( snext - s ) / ( spancount - 1 );
					tstep = ( tnext - t ) / ( spancount - 1 );
				}
			}


			// Drawing phrase
			if( !SW_TEXFILT )
			{
				do
				{
					pixel_t btemp;

					btemp = *( pbase + ( s >> 16 ) + ( t >> 16 ) * cachewidth );
					if( btemp != TRANSPARENT_COLOR )
					{
						if( *pz <= ( izi >> 16 ))
						{
							*pdest = btemp;
							*pz = izi >> 16;
						}
					}
					pdest++;
					pz++;
					izi += izistep;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}
			else
			{
				do
				{
					if( *pz <= ( izi >> 16 ))
					{
						int     idiths = s;
						int     iditht = t;

						int     X = ( pspan->u + spancount ) & 1;
						int     Y = ( pspan->v ) & 1;
						pixel_t btemp;

						// Using the kernel
						idiths += kernel[X][Y][0];
						iditht += kernel[X][Y][1];

						idiths = idiths >> 16;
						idiths = idiths ? idiths - 1 : idiths;


						iditht = iditht >> 16;
						iditht = iditht ? iditht - 1 : iditht;


						btemp = *( pbase + idiths + iditht * cachewidth );
						if( btemp != TRANSPARENT_COLOR )
						{
							*pdest = btemp;
							*pz = izi >> 16;
						}
					}
					pdest++;
					pz++;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}


		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}



/*
=============
D_DrawSpans16

  FIXME: actually make this subdivide by 16 instead of 8!!!
=============
*/
void D_BlendSpans16( espan_t *pspan, int alpha )
{
	int       count, spancount;
	pixel_t   *pbase, *pdest;
	fixed16_t s, t, snext, tnext, sstep, tstep;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz8stepu, tdivz8stepu, zi8stepu;
	int       izi, izistep;
	short     *pz;

	if( alpha > 7 )
		alpha = 7;
	if( alpha == 0 )
		return;

	sstep = 0; // keep compiler happy
	tstep = 0; // ditto

	pbase = cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;
	izistep = (int)( d_zistepu * 0x8000 * 0x10000 );

	do
	{
		pdest = ( d_viewbuffer
			  + ( r_screenwidth * pspan->v ) + pspan->u );
		pz = d_pzbuffer + ( d_zwidth * pspan->v ) + pspan->u;

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		izi = (int)( zi * 0x8000 * 0x10000 );
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		s = (int)( sdivz * z ) + sadjust;
		if( s > bbextents )
			s = bbextents;
		else if( s < 0 )
			s = 0;

		t = (int)( tdivz * z ) + tadjust;
		if( t > bbextentt )
			t = bbextentt;
		else if( t < 0 )
			t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 8 )
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				sstep = ( snext - s ) >> 3;
				tstep = ( tnext - t ) >> 3;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				if( spancount > 1 )
				{
					sstep = ( snext - s ) / ( spancount - 1 );
					tstep = ( tnext - t ) / ( spancount - 1 );
				}
			}


			// Drawing phrase
			if( !SW_TEXFILT )
			{
				do
				{
					if( *pz <= ( izi >> 16 ))
					{
						pixel_t btemp;

						btemp = *( pbase + ( s >> 16 ) + ( t >> 16 ) * cachewidth );

						if( btemp != TRANSPARENT_COLOR )
						{
							if( alpha != 7 )
								btemp = BLEND_ALPHA( alpha, btemp, *pdest );
							*pdest = btemp;
						}
						// *pz    = izi >> 16;
					}
					pdest++;
					pz++;
					izi += izistep;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}
			else
			{
				do
				{
					int idiths = s;
					int iditht = t;

					int X = ( pspan->u + spancount ) & 1;
					int Y = ( pspan->v ) & 1;
					if( *pz <= ( izi >> 16 ))
					{
						pixel_t btemp;

						// Using the kernel
						idiths += kernel[X][Y][0];
						iditht += kernel[X][Y][1];

						idiths = idiths >> 16;
						idiths = idiths ? idiths - 1 : idiths;


						iditht = iditht >> 16;
						iditht = iditht ? iditht - 1 : iditht;

						btemp = *( pbase + idiths + iditht * cachewidth );

						if( btemp != TRANSPARENT_COLOR )
						{
							if( alpha != 7 )
								btemp = BLEND_ALPHA( alpha, btemp, *pdest );
							*pdest = btemp;
						}
						// *pz    = izi >> 16;
					}
					pdest++;
					pz++;
					izi += izistep;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}


		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}



/*
=============
D_DrawSpans16

  FIXME: actually make this subdivide by 16 instead of 8!!!
=============
*/
void D_AddSpans16( espan_t *pspan )
{
	int       count, spancount;
	pixel_t   *pbase, *pdest;
	fixed16_t s, t, snext, tnext, sstep, tstep;
	float     sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float     sdivz8stepu, tdivz8stepu, zi8stepu;
	int       izi, izistep;
	short     *pz;

	sstep = 0; // keep compiler happy
	tstep = 0; // ditto

	pbase = cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;
	izistep = (int)( d_zistepu * 0x8000 * 0x10000 );

	do
	{
		pdest = ( d_viewbuffer
			  + ( r_screenwidth * pspan->v ) + pspan->u );
		pz = d_pzbuffer + ( d_zwidth * pspan->v ) + pspan->u;

		count = pspan->count;

		// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
		tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		izi = (int)( zi * 0x8000 * 0x10000 );
		z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

		s = (int)( sdivz * z ) + sadjust;
		if( s > bbextents )
			s = bbextents;
		else if( s < 0 )
			s = 0;

		t = (int)( tdivz * z ) + tadjust;
		if( t > bbextentt )
			t = bbextentt;
		else if( t < 0 )
			t = 0;

		do
		{
			// calculate s and t at the far end of the span
			if( count >= 8 )
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if( count )
			{
				// calculate s/z, t/z, zi->fixed s and t at far end of span,
				// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point

				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				sstep = ( snext - s ) >> 3;
				tstep = ( tnext - t ) >> 3;
			}
			else
			{
				// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
				// can't step off polygon), clamp, calculate s and t steps across
				// span by division, biasing steps low so we don't run off the
				// texture
				spancountminus1 = (float)( spancount - 1 );
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi; // prescale to 16.16 fixed-point
				snext = (int)( sdivz * z ) + sadjust;
				if( snext > bbextents )
					snext = bbextents;
				else if( snext < 8 )
					snext = 8; // prevent round-off error on <0 steps from
				//  from causing overstepping & running off the
				//  edge of the texture

				tnext = (int)( tdivz * z ) + tadjust;
				if( tnext > bbextentt )
					tnext = bbextentt;
				else if( tnext < 8 )
					tnext = 8; // guard against round-off error on <0 steps

				if( spancount > 1 )
				{
					sstep = ( snext - s ) / ( spancount - 1 );
					tstep = ( tnext - t ) / ( spancount - 1 );
				}
			}


			// Drawing phrase
			if( !SW_TEXFILT )
			{
				do
				{
					if( *pz <= ( izi >> 16 ))
					{
						pixel_t btemp;

						btemp = *( pbase + ( s >> 16 ) + ( t >> 16 ) * cachewidth );

						if( btemp != TRANSPARENT_COLOR )
						{
							btemp = BLEND_ADD( btemp, *pdest );
							*pdest = btemp;
						}
						// *pz    = izi >> 16;
					}
					pdest++;
					pz++;
					izi += izistep;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}
			else
			{
				do
				{
					int idiths = s;
					int iditht = t;

					int X = ( pspan->u + spancount ) & 1;
					int Y = ( pspan->v ) & 1;
					if( *pz <= ( izi >> 16 ))
					{
						pixel_t btemp;

						// Using the kernel
						idiths += kernel[X][Y][0];
						iditht += kernel[X][Y][1];

						idiths = idiths >> 16;
						idiths = idiths ? idiths - 1 : idiths;


						iditht = iditht >> 16;
						iditht = iditht ? iditht - 1 : iditht;

						btemp = *( pbase + idiths + iditht * cachewidth );

						if( btemp != TRANSPARENT_COLOR )
						{
							btemp = BLEND_ADD( btemp, *pdest );
							*pdest = btemp;
						}
						// *pz    = izi >> 16;
					}
					pdest++;
					pz++;
					izi += izistep;
					s += sstep;
					t += tstep;
				}
				while( --spancount > 0 );
			}


		}
		while( count > 0 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}

/*
=============
D_DrawZSpans
=============
*/
void D_DrawZSpans( espan_t *pspan )
{
	int      count, doublecount, izistep;
	int      izi;
	short    *pdest;
	unsigned ltemp;
	float    zi;
	float    du, dv;

// FIXME: check for clamping/range problems
// we count on FP exceptions being turned off to avoid range problems
	izistep = (int)( d_zistepu * 0x8000 * 0x10000 );

	do
	{
		pdest = d_pzbuffer + ( d_zwidth * pspan->v ) + pspan->u;

		count = pspan->count;

		// calculate the initial 1/z
		du = (float)pspan->u;
		dv = (float)pspan->v;

		zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
		// we count on FP exceptions being turned off to avoid range problems
		izi = (int)( zi * 0x8000 * 0x10000 );

		if((uintptr_t)pdest & 0x02 )
		{
			*pdest++ = (short)( izi >> 16 );
			izi += izistep;
			count--;
		}

		if(( doublecount = count >> 1 ) > 0 )
		{
			do
			{
				ltemp = izi >> 16;
				izi += izistep;
				ltemp |= izi & 0xFFFF0000;
				izi += izistep;
				*(int *)pdest = ltemp;
				pdest += 2;
			}
			while( --doublecount > 0 );
		}

		if( count & 1 )
			*pdest = (short)( izi >> 16 );

	}
	while(( pspan = pspan->pnext ) != NULL );
}

