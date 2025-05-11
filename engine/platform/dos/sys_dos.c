/*
sys_dos.c - dos timer
Copyright (C) 2020 mittorn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"
#include <dos.h>

void Platform_ShellExecute( const char *path, const char *parms )
{
}

volatile int ticks=0;

#if XASH_TIMER == TIMER_DOS
double Platform_DoubleTime( void )
{
	return 0.005*ticks;
}
#endif // XASH_TIMER == TIMER_DOS

#define PIT_FREQUENCY  0x1234DDL
#define frequency      140
#define counter        PIT_FREQUENCY/frequency

volatile int BIOS_ticks=0;
volatile int second_ticks=0;
volatile int second_flag=0;


static void (__interrupt __far *orig_int_1c)();
static void (__interrupt __far *orig_int_09)();


static void __interrupt __far timerhandler()
{
	BIOS_ticks += counter;
	second_ticks++, ticks++;

	if( BIOS_ticks >= 0x10000 )
	{
		BIOS_ticks = 0;
		_chain_intr( orig_int_1c );
	}

	if( second_ticks>=frequency )
	{
		second_flag = 1;
		second_ticks = 0;
	}

	outp( 0x20, 0x20 );
}

// in_dos.c
extern void __interrupt __far keyhandler( void );

void DOS_Init( void )
{
	// save original vectors
	orig_int_1c = _dos_getvect( 0x1c );
	orig_int_09 = _dos_getvect( 0x09 );
	_dos_setvect( 0x1c, timerhandler );
	_dos_setvect( 0x09, keyhandler );

	// set clock freq
	outp( 0x43, 0x34 );
	outp( 0x40, (char)(counter%256) );
	outp( 0x40, (char)(counter/256) );
}


void DOS_Shutdown( void )
{
	// restore freq
	outp( 0x43, 0x34 );
	outp( 0x40, 0 );
	outp( 0x40, 0 );

	//restore vectors
	_dos_setvect( 0x1c, orig_int_1c );
	_dos_setvect( 0x09, orig_int_09 );
}
