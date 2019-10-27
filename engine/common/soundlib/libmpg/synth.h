/*
synth.h - compact version of famous library mpg123
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

#ifndef SYNTH_H
#define SYNTH_H

typedef int (*func_synth)( float*, int, mpg123_handle_t*, int );
typedef int (*func_synth_mono)( float*, mpg123_handle_t* );
typedef int (*func_synth_stereo)( float*, float*, mpg123_handle_t* );

enum synth_channel
{
	c_plain = 0,
	c_stereo,
	c_m2s,
	c_mono,
	c_limit
};

enum synth_resample
{
	r_none = -1,
	r_1to1 = 0,
	r_limit
};

enum synth_format
{
	f_none = -1,
	f_16,
	f_limit
};

typedef struct synth_s
{
	func_synth	plain[r_limit][f_limit];
	func_synth_stereo	stereo[r_limit][f_limit];
	func_synth_mono	mono2stereo[r_limit][f_limit];
	func_synth_mono	mono[r_limit][f_limit];
} synth_t;

void init_synth( mpg123_handle_t *fr );
int set_synth_functions( mpg123_handle_t *fr );

#endif//SYNTH_H
