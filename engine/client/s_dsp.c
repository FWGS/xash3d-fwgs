/*
s_dsp.c - digital signal processing algorithms for audio FX
Copyright (C) 2009 Uncle Mike
Copyright (C) 2016-2024 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "sound.h"

#define MAX_DELAY		0.4f
#define MAX_ROOM_TYPES	ARRAYSIZE( rgsxpre )

#define MONODLY		0
#define MAX_MONO_DELAY	0.4f

#define REVERBPOS		1
#define MAX_REVERB_DELAY	0.1f

#define STEREODLY		3
#define MAX_STEREO_DELAY	0.1f

#define REVERB_XFADE	32

#define MAXDLY		(STEREODLY + 1)
#define MAXLP		10

typedef struct sx_preset_s
{
	float	room_lp;	// lowpass
	float	room_mod;	// modulation

	// reverb
	float	room_size;
	float	room_refl;
	float	room_rvblp;

	// delay
	float	room_delay;
	float	room_feedback;
	float	room_dlylp;
	float	room_left;
} sx_preset_t;

typedef struct dly_s
{
	size_t	cdelaysamplesmax;	// delay line array size

	// delay line pointers
	size_t	idelayinput;
	size_t	idelayoutput;

	// crossfade
	int	idelayoutputxf;	// output pointer
	int	xfade;		// value

	int	delaysamples;	// delay setting
	int	delayfeedback;	// feedback setting

	// lowpass
	int	lp;		// is lowpass enabled
	int	lp0, lp1, lp2;	// lowpass buffer

	// modulation
	int	mod;
	int	modcur;

	// delay line
	int	*lpdelayline;
} dly_t;

static const sx_preset_t rgsxpre[] =
{
//          -------reverb--------  -------delay--------
// lp  mod  size   refl   rvblp  delay  feedback  dlylp  left
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.0,   0.0,      2.0,   0.0    }, // 0 off
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.065, 0.1,      0.0,   0.01   }, // 1 generic
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.02,  0.75,     0.0,   0.01   }, // 2 metalic
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.03,  0.78,     0.0,   0.02   }, // 3
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.06,  0.77,     0.0,   0.03   }, // 4
{ 0.0, 0.0, 0.05,  0.85,  1.0,   0.008, 0.96,     2.0,   0.01   }, // 5 tunnel
{ 0.0, 0.0, 0.05,  0.88,  1.0,   0.01,  0.98,     2.0,   0.02   }, // 6
{ 0.0, 0.0, 0.05,  0.92,  1.0,   0.015, 0.995,    2.0,   0.04   }, // 7
{ 0.0, 0.0, 0.05,  0.84,  1.0,   0.0,   0.0,      2.0,   0.012  }, // 8 chamber
{ 0.0, 0.0, 0.05,  0.9,   1.0,   0.0,   0.0,      2.0,   0.008  }, // 9
{ 0.0, 0.0, 0.05,  0.95,  1.0,   0.0,   0.0,      2.0,   0.004  }, // 10
{ 0.0, 0.0, 0.05,  0.7,   0.0,   0.0,   0.0,      2.0,   0.012  }, // 11 brite
{ 0.0, 0.0, 0.055, 0.78,  0.0,   0.0,   0.0,      2.0,   0.008  }, // 12
{ 0.0, 0.0, 0.05,  0.86,  0.0,   0.0,   0.0,      2.0,   0.002  }, // 13
{ 1.0, 0.0, 0.0,   0.0,   1.0,   0.0,   0.0,      2.0,   0.01   }, // 14 water
{ 1.0, 0.0, 0.0,   0.0,   1.0,   0.06,  0.85,     2.0,   0.02   }, // 15
{ 1.0, 0.0, 0.0,   0.0,   1.0,   0.2,   0.6,      2.0,   0.05   }, // 16
{ 0.0, 0.0, 0.05,  0.8,   1.0,   0.0,   0.48,     2.0,   0.016  }, // 17 concrete
{ 0.0, 0.0, 0.06,  0.9,   1.0,   0.0,   0.52,     2.0,   0.01   }, // 18
{ 0.0, 0.0, 0.07,  0.94,  1.0,   0.3,   0.6,      2.0,   0.008  }, // 19
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.3,   0.42,     2.0,   0.0    }, // 20 outside
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.35,  0.48,     2.0,   0.0    }, // 21
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.38,  0.6,      2.0,   0.0    }, // 22
{ 0.0, 0.0, 0.05,  0.9,   1.0,   0.2,   0.28,     0.0,   0.0    }, // 23 cavern
{ 0.0, 0.0, 0.07,  0.9,   1.0,   0.3,   0.4,      0.0,   0.0    }, // 24
{ 0.0, 0.0, 0.09,  0.9,   1.0,   0.35,  0.5,      0.0,   0.0    }, // 25
{ 0.0, 1.0, 0.01,  0.9,   0.0,   0.0,   0.0,      2.0,   0.05   }, // 26 weirdo
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.009, 0.999,    2.0,   0.04   }, // 27
{ 0.0, 0.0, 0.001, 0.999, 0.0,   0.2,   0.8,      2.0,   0.05   }  // 28
};

// 0x0045dca8 enginegl.exe
// SHA256: 42383d32cd712e59ee2c1bd78b7ba48814e680e7026c4223e730111f34a60d66
static const sx_preset_t rgsxpre_hlalpha052[] =
{
//          -------reverb--------  -------delay--------
// lp  mod  size   refl   rvblp  delay  feedback  dlylp  left
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.0,   0.0,      2.0,   0.0    }, // 0 off
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.08,  0.8,      2.0,   0.0    }, // 1 generic
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.02,  0.75,     0.0,   0.001  }, // 2 metalic
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.03,  0.78,     0.0,   0.002  }, // 3
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.06,  0.77,     0.0,   0.003  }, // 4
{ 0.0, 0.0, 0.05,  0.85,  1.0,   0.008, 0.96,     2.0,   0.01   }, // 5 tunnel
{ 0.0, 0.0, 0.05,  0.88,  1.0,   0.01,  0.98,     2.0,   0.02   }, // 6
{ 0.0, 0.0, 0.05,  0.92,  1.0,   0.015, 0.995,    2.0,   0.04   }, // 7
{ 0.0, 0.0, 0.05,  0.84,  1.0,   0.0,   0.0,      2.0,   0.003  }, // 8 chamber
{ 0.0, 0.0, 0.05,  0.9,   1.0,   0.0,   0.0,      2.0,   0.002  }, // 9
{ 0.0, 0.0, 0.05,  0.95,  1.0,   0.0,   0.0,      2.0,   0.001  }, // 10
{ 0.0, 0.0, 0.05,  0.7,   0.0,   0.0,   0.0,      2.0,   0.003  }, // 11 brite
{ 0.0, 0.0, 0.055, 0.78,  0.0,   0.0,   0.0,      2.0,   0.002  }, // 12
{ 0.0, 0.0, 0.05,  0.86,  0.0,   0.0,   0.0,      2.0,   0.001  }, // 13
{ 1.0, 1.0, 0.0,   0.0,   1.0,   0.0,   0.0,      2.0,   0.01   }, // 14 water
{ 1.0, 1.0, 0.0,   0.0,   1.0,   0.06,  0.85,     2.0,   0.02   }, // 15
{ 1.0, 1.0, 0.0,   0.0,   1.0,   0.2,   0.6,      2.0,   0.05   }, // 16
{ 0.0, 0.0, 0.05,  0.8,   1.0,   0.15,  0.48,     2.0,   0.008  }, // 17 concrete
{ 0.0, 0.0, 0.06,  0.9,   1.0,   0.22,  0.52,     2.0,   0.005  }, // 18
{ 0.0, 0.0, 0.07,  0.94,  1.0,   0.3,   0.6,      2.0,   0.001  }, // 19
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.3,   0.42,     2.0,   0.0    }, // 20 outside
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.35,  0.48,     2.0,   0.0    }, // 21
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.38,  0.6,      2.0,   0.0    }, // 22
{ 0.0, 0.0, 0.05,  0.9,   1.0,   0.2,   0.28,     0.0,   0.0    }, // 23 cavern
{ 0.0, 0.0, 0.07,  0.9,   1.0,   0.3,   0.4,      0.0,   0.0    }, // 24
{ 0.0, 0.0, 0.09,  0.9,   1.0,   0.35,  0.5,      0.0,   0.0    }, // 25
{ 0.0, 1.0, 0.01,  0.9,   0.0,   0.0,   0.0,      2.0,   0.05   }, // 26 weirdo
{ 0.0, 0.0, 0.0,   0.0,   1.0,   0.009, 0.999,    2.0,   0.04   }, // 27
{ 0.0, 0.0, 0.001, 0.999, 0.0,   0.2,   0.8,      2.0,   0.05   }, // 28
};

static const sx_preset_t *ptable = rgsxpre;

// cvars
static CVAR_DEFINE_AUTO( dsp_off, "0",  FCVAR_ARCHIVE, "disable DSP processing (deprecated)" );
static CVAR_DEFINE_AUTO( room_off, "0", FCVAR_ARCHIVE, "disable DSP processing (GoldSrc compatible cvar)" );
static CVAR_DEFINE_AUTO( dsp_coeff_table, "0", FCVAR_ARCHIVE, "select DSP coefficient table: 0 for release or 1 for alpha 0.52" );
static CVAR_DEFINE_AUTO( room_type, "0",  0, "current room type preset" );

static CVAR_DEFINE( roomwater_type, "waterroom_type", "14", 0, "water room type" );
static CVAR_DEFINE( hisound, "room_hires", "2", FCVAR_ARCHIVE, "dsp quality. 1 for 22k, 2 for 44k(recommended) and 3 for 96k" );

// underwater/special fx modulations
static CVAR_DEFINE( sxmod_mod, "room_mod", "0", 0, "stereo amptitude modulation for room" );
static CVAR_DEFINE( sxmod_lowpass, "room_lp", "0", 0, "for water fx, lowpass for entire room" );

// stereo delay(no feedback)
static CVAR_DEFINE( sxste_delay, "room_left", "0", 0, "left channel delay time" );

// mono reverb
static CVAR_DEFINE( sxrvb_lp, "room_rvblp", "1", 0, "reverb: low pass filtering level" );
static CVAR_DEFINE( sxrvb_feedback, "room_refl", "0", 0, "reverb: decay time" );
static CVAR_DEFINE( sxrvb_size, "room_size", "0", 0, "reverb: initial reflection size" );

// mono delay
static CVAR_DEFINE( sxdly_lp, "room_dlylp", "1", 0, "mono delay: low pass filtering level" );
static CVAR_DEFINE( sxdly_feedback, "room_feedback", "0.2", 0, "mono delay: decay time" );
static CVAR_DEFINE( sxdly_delay, "room_delay", "0.8", 0, "mono delay: delay time" );

static int			idsp_dma_speed;
int			idsp_room;
static int			room_typeprev;

// routines
static int			sxamodl, sxamodr;      // amplitude modulation values
static int			sxamodlt, sxamodrt;    // modulation targets
static int			sxmod1cur, sxmod2cur;
static int			sxmod1, sxmod2;
static int			sxhires;

static portable_samplepair_t	*paintto = NULL;

static dly_t			rgsxdly[MAXDLY]; // stereo is last
static int			rgsxlp[MAXLP];

static void SX_Profiling_f( void );

/*
============
SX_ReloadRoomFX

============
*/
static void SX_ReloadRoomFX( void )
{
	SetBits( sxste_delay.flags, FCVAR_CHANGED );
	SetBits( sxrvb_feedback.flags, FCVAR_CHANGED );
	SetBits( sxdly_delay.flags, FCVAR_CHANGED );
	SetBits( room_type.flags, FCVAR_CHANGED );
}

/*
============
SX_Init()

Starts sound crackling system
============
*/
void SX_Init( void )
{
	memset( rgsxdly, 0, sizeof( rgsxdly ));
	memset( rgsxlp,  0, sizeof( rgsxlp  ));

	sxamodr = sxamodl = sxamodrt = sxamodlt = 255;
	idsp_dma_speed = SOUND_11k;

	Cvar_RegisterVariable( &hisound );
	sxhires = 2;

	sxmod1cur = sxmod1 = 350 * ( idsp_dma_speed / SOUND_11k );
	sxmod2cur = sxmod2 = 450 * ( idsp_dma_speed / SOUND_11k );

	Cvar_RegisterVariable( &dsp_off );
	Cvar_RegisterVariable( &room_off );
	Cvar_RegisterVariable( &dsp_coeff_table );

	Cvar_RegisterVariable( &roomwater_type );
	Cvar_RegisterVariable( &room_type );

	Cvar_RegisterVariable( &sxmod_lowpass );
	Cvar_RegisterVariable( &sxmod_mod );

	Cvar_RegisterVariable( &sxrvb_size );
	Cvar_RegisterVariable( &sxrvb_feedback );
	Cvar_RegisterVariable( &sxrvb_lp );

	Cvar_RegisterVariable( &sxdly_delay );
	Cvar_RegisterVariable( &sxdly_feedback );
	Cvar_RegisterVariable( &sxdly_lp );

	Cvar_RegisterVariable( &sxste_delay );

	Cmd_AddCommand( "dsp_profile", SX_Profiling_f, "dsp stress-test, first argument is room_type" );

	SX_ReloadRoomFX();
}

/*
===========
DLY_Free

Free memory allocated for DSP
===========
*/
static void DLY_Free( int idelay )
{
	Assert( idelay >= 0 && idelay < MAXDLY );

	if( rgsxdly[idelay].lpdelayline )
	{
		Z_Free( rgsxdly[idelay].lpdelayline );
		rgsxdly[idelay].lpdelayline = NULL;
	}
}

/*
==========
SX_Shutdown

Stop DSP processor
==========
*/
void SX_Free( void )
{
	int	i;

	for( i = 0; i <= 3; i++ )
		DLY_Free( i );

	Cmd_RemoveCommand( "dsp_profile" );
}


/*
===========
DLY_Init

Initialize dly
===========
*/
static int DLY_Init( int idelay, float delay )
{
	dly_t	*cur;

	// DLY_Init called anytime with constants. So valid it in debug builds only.
	Assert( idelay >= 0 && idelay < MAXDLY );
	Assert( delay > 0.0f && delay <= MAX_DELAY );

	DLY_Free( idelay ); // free dly if it's allocated

	cur = &rgsxdly[idelay];
	cur->cdelaysamplesmax = ((int)(delay * idsp_dma_speed) << sxhires) + 1;
	cur->lpdelayline = (int *)Mem_Calloc( sndpool, cur->cdelaysamplesmax * sizeof( int ));
	cur->xfade = 0;

	// init modulation
	cur->mod = cur->modcur = 0;

	// init lowpass
	cur->lp = 1;
	cur->lp0 = cur->lp1 = cur->lp2 = 0;

	cur->idelayinput = 0;
	cur->idelayoutput = cur->cdelaysamplesmax - cur->delaysamples; // NOTE: delaysamples must be set!!!


	return 1;
}

/*
============
DLY_MovePointer

Checks overflow and moves pointer
============
*/
static void DLY_MovePointer( dly_t *dly )
{
	if( ++dly->idelayinput >= dly->cdelaysamplesmax )
		dly->idelayinput = 0;

	if( ++dly->idelayoutput >= dly->cdelaysamplesmax )
		dly->idelayoutput = 0;
}

/*
=============
DLY_CheckNewStereoDelayVal

Update stereo processor settings if we are in new room
=============
*/
static void DLY_CheckNewStereoDelayVal( void )
{
	dly_t *const	dly = &rgsxdly[STEREODLY];
	float		delay = sxste_delay.value;

	if( !FBitSet( sxste_delay.flags, FCVAR_CHANGED ))
		return;

	if( delay == 0 )
	{
		DLY_Free( STEREODLY );
	}
	else
	{
		int	samples;

		delay = Q_min( delay, MAX_STEREO_DELAY );
		samples = (int)(delay * idsp_dma_speed) << sxhires;

		// re-init dly
		if( !dly->lpdelayline )
		{
			dly->delaysamples = samples;
			DLY_Init( STEREODLY, MAX_STEREO_DELAY );
		}

		if( dly->delaysamples != samples )
		{
			dly->xfade = 128;
			dly->idelayoutputxf = dly->idelayinput - samples;
			if( dly->idelayoutputxf < 0 )
				dly->idelayoutputxf += dly->cdelaysamplesmax;
		}

		dly->modcur = dly->mod = 0;

		if( dly->delaysamples == 0 )
			DLY_Free( STEREODLY );
	}
}

/*
=============
DLY_DoStereoDelay

Do stereo processing
=============
*/
static void DLY_DoStereoDelay( int count )
{
	int			delay, samplexf;
	dly_t *const		dly = &rgsxdly[STEREODLY];
	portable_samplepair_t	*paint = paintto;

	if( !dly->lpdelayline )
		return; // inactive

	for( ; count; count--, paint++ )
	{
		if( dly->mod && --dly->modcur < 0 )
			dly->modcur = dly->mod;

		delay = dly->lpdelayline[dly->idelayoutput];

		// process only if crossfading, active left value or delayline
		if( delay || paint->left || dly->xfade )
		{
			// set up new crossfade, if not crossfading, not modulating, but going to
			if( !dly->xfade && !dly->modcur && dly->mod )
			{
				dly->idelayoutputxf = dly->idelayoutput + ((COM_RandomLong( 0, 255 ) * dly->delaysamples ) >> 9 );

				dly->xfade = 128;
			}

			dly->idelayoutputxf %= dly->cdelaysamplesmax;

			// modify delay, if crossfading
			if( dly->xfade )
			{
				samplexf = dly->lpdelayline[dly->idelayoutputxf] * (128 - dly->xfade) >> 7;
				delay = samplexf + ((delay * dly->xfade) >> 7);

				if( ++dly->idelayoutputxf >= dly->cdelaysamplesmax )
					dly->idelayoutputxf = 0;

				if( --dly->xfade == 0 )
					dly->idelayoutput = dly->idelayoutputxf;
			}

			// save left value to delay line
			dly->lpdelayline[dly->idelayinput] = CLIP( paint->left );

			// paint new delay value
			paint->left = delay;
		}
		else
		{
			// clear delay line
			dly->lpdelayline[dly->idelayinput] = 0;
		}

		DLY_MovePointer( dly );
	}
}

/*
=============
DLY_CheckNewDelayVal

Update delay processor settings if we are in new room
=============
*/
static void DLY_CheckNewDelayVal( void )
{
	float		delay = sxdly_delay.value;
	dly_t *const	dly = &rgsxdly[MONODLY];

	if( FBitSet( sxdly_delay.flags, FCVAR_CHANGED ))
	{
		if( delay == 0 )
		{
			DLY_Free( MONODLY );
		}
		else
		{
			delay = Q_min( delay, MAX_MONO_DELAY );
			dly->delaysamples = (int)(delay * idsp_dma_speed) << sxhires;

			// init dly
			if( !dly->lpdelayline )
				DLY_Init( MONODLY, MAX_MONO_DELAY );

			if( dly->lpdelayline )
			{
				memset( dly->lpdelayline, 0, dly->cdelaysamplesmax * sizeof( int ) );
				dly->lp0 = dly->lp1 = dly->lp2 = 0;
			}

			dly->idelayinput = 0;
			dly->idelayoutput = dly->cdelaysamplesmax - dly->delaysamples;

			if( !dly->delaysamples )
				DLY_Free( MONODLY );

		}
	}

	dly->lp = sxdly_lp.value;
	dly->delayfeedback = 255 * sxdly_feedback.value;
}

/*
=============
DLY_DoDelay

Do delay processing
=============
*/
static void DLY_DoDelay( int count )
{
	dly_t *const		dly = &rgsxdly[MONODLY];
	portable_samplepair_t	*paint = paintto;
	int			delay;

	if( !dly->lpdelayline || !count )
		return; // inactive

	for( ; count; count--, paint++ )
	{
		delay = dly->lpdelayline[dly->idelayoutput];

		// don't process if delay line and left/right samples are zero
		if( delay || paint->left || paint->right )
		{
			// calculate delayed value from average
			int val = (( paint->left + paint->right ) >> 1 ) + (( dly->delayfeedback * delay ) >> 8);
			val = CLIP( val );

			if( dly->lp ) // lowpass
			{
				val = ( dly->lp0 + dly->lp1 + val ) / 3;
				dly->lp0 = dly->lp1;
				dly->lp1 = val;
			}

			dly->lpdelayline[dly->idelayinput] = val;

			val >>= 2;

			paint->left = CLIP( paint->left + val );
			paint->right = CLIP( paint->right + val );
		}
		else
		{
			dly->lpdelayline[dly->idelayinput] = 0;
			dly->lp0 = dly->lp1 = dly->lp2 = 0;
		}

		DLY_MovePointer( dly );
	}
}

/*
===========
RVB_SetUpDly

Set up dly for reverb
===========
*/
static void RVB_SetUpDly( int pos, float delay, int kmod )
{
	int	samples;

	delay = Q_min( delay, MAX_REVERB_DELAY );
	samples = (int)(delay * idsp_dma_speed) << sxhires;

	if( !rgsxdly[pos].lpdelayline )
	{
		rgsxdly[pos].delaysamples = samples;
		DLY_Init( pos, MAX_REVERB_DELAY );
	}

	rgsxdly[pos].modcur = rgsxdly[pos].mod = (int)(kmod * idsp_dma_speed / SOUND_11k) << sxhires;

	// set up crossfade, if delay has changed
	if( rgsxdly[pos].delaysamples != samples )
	{
		rgsxdly[pos].idelayoutputxf = rgsxdly[pos].idelayinput - samples;
		if( rgsxdly[pos].idelayoutputxf < 0 )
			rgsxdly[pos].idelayoutputxf += rgsxdly[pos].cdelaysamplesmax;
		rgsxdly[pos].xfade = REVERB_XFADE;
	}

	if( !rgsxdly[pos].delaysamples )
		DLY_Free( pos );

}

/*
===========
RVB_CheckNewReverbVal

Update reverb settings if we are in new room
===========
*/
static void RVB_CheckNewReverbVal( void )
{
	dly_t *const	dly1 = &rgsxdly[REVERBPOS];
	dly_t *const	dly2 = &rgsxdly[REVERBPOS + 1];
	float		delay = sxrvb_size.value;

	if( FBitSet( sxrvb_size.flags, FCVAR_CHANGED ))
	{
		if( delay == 0.0f )
		{
			DLY_Free( REVERBPOS );
			DLY_Free( REVERBPOS + 1 );
		}
		else
		{
			RVB_SetUpDly( REVERBPOS, sxrvb_size.value, 500 );
			RVB_SetUpDly( REVERBPOS+1, sxrvb_size.value * 0.71f, 700 );
		}
	}

	dly1->lp = dly2->lp = sxrvb_lp.value;
	dly1->delayfeedback = dly2->delayfeedback = (int)(255 * sxrvb_feedback.value);
}

/*
===========
RVB_DoReverbForOneDly

Do reverberation for one dly
===========
*/
static int RVB_DoReverbForOneDly( dly_t *dly, const int vlr, const portable_samplepair_t *samplepair )
{
	int	delay;
	int	samplexf;
	int	val, valt;
	int	voutm = 0;

	if( --dly->modcur < 0 )
		dly->modcur = dly->mod;

	delay = dly->lpdelayline[dly->idelayoutput];

	if( dly->xfade || delay || samplepair->left || samplepair->right )
	{
		// modulate delay rate
		if( !dly->mod )
		{
			dly->idelayoutputxf = dly->idelayoutput + ((COM_RandomLong( 0, 255 ) * delay) >> 9 );

			dly->idelayoutputxf %= dly->cdelaysamplesmax;

			dly->xfade = REVERB_XFADE;
		}

		if( dly->xfade )
		{
			samplexf = (dly->lpdelayline[dly->idelayoutputxf] * (REVERB_XFADE - dly->xfade)) / REVERB_XFADE;
			delay = ((delay * dly->xfade) / REVERB_XFADE) + samplexf;

			if( ++dly->idelayoutputxf >= dly->cdelaysamplesmax )
				dly->idelayoutputxf = 0;

			if( --dly->xfade == 0 )
				dly->idelayoutput = dly->idelayoutputxf;
		}


		if( delay )
		{
			val = vlr + ( ( dly->delayfeedback * delay ) >> 8 );
			val = CLIP( val );
		}
		else
			val = vlr;

		if( dly->lp )
		{
			valt = (dly->lp0 + val) >> 1;
			dly->lp0 = val;
		}
		else
			valt = val;

		voutm = dly->lpdelayline[dly->idelayinput] = valt;
	}
	else
	{
		voutm = dly->lpdelayline[dly->idelayinput] = 0;
		dly->lp0 = 0;
	}

	DLY_MovePointer( dly );

	return voutm;

}

/*
===========
RVB_DoReverb

Do reverberation processing
===========
*/
static void RVB_DoReverb( int count )
{
	dly_t *const		dly1 = &rgsxdly[REVERBPOS];
	dly_t *const		dly2 = &rgsxdly[REVERBPOS+1];
	portable_samplepair_t	*paint = paintto;
	int			vlr, voutm;

	if( !dly1->lpdelayline )
		return;

	for( ; count; count--, paint++ )
	{
		vlr = ( paint->left + paint->right ) >> 1;

		voutm = RVB_DoReverbForOneDly( dly1, vlr, paint );
		voutm += RVB_DoReverbForOneDly( dly2, vlr, paint );

		if( dsp_coeff_table.value == 1.0f )
			voutm /= 6; // alpha
		else voutm = (11 * voutm) >> 6;

		paint->left = CLIP( paint->left + voutm );
		paint->right = CLIP( paint->right + voutm );
	}
}

/*
===========
RVB_DoAMod

Do amplification modulation processing
===========
*/
static void RVB_DoAMod( int count )
{
	portable_samplepair_t	*paint = paintto;

	if( !sxmod_lowpass.value && !sxmod_mod.value )
		return;

	for( ; count; count--, paint++ )
	{
		portable_samplepair_t	res = *paint;

		if( sxmod_lowpass.value )
		{
			res.left  = rgsxlp[0] + rgsxlp[1] + rgsxlp[2] + rgsxlp[3] + rgsxlp[4] + res.left;
			res.right = rgsxlp[5] + rgsxlp[6] + rgsxlp[7] + rgsxlp[8] + rgsxlp[9] + res.right;

			res.left >>= 2;
			res.right >>= 2;

			rgsxlp[4] = paint->left;
			rgsxlp[9] = paint->right;

			rgsxlp[0] = rgsxlp[1];
			rgsxlp[1] = rgsxlp[2];
			rgsxlp[2] = rgsxlp[3];
			rgsxlp[3] = rgsxlp[4];
			rgsxlp[4] = rgsxlp[5];
			rgsxlp[5] = rgsxlp[6];
			rgsxlp[6] = rgsxlp[7];
			rgsxlp[7] = rgsxlp[8];
			rgsxlp[8] = rgsxlp[9];
		}

		if( sxmod_mod.value )
		{
			if( --sxmod1cur < 0 )
				sxmod1cur = sxmod1;

			if( !sxmod1 )
				sxamodlt = COM_RandomLong( 32, 255 );

			if( --sxmod2cur < 0 )
				sxmod2cur = sxmod2;

			if( !sxmod2 )
				sxamodrt = COM_RandomLong( 32, 255 );

			res.left = (sxamodl * res.left) >> 8;
			res.right = (sxamodr * res.right) >> 8;

			if( sxamodl < sxamodlt )
				sxamodl++;
			else if( sxamodl > sxamodlt )
				sxamodl--;

			if( sxamodr < sxamodrt )
				sxamodr++;
			else if( sxamodr > sxamodrt )
				sxamodr--;
		}

		paint->left = CLIP(res.left);
		paint->right = CLIP(res.right);
	}
}

/*
===========
DSP_Process

(xash dsp interface)
===========
*/
void DSP_Process( portable_samplepair_t *pbfront, int sampleCount )
{
	if( dsp_off.value || room_off.value || !sampleCount )
		return;

	// preset is already installed by CheckNewDspPresets
	paintto = pbfront;

	RVB_DoAMod( sampleCount );
	RVB_DoReverb( sampleCount );
	DLY_DoDelay( sampleCount );
	DLY_DoStereoDelay( sampleCount );
}

/*
===========
DSP_ClearState

(xash dsp interface)
===========
*/
void DSP_ClearState( void )
{
	Cvar_DirectSet( &room_type, "0" );
	SX_ReloadRoomFX();
}

/*
===========
CheckNewDspPresets

(xash dsp interface)
===========
*/
void CheckNewDspPresets( void )
{
	if( dsp_off.value || room_off.value )
		return;

	if( FBitSet( dsp_coeff_table.flags, FCVAR_CHANGED ))
	{
		switch( (int)dsp_coeff_table.value )
		{
		case 0: // release
			ptable = rgsxpre;
			break;
		case 1: // alpha
			ptable = rgsxpre_hlalpha052;
			break;
		default:
			ptable = rgsxpre;
			break;
		}

		SX_ReloadRoomFX();
		room_typeprev = -1;

		ClearBits( dsp_coeff_table.flags, FCVAR_CHANGED );
	}

	if( s_listener.waterlevel > 2 )
		idsp_room = roomwater_type.value;
	else idsp_room = room_type.value;

	// don't pass invalid presets
	idsp_room = bound( 0, idsp_room, MAX_ROOM_TYPES );

	if( FBitSet( hisound.flags, FCVAR_CHANGED ))
	{
		sxhires = hisound.value;
		ClearBits( hisound.flags, FCVAR_CHANGED );
	}

	if( idsp_room == room_typeprev && idsp_room == 0 )
		return;

	if( idsp_room != room_typeprev )
	{
		const sx_preset_t *cur;

		cur = ptable + idsp_room;

		Cvar_DirectSetValue( &sxmod_lowpass, cur->room_lp );
		Cvar_DirectSetValue( &sxmod_mod, cur->room_mod );
		Cvar_DirectSetValue( &sxrvb_size, cur->room_size );
		Cvar_DirectSetValue( &sxrvb_feedback, cur->room_refl );
		Cvar_DirectSetValue( &sxrvb_lp, cur->room_rvblp );
		Cvar_DirectSetValue( &sxdly_delay, cur->room_delay );
		Cvar_DirectSetValue( &sxdly_feedback, cur->room_feedback );
		Cvar_DirectSetValue( &sxdly_lp, cur->room_dlylp );
		Cvar_DirectSetValue( &sxste_delay, cur->room_left );
	}

	room_typeprev = idsp_room;

	RVB_CheckNewReverbVal( );
	DLY_CheckNewDelayVal( );
	DLY_CheckNewStereoDelayVal();

	ClearBits( sxrvb_size.flags, FCVAR_CHANGED );
	ClearBits( sxdly_delay.flags, FCVAR_CHANGED );
	ClearBits( sxste_delay.flags, FCVAR_CHANGED );
}

static void SX_Profiling_f( void )
{
	portable_samplepair_t	testbuffer[512];
	float			oldroom = room_type.value;
	double			start, end;
	int			i, calls;

	for( i = 0; i < 512; i++ )
	{
		testbuffer[i].left = COM_RandomLong( 0, 3000 );
		testbuffer[i].right = COM_RandomLong( 0, 3000 );
	}

	if( Cmd_Argc() > 1 )
	{
		Cvar_DirectSetValue( &room_type, Q_atof( Cmd_Argv( 1 )));
		SX_ReloadRoomFX();
		CheckNewDspPresets(); // we just need idsp_room immediately, for message below
	}

	Con_Printf( "Profiling 10000 calls to DSP. Sample count is 512, room_type is %i\n", idsp_room );

	start = Sys_DoubleTime();
	for( calls = 10000; calls; calls-- )
	{
		DSP_Process( testbuffer, 512 );
	}
	end = Sys_DoubleTime();

	Con_Printf( "----------\nTook %g seconds.\n", end - start );

	if( Cmd_Argc() > 1 )
	{
		Cvar_DirectSetValue( &room_type, oldroom );
		SX_ReloadRoomFX();
		CheckNewDspPresets();
	}
}
