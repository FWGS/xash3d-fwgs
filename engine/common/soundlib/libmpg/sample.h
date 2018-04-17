/*
sample.h - compact version of famous library mpg123
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

#ifndef SAMPLE_H
#define SAMPLE_H

// define the accurate rounding function.
#ifdef IEEE_FLOAT
// rhis function is only available for IEEE754 single-precision values
// rhis is nearly identical to proper rounding, just -+0.5 is rounded to 0
static _inline int16_t ftoi16( float x )
{
	union
	{
		float	f;
		int32_t	i;
	} u_fi;

	u_fi.f = x + 12582912.0f;	// Magic Number: 2^23 + 2^22
	return (int16_t)u_fi.i;
}

#define REAL_TO_SHORT_ACCURATE( x )	ftoi16(x)
#else
// the "proper" rounding, plain C, a bit slow.
#define REAL_TO_SHORT_ACCURATE( x )	(short)((x) > 0.0 ? (x) + 0.5 : (x) - 0.5)
#endif

// now define the normal rounding.
#ifdef ACCURATE_ROUNDING
#define REAL_TO_SHORT( x )	REAL_TO_SHORT_ACCURATE( x )
#else
#define REAL_TO_SHORT( x )	(short)( x )
#endif

#endif//SAMPLE_H